# pinhole

> **AI-assisted project.** This codebase was created with [Claude](https://claude.com/claude-code)
> (Anthropic), directed and reviewed by a human author. **It has never been loaded into
> Resolume.** What's established is established by an offline harness, `phtest`, that drives
> the real plugin class through the same `ProcessOpenGL` a host calls, in a headless GL
> context, on this Mac's GPU at two rasters and on Apple's software renderer. The plugin's
> Fresnel pattern builder reproduces **69 of the 80** values of Lommel's tables that Rayleigh
> printed in 1891 to half their last digit, and Rayleigh's closed form on the axis to
> **1.5e-5** of the peak. Read off the picture, an edge's spread is a disc's for a big hole
> (**24.04 px** against 24.24) and a band-mean Airy pattern's for a small one, red against blue
> **1.425** against 1.447; a point's image is tightest at **1.0–1.5%** below
> Mielenz's optimum hole, 1.844 √(λf), in each colour; the corners fall as cos⁴ to **2.8e-4**.
> **17** deliberately wrong models and all **51** one-character mutants of the shipped code
> are caught. The screen's size, the colour bands and the Exposure default are **chosen**, not
> measured: see [Status](#status).

A pinhole camera and camera obscura looking at your clip, for
[Resolume](https://resolume.com) Arena and Avenue: an FFGL effect, **SW Pinhole**.

There is no lens. The clip is the scene, and a hole in a thin plate throws it onto a screen.
**The hole does not blur a sharp picture; it makes the picture by diffraction.** Every point
of the scene lights the screen with the Fresnel pattern of a round hole, one pattern per
wavelength, and the picture is the sum of those patterns. So the picture is sharpest at one
hole size and softer either side of it: a smaller hole spreads light by diffraction, a larger
one by its own shadow. Small holes make red edges spread further than blue ones; near the best
hole the pattern's middle is up to four times brighter than the open light, and a pinhole
picture carries a faint veil of light from its patterns' outer rings. The corners darken as
cos⁴ of the angle off the axis, hard for a short box and barely for a long one. Swap the hole
for a **Fresnel zone plate** and it focuses harder, but one colour at a time: the colour the
plate is made for comes sharp and the others land around it as soft coloured light. Nothing is
drawn: all of it comes out of one Fresnel integral over the aperture's open rings.

## Controls

| Group | |
| --- | --- |
| **Camera** | Aperture (Pinhole or Zone Plate), Diameter (the hole, 0.1–2 mm), Open Zones (the plate's open rings, 1–16: 2n − 1 zones, the centre and the edge open), Zone Focus (the colour the plate brings to focus on the screen, 400–700 nm), Distance (hole to screen, 10–500 mm). |
| **Picture** | Exposure (−4 to +4 stops), View (Upright, or Obscura: turned 180°, as on the wall of a camera obscura), Edge (Mirror: the scene carries on past the frame; Black: a picture on a black wall). |
| **Look** | Mix. |

The screen is a 24 mm tall frame (a 35 mm film frame's height) at any raster, so the blur is
the same picture at 720p and 4K. The defaults are a 0.31 mm hole 50 mm from the screen, the
sharpest hole for that box in green light (Mielenz's optimum), and four open zones focusing
550 nm when the plate is chosen. **Exposure starts at +0.2 stops, set from Resolume's own demo
clips**: the blur keeps the light, so a picture only loses brightness to the dark corners, and
+0.2 stops is what an averaging meter sets on the median of Arena's 33 demo clips (it brings
that clip's mean back to 1.02 of its own).

Diameter is the pinhole's; Open Zones and Zone Focus are the plate's. Each does nothing under
the other Aperture.

Over a transparent part of a clip the screen still shows light spread from the clip's opaque
parts; a transparent pixel sends the hole no light. The output is opaque at Mix 1.

## Status

**v0.1.0, local only, 16 November 2026. Never loaded into Resolume.**

### Measured offline, on macOS

`tools/verify.sh` (below) ends **all green**, exit 0, on an Apple M4 Max; without `OXBOW` it
fails at the bundle step, as it should. Every picture check runs at **two rasters**, 1280×720 and 320×180, and again on
**Apple's software renderer** at 320×180 (`--optimum`, which needs 1280×720, at 1280×720
there). The numbers below are from `phtest --all` on the same code (an arm64 build).

| check | result |
| --- | --- |
| `--fresnel` | the pattern builder (no GL): Lommel's tables as Rayleigh prints them, **69 of 80** values to 5e-5 (half the last printed digit; the 11 others are listed, and one, 3π at z = 10, disagrees with the table's own minimum row); the axis against Rayleigh's 4 sin²(u/4) to **1.5e-5** of the peak and the light through the hole against πu² to **5.6e-5**, u from π/2 to 40π; the Airy limit (u = 0.01) against libm's j1 to **3.1e-5**; a big hole's edge spread 0.80761 a against a disc's 0.80795 a; the area width least at u = **1.688π** (Mielenz: 1.7π); zone plates' axes against (2M)² to **1e-8**; the plugin's own sampling against four times as fine, **5.4e-4** of the peak; its cumulative light exact to **1e-14**; the pyramid's taps holding the light inside the support to **3.9e-4** |
| `--blur` | a white edge's spread off the picture: a 2 mm hole at 10 mm, R G B **24.04–24.07 px** against a disc's 24.24 (±3); a 0.1 mm hole at 500 mm, R **60.43**, G **51.51**, B **42.40 px** against the band-mean Airy patterns' 62.66, 52.99, 43.32 (±5), red over blue **1.425** (1.447); at 320×180 within 0.6 px of the same |
| `--optimum` | a point's image over 25 diameters at 500 mm (1280×720): the area width least at **1.0403, 0.9554, 0.8619 mm** for R, G, B, against Mielenz's 1.0512, 0.9670, 0.8746 (−1.03%, −1.19%, −1.45%; tolerance 1.5% + 2.2%); the same on the software renderer |
| `--falloff` | a flat field against cos⁴ of the field angle at 10, 25 and 50 mm (the corner gets 0.0205, 0.2612, 0.6512 of the centre): worst **2.8e-4** (tolerance 0.002) |
| `--zone` | an 8-open-zone plate at 500 mm, Zone Focus 400–700 nm: each channel's point is sharpest when the plate is made for its band, R at **650**, G at **540**, B at **440 nm** (550 and 450 at 320×180) |
| `--edges` | Mirror: the card against the middle of the card between its own mirror images, **0** difference (tolerance 1e-5); upside down against upside down to **9.2e-5**; Black: a white strip against each of the four edges symmetric to **4.6e-5** |
| `--energy` | a 9 × 9 white square: the picture's light **0.99953–1.00000** of the clip's (tolerance 0.002); the picture symmetric under swapping x and y; the centroid within **0.0036 px** of where the pyramid puts it (0.16 px off the square's centre at 720p for a plate: see [Not established](#not-established)) |
| `--view` | Obscura moves a marked pixel exactly to the opposite corner |
| `--alpha` | alpha = mix(α, 1, Mix) to **3e-8**; white under transparent pixels changes nothing; grey under an alpha ramp comes out as the grey's light times α to **3.2e-4**; Mix 0 the clip bit for bit. On two of Arena's own alpha clips, white painted under every transparent pixel changes **0** output bytes |
| `--resize` | 640×360 → 320×180 mid-run against a fresh instance: **0** difference |
| `--glstate` | the host's GL state comes back, **0** things changed |
| `--pipe` | 2.5 frames in, exactly 2 out; bad cues and settings refused; a closed stdout (`\| head -c 1`) exits 1, not 141 |
| `--negative` | **17** deliberately wrong models, **17** caught |
| mutation | **51** one-character mutants of the shipped GLSL and C++, **51** caught, each by the check named for it (`AGENTS.md` lists them) |
| `tools/sweep.py` | all **9** controls measurably change the picture |
| shaders | all 5 compile through `glslc`, not merely through Apple's driver |
| the bundle | universal (`x86_64 arm64`), exports `plugMain`, ad-hoc signs; `oxbow` reads `SW Pinhole` / `PC01` / `effect` and renders through it |

### Cost

GPU time a frame (`GL_TIME_ELAPSED`, the median and the worst of the run) on the harness's
card, from `phtest --bench` at the end of that verify run, on an Apple M4 Max shared with
other builds:

| raster | the defaults (0.31 mm at 50 mm) | the largest blur (0.1 mm at 500 mm) |
| --- | --- | --- |
| 1280×720 | **2.42 ms** (worst 3.45) | **2.91 ms** (worst 3.96) |
| 1920×1080 | **5.55 ms** (worst 6.04) | **6.03 ms** (worst 6.74) |
| 3840×2160 | not run | **20.09 ms** (worst 20.67): more than a 60 fps frame |

The kernel's levels grow with the blur (3 to 7 at 720p, 4 to 8 at 1080p, 9 at 4K) and each
level is at most 241 taps, so the largest blur costs little more than the defaults. The CPU
rebuilds the patterns and taps in 10–11 ms, only when the aperture, the distance or the
raster's height changes. An earlier build the same afternoon measured 2.32, 2.86, 4.83, 5.29
and 17.72 ms for the same five cases: take that spread as this shared machine's noise. No
other GPU has been measured.

### Not established

- **No host has loaded it.** Not Arena, not Avenue, on any platform. `oxbow` (the fleet's test
  host) reads the bundle and renders through it, which is all.
- **Windows** has never been built, and there is no CI.
- **Chosen, not measured:** the screen's 24 mm height (a 35 mm frame's), the three colour
  bands (400–500, 500–600, 600–700 nm, flat), the cut at 99% of each pattern's light, and the
  Exposure default's criterion (an averaging meter).
- **What Fresnel's approximation leaves out:** the scene at a finite distance, the obliquity
  factor, the hole's thickness and its own vignetting, polarisation. One pattern serves the
  whole frame, so the pattern's growth off the axis is left out.
- **The pyramid is not exact.** Each pattern's outer rings are gathered from coarser and
  coarser texels: a feature at radius r is smeared by about a texel of the level holding it,
  and a coarse level sees a texel's light at its centre, so a point's light moves by up to
  0.67 px with its position against the coarse grid (measured at 720p, four open zones). The
  pyramid stores half floats: a lone white pixel on black loses 0.9% of its light, in the
  far rings. Mirror is exact only where the coarsest texels divide the frame.
- **The zone plate's orders** (the booklet's 25% zero order, 10% first order) are checked
  through the plate's axis and its light, not read off the picture.
- **Resolume's alpha.** The plugin treats a clip's colour as straight alpha, which is what
  Arena's DXV demo clips decode to; whether Resolume hands an effect premultiplied colour has
  not been checked.
- The Exposure default was measured on the demo clips through `--pipe`, not looked at in a
  show.
- No user guide, no presets, no OpenFX port and no browser demo.

## Build

macOS (universal: Apple Silicon and Intel):

```bash
git clone --recursive <this repo>
```

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

```bash
cmake --build build
```

```bash
cmake --install build
```

The last one copies `Pinhole.bundle` into `~/Documents/Resolume Arena/Extra Effects`.
Restart Arena and it's under Effects as **SW Pinhole**.

## Building and testing

```bash
OXBOW=/path/to/oxbow tools/verify.sh
```

That's the whole lot: shader hygiene and `glslc`, a fresh universal build with no warnings,
the bundle's architecture, plist and ad-hoc signature, a host's view of it through `oxbow`
(required), every check at two rasters and again on the software renderer, the alpha check on
Arena's demo clips (required), the pipe, the negative controls, the mutants, the sweep for
dead controls and the render cost. There is no CI: this script is the verification.
`CLAUDE.md` lists the checks one by one; `./build/phtest --all` runs them all.

Render a frame of your own:

```bash
./build/phtest --out /tmp/pinhole.png --set "Aperture=Zone Plate" --set "Open Zones=8"
```

Run a clip through it:

```bash
ffmpeg -nostdin -i in.mov -f rawvideo -pix_fmt rgba - | ./build/phtest --pipe --size 1280x720 | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 30 -i - out.mov
```

## License

MIT. See [LICENSE](LICENSE) and [ATTRIBUTIONS.md](ATTRIBUTIONS.md).

<!-- attributions:start -->
This project is built on other people's work — see [ATTRIBUTIONS.md](ATTRIBUTIONS.md).
<!-- attributions:end -->
