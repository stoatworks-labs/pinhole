#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders, or its own copy of the physics, would pass every mutant here.
#
# One copy of the tree in a temporary directory (the FFGL SDK is symlinked,
# not copied), built arm64-only once. Each mutant edits its file in the copy,
# deletes that file's object (make compares mtimes to the second, so an edit
# within the same second as the last build would otherwise be missed), builds
# phtest, runs the named check at 320x180 (or the raster it names) under a
# time limit (a mutant can hang: honeydew's lesson; a timeout counts as
# caught), then puts the file back and deletes the object again. --resize
# always runs 640x360 -> 320x180 and the offline --fresnel ignores --size.
# (ferrofluid's script by way of fountain, filings and intensifier; pinhole's
# mutants.)
#
#     tools/mutate.sh          every mutant
#     ONLY=3 tools/mutate.sh   the third alone
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
# A check runs at 320x180 unless it names a raster: '--edges@1280x720' (a Black edge with
# no margin past it is off by 0.00098 at 320x180, exactly the visibility tolerance,
# and by 0.0048 at 1280x720).
# No mutant may make the kernel enormous: the plate's radius from n / lambda (1800 times
# too wide) had the OS kill phtest for memory (exit 137, not the alarm's 142), on a Mac
# other builds share. Every-zone-open replaced it.
# The fields are split on '|', so neither a target nor a mutant may contain one
# (conway's trap): no GLSL || here.
MUTANTS=(
	"source/Shaders.cpp|( c + 0.055 ) / 1.055, vec3( 2.4 ) );|( c + 0.055 ) / 1.055, vec3( 2.5 ) );|--falloff|GLSL: the sRGB decode's exponent 2.5, 4 -> 5"
	"source/Shaders.cpp|vec3( 1.0 / 2.4 ) ) - 0.055;|vec3( 1.0 / 2.4 ) ) - 0.065;|--falloff|GLSL: the sRGB encode's offset 0.065, 5 -> 6"
	"source/Shaders.cpp|		i = -1 - i;|		i = -0 - i;|--edges|GLSL: the left and bottom reflection about the edge pixel's centre, 1 -> 0"
	"source/Shaders.cpp|return i < n ? i : 2 * n - 1 - i;|return i < n ? i : 2 * n - 2 - i;|--edges|GLSL: the right and top reflection a pixel in, 1 -> 2"
	"source/Shaders.cpp|	if( EdgeMode == 1 )|	if( EdgeMode == 0 )|--edges|GLSL: Mirror black and Black mirrored, 1 -> 0"
	"source/Shaders.cpp|ivec2( 2 * t + 1, TapRow ), 0 );|ivec2( 2 * t - 1, TapRow ), 0 );|--energy|GLSL: each tap's weights read from the texel before, + -> -"
	"source/Shaders.cpp|ivec2( 2 * t, TapRow ), 0 );|ivec2( 3 * t, TapRow ), 0 );|--energy|GLSL: the taps' offsets read with the wrong stride, 2 -> 3"
	"source/Shaders.cpp|( vec2( at ) + 0.5 ) * 0.5 - 0.5;|( vec2( at ) + 0.5 ) * 0.5 + 0.5;|--energy|GLSL: the coarser level sampled a texel off, - -> +"
	"source/Shaders.cpp|ivec2 b    = ivec2( base ) + 1;|ivec2 b    = ivec2( base ) + 2;|--energy|GLSL: the sum buffer's margin skipped twice, 1 -> 2"
	"source/Shaders.cpp|mix( c01, c11, f.x ), f.y );|mix( c01, c11, f.x ), f.x );|--energy|GLSL: the bilinear upsample's rows blended by x, y -> x"
	"source/Shaders.cpp|Premultiply == 1 ? lin * a : lin, a );|Premultiply == 1 ? lin / a : lin, a );|--alpha|GLSL: the clip's light divided by its alpha, * -> /"
	"source/Shaders.cpp|FragColor = vec4( 0.25 * sum, 1.0 );|FragColor = vec4( 0.35 * sum, 1.0 );|--energy|GLSL: a coarse texel 1.4 times its block's mean, 2 -> 3"
	"source/Shaders.cpp|ivec2 at = ivec2( gl_FragCoord.xy ) * 2;|ivec2 at = ivec2( gl_FragCoord.xy ) * 3;|--energy|GLSL: the next level's blocks 3 texels apart, 2 -> 3"
	"source/Shaders.cpp|ivec2 at  = ivec2( gl_FragCoord.xy ) - 1;|ivec2 at  = ivec2( gl_FragCoord.xy ) - 2;|--energy|GLSL: a ring's sum stored a texel off its margin, 1 -> 2"
	"source/Shaders.cpp|pic = Size - 1 - at;|pic = Size - 2 - at;|--view|GLSL: Obscura turned about a point half a pixel off, 1 -> 2"
	"source/Shaders.cpp|+ 0.5 - 0.5 * vec2( Size ) )|+ 0.5 - 0.6 * vec2( Size ) )|--falloff|GLSL: the falloff centred off the axis, 5 -> 6"
	"source/Shaders.cpp|float c2   = 1.0 / ( 1.0 + dot( t, t ) );|float c2   = 1.0 / ( 2.0 + dot( t, t ) );|--falloff|GLSL: cos^2 halved, 1 -> 2"
	"source/Shaders.cpp|float fall = pow( c2, 0.5 * CosPower );|float fall = pow( c2, 0.6 * CosPower );|--falloff|GLSL: cos^4.8 for cos^4, 5 -> 6"
	"source/Shaders.cpp|AlphaModel == 1 ? src.a : 1.0;|AlphaModel == 1 ? src.a : 0.0;|--alpha|GLSL: the screen transparent, 1 -> 0"
	"source/Shaders.cpp|src.a * keep + outA * MixAmount );|src.a * keep - outA * MixAmount );|--alpha|GLSL: the screen's alpha subtracted, + -> -"
	"source/Shaders.cpp|float keep = 1.0 - MixAmount;|float keep = 2.0 - MixAmount;|--alpha|GLSL: the clip's share 2 - Mix, 1 -> 2"
	"source/Shaders.cpp|	if( MixAmount <= 0.0 )|	if( MixAmount <= 1.0 )|--alpha|GLSL: Mix 1 hands the clip through, 0 -> 1"
	"source/Optics.cpp|a.radius = hooks.diameterAsRadius ? diameter : 0.5 * diameter;|a.radius = hooks.diameterAsRadius ? diameter : 0.6 * diameter;|--blur|C++: the hole 1.2 times as wide as asked, 5 -> 6"
	"source/Optics.cpp|const int zones = 2 * open - 1;|const int zones = 2 * open + 1;|--fresnel|C++: the plate's zones counted two over, - -> +"
	"source/Optics.cpp|for( int n = 1; n <= zones; n += 2 )|for( int n = 1; n <= zones; n += 1 )|--fresnel|C++: every zone open, the plate a plain hole, 2 -> 1"
	"source/Optics.cpp|std::sqrt( static_cast< double >( n - 1 ) / zones )|std::sqrt( static_cast< double >( n - 2 ) / zones )|--fresnel|C++: each open zone starts a zone early, 1 -> 2"
	"source/Optics.cpp|return kBandLow[ c ] + ( sample + 0.5 )|return kBandLow[ c ] + ( sample + 1.5 )|--fresnel|C++: each band's wavelengths a sub-band long, 0 -> 1"
	"source/Optics.cpp|const double u = 2.0 * kPi * R * R / ( wavelength * distance );|const double u = 3.0 * kPi * R * R / ( wavelength * distance );|--fresnel|C++: Lommel's u 1.5 times, 2 -> 3"
	"source/Optics.cpp|const double toOpen = u * u / ( 4.0 * kPi * kPi );|const double toOpen = u * u / ( 5.0 * kPi * kPi );|--fresnel|C++: the open wave's level 5 pi^2, 4 -> 5"
	"source/Optics.cpp|std::polar( 1.0, 0.5 * uc * xx ) * sum;|std::polar( 1.0, 0.6 * uc * xx ) * sum;|--fresnel|C++: the chirp's phase 0.6 u x^2, 5 -> 6"
	"source/Optics.cpp|( G( std::sqrt( b2 ) ) - ( a2 > 0.0|( G( std::sqrt( b2 ) ) + ( a2 > 0.0|--fresnel|C++: a ring's inner edge added, - -> +"
	"source/Optics.cpp|part += w3[ q ] * std::polar( 1.0, 0.5 * u * y * y );|part += w3[ q ] * std::polar( 1.0, 0.6 * u * y * y );|--fresnel|C++: G's table with 0.6 u y^2, 5 -> 6"
	"source/Optics.cpp|buffer[ ( N - k ) % N ] - p0 );|buffer[ ( N - k ) % N ] + p0 );|--fresnel|C++: the trapezoid's end counted thrice, - -> +"
	"source/Optics.cpp|2.0 * p0 - 5.0 * p1 + 4.0 * p2|2.0 * p0 - 6.0 * p1 + 4.0 * p2|--fresnel|C++: Catmull-Rom's second coefficient, 5 -> 6"
	"source/Optics.cpp|vPerPx[ c ][ s ] = 2.0 * kPi * R * pixel|vPerPx[ c ][ s ] = 3.0 * kPi * R * pixel|--blur|C++: the screen's v a pixel 1.5 times, 2 -> 3"
	"source/Optics.cpp|( ev[ k ] * ( rk * dr + 0.5 * dr * dr )|( ev[ k ] * ( rk * dr + 0.6 * dr * dr )|--fresnel|C++: the cumulative's ring area, 5 -> 6"
	"source/Optics.cpp|slope * ( 0.5 * rk * t * t + t * t * t / 3.0 ) );|slope * ( 0.5 * rk * t * t + t * t * t / 4.0 ) );|--fresnel|C++: the cumulative between samples, 3 -> 4"
	"source/Optics.cpp|AngleInside( a + ( m + 0.5 ) * h, x0|AngleInside( a + ( m + 1.5 ) * h, x0|--fresnel|C++: a tap's arc taken a step out, 0 -> 1"
	"source/Optics.cpp|const double x0 = i * s - 0.5 * s, x1|const double x0 = i * s - 0.6 * s, x1|--fresnel|C++: a tap's square 1.1 texels wide, 5 -> 6"
	"source/Optics.cpp|std::acos( std::min( 1.0, a1 / r ) ), std::asin|std::acos( std::min( 1.0, a0 / r ) ), std::asin|--energy|C++: the arc inside a square from its near side, 1 -> 0"
	"source/Optics.cpp|static_cast< float >( tap.w[ c ] / kernel.sum[ c ] )|static_cast< float >( tap.w[ c ] * kernel.sum[ c ] )|--energy|C++: the taps scaled by their sum, / -> *"
	"source/Optics.cpp|kCoreRadius * std::ldexp( 1.0, level - 1 );|kCoreRadius * std::ldexp( 1.0, level - 2 );|--fresnel|C++: each ring starting at half its radius, 1 -> 2"
	"source/Controls.cpp|return low * std::pow( high / low, clamp01( v ) );|return low * std::pow( high * low, clamp01( v ) );|--blur|C++: a geometric control's range high x low, / -> *"
	"source/Controls.cpp|return low + ( high - low ) * clamp01( v );|return low + ( high + low ) * clamp01( v );|--falloff|C++: a linear control's range high + low, - -> +"
	"source/Controls.cpp|std::sqrt( 2.0 * optics::kOptimumU|std::sqrt( 3.0 * optics::kOptimumU|--fresnel|C++: the default hole sqrt( 3 u / pi ) wide, 2 -> 3"
	"source/Pinhole.cpp|optics::kFrameHeight / height / s.distance|optics::kFrameHeight / height * s.distance|--falloff|C++: the field angle's tangent times the distance, / -> *"
	"source/Pinhole.cpp|( hooks.mirrorView ? 2 : 1 ) : 0 );|( hooks.mirrorView ? 2 : 2 ) : 0 );|--view|C++: Obscura mirrored, 1 -> 2"
	"source/Pinhole.h|zones == o.zones && height == o.height|zones == o.zones && height != o.height|--resize|C++: a new raster counted as the same kernel, = -> !"
	"source/Pinhole.cpp|ceilShift( width, k ) + 2, ceilShift|ceilShift( width, k ) + 1, ceilShift|--edges@1280x720|C++: the sum buffers' right margin missing, 2 -> 1"
	"source/Pinhole.cpp|edgeClamped ? 2 : 0 );|edgeClamped ? 2 : 1 );|--edges|C++: Mirror drawn as Black, 0 -> 1"
	"source/Pinhole.cpp|const double pixel    = optics::kFrameHeight / height;|const double pixel    = optics::kFrameHeight * height;|--blur|C++: the pixel's size times the raster, / -> *"
)

limit() {
	# macOS has no timeout(1): perl's alarm is enough.
	perl -e 'alarm shift; exec @ARGV' "$@"
}

tree="$WORK/tree"
mkdir -p "$tree/external"
cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
ln -s "$REPO/external/ffgl" "$tree/external/ffgl"
cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 -DPINHOLE_BUILD_TOOLS=ON >/dev/null
cmake --build "$tree/build" --target phtest -j8 >/dev/null 2>&1 || { echo "the unmutated copy does not build"; exit 1; }

caught=0
ran=0
index=0
for entry in "${MUTANTS[@]}"; do
	index=$(( index + 1 ))
	[[ -n "${ONLY:-}" && "$ONLY" != "$index" ]] && continue
	ran=$(( ran + 1 ))
	IFS='|' read -r file original mutant check what <<<"$entry"
	cp "$REPO/$file" "$tree/$file"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	# A source file's own object; a header's dependents are not known here,
	# so every object goes (a full rebuild for that mutant).
	object="$( basename "$file" ).o"
	[[ "$file" == *.h ]] && object='*.o'
	find "$tree/build" -name "$object" -delete
	# The binary too: a relink skipped would run the last mutant's phtest.
	rm -f "$tree/build/phtest"
	printf '\n== mutant %d: %s\n' "$index" "$what"
	status=0
	if ! cmake --build "$tree/build" --target phtest -j8 >"$WORK/build.log" 2>&1; then
		# A mutant that does not compile proves nothing about the checks.
		printf '   FAIL  the mutant does not build -- choose another\n'
		tail -5 "$WORK/build.log" | sed 's/^/        /'
	elif [[ "$file" == source/Shaders.cpp ]] && ! LC_ALL=C grep -qF -- "$mutant" "$tree/build/phtest"; then
		# A GLSL mutant is a string literal: it must be in the binary, verbatim.
		printf '   FAIL  the mutant is not in the built binary -- the build did not pick it up\n'
	else
		size=320x180
		if [[ "$check" == *@* ]]; then
			size="${check#*@}"
			check="${check%@*}"
		fi
		limit 600 "$tree/build/phtest" "$check" --size "$size" >"$WORK/log" 2>&1 || status=$?
		if [[ "$status" -eq 0 ]]; then
			printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
		else
			printf '   ok    %s fails against the mutant (exit %d%s):\n' "$check" "$status" "$( [[ "$status" -eq 142 ]] && echo ', the time limit' )"
			# A mutant that timed out printed no FAIL: grep finding none must not end the run.
			{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
			caught=$(( caught + 1 ))
		fi
	fi
	cp "$REPO/$file" "$tree/$file"
	find "$tree/build" -name "$object" -delete
done

printf '\nmutants: %d, caught: %d\n' "$ran" "$caught"
[[ "$ran" -gt 0 && "$caught" -eq "$ran" ]]
