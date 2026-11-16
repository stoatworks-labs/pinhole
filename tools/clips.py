"""Resolume's own demo clips through `phtest --pipe`: the alpha check, and
the measurement the Exposure default was set from.

    tools/clips.py alpha [--build DIR]      the output alpha on the DXV alpha clips
    tools/clips.py defaults [--build DIR]   the exposure an averaging meter sets on each clip

The clips are Resolume Arena's (`/Applications/Resolume Arena/media`), DXV
with alpha, decoded by ffmpeg (`-nostdin`: ffmpeg reads the terminal
otherwise, and a script running it in a loop loses its input). Nothing here
is a second copy of the plugin's formulas: every number is read off the
pipe's frames.

**alpha.** The plugin's decision: output alpha = mix( a, 1, Mix ) -- the
screen is lit (or dark) everywhere, so at Mix 1 the output is opaque, and
the clip's light reaches the screen weighted by its alpha (a transparent
pixel sends the hole nothing). Checked on BattleWeapon_Tank_09 and Ethnik2,
the two clips with real transparency: at Mix 1 every output alpha is 255; at
Mix 0.5 every output alpha is within half a code of a / 2 + 127.5; and white
painted under every fully transparent pixel changes no output byte at Mix 1
(the clips are straight alpha over black, so without the paint a plugin that
ignored alpha would pass). The negative control paints ~200 opaque pixels
instead, which must show.

**defaults.** Exposure: the hole's blur keeps the light (the kernel sums to
one, and Mirror keeps the frame's edge), so the picture's only loss is the
cos^4 falloff toward the corners. A camera's averaging meter sets the
exposure that brings the frame's mean back. For each demo clip, at 0 EV, the
ratio of the output's mean linear luminance to the clip's (its light weighted
by alpha, as the plugin weights it), over 30 frames at 640x360; the stops
that undo it are -log2 of the ratio. The default is the median clip's, to
0.1 stop, and the cost is measured on that clip: the share of output pixels
the brighter centre drives to full scale, against the clip's own.
"""

import argparse
import math
import pathlib
import subprocess
import sys

import numpy as np

REPO = pathlib.Path(__file__).resolve().parent.parent
MEDIA = pathlib.Path("/Applications/Resolume Arena/media")
ALPHA_CLIPS = ["Shop74/BattleWeapon_Tank_09.mov", "Shop74/Ethnik2.mov"]


def exposure_param(stops):
    """Exposure is -4 .. +4 stops, linear (the spec)."""
    return (stops + 4.0) / 8.0


def decode(path, width, height, frames, start=1.0):
    args = ["ffmpeg", "-nostdin", "-v", "error", "-ss", str(start), "-i", str(path), "-frames:v", str(frames),
            "-vf", f"scale={width}:{height}", "-f", "rawvideo", "-pix_fmt", "rgba", "-"]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    size = width * height * 4
    got = len(raw) // size
    if got == 0:
        raise RuntimeError(f"ffmpeg decoded nothing from {path}")
    return raw[:got * size], got


def pipe(phtest, raw, width, height, settings):
    args = [str(phtest), "--pipe", "--size", f"{width}x{height}"]
    for s in settings:
        args += ["--set", s]
    result = subprocess.run(args, input=raw, capture_output=True)
    if result.returncode != 0:
        raise RuntimeError(f"phtest --pipe failed ({result.returncode}): {result.stderr.decode()[-400:]}")
    return np.frombuffer(result.stdout, dtype=np.uint8).reshape(-1, height, width, 4)


def linear(codes):
    c = codes.astype(np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def luminance(frames):
    lin = linear(frames[..., :3])
    return 0.2126 * lin[..., 0] + 0.7152 * lin[..., 1] + 0.0722 * lin[..., 2]


def alpha_check(phtest):
    width, height, count = 320, 180, 20
    failures, negative_caught = 0, 0
    for name in ALPHA_CLIPS:
        path = MEDIA / name
        if not path.exists():
            print(f"  FAIL  {path} is missing: the alpha check needs Resolume Arena's demo clips")
            return 1
        raw, frames = decode(path, width, height, count)
        clip = np.frombuffer(raw, dtype=np.uint8).reshape(frames, height, width, 4)
        a = clip[..., 3].astype(np.float64)
        full = pipe(phtest, raw, width, height, ["Mix=1"])
        half = pipe(phtest, raw, width, height, ["Mix=0.5"])
        opaque_alpha = int(full[..., 3].min())
        off_half = float(np.abs(half[..., 3].astype(np.float64) - (a / 2.0 + 127.5)).max())
        # The clips are straight alpha over black: where a = 0 the colour is
        # (almost always) black already, so a plugin that ignored alpha would
        # look the same. Paint every fully transparent pixel white: the output
        # must not change by a single byte.
        white = clip.copy()
        hidden = white[..., 3] == 0
        white[..., :3][hidden] = 255
        painted = pipe(phtest, white.tobytes(), width, height, ["Mix=1"])
        changed = int(np.count_nonzero(painted != full))
        # The negative control: the same paint on a few opaque pixels must show.
        shown = clip.copy()
        opaque = np.argwhere(shown[..., 3] == 255)
        pick = opaque[:: max(1, len(opaque) // 200)]
        shown[pick[:, 0], pick[:, 1], pick[:, 2], :3] = 255
        control = pipe(phtest, shown.tobytes(), width, height, ["Mix=1"])
        negative_caught += int(np.count_nonzero(control != full)) > 0
        ok = opaque_alpha == 255 and off_half <= 0.5 + 1e-9 and changed == 0
        failures += not ok
        print(f"  {'ok  ' if ok else 'FAIL'}  {path.name}: {frames} frames at {width}x{height}, {hidden.mean():.0%} fully "
              f"transparent; Mix 1: alpha >= {opaque_alpha} (want 255); Mix 0.5: alpha within {off_half:.2f} codes of "
              f"a/2 + 127.5 (tolerance 0.5, the byte's rounding); white painted under every transparent pixel changes "
              f"{changed} output bytes (want 0)")
    print(f"  {'ok  ' if negative_caught == len(ALPHA_CLIPS) else 'FAIL'}  negative control: white painted on ~200 "
          f"opaque pixels shows in the output on {negative_caught} of {len(ALPHA_CLIPS)} clips")
    failures += negative_caught != len(ALPHA_CLIPS)
    print(f"\n  {'PASS' if failures == 0 else 'FAIL'}")
    return 0 if failures == 0 else 1


def metered(phtest, clip, raw, width, height, stops):
    """The output's mean linear luminance over the clip's (alpha-weighted)."""
    out = pipe(phtest, raw, width, height, [f"Exposure={exposure_param(stops):.7f}", "Mix=1"])
    light_in = luminance(clip) * (clip[..., 3].astype(np.float64) / 255.0)
    return float(luminance(out).mean() / light_in.mean()), out


def defaults(phtest):
    clips = sorted(p for p in (MEDIA / "AV").glob("*.mov")) + sorted(p for p in (MEDIA / "Shop74").glob("*.mov"))
    if not clips:
        print(f"no demo clips under {MEDIA}")
        return 1
    width, height = 640, 360
    rows = []
    for path in clips:
        raw, frames = decode(path, width, height, 30)
        clip = np.frombuffer(raw, dtype=np.uint8).reshape(frames, height, width, 4)
        if luminance(clip).mean() <= 0.0:
            print(f"  {path.parent.name}/{path.name:28s} black: no reading")
            continue
        ratio, _ = metered(phtest, clip, raw, width, height, 0.0)
        stops = -math.log2(ratio)
        rows.append((stops, path, raw, clip))
        print(f"  {path.parent.name}/{path.name:28s} at 0 EV the mean keeps {ratio:.4f} of the clip's: {stops:+.3f} EV undoes it")
    rows.sort(key=lambda r: r[0])
    stops, path, raw, clip = rows[len(rows) // 2]
    print(f"\n{len(rows)} clips: from {rows[0][0]:+.3f} to {rows[-1][0]:+.3f} EV; the median clip, {path.name}, {stops:+.3f} EV")
    chosen = round(stops * 10.0) / 10.0
    ratio, out = metered(phtest, clip, raw, width, height, chosen)
    _, flat = metered(phtest, clip, raw, width, height, 0.0)
    full_in = float(np.any(clip[..., :3] == 255, axis=-1).mean())
    full_0 = float(np.any(flat[..., :3] == 255, axis=-1).mean())
    full_out = float(np.any(out[..., :3] == 255, axis=-1).mean())
    print(f"Exposure {chosen:+.1f} EV (parameter {exposure_param(chosen):.4f}): on {path.name} the mean keeps {ratio:.4f} of "
          f"the clip's; pixels at full scale in any channel: the clip {full_in:.2%}, at 0 EV {full_0:.2%}, at {chosen:+.1f} EV "
          f"{full_out:.2%}")
    return 0


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("what", choices=["alpha", "defaults"])
    parser.add_argument("--build", type=pathlib.Path, default=REPO / "build")
    args = parser.parse_args()
    build = args.build if args.build.is_absolute() else REPO / args.build
    phtest = build / "phtest"
    if not phtest.exists():
        print(f"{phtest} not found", file=sys.stderr)
        return 1
    if args.what == "alpha":
        print("\n=== alpha on a real clip: Resolume's DXV alpha clips through --pipe")
        return alpha_check(phtest)
    return defaults(phtest)


if __name__ == "__main__":
    sys.exit(main())
