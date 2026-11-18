# AGENTS.md: Pinhole

Read this before changing the pattern builder (`Optics.cpp`: `Monochromatic`, `FresnelG`,
`Pattern::At`, `BuildProfile`), the pyramid's taps (`BuildKernel`, `AngleInside`), the sum
buffers' margin (`Pinhole.cpp`, and `kGather`/`kRingFragment` in `Shaders.cpp`), the edge
modes, or the final pass. `CLAUDE.md` has the commands; the spec is
`~/Projects/resolume/specs/SPEC-pinhole.md`, written before any code and kept in step with
this repo (no As-built section: where the build departed from the first draft, the spec was
changed, and the changes are listed below).

## The idea

**A pinhole does not blur a sharp picture; it makes the picture by Fresnel diffraction.**
Every point of the scene lights the screen with the Fresnel–Lommel pattern of the aperture, one
pattern per wavelength, and the picture is the sum. The sharpest hole, red softer than blue
for small holes, the bright middle near the optimum, the veil from the outer rings, the zone
plate's one-colour focus and its coloured light all come out of one integral over the open
rings; none is drawn. `hyacinth` (a thin lens, a circle of confusion, no wavelength) is the
neighbour not duplicated: nothing in the fleet computes diffraction.

## Decisions taken without asking (the brief says decide and write it down)

- **Identity.** `SW Pinhole`, `PC01` (checked free against every `"PC01"` in
  `~/Projects/*/*/source` and `~/dev/*/source`), an effect, `com.stoatworks.ffgl.pinhole`,
  harness `phtest`. One class, `PinholePlugin`; host indices are the ParamIds, the About block
  last.
- **The clip is the scene's geometric image on the screen**, the scene at infinity (every
  point a plane wave at the hole): what a hole of zero size would show, the right way up.
- **The screen is 24 mm tall at every raster** (a 135 frame), so a pixel is 24 mm / H and the
  kernel in pixels depends on the raster's height alone.
- **The pattern is Fresnel's, computed exactly as far as the sampling goes**: the projection
  of the pupil exp(iuρ²/2) over the open rings, through G(Y) = ∫₀^Y exp(iuy²/2) dy (a
  3-point Gauss–Legendre table and a 4-point step from the nearest node), then one FFT
  (projection-slice). x is sampled at M = max(256, 6 v_max/π, 12 u/π) points, so the chirp's
  phase moves under π/12 a step and v dx stays under π/6 at v_max; v in steps of 0.2 (0.3 for
  a hole past u = 8π). Between samples, Catmull–Rom. `--fresnel` checks the result against
  the sources and against four times the sampling.
- **Each channel is a box band**, B 400–500, G 500–600, R 600–700 nm, the mean of six
  wavelengths at its sub-bands' midpoints, equal energy per nanometre: a decision, not
  physics (an sRGB channel is not a band).
- **Light adds incoherently** between scene points and wavelengths: each channel's picture is
  the clip's linear light, weighted by α, convolved with its band's mean pattern.
- **The pattern is cut at the radius holding 99% of each channel's light** (capped at the
  frame's height), and the taps renormalised to one: a flat field is exact, the picture is
  metered for the centre (Rayleigh does the same, p. 93), and the 1% is lost.
- **The convolution is an annulus pyramid.** The core, r ≤ 8 px, is gathered from level 0;
  the ring 8·2^(k−1) < r ≤ 8·2^k from level k (2 × 2 means of level k − 1), at level k's
  resolution, each tap's weight the pattern integrated exactly over its texel square inside
  the ring (the cumulative light, by arc length); each level adds a bilinear sample of the
  level above. At most 241 taps a level, levels RGBA16F. Each sum buffer has a one-texel
  margin all round, computed under the edge mode, so a pixel at the frame's edge reads the
  coarse sum just past it as the edge mode makes it, not a copy of the edge's own.
- **The optimum is Mielenz's area-width minimum**, u = 1.7π, d = 1.844 √(λf): the only one of
  the five candidate constants (spec, "The best hole") defined by a property of the picture
  that the harness can measure, and the one Rayleigh's experiment landed on. The default hole
  is it, at 50 mm in 550 nm light: 0.3058 mm.
- **The zone plate is made for the screen**: r_n² = n λ_d f, the X-Ray Data Booklet's eq. 1
  without its n²λ²/4 term, λ_d the Zone Focus. **Open Zones** counts the open rings, M = 1–16
  (2M − 1 zones, the centre and the edge open, so the plate is the booklet's): the axis is
  then (2M)² of the open wave at λ_d (Rayleigh's eq. 20 by superposition). The default is 4
  open zones: past N ≈ λ/Δλ ≈ 5.5 zones (the booklet's chromatic limit, for a 100 nm band) a
  plate is soft even in its own colour.
- **cos⁴ of the field angle** multiplies the convolved picture (Aggarwal, Hua & Ahuja), the
  angle from the pixel's offset on the 24 mm screen over the distance.
- **Edges.** Mirror (default) reflects the clip about the frame's edge at every level; Black
  extends it with darkness. Mirror is exact where the coarsest texels divide the frame (a
  reflection then maps texel blocks onto blocks); otherwise the coarse rings see the
  reflection up to half a coarse texel out.
- **Output alpha = mix(α, 1, Mix)**: the screen is lit, or dark, everywhere, and the pattern
  spreads light over transparent parts of the clip. The clip's light is weighted by α before
  the blur (right for straight alpha; Arena's DXV clips decode straight, intensifier's
  measurement). Mix blends the clip and the picture in code values, written out rather than
  `mix()` (Apple's software renderer lets the clip's colour under a transparent pixel through
  `mix()` in the last bit at Mix 1).
- **View: Obscura turns the picture 180°** (the camera obscura's wall); Upright, the default,
  shows it as a print would.
- **The Exposure default, +0.2 EV, is an averaging meter's**, from Resolume's demo clips
  (`tools/clips.py defaults`, all 33 DXV clips in `media/AV` and `media/Shop74`, through
  `--pipe` at 640×360): at 0 EV the clips' output means keep 0.736 (Synth 003) to 0.968
  (SpaceUniverse_04) of their own; the median clip, Bass 003, is brought back by +0.174 EV,
  rounded to +0.2 (its mean then 1.0185 of the clip's, and no pixel at full scale that was
  not before: 0.00% at both 0 and +0.2 EV against the clip's own 0.10%).
- **No time, no randomness.** The plugin keeps no clock and draws nothing at random; the CPU
  rebuilds the patterns and taps only when the aperture, the distance or the raster's height
  changes.
- **No OpenFX port and no presets.** (At the build there was also no browser demo, no user
  guide, no Windows build and no workflows; the release added them once the repo went public
  as `stoatworks-labs/pinhole`, where Actions are allowed: `ci.yml` and `release.yml` are
  intensifier's (xenon's), `tools/wincheck.ps1` penumbra's.)

## Changes from the spec's first draft

Each was made in the spec as well.

- **Zones became Open Zones**, 1–16 open rings (default 4), not 1–32 zones counted separately
  (default 8): a plate that ends on a blocked zone is not the booklet's plate, and counting
  the open rings makes the axis's closed form (2M)².
- **`--zone` no longer reads the zero-order glow's radius or level off the picture.** The
  glow's edge is overlapped by every other order and the Fresnel fringes, and no clean
  criterion was found. The plate's geometry is checked instead through `--fresnel`: its axis
  against (2M)², the rings' sum at other wavelengths, and its light against the open area.
  `--zone` keeps the per-colour focus off the picture.
- **The geometric limit's tolerance** is λf/a (the edge's Fresnel scale squared over the
  radius, 2π/u in units of a), not "the Fresnel edge width": the wider one let two wrong
  models through.
- **The sampling**: the draft's "fine grid" became the numbers above, after the first build's
  flux was biased and a big hole's fringes rippled (linear interpolation at the plugin's v step
  missed them by 1% of the peak). `--fresnel` gained the plugin-against-finer comparison.
- **The sum buffers gained a margin**: without it a Black edge was asymmetric by 0.0048 (the
  upsample clamped to the edge texel).
- **The disc-plus-Airy shortcut**, as the plugin's builder measures it at u = 1.7π: a peak
  0.230 of Fresnel's and an edge spread 1.468 times as wide (the draft's prototype: 4.3× and
  32% narrower, the same numbers).
- **`--optimum` runs at 1280×720 only** (at 320×180 the area width at 500 mm is 2.8 px), on
  the GPU and once on the software renderer.
- **`--edges`** checks Mirror against the clip reflected by hand (left and right) and against
  the clip upside down (top and bottom), and Black at all four edges; a wrong model (Mirror
  repeating the edge pixel) was added that passes the draft's flat-field test.
- **`--energy`** gained the centroid and the swap of x and y; **`--alpha`** the alpha ramp;
  **`--fresnel`** the cumulative's exactness, the taps' total and the bands' wavelengths. All
  four came from mutants that survived the first mutation run (below).

## The traps actually hit

- **Linear interpolation of the pattern** at the plugin's v step missed a big hole's fringes
  (period about 2π in v) by 1% of the peak, and the trapezoid rule at the Nyquist rate in x
  left 1% ripples near a big hole's edge: Catmull–Rom, and π/12 a step.
- **A wrong model hung `--offline`**: the disc-plus-Airy shortcut summed Bessel functions per
  sample at the harness's fine tables, for minutes. Its cost is capped now (a wrong model need
  not be accurate). The hung run was stopped from this session.
- **A tolerance two wrong models passed**: the geometric limit's first tolerance (the Fresnel
  edge width) let the Fraunhofer and the detuned chirp through. It is λf/a now.
- **The frame's edge read a copy of itself**: the upsample's clamp at the edge texel made Black
  asymmetric by 0.0048 until the sum buffers kept a margin.
- **Apple's software renderer**: `mix()` let the clip's colour under α = 0 through in the last
  bit at Mix 1, and the same frame rendered twice differed by 1e-10. The blend is written out;
  the comparisons between renders are to 1e-6 (alpha) or 1e-5 (kFloatCode).
- **17 of 51 mutants survived the first mutation run**, each a real gap: Mirror was checked
  on one side only (mutant 3, the left edge); Black at one edge only (49, the right margin);
  the taps' order and the upsample's offsets (6, 8, 9, 10, 14) move or distort light without
  changing a total or an interquartile width; the clip's light divided by α (11) made NaN
  under α = 0 that the encode's clamp hid; the cumulative's and the taps' geometry (36, 37,
  38, 39, 42) are renormalised away before the picture; the zone count (24) and the bands'
  wavelengths (27) were assigned to a check that cannot see them. Each check named in the
  table below now catches its mutant.
- **The pyramid moves light**: adding the centroid check found the 9 × 9 square's light
  0.16 px up and right at 720p (four open zones), and a lone point's moving by −0.36 to
  +0.67 px with its position against the coarse grid. The same in a float32 pyramid (an
  experiment, not shipped): a coarse level sees each texel's light at its centre. The
  check now predicts the centroid from the taps' level shares and agrees to 0.0036 px.
- **Half floats lose faint light**: in the same experiment a lone white pixel kept 0.9911 of
  its light in RGBA16F and 1.000000 in RGBA32F (four open zones, 720p); the 9 × 9 square
  keeps 0.9995. Kept at RGBA16F: real pictures are not single points on black.
- **`Key` has only `operator==`**: a mutant `!=` on it did not compile; the mutant changes
  the height's comparison inside it instead.
- **A mutant the OS killed**: the plate's radius from n / λ (1800 times too wide) built a
  kernel so large that phtest died of SIGKILL (exit 137; the alarm's is 142). A non-zero exit
  counts as caught, but on a Mac other builds share it is a hazard, so every-zone-open
  replaced it (caught by `--fresnel`: the axis against (2M)², worst 1 relative).
- **Mutant 49 passed by exactly its tolerance** at 320×180 (Black's asymmetry 0.00098 against
  1/1020) and runs at 1280×720, where it is 0.0048.
- **zsh has no PIPESTATUS** and **macOS has no timeout**: the pipe checks run under bash, the
  mutants under perl's alarm. **ffmpeg reads the terminal** unless `-nostdin`.
- **The FFGL SDK warns** (FFGLUtilities.cpp); verify.sh ignores warnings under external/ffgl
  only.

## Verified, and assumed

Verified, by `OXBOW=... tools/verify.sh` on 16 November 2026 (numbers in README's Status):
every check at 1280×720 and 320×180 on this Mac's GPU and again on Apple's software renderer
at 320×180 (`--optimum` there at 1280×720); the alpha decision on two of Arena's clips; the
pipe; the negative controls and mutants; the sweep; the bundle universal, signed ad hoc, read
and rendered by oxbow. The cost, from the same run on an Apple M4 Max shared with other
builds (GPU median per frame): 2.42 ms at 720p and 5.55 ms at 1080p at the defaults, 2.91 and
6.03 ms at the largest blur, 20.09 ms at 4K at the largest blur (more than a 60 fps frame);
the CPU rebuild 10–11 ms. An earlier build the same afternoon measured 2.32, 2.86, 4.83, 5.29
and 17.72 ms: the shared machine's noise is that large.

Each claim, its number and its tolerance's source:

- **The pattern is Fresnel's** (`--fresnel`, no GL). Lommel's tables as Rayleigh prints them
  (pp. 94–96): 69 of 80 to 5e-5, half the last printed digit, at least 60 (three quarters)
  required, every miss listed. The largest miss, 3π at z = 10 (0.0163 against 0.0156), sits
  where the same table's own minimum row (z = 10.1735, 0.0154) is lower by the same amount,
  so it reads as a slip in the hand computation. The axis against 4 sin²(u/4) (Rayleigh eq.
  20): 1.5e-5 of the peak; the light against πu² (Parseval): 5.6e-5; the Airy limit at u =
  0.01 against libm's j1 (Mielenz, under eq. 7a): 3.1e-5. Tolerance for all three: a quarter
  of an 8-bit code step, 1/1020 (an exact formula evaluated numerically should not be visibly
  wrong). The geometric limit, u = 400π: the edge spread's interquartile width 0.80761 a
  against a disc's 0.80795 a (x√(1 − x²) + asin x = π/4), tolerance λf/a = 2π/u = 0.005 a.
  Zone plates of 1–16 open zones: the axis against (2M)² to 1e-8, the other wavelengths
  against the rings' sum to 3.9e-5 of (2M)², the light against the open area to 1.7e-4 (1/1020
  each). The plugin's sampling against four times as fine: 5.4e-4 of the peak (1/1020).
  The cumulative light against 2-point Gauss–Legendre on the same profiles: 9.8e-15 of the
  open area (tolerance 1e-9, double rounding over 200,000 segments). The taps' total against
  the cumulative at the support: 3.9e-4 (tolerance 2e-3, `kTapRule`: the taps integrate the
  angle inside each square by the midpoint rule, first order where r crosses a side, so the
  tolerance is the measured worst with room, chosen rather than derived). Each band's six
  wavelengths against the sub-band midpoints: 0 (1e-12).
- **The optimum** (`--fresnel` on the builder, `--optimum` off the picture). The builder's
  area width is least at u = 1.6878π against Mielenz's 1.7π, tolerance ±0.05π (his two
  figures). Off the picture at 500 mm, 1280×720: −1.03% (R), −1.19% (G), −1.45% (B) from
  1.844 √(λf) at each band's centre, tolerance 1.5% (Mielenz's ±0.05π in u is ±1.5% in d)
  plus half the ladder's step, 2^(1/32) (2.2%). Every measured minimum sits below the
  optimum; the builder's own minimum is 0.4% below in d (1.6878π, 0.7% below in u), and the
  rest is not explained.
- **The blur's width** (`--blur`): the interquartile width of an edge's spread, R G B, against
  a disc's 0.80795 a (2 mm at 10 mm: 24.04–24.07 px against 24.24) and a band-mean Airy
  pattern's (0.1 mm at 500 mm: 60.43, 51.51, 42.40 px against 62.66, 52.99, 43.32; the Airy
  ESF through the OTF, 40 sub-bands a band). Tolerance: one texel of the pyramid level holding
  the quartile radius, plus a pixel for the two crossings (±3 and ±5 px at 720p, ±2 at 180p).
  Red over blue 1.425 (1.447).
- **cos⁴** (`--falloff`): every pixel of a flat field against cos⁴ from the screen's geometry,
  at 10, 25 and 50 mm: 2.8e-4, tolerance four half-float roundings (4 × 2^-11 = 0.002).
- **The plate focuses one colour** (`--zone`): each channel's point image sharpest (most of
  its light in its own pixel) for a Zone Focus inside its band, to 10 nm steps, ±5 nm.
- **Edges** (`--edges`): Mirror flat to the edge (5.3e-5 to 1.6e-4, tolerance 0.002); against
  the card between its mirror images, 0 on the GPU and 1.2e-8 on the software renderer
  (tolerance kFloatCode, 1e-5: float32 rounding times the sRGB slope, with room); upside down
  9.2e-5 (tolerance 0.002, four half-float roundings: the blocks' four values are added in
  another order); Black's strips symmetric to 4.6e-5 (1/1020).
- **Energy** (`--energy`): the light of a 9 × 9 square 0.99953–1.00000 of the clip's
  (tolerance 0.002); x and y swapped within four half-float roundings of the larger value plus
  four denormal steps (worst 0.12 of that); the centroid within 0.0036 px of the pyramid's
  prediction (tolerance 0.002 × the kernel's reach: 0.04–0.34 px).
- **Alpha** (`--alpha`): mix(α, 1, Mix) to 3e-8 (tolerance 1e-6); white against black under
  α = 0: 0 (1e-6); the alpha ramp to 3.2e-4 (tolerance 0.002 plus the ramp's half 8-bit step,
  4.2e-4); Mix 0 bit for bit. On Arena's clips (`tools/clips.py alpha`): alpha 255 at Mix 1,
  within half a code of a/2 + 127.5 at Mix 0.5, 0 bytes changed by white under α = 0.
- **The host** (`--resize`, `--glstate`): exact.

Assumed or chosen, not measured: the 24 mm screen; the box bands; the 99% cut; that the
pattern on the axis serves the whole frame; straight alpha in Resolume; the averaging meter
as the Exposure criterion.

Not done: loading it into Resolume on any platform; a Windows build; a user guide, presets,
an OpenFX port, a browser demo; looking at the demo clips in a show rather than as numbers.

## Would this hold on another rasteriser, at another raster?

One line per check. Apple's software renderer at 320×180 passes every one of them, and
`--optimum` passes there at 1280×720.

- `--fresnel`: no GL; double.
- `--blur`: the tolerance is in texels of the level holding the quartile, so it scales with
  the raster; at 320×180 the 2 mm hole's interquartile width is 6 px, enough for the crossings.
- `--optimum`: needs 1280×720 (the minimum's width is 11 px there, 2.8 px at 180 lines); a
  ladder step of 2^(1/16) sets the resolution, not the rasteriser.
- `--falloff`, `--energy` (light and centroid), `--edges` (flat, flip, Black): half-float
  tolerances, which any GPU storing RGBA16F meets; the swap's denormal floor assumes a driver
  that keeps half-float denormals (one that flushes them would fail the far field's swap).
- `--edges` (Mirror against the reflected card): needs the raster's width to be a whole number
  of the coarsest texels (it refuses otherwise) and a renderer repeatable to 1e-5.
- `--zone`: an argmax over 10 nm steps; at 320×180 the point holds 0.12–0.14 of its light in
  one pixel, at 720p 0.023, both clear of their neighbours.
- `--view`: an exact pixel position.
- `--alpha`: 1e-6 is a few float roundings; Mix 0 is a texelFetch and a copy.
- `--resize`: only 640×360 → 320×180.
- `--glstate`: whatever the driver reports.

## The negative controls and mutants

`phtest --negative`, each wrong model required to fail (checks failed of those run), all 17
caught:

| check | wrong model | failed |
| --- | --- | --- |
| fresnel | the chirp dropped: the Fraunhofer (Airy) pattern at every u | 5 |
| fresnel | u detuned 2% in the chirp | 4 |
| fresnel | the geometric disc convolved with the Airy pattern | 5 |
| blur | the diameter taken as the radius | 4 |
| blur | every band at 550 nm | 2 |
| optimum | u doubled in the chirp | 1 |
| falloff | cos³ | 6 |
| zone | every band at 550 nm | 2 |
| zone | every zone radius squared 1.2 times too large | 2 |
| edges | Mirror and Black swapped | 6 |
| edges | Mirror repeating the edge pixel instead of reflecting | 2 |
| energy | the pyramid's level-1 ring left out | 6 |
| view | Obscura mirrors instead of turning | 2 |
| alpha | the clip's alpha passed through | 2 |
| alpha | the clip's light not weighted by its alpha | 2 |
| resize | the kernel not rebuilt when the raster changes | 1 |
| glstate | the host's GL state not put back | 2 |

The clamped Mirror passes the flat-field test (a flat field clamps to itself) and fails only
against the reflected card; the 2% detuned chirp passes the Airy limit (u = 0.01 has no chirp
to detune).

`tools/mutate.sh`: **51** one-character mutants of the shipped code (22 in the GLSL of
`Shaders.cpp`, 20 in `Optics.cpp`, 5 in `Pinhole.cpp`, 1 in `Pinhole.h`, 3 in `Controls.cpp`),
each run against the one check named for it at 320×180 (one at 1280×720), arm64, under a
600 s alarm: **all 51 caught** (verify, 16 November 2026). By the check that catches them:

| check | mutants | what they break |
| --- | --- | --- |
| `--fresnel` | 17 | the plate's zone count and every zone open, each open zone's inner edge, the bands' wavelengths, Lommel's u, the open wave's level, the chirp's phase, a ring's inner edge added, G's table, the trapezoid's end, Catmull–Rom, the cumulative's ring area and its in-between term, a tap's arc and its square, the rings' radii, the default hole |
| `--energy` | 10 | the taps' offsets and weights read from the wrong texels, the coarser level sampled a texel off, the sum buffer's margin and its stored offset, the upsample's rows, a coarse texel's mean, the next level's block stride, the arc inside a square, the taps' normalisation |
| `--falloff` | 7 | the sRGB decode's exponent and the encode's offset, the falloff's centre, cos², its power, a linear control's range, the field angle's tangent |
| `--alpha` | 5 | the light divided by α, the screen transparent, the screen's alpha subtracted, the clip's share 2 − Mix, Mix 1 handing the clip through |
| `--edges` | 5 | both reflections, Mirror and Black swapped (in GLSL and in C++), the sum buffers' right margin (at 1280×720) |
| `--blur` | 4 | the hole 1.2 times wide, the screen's v per pixel, a geometric control's range, the pixel's size |
| `--view` | 2 | Obscura turned about the wrong point, Obscura mirrored |
| `--resize` | 1 | a new raster taken for the same kernel |

No mutant is assigned to `--zone`, `--optimum` or `--glstate`; those three are covered by the
negative controls above alone.

## The sweep's honest list

`python3 tools/sweep.py`: all 9 controls change the picture (mean 8-bit difference between
their ends on the harness's card at 320×180: Exposure 92.5, View 61.4, Distance 25.6, Open
Zones 14.0, Mix 12.0, Diameter 10.3, Aperture 8.5, Zone Focus 1.27, Edge 0.52), each in a
context where it acts: Open Zones and Zone Focus under Aperture=Zone Plate, Edge with a 1.1 mm
hole. **`--bare`** (the defaults, no context) finds **Open Zones and Zone Focus dead**: they
belong to the plate, and the default is the pinhole. Diameter is likewise dead under the
plate. Edge stays alive bare (0.17) but faint: at the default hole the blur reaches two or
three pixels past the frame's edge at 180 lines.

## The browser demo (2026-11-18)

https://pinhole-demo.stoatworks-labs.com/, from `demo/`, deployed by `wrangler.toml` (a
Worker route: the zone is at Cloudflare's 100 custom domains, so `pinhole-demo` is a proxied
AAAA `100::` record made through the API) and `.github/workflows/deploy.yml` on every push to
main.

- **It runs the plugin, it doesn't port it.** `demo/pinhole-core.wasm` is `Pinhole.cpp` and
  everything it calls (Optics, Controls, PassBuffer, Shaders, Diag) plus the FFGL SDK's plugin
  classes, FFGLShader and FFGLFBO, compiled unmodified by emscripten
  (`demo/tools/build-wasm.sh`, fountain's and polyhedral's pattern). Chosen over a JavaScript
  port because the pattern builder (the pupil's projection through G(Y), one FFT per
  wavelength, the band means, in double) and the pyramid's taps are where the plugin's checks
  are, and a port would have only a reader to check it. Left out: the registration file
  (`EffectPlugin.cpp`). The script fails if a `source/*.cpp` it doesn't list appears.
- **What is the page's**: `demo/wasm/glue.cpp` plays the host (constructs the plugin, reads
  its declarations back through the SDK so the panel is built from them, forwards
  SetFloatParameter and ProcessOpenGL with Width equal to HardwareWidth and the canvas as the
  host FBO, as phtest's rig does, and hands the page the plugin's own read-outs: levels, taps,
  support, the light kept per band, the last rebuild's time, u); `demo/wasm/gl_shim.cpp` takes
  four GL calls (glShaderSource, refusing any text that isn't byte for byte the page's
  assembly of `demo/shaders.js`, then the kit's `port()`; glEnable/glDisable/glIsEnabled, for
  GL_PROGRAM_POINT_SIZE, which WebGL2 lacks and the plugin saves and restores); the read-outs'
  units, the tooltips, the Open Zones number field, the line under the picture, Diag's log
  shown from emscripten's memory file system, and the presets (written in millimetres,
  nanometres and EV, converted by the plugin's own inverse functions through `ph_param_from`).
- **Checked by `tools/verify.sh`** (`demo/tools/check_shaders.py`, before the build): every
  GLSL piece character for character, the order AssembleVertex()/Assemble() join them and
  InitGL's stage table; every uniform each pass declares set by the setter its GLSL type takes
  and nothing undeclared set (linear 2, down 3, ring 9, final 18: WebGL2 drops a wrong setter
  without an error); and the hashes of every input the .wasm was built from
  (`demo/wasm/inputs.sha256`, 34 files). A glUniform1f on the integer TapCount, a removed
  AlphaModel set and a changed source each failed it. Any change under `source/` needs
  `demo/tools/build-wasm.sh` rerun and its outputs committed.
- **Compared with phtest, once (2026-11-18), by `demo/tools/crosscheck.py`** (in the repo;
  nothing runs it by itself: it needs Chrome, cdpshot and a built phtest). Headless Chrome on
  the real GPU (ANGLE on Metal), the page paused; Mix 0 makes the final pass hand the clip back
  untouched, so the canvas read then is the exact input frame; that frame goes to
  `phtest --pipe` at the same raster with the same host values (the float32 the page sent).
  Apple M4 Max, page through ANGLE/Metal against phtest through Apple's OpenGL:

  | case | raster, clip | colour values off | max |
  | --- | --- | --- | --- |
  | defaults | 960×540 grid | 0 | 0 |
  | defaults | 960×540 scene | 0 | 0 |
  | zone plate | 960×540 grid | 0 | 0 |
  | zone plate, 8 zones at 450 nm | 960×540 spot | 1 of 1,555,200 | 1 code |
  | 2 mm hole at 10 mm | 960×540 grid | 0 | 0 |
  | 0.1 mm hole at 500 mm | 960×540 grid | 0 | 0 |
  | Obscura, Edge Black, 1.1 mm | 960×540 grid | 0 | 0 |
  | defaults | 1280×720 grid | 4 of 2,764,800 | 1 code |
  | zone plate | 1280×720 grid | 0 | 0 |

  Alpha identical in every case. The effect itself moves the clip by 1.9 to 26.1 codes on
  average across the cases; the negative control (phtest with Diameter one slider step, 0.001,
  off) puts 28,399 values off on the defaults case (mean 0.018 codes), so the comparison can
  see a step the panel can make.
- **Also checked on the page**: the read-outs at the defaults (0.306 mm, 550 nm, 50.0 mm,
  +0.20 EV) and the line under the picture (u = 1.70π, 3 coarser levels, 749 taps, 47.4 px of
  support, 99.0/99.2/99.3% kept); Diameter to its end changes the picture (mean 14.4 codes,
  max 137), Aperture to Zone Plate (7.8, 57), Open Zones 4 to 12 (4.35); Defaults gives the
  first frame back exactly; all seven presets render; a raster change (1920×1080, a pixel
  22.2 µm); parameters from the query string; embed mode; no horizontal scroll at 390 px; no
  console errors and no WebGL warnings (CDP's Runtime and Log, the real GPU). The rebuild
  takes 12–103 ms in WebAssembly here (103 at twelve open zones), against 10–11 ms natively.
- **No photosensitivity gate.** Pinhole keeps no time, draws nothing at random and has no
  audio: each frame is a blur and a fixed gain of the clip's current frame, so the picture
  changes only when the clip or a control does, and it cannot flash where the clip doesn't.
  The clips are the kit's, shared by every demo, and Pause holds them. Not measured.
- **Every gap, as the page discloses it** (banner, the "does not reproduce" list): it runs
  the plugin, not a port, and the panel's units, tooltips and presets are the page's; GL is
  emscripten's WebGL2 with four shimmed entry points and the kit's GLSL port (version line,
  highp defaults), GL_PROGRAM_POINT_SIZE reading as off; the RGBA16F pyramid needs
  EXT_color_buffer_float (refused without it) and the arithmetic is the browser's GPU, not the
  Mac's driver; the raster is the page's (the 24 mm screen makes a pixel 24 mm / H); the input
  is unpadded with the viewport at the origin, and the output an 8-bit canvas; the kit uploads
  premultiplied colour where the plugin expects straight, so the kit's alpha clip is left out;
  the patterns are rebuilt on the page's thread in WebAssembly (no fused multiply-add, musl's
  libm); the crosscheck ran once, on one GPU; no time and no randomness, so Pause, Step and
  Restart act on the clip alone; Open Zones is a number field and the About block is off the
  panel; nothing is measured, and the plugin has never been in Resolume.
- **Traps hit**: `cdpshot.Chrome.__init__` already calls `self.call()`, so a subclass that
  collects events must set its attributes before `super().__init__()`; a paused page draws
  exactly one frame per change, so read the frame count before dispatching the change, not
  after; `preserveDrawingBuffer` can be forced from outside with
  `Page.addScriptToEvaluateOnNewDocument` and a `getContext` override, leaving the page alone;
  `LastRebuildMs()` is 0 on every frame that doesn't rebuild, so the page keeps the last
  non-zero value; every vertex stage is the same text, so the shim's match list names only the
  first; `AssembleVertex( Pass )` with one return and no switch needed its own parse in
  `check_shaders.py`; the kit's hidden embed statement says "ported to WebGL2" even for a
  WebAssembly demo (kit text, left alone).

## Open questions

- **Straight or premultiplied alpha in Resolume's pipeline?** The plugin weights the clip's
  light by α (right for straight alpha, and Arena's DXV clips decode straight through ffmpeg).
  If Resolume hands an effect premultiplied colour, transparent edges are darkened twice.
  Needs a look in Arena.
- **Centre-metered or average-metered?** Exposure 0 keeps the centre at the clip's level; the
  default +0.2 EV brings a typical clip's mean back instead, so its centre is 15% brighter
  than the clip. Either is defensible; the demo clips were measured, not looked at.
- **Every measured optimum is 1.0–1.5% below Mielenz's** at 500 mm off the picture, and the
  builder's own minimum is 0.7% below 1.7π (1.6878π, as the prototype found before the build).
  Inside the tolerance, but one-sided; Mielenz's quadrature (v ≤ 5u) would bias his value the
  other way. Not explained.
- **Should the 99% cut be wider?** The veil of the outer rings is part of a pinhole's look;
  the cut drops the last 1% and renormalises.
- **A float32 pyramid** would keep a lone point's far light (0.9% of it is lost now) at about
  twice the bandwidth; nothing measured says it is visible.
- **The pyramid's centroid shift** (up to 0.67 px for a point at 720p with five levels) could
  be removed by jittering or by centring the boxes differently; it is under a pixel and moves
  with the point's position, so a moving point shimmers by that much. Not looked at in motion.
- **Off-axis patterns**: one pattern for the whole frame; at 10 mm the corner of a 16:9 frame
  is 67° off the axis, where the real hole is foreshortened and the pattern stretched.
- Should Distance reach below 10 mm (wider views and darker corners), or Diameter above 2 mm?
