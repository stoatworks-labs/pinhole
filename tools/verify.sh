#!/usr/bin/env bash
#
# Everything, in the order that fails fastest. (ferrofluid's script by way of
# fountain, filings and intensifier; pinhole's steps.)
#
# The build is universal and fresh on purpose. An arm64-only bundle builds and
# tests perfectly well here and then fails to load in an Intel Resolume, and
# the build log calls it a success either way -- so the architecture is
# checked with lipo, never with the log. KEEP_BUILD=1 reuses the build
# directory instead of deleting it first.
#
#     OXBOW=/path/to/oxbow tools/verify.sh [BUILD_DIR]
#
# oxbow is required: a host's reading of the bundle (id, name, type, a
# rendered selftest) is the only check that the FFGL name field survives, and
# a verify that passes with it skipped has not verified the bundle. So are
# Resolume Arena's demo clips: the alpha decision is checked on two of them.
#
# CI (.github/workflows/ci.yml) builds on macOS and Windows and runs the
# checks that need no GL (--list, --offline, glslc); the pixel checks need a
# GPU and Arena's clips, so this script is where they run, on a Mac.
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="${1:-$REPO/build-verify}"

cd "$REPO"

step() { printf '\n\033[1m== %s\033[0m\n' "$1"; }
fail() { printf '\033[31mFAIL\033[0m %s\n' "$1"; exit 1; }

#---------------------------------------------------------------------------
# GLSL reserved words, declared as identifiers.
#
# glslc catches these, but the trap most likely to be walked into gets its own
# grep that always runs. The list is GLSL 4.10's keywords reserved for future
# use (3.6) plus the ones already bitten: `packed` passed Apple's compiler and
# glslc and failed only Mesa (atrac); noise1..noise4 are built-ins a user
# function is silently shadowed by (dice); `any` is a built-in function and
# `far`/`near` are reserved on some compilers.
#---------------------------------------------------------------------------
reserved_words() {
	local words="common partition active asm class union enum typedef template this resource goto inline noinline
	             public static extern external interface long short half fixed unsigned superp input output filter
	             sizeof cast namespace using packed sample patch layout flat smooth noperspective precise
	             noise1 noise2 noise3 noise4 any far near"
	local bad=0 word
	for word in $words; do
		if grep -nE "(float|int|uint|bool|vec[234]|ivec[234]|uvec[234]|mat[234])[[:space:]]+$word[[:space:]]*[;=,)(\[]" \
		            source/Shaders.cpp >/dev/null 2>&1; then
			printf '   "%s" is declared as an identifier and is a GLSL reserved word\n' "$word"
			bad=$(( bad + 1 ))
		fi
	done
	# M_PI is not in every compiler's prelude: the fleet writes kPi.
	if grep -n 'M_PI' source/*.cpp source/*.h >/dev/null 2>&1; then
		printf '   M_PI is used; write kPi\n'
		bad=$(( bad + 1 ))
	fi
	[ "$bad" -eq 0 ] && printf '   none of the reserved words is used as an identifier, and no M_PI\n'
	return "$bad"
}

step "GLSL reserved words"
reserved_words || fail "a GLSL reserved word is used as an identifier"

#---------------------------------------------------------------------------
step "Shaders"
#---------------------------------------------------------------------------
tools/glslc.sh || fail "a shader does not compile"

#---------------------------------------------------------------------------
step "Submodule"
#---------------------------------------------------------------------------
if [[ ! -f external/ffgl/CMakeLists.txt ]]; then
	fail "FFGL SDK missing -- run: git submodule update --init --recursive"
fi
pin="$(git -C "$REPO/external/ffgl" rev-parse --short=7 HEAD)"
[[ "$pin" == "b1afaf9" ]] || fail "FFGL SDK at $pin, not the fleet's b1afaf9"
echo "ok   FFGL SDK pinned at $pin"

#---------------------------------------------------------------------------
step "Build (universal, Release, fresh)"
#---------------------------------------------------------------------------
[[ "${KEEP_BUILD:-0}" == 1 ]] || rm -rf "$BUILD"
log="$( mktemp )"
cmake -B "$BUILD" -DCMAKE_BUILD_TYPE=Release -DPINHOLE_BUILD_TOOLS=ON >"$log" 2>&1 || { cat "$log"; fail "configure"; }
cmake --build "$BUILD" -j8 >"$log" 2>&1 || { tail -40 "$log"; fail "build"; }
# The SDK's own FFGLUtilities.cpp warns about RAND_MAX; anything else is ours.
if grep -E 'warning:' "$log" | grep -v 'external/ffgl' | grep -q .; then
	grep -E 'warning:' "$log" | grep -v 'external/ffgl' | sed 's/^/   /'
	fail "a warning in this repo's code"
fi
rm -f "$log"
echo "ok   built, no warnings outside the SDK"
PHTEST="$BUILD/phtest"

#---------------------------------------------------------------------------
step "Bundle"
#---------------------------------------------------------------------------
OXBOW="${OXBOW:-$HOME/Projects/resolume/oxbow/build/oxbow}"
[[ -x "$OXBOW" ]] || fail "no oxbow at $OXBOW -- build it and pass OXBOW=/path/to/oxbow (it is not optional here)"
declared="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' CMakeLists.txt | head -1 )"
grep -q "versionFallback = \"v$declared\"" source/StoatworksAbout.h || fail "StoatworksAbout.h's versionFallback is not v$declared"
check_bundle() {
	local name="$1" id="$2" display="$3" ffid="$4" kind="$5"
	local bundle="$BUILD/$name.bundle" binary="$BUILD/$name.bundle/Contents/MacOS/$name"
	[[ -f "$binary" ]] || fail "no binary at $binary"
	local arches; arches="$(lipo -archs "$binary")"
	[[ "$arches" == *arm64* ]]  || fail "$name: no arm64 slice (got: $arches)"
	[[ "$arches" == *x86_64* ]] || fail "$name: no x86_64 slice (got: $arches)"
	# Captured, then matched from a herestring -- never `nm ... | grep -q`: under
	# pipefail the grep's early exit SIGPIPEs nm and fails the pipeline.
	local symbols; symbols=$( nm -gU "$binary" 2>/dev/null || true )
	grep -q '_plugMain' <<<"$symbols" || fail "$name: plugMain not exported"
	local plist="$bundle/Contents/Info.plist"
	[[ -f "$plist" ]] || fail "$name: no Info.plist"
	read_plist() { /usr/libexec/PlistBuddy -c "Print :$1" "$plist" 2>/dev/null || true; }
	[[ "$( read_plist CFBundleIdentifier )" == "$id" ]] || fail "$name: bundle id is '$( read_plist CFBundleIdentifier )'"
	[[ "$( read_plist CFBundleExecutable )" == "$name" ]] || fail "$name: CFBundleExecutable is wrong"
	[[ "$( read_plist CFBundlePackageType )" == "BNDL" ]] || fail "$name: CFBundlePackageType is not BNDL"
	[[ "$( read_plist CFBundleVersion )" == "$declared" ]] || fail "$name: plist version != CMakeLists $declared"
	codesign --force --sign - --timestamp=none "$bundle" >/dev/null 2>&1 || fail "$name: could not be ad-hoc signed"
	codesign --verify --deep --strict "$bundle" >/dev/null 2>&1 || fail "$name: the ad-hoc signature does not verify"
	echo "ok   $name: $arches, plugMain exported, $id, BNDL, v$declared, ad-hoc signed and verified"
	# What a host reads: id, name, type. The FFGL name field is char[16] and not
	# null-terminated, so only something reading it back as a host does notices
	# a truncation. The selftest renders frames through the bundle as a host
	# would.
	local probe; probe="$( "$OXBOW" probe "$bundle" 2>&1 || true )"
	printf '%s\n' "$probe" | sed -n '1,3p' | sed 's/^/   /'
	grep -q "id:          $ffid" <<<"$probe" || fail "oxbow did not read the id $ffid"
	grep -q "name:        $display\$" <<<"$probe" || fail "oxbow did not read the name '$display'"
	grep -q "type:        $kind" <<<"$probe" || fail "oxbow did not read the type as $kind"
	local selftest; selftest="$( "$OXBOW" selftest "$bundle" 2>&1 || true )"
	grep -q "selftest:    PASS" <<<"$selftest" || { printf '%s\n' "$selftest" | tail -5; fail "oxbow selftest failed on $name"; }
	echo "ok   a host reads $display $ffid $kind, and oxbow selftest renders through it"
}
check_bundle "Pinhole" "com.stoatworks.ffgl.pinhole" "SW Pinhole" "PC01" "effect"

#---------------------------------------------------------------------------
step "Checks (this Mac's GPU, 1280x720 and 320x180)"
#---------------------------------------------------------------------------
# Every claim, one flag each, each at both rasters. The list is read from
# phtest itself, so a new check cannot be left out here. --resize always runs
# 640x360 -> 320x180; --optimum measures at 1280x720 alone (at 320x180 the
# kernel's width is a pixel or two, too coarse to find the minimum: it says
# so and passes nothing).
CHECKS="$( "$PHTEST" --help | sed -n 's/^  checks, one flag each://p' | tr -d '-' )"
[[ -n "$CHECKS" ]] || fail "could not read the list of checks from phtest --help"
for check in $CHECKS; do
	if out=$( "$PHTEST" --$check 2>&1 ); then
		printf '   ok   phtest --%s: %s measurements\n' "$check" "$( printf '%s\n' "$out" | grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | grep -E 'FAIL' | sed 's/^/      /'
		fail "phtest --$check"
	fi
done

#---------------------------------------------------------------------------
step "Software renderer (Apple's: every check at 320x180, --optimum at 1280x720)"
#---------------------------------------------------------------------------
# Every check again on Apple's software renderer: another rasteriser, another
# float pipeline. All of them, not a subset (AGENTS.md: what holds where).
for check in $CHECKS; do
	if out=$( PHTEST_RENDERER=software "$PHTEST" --$check --size 320x180 2>&1 ); then
		printf '   ok   phtest --%s (software): %s measurements\n' "$check" "$( printf '%s\n' "$out" | grep -c '^  ok' )"
	else
		printf '%s\n' "$out" | grep -E 'FAIL' | sed 's/^/      /'
		fail "phtest --$check on the software renderer -- run: PHTEST_RENDERER=software $PHTEST --$check --size 320x180"
	fi
done
# --optimum measures nothing at 320x180 (it says so): once more at 1280x720,
# the raster it needs. About three minutes on the software renderer.
if out=$( PHTEST_RENDERER=software "$PHTEST" --optimum --size 1280x720 2>&1 ) && grep -q '^  ok' <<<"$out"; then
	printf '   ok   phtest --optimum (software, 1280x720): %s measurements\n' "$( printf '%s\n' "$out" | grep -c '^  ok' )"
else
	printf '%s\n' "$out" | grep -E 'FAIL' | sed 's/^/      /'
	fail "phtest --optimum on the software renderer at 1280x720"
fi

#---------------------------------------------------------------------------
step "Offline"
#---------------------------------------------------------------------------
"$PHTEST" --offline >/dev/null || fail "phtest --offline"
echo "ok   phtest --offline"

#---------------------------------------------------------------------------
step "Alpha on Resolume's clips"
#---------------------------------------------------------------------------
python3 tools/clips.py alpha --build "$BUILD" | grep -E '^  (ok|FAIL)' || fail "the alpha check on the demo clips"

#---------------------------------------------------------------------------
step "Pipe"
#---------------------------------------------------------------------------
# The fleet's --pipe frame format, which the video renders through. Two and a
# half frames in must be exactly two out and a clean exit -- a partial frame is
# the end of the stream, never a frame -- --film N must be N frames, a cue
# naming no parameter, a --set that is not a number or an option and a --set
# of nan must be refused, and a reader that hangs up must end the run with
# exit 1, not SIGPIPE's silent 141. Under bash: zsh has no PIPESTATUS.
frame=$(( 64 * 36 * 4 ))
raw=$( mktemp ); cues=$( mktemp ); out=$( mktemp )
/usr/bin/head -c $(( frame * 5 / 2 )) /dev/zero > "$raw"
status=0
"$PHTEST" --pipe --size 64x36 < "$raw" > "$out" 2>/dev/null || status=$?
got=$( wc -c < "$out" | tr -d ' ' )
[[ "$status" -eq 0 && "$got" == "$(( frame * 2 ))" ]] \
	|| fail "2.5 frames in gave $got bytes out (want $(( frame * 2 ))), exit $status"
status=0
"$PHTEST" --film 3 --size 64x36 > "$out" 2>/dev/null || status=$?
got=$( wc -c < "$out" | tr -d ' ' )
[[ "$status" -eq 0 && "$got" == "$(( frame * 3 ))" ]] || fail "--film 3 gave $got bytes, exit $status"
# Read from a file, not a pipe: a writer killed by SIGPIPE would fail the
# pipeline whatever phtest did, and the refusal would pass for the wrong reason.
printf '0 No Such Control 0.5\n' > "$cues"
status=0
"$PHTEST" --pipe --size 64x36 --script "$cues" < "$raw" >/dev/null 2>&1 || status=$?
[[ "$status" -eq 2 ]] || fail "a cue naming no parameter gave exit $status, not 2"
for bad in "Aperture=Nope" "Diameter=abc" "Diameter=nan"; do
	status=0
	"$PHTEST" --film 1 --size 64x36 --set "$bad" >/dev/null 2>&1 || status=$?
	[[ "$status" -eq 2 ]] || fail "--set '$bad' gave exit $status, not 2"
done
status=0
"$PHTEST" --film 1 --size 64x36 --set "Aperture=Zone Plate" >/dev/null 2>&1 || status=$?
[[ "$status" -eq 0 ]] || fail "--set 'Aperture=Zone Plate' (an option by name) gave exit $status"
/usr/bin/head -c $(( frame * 20 )) /dev/zero > "$raw"
set +e
"$PHTEST" --pipe --size 64x36 < "$raw" 2>/dev/null | /usr/bin/head -c 1 >/dev/null
s1=${PIPESTATUS[0]}
"$PHTEST" --film 20 --size 64x36 2>/dev/null | /usr/bin/head -c 1 >/dev/null
s2=${PIPESTATUS[0]}
set -e
[[ "$s1" -eq 1 && "$s2" -eq 1 ]] || fail "a closed stdout gave exit $s1 (--pipe), $s2 (--film), not 1"
echo "ok   2.5 frames in, exactly 2 out; --film 3 is 3; a bad cue, a bad option, abc and nan refused (exit 2); | head -c 1 exits 1, not 141"
rm -f "$raw" "$cues" "$out"

#---------------------------------------------------------------------------
step "Negative controls"
#---------------------------------------------------------------------------
# Every check above, against a model that is deliberately wrong, required to
# FAIL. A check that cannot fail is not a check.
if out=$( "$PHTEST" --negative 2>&1 ); then
	printf '%s\n' "$out" | grep -E '^negative controls' | sed 's/^/   /'
else
	printf '%s\n' "$out" | grep -E 'PASSED against|undetected' | sed 's/^/      /'
	fail "a negative control went undetected"
fi

#---------------------------------------------------------------------------
step "Mutants"
#---------------------------------------------------------------------------
tools/mutate.sh | grep -E '^== mutant|^   (ok|FAIL)|^mutants' || fail "a mutant survived"

#---------------------------------------------------------------------------
step "Dead controls"
#---------------------------------------------------------------------------
# The only thing that catches a uniform whose name does not match the C++.
python3 tools/sweep.py --build "$BUILD" || fail "a dead control"
# The honesty check: with no per-control context, what goes dead? (Recorded
# in AGENTS.md; not a failure.)
python3 tools/sweep.py --build "$BUILD" --bare || true

#---------------------------------------------------------------------------
step "Cost"
#---------------------------------------------------------------------------
"$PHTEST" --bench

printf '\n\033[32mall green\033[0m\n'
