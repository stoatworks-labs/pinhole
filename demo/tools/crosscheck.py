"""The browser demo against the plugin's harness: the same input frame, the same
raster, the same settings, two renderers.

    python3 demo/tools/crosscheck.py --phtest build/phtest [--keep DIR]

Needs a GPU, Google Chrome, numpy, a built phtest, and stoatworks-backend's
`release/cdpshot.py` (CDPSHOT=path overrides ~/Projects/infrastructure/
stoatworks-backend/release/cdpshot.py). Not run by tools/verify.sh.

Each case is rendered twice:

  - by the PAGE: demo/ served from here on a free port and loaded in headless
    Chrome on the real GPU (ANGLE on Metal), at ?size=WxH&clip=C. The page is
    paused (the clip stands still; the plugin keeps no time) and Mix set to 0:
    the plugin's final pass then hands the clip through untouched, so the
    canvas IS the input frame, as 8-bit, and is read back. Then the case's
    settings are applied and the page's output is read back.
    preserveDrawingBuffer is forced on from outside so the canvas can be read
    after its frame; nothing in the page is changed for this.
  - by the PLUGIN: `phtest --pipe --size WxH --set ...` (the real
    PinholePlugin in a GL 4.1 context), fed the same input bytes, with the
    same host values: the float32 the page sent, printed to 9 digits; a
    converted value goes through the page's compiled ParamFrom* first.

The two outputs are compared byte for byte (RGB, and alpha separately). A case
passes at max 1 code and at most 1e-4 of its values off by any amount. A
negative control -- phtest with Diameter one slider step (0.001) from the
page's -- must differ from the page by more than the defaults case does, so
the comparison is shown to see a slip that small.

Exit 1 when a case fails, the negative control passes, or the page logs an
error or a WebGL warning.
"""
import argparse
import base64
import functools
import http.server
import json
import os
import subprocess
import sys
import tempfile
import threading
import time

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
DEMO = os.path.abspath(os.path.join(HERE, ".."))
sys.dont_write_bytecode = True

CDPSHOT = os.environ.get("CDPSHOT", os.path.expanduser("~/Projects/infrastructure/stoatworks-backend/release/cdpshot.py"))
sys.path.insert(0, os.path.dirname(CDPSHOT))
import cdpshot  # noqa: E402

# The real GPU: SwiftShader rounds differently and is slow; --disable-gpu has no WebGL.
cdpshot.GL_FLAGS = ["--use-gl=angle", "--use-angle=metal", "--ignore-gpu-blocklist"]

# name, raster, clip, settings by the plugin's own names: {'units': x} is in
# the plugin's units and converted by its inverse (ParamFrom*, compiled into
# the page), an option is its element's name, Open Zones its count, a bare
# number the host's 0..1.
CASES = [
    ("defaults", "960x540", "grid", {}),
    ("defaults, scene", "960x540", "scene", {}),
    ("zone plate", "960x540", "grid", {"Aperture": "Zone Plate"}),
    ("zone plate, 8 zones at 450 nm", "960x540", "spot", {"Aperture": "Zone Plate", "Open Zones": 8, "Zone Focus": {"units": 450e-9}}),
    ("2 mm hole at 10 mm", "960x540", "grid", {"Diameter": {"units": 2e-3}, "Distance": {"units": 10e-3}}),
    ("0.1 mm hole at 500 mm", "960x540", "grid", {"Diameter": {"units": 0.1e-3}, "Distance": {"units": 500e-3}}),
    ("Obscura, Black, 1.1 mm", "960x540", "grid", {"View": "Obscura", "Edge": "Black", "Diameter": {"units": 1.1e-3}}),
    ("defaults, 720p", "1280x720", "grid", {}),
    ("zone plate, 720p", "1280x720", "grid", {"Aperture": "Zone Plate"}),
]

PRESERVE = """
(() => {
  const real = HTMLCanvasElement.prototype.getContext;
  HTMLCanvasElement.prototype.getContext = function (kind, attrs) {
    if (kind === 'webgl2') attrs = Object.assign({}, attrs || {}, { preserveDrawingBuffer: true });
    return real.call(this, kind, attrs);
  };
})();
"""


class Page(cdpshot.Chrome):
    """cdpshot's Chrome, keeping the events it would discard."""

    def __init__(self):
        self.events = []
        super().__init__()
        self.call("Runtime.enable")
        self.call("Log.enable")
        self.call("Page.addScriptToEvaluateOnNewDocument", {"source": PRESERVE})

    def call(self, method, params=None):
        self.msg_id += 1
        want = self.msg_id
        self.ws.send(json.dumps({"id": want, "method": method, "params": params or {}}))
        while True:
            msg = json.loads(self.ws.recv())
            if msg.get("id") == want:
                if "error" in msg:
                    raise RuntimeError(f"{method}: {msg['error']}")
                return msg.get("result", {})
            if "method" in msg:
                self.events.append(msg)

    def run(self, expression):
        r = self.call("Runtime.evaluate", {"expression": expression, "returnByValue": True, "awaitPromise": True})
        if "exceptionDetails" in r:
            raise RuntimeError(f"the page threw: {json.dumps(r['exceptionDetails'])[:400]}")
        return r.get("result", {}).get("value")

    def frames(self):
        return int(self.run("Number(document.querySelector('canvas')?.dataset.frames || 0)"))

    def load(self, url):
        self.call("Page.navigate", {"url": url})
        deadline = time.time() + 30
        while time.time() < deadline:
            try:
                if self.frames() > 2:
                    return
            except RuntimeError:
                pass
            time.sleep(0.25)
        raise RuntimeError(f"{url} drew no frames")

    def after(self, expression):
        """Run, then wait for the frame it causes (the page is paused)."""
        n0 = self.frames()
        self.run(expression)
        deadline = time.time() + 20
        while time.time() < deadline:
            if self.frames() > n0:
                return
            time.sleep(0.03)
        raise RuntimeError("no frame after " + expression[:80])

    def canvas(self):
        """The canvas's bytes, rows top first."""
        r = self.run("""(() => {
          const c = document.querySelector('canvas'); const gl = c.getContext('webgl2');
          const w = c.width, h = c.height, px = new Uint8Array(w * h * 4);
          gl.bindFramebuffer(gl.FRAMEBUFFER, null);
          gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, px);
          const out = new Uint8Array(w * h * 4);
          for (let y = 0; y < h; y += 1) out.set(px.subarray((h - 1 - y) * w * 4, (h - y) * w * 4), y * w * 4);
          let s = '';
          for (let i = 0; i < out.length; i += 0x8000) s += String.fromCharCode.apply(null, out.subarray(i, i + 0x8000));
          return { w, h, b64: btoa(s) };
        })()""")
        return r["w"], r["h"], base64.b64decode(r["b64"])

    def problems(self):
        out = []
        for e in self.events:
            m, p = e["method"], e.get("params", {})
            if m == "Runtime.exceptionThrown":
                out.append(json.dumps(p.get("exceptionDetails", {}))[:300])
            elif m == "Runtime.consoleAPICalled" and p.get("type") in ("error", "warning", "assert"):
                out.append(" ".join(str(a.get("value", a.get("description", ""))) for a in p.get("args", []))[:300])
            elif m == "Log.entryAdded" and p.get("entry", {}).get("level") in ("error", "warning"):
                out.append(f"{p['entry'].get('source')}: {p['entry'].get('text')}"[:300])
        return out


class Quiet(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def serve():
    handler = functools.partial(Quiet, directory=DEMO)
    server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    return server, f"http://127.0.0.1:{server.server_address[1]}/"


def host_values(page, settings):
    """{name: (kit id, kit value, phtest value)} from the page's own declarations
    and conversions."""
    return page.run(f"""import('./plugin.js').then(m => {{
      const out = {{}};
      for (const [name, v] of Object.entries({json.dumps(settings)})) {{
        const p = m.PARAMS.find(x => x.name === name);
        if (!p) throw new Error('no control ' + name);
        if (p.type === 'option') out[name] = [p.id, p.elements.indexOf(v), v];
        else if (p.ff.type === 13) out[name] = [p.id, String(v), String(v)];
        else {{
          const h = Math.fround(typeof v === 'object' ? m.core._ph_param_from(p.ff.index, v.units) : v);
          out[name] = [p.id, h, h];
        }}
      }}
      return out;
    }})""")


def phtest(binary, size, values, frame, override=None):
    sets = []
    for name, (_, _, value) in values.items():
        value = (override or {}).get(name, value)
        sets += ["--set", f"{name}={value:.9g}" if isinstance(value, float) else f"{name}={value}"]
    for name, value in (override or {}).items():
        if name not in values:
            sets += ["--set", f"{name}={value:.9g}"]
    r = subprocess.run([binary, "--pipe", "--size", size, *sets], input=frame, capture_output=True, timeout=300)
    if r.returncode != 0:
        raise RuntimeError(f"phtest {' '.join(sets)}: {r.stderr.decode()[:400]}")
    return r.stdout


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--phtest", required=True)
    ap.add_argument("--keep", help="write each case's two pictures and their difference (x40) here as PNG")
    args = ap.parse_args()

    server, base = serve()
    page = Page()
    failures = 0
    defaults_mean = None
    try:
        for name, size, clip, settings in CASES:
            w, h = map(int, size.split("x"))
            page.load(f"{base}?size={size}&clip={clip}")
            page.after("document.querySelector('.btn--play').click()")
            page.after("import('./plugin.js').then(m => m.mounted.params.set('mix', 0))")
            cw, ch, frame = page.canvas()
            if (cw, ch) != (w, h):
                raise RuntimeError(f"the canvas is {cw}x{ch}, not {size}")
            values = host_values(page, settings)
            kit = {kid: kv for kid, kv, _ in values.values()}
            page.after(f"import('./plugin.js').then(m => m.mounted.params.apply({json.dumps(kit)}))")
            _, _, ours = page.canvas()
            theirs = phtest(args.phtest, size, values, frame)

            a = np.frombuffer(ours, np.uint8).reshape(h, w, 4).astype(np.int16)
            b = np.frombuffer(theirs, np.uint8).reshape(h, w, 4).astype(np.int16)
            src = np.frombuffer(frame, np.uint8).reshape(h, w, 4).astype(np.int16)
            diff = np.abs(a - b)
            rgb = diff[..., :3]
            off = int((rgb > 0).sum())
            ok = rgb.max() <= 1 and off <= 1e-4 * rgb.size and diff[..., 3].max() <= 1
            failures += not ok
            print(f"{'ok  ' if ok else 'FAIL'}  {name:<32} {size:>9} {clip:<6} max {rgb.max()} code, mean {rgb.mean():.2e},"
                  f" {off} of {rgb.size} values off, alpha max {diff[..., 3].max()}"
                  f"  (the effect itself moves the clip by {np.abs(b - src)[..., :3].mean():.1f} codes)")
            if name == "defaults":
                defaults_mean = rgb.mean()
                step = float(np.float32(page.run("import('./plugin.js').then(m => m.PARAMS.find(p => p.name === 'Diameter').default)") + 0.001))
                slip = np.frombuffer(phtest(args.phtest, size, values, frame, {"Diameter": step}), np.uint8).reshape(h, w, 4).astype(np.int16)
                slip_mean = np.abs(a - slip)[..., :3].mean()
                caught = slip_mean > defaults_mean
                failures += not caught
                print(f"{'ok  ' if caught else 'FAIL'}  {'negative: Diameter one step off':<32} mean {slip_mean:.2e} against the defaults' {defaults_mean:.2e},"
                      f" {int((np.abs(a - slip)[..., :3] > 0).sum())} values off")
            if args.keep:
                from PIL import Image
                os.makedirs(args.keep, exist_ok=True)
                stem = os.path.join(args.keep, name.replace(" ", "-").replace(",", ""))
                Image.fromarray(a[..., :3].astype(np.uint8)).save(f"{stem}-page.png")
                Image.fromarray(b[..., :3].astype(np.uint8)).save(f"{stem}-phtest.png")
                Image.fromarray(np.clip(rgb * 40, 0, 255).astype(np.uint8)).save(f"{stem}-diff-x40.png")
        problems = page.problems()
        for p in problems:
            print(f"FAIL  the page logged: {p}")
        failures += len(problems)
        renderer = page.run("(() => { const gl = document.createElement('canvas').getContext('webgl2'); const x = gl.getExtension('WEBGL_debug_renderer_info'); return x ? gl.getParameter(x.UNMASKED_RENDERER_WEBGL) : gl.getParameter(gl.RENDERER); })()")
        print(f"\nthe page rendered on: {renderer}")
    finally:
        page.close()
        server.shutdown()
    print(f"{'all cases agree' if failures == 0 else f'{failures} failure(s)'}")
    return 1 if failures else 0


if __name__ == "__main__":
    os.environ.setdefault("TMPDIR", tempfile.gettempdir())
    sys.exit(main())
