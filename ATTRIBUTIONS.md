# Attributions

Pinhole is built on other people's work. This file lists what that work is, who did
it, and what it is doing here.

It is generated — the master lists live in the `stoatworks-backend` repo and are
pushed out by `scripts/sync-attributions.py`. Edit it there, not here.

## Code we derived from other people's work

Someone else solved this first, and this project would not exist in its current form without their work.

### The harness and the tooling — Stoatworks intensifier, and filings, fountain and ferrofluid before it

<https://github.com/stoatworks-labs/intensifier>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's rig, PNG writer, cue sheets, --set by display name, --pipe/--film (SIGPIPE ignored, a closed stdout an exit of 1), --negative with its Perturb, and --offline (tools/phtest is intensifier's ittest, which is filings' fgtest, fountain's fttest and ferrofluid's fetest, under new checks); tools/verify.sh, tools/mutate.sh (with fountain's perl alarm), tools/glslc.sh, tools/sweep.py and tools/clips.py. All adapted.

### The pass buffers, the GL state and the log — Stoatworks millpond and radar

<https://github.com/stoatworks-labs/millpond>  
Licence: MIT  
Copyright: Stoatworks Labs

source/PassBuffer.{h,cpp} and source/Diag.{h,cpp} are millpond's, by way of filings and intensifier. source/GLState.h is radar's, which is vectrix's, from resolume-scopes; pinhole lets the harness's negative control skip the restore.

### The release workflow and the DLL check — Stoatworks intensifier, xenon and penumbra

<https://github.com/stoatworks-labs/xenon>  
Licence: MIT  
Copyright: Stoatworks Labs

.github/workflows/ci.yml and release.yml are intensifier's, which are xenon's (slitscan's before it): a universal bundle and a Windows DLL, ATTRIBUTIONS.md in each zip, the Windows installer per user into Resolume's Extra Effects. tools/wincheck.ps1, which loads the DLL as a host loads it and reads back its name, id, type and parameters, is penumbra's (pepper's). Adapted.

### The browser demo's build and kit — Stoatworks fountain, and polyhedral before it

<https://github.com/stoatworks-labs/fountain>  
Licence: MIT  
Copyright: Stoatworks Labs

demo/tools/build-wasm.sh, which compiles the plugin's own C++ to WebAssembly unmodified, demo/wasm/glue.cpp (the page as the host), demo/wasm/gl_shim.cpp and demo/tools/check_shaders.py are fountain's, polyhedral's pattern; the page around them is the fleet's shared demo kit (demo/vendor/). Adapted.

### The look card — Stoatworks schlieren

<https://github.com/stoatworks-labs/schlieren>  
Licence: MIT  
Copyright: Stoatworks Labs

The harness's test card (a sunset sky, a sun, a skyline, colour bars), the default clip in phtest and the picture the Mirror and sweep checks use.

## Third-party code this project uses

Libraries, SDKs and frameworks the project is built on or bundles.

### Resolume FFGL SDK

<https://github.com/resolume/ffgl>  
Licence: BSD-3-Clause  
Copyright: FreeFrame

Vendored as a git submodule at external/ffgl (third_party/ffgl in oxbow).

The plugin ABI itself. An FFGL effect or source is defined by this SDK's headers — there is no other way to be loadable by Resolume Arena and Avenue.

### GLEW — the OpenGL Extension Wrangler Library

<https://github.com/nigels-com/glew>  
Licence: BSD-3-Clause (with Mesa 3-D and Khronos components)  
Copyright: Milan Ikits, Marcelo E. Magallon and Lev Povalahev

Arrives inside the FFGL submodule at external/ffgl/deps/glew-2.1.0. Not fetched separately.

Resolves OpenGL entry points on Windows, where the system headers stop at OpenGL 1.1.

### libpng

<http://www.libpng.org/pub/png/libpng.html>  
Licence: PNG Reference Library License (libpng)  
Copyright: the PNG Reference Library authors

Arrives inside the FFGL submodule, under the SDK's CustomThumbnail sample.

Part of the upstream SDK tree rather than something these plugins call directly — listed because it is present in the checkout.

## Work we checked ourselves against

No code was taken from these — but they were how we knew we had it right, and that is worth saying out loud.

### On pin-hole photography — Lord Rayleigh, Phil. Mag. S. 5, 31(189), 87–99 (1891)

The Fresnel integral over a circular hole in Lommel's variables (eqs 4–9), the axis 4 sin²(u/4) (eq. 20), the first-zone hole (eq. 26), his experiment's best hole at y = 1.8π (p. 99), and Lommel's tables of the pattern (pp. 94–96), which phtest --fresnel checks the pattern builder against: 69 of the 80 printed values to half the last digit, every disagreement listed. Read from the Taylor & Francis PDF's page images on 2026-11-16.

### On the diffraction limit for lensless imaging — K. D. Mielenz, J. Res. NIST 104(5), 479–485 (1999)

The area width of a point's image and its minimum at u = 1.7π (eq. 9), the plugin's optimum and the default hole; the Airy limit (under eq. 7a) and the Airy area width 16/(3π) (Table 1). Read on 2026-11-16.

### Zone plates, X-Ray Data Booklet §4.4 — J. Kirz and D. Attwood, Lawrence Berkeley National Laboratory

The zone radii r_n² = nλf (eq. 1), the first-order efficiency 1/π² and the shares of the other orders, and the chromatic limit N < λ/Δλ: the Zone Plate aperture. Read as HTML with its equation images at xdb.lbl.gov on 2026-11-16.

### On cosine-fourth and vignetting effects in real lenses — M. Aggarwal, H. Hua and N. Ahuja, ICCV 2001

The image irradiance's cos⁴ fall-off with the angle off the axis, and that vignetting is absent for small apertures: the plugin's falloff, checked by phtest --falloff. Read on 2026-11-16.

### Wikipedia, "135 film"

The 24 × 36 mm frame (and ISO 1007, which it cites and which was not read): the screen's 24 mm height. Read on 2026-11-16.

## Standards and published specifications

What the implementation is measured against.

- **IEC 61966-2-1 (sRGB)** — The clip's decoding and the output's encoding (source/Shaders.cpp).

## Getting this wrong

If your work is here and the description is inaccurate, the licence is wrong, or you would rather not be listed — open an issue and it will be fixed.
