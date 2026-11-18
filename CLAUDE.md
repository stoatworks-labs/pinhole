# pinhole

A pinhole camera and camera obscura looking at the clip, with a Fresnel zone plate in place
of the hole as an option, for Resolume Arena/Avenue as one FFGL effect. The clip is the
scene's geometric image on the screen; every point of it lights the screen with the
Fresnel–Lommel pattern of the aperture, one pattern per wavelength, and the picture is the
sum: the clip convolved, per colour band, with the band's mean pattern, darkened toward the
corners by cos⁴ of the field angle. C++/GLSL, CMake MODULE → universal `.bundle` (macOS). MIT.

| id | display name | bundle | type |
| --- | --- | --- | --- |
| `PC01` | `SW Pinhole` | `Pinhole.bundle`, `com.stoatworks.ffgl.pinhole` | effect |

Read `AGENTS.md` before touching the pattern builder (`Optics.cpp`: `Monochromatic`,
`FresnelG`, `BuildProfile`), the pyramid's taps (`BuildKernel`, `AngleInside`), the sum
buffers' margin (`Pinhole.cpp`, `kGather`/`kRingFragment` in `Shaders.cpp`), the edge modes,
or the final pass. The spec is `~/Projects/resolume/specs/SPEC-pinhole.md` and is kept in
step with this repo.

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release -DPINHOLE_BUILD_TOOLS=ON`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build -j8`
- Install to Resolume: `cmake --install build` (into Arena's Extra Effects). Only when
  Arena is not running (`pgrep -x Arena`): Allan may be using it. It has loaded in Arena
  7.29.0 here and 7.27.1 on win-lab (v0.1.0).
- Render offline: `./build/phtest --out /tmp/p.png` (the defaults over the harness's card,
  1280x720; the plugin keeps no time, so one frame is the picture)
- A clip of your own: `--clip clip.rgba` (raw RGBA at `--size`, top row first)
- List parameters: `./build/phtest --list`
- Set anything by name: `./build/phtest --set "Aperture=Zone Plate" --set "Open Zones=8"`
  (a standard control takes the host's 0..1; an option takes its name; Open Zones takes the
  count; anything unparseable is refused with exit 2)
- Change something later: `./build/phtest --out /tmp/p.png --frames 2 --at 1 "Distance=0.9"`
- Film: `./build/phtest --film 600 --size 1280x720 --script cues | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
- A clip through it: `ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/phtest --pipe --size WxH | ffmpeg -f rawvideo -pix_fmt rgba -s WxH -r 30 -i - out.mp4`
- `--stats` prints each frame's kernel (levels, taps, support) and rebuild time on stderr.
- The default's evidence: `python3 tools/clips.py defaults` (all 33 of Arena's demo clips,
  about 40 s). The alpha check on two of them: `python3 tools/clips.py alpha`.

## Verify
- Everything: `OXBOW=/path/to/oxbow tools/verify.sh` (GLSL reserved words, `glslc` on all 5
  shaders, the submodule pin, a fresh universal build with no warnings outside external/ffgl,
  lipo, plist, ad-hoc signature, `oxbow probe` and `selftest` (required: no oxbow is a
  failure); every check at 1280x720 and 320x180; every check again on Apple's software
  renderer at 320x180, and `--optimum` there at 1280x720; `--offline`;
  the alpha check on Arena's clips; `--pipe`; the negative controls; the mutants; the sweep;
  the bench). `KEEP_BUILD=1` reuses `build-verify/`. It needs Resolume Arena's demo clips in
  `/Applications/Resolume Arena/media`.
- Every check at once: `./build/phtest --all`. One at a time (`--size WxH`: that raster alone):
  - **No GL**: `--fresnel` (the pattern builder against Lommel's tables as Rayleigh prints
    them, Rayleigh's axis 4 sin²(u/4), the flux, the Airy limit, the geometric limit, the
    area-width minimum at 1.7π, a zone plate's axis (2M)², the conversions).
  - **The picture**: `--blur` (an edge's spread per channel: a disc's for a big hole, a
    band-mean Airy pattern's for a small one), `--optimum` (a point's area width least at
    1.844 √(λf); 1280x720 only), `--falloff` (cos⁴), `--zone` (each channel sharpest when the
    plate is made for its band), `--edges` (Mirror and Black), `--energy` (the kernel sums to
    one), `--view` (Obscura turns 180°), `--alpha`.
  - **The host**: `--resize` (a fresh instance's picture at the new raster), `--glstate`.
- The checks can fail: `./build/phtest --negative` (17 wrong models), `tools/mutate.sh`
  (51 mutants, arm64, 320x180, perl alarm at 600 s; `ONLY=n` runs one).
- No dead controls: `python3 tools/sweep.py` (each control in a context where it acts;
  `--bare` drops the context).
- Render cost: `./build/phtest --bench` (720p, 1080p, one 4K run).
- `PHTEST_RENDERER=software` runs any of it on Apple's software renderer.
- Shared Mac: arm64 builds while iterating, no 4K loops, never kill a process you didn't start.

## Notes
- **Four fragment passes and one shared full-raster triangle** (`Shaders.h`, five shaders for
  `glslc`): linear (the clip decoded and weighted by α: level 0), down (2 × 2 means: levels
  1..K), ring (level k's annulus of taps plus a bilinear sample of the level above, into a sum
  buffer with a one-texel margin), final (the core taps, the falloff, the exposure, sRGB, Mix,
  alpha) into the host's framebuffer. Nothing is blended.
- **The patterns and taps are built on the CPU** (`Optics.cpp`), only when the aperture, the
  distance or the raster's height changes (about 10 ms); every frame runs the passes. The
  plugin keeps no time and draws nothing at random.
- **The screen is 24 mm tall at every raster**, so the kernel in pixels depends on the height
  alone: a pixel is 24 mm / H.
- All host parameters are 0..1 and mapped in `Controls.cpp`, except Open Zones, an
  `FF_TYPE_INTEGER` holding the count (1..16); options hold the element index.
- GLSL 4.10 core. Reserved words (`common partition active … packed sample patch layout flat`,
  `noise1`–`noise4`, `any far near fixed`) must not be identifiers, and nothing writes `M_PI`;
  `verify.sh` greps for both.
- Override `SetTextParameter` to return `FF_SUCCESS` for the About block, or no host can
  instantiate the plugin.
- `pinhole_core` is an OBJECT library: the plugin registers itself from a file-scope
  constructor nothing references.
- The macOS build must be universal. Check with `lipo`, never the build log.
- **Generated, never edit here:** `source/StoatworksAbout.h`, `ATTRIBUTIONS.md` (and the
  README's attributions block), the issue forms and `.github/FUNDING.yml` come from
  stoatworks-backend's sync scripts (registered 2026-11-18). Edit them there and re-sync.
  sync-about added the User guide button, so the plugin declares 14 parameters (the 9
  controls, the About text and four link buttons). `StoatworksAboutLinks.h` and
  `StoatworksAboutParams.h` are the FFGL renderer, this repo's own.
- **Workflows** (the repo is public, `stoatworks-labs/pinhole`, so Actions are allowed):
  `ci.yml` builds on macOS (arm64, `phtest --list` and `--offline`, `glslc`) and Windows on
  every push and pull request; `release.yml` builds the universal bundle and the x64 DLL
  (checked by `tools/wincheck.ps1`), with a .dmg, zips and an NSIS installer, and publishes on
  a `v*` tag. A `workflow_dispatch` of `release.yml` builds without publishing. The pixel
  checks still run only in `tools/verify.sh`, on a Mac.
- `~/Projects/resolume/pinhole`, public at `github.com/stoatworks-labs/pinhole`.

## Browser demo
- `demo/` is served at https://pinhole-demo.stoatworks-labs.com/ by the Worker in
  `wrangler.toml` (a route, plus a proxied AAAA `100::` record for `pinhole-demo`: delete the
  record and the page goes dark while deploys stay green). `.github/workflows/deploy.yml`
  redeploys it on every push to main; by hand: `cf-run npx wrangler deploy`.
- **`demo/pinhole-core.{js,wasm}` is the plugin's C++, compiled and committed.** After ANY
  change under `source/` (the generated `StoatworksAbout*.h` included), run
  `demo/tools/build-wasm.sh` (emscripten's `em++`) and commit both outputs and
  `demo/wasm/inputs.sha256`; `tools/verify.sh` fails until you do.
- `demo/shaders.js` is a copy: after changing a shader, `python3 demo/tools/check_shaders.py
  --write`. The same script checks every uniform's setter against its GLSL type and the
  .wasm's input hashes.
- Serve locally: `cd demo && python3 -m http.server 8000`. Against phtest:
  `python3 demo/tools/crosscheck.py --phtest build/phtest` (Chrome on the real GPU).
- `demo/vendor/` is the shared kit (`stoatworks-backend/resolume-demo`); never edit it.

## Diagnostics

`source/Diag.{h,cpp}`: log file only, no crash handler (this runs inside Resolume).

    ~/Library/Logs/pinhole/pinhole.YYYY-MM-DD.log
    %LOCALAPPDATA%\pinhole\logs\pinhole.YYYY-MM-DD.log

`PINHOLE_LOG_DIR` overrides the folder.
