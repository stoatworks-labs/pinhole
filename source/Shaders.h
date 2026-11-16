#pragma once

#include <string>

/**
    The passes, in the order a frame runs them. Every one is a full-raster
    triangle into a buffer of its own; nothing is blended.

    1. **linear** -- the clip decoded to linear light (IEC 61966-2-1) and
       premultiplied by its alpha: pyramid level 0.
    2. **down** -- level k from level k - 1: 2 x 2 box averages, the edge mode
       (Mirror or Black) applied to what lies past level k - 1's edge.
    3. **ring** -- from the coarsest level down: level k's taps (the pattern's
       ring between 8 2^( k - 1 ) and 8 2^k px, in level k's texels) plus a
       bilinear sample of the level above's sum. A sum buffer is its level's
       size plus a texel of margin all round, so the bilinear sample at the
       frame's edge reads a sum computed past the edge, not a clamped copy.
    4. **final** -- to the host: the core's taps from level 0 plus a bilinear
       sample of level 1's sum, x cos^4 of the field angle, x 2^Exposure,
       encoded, turned 180 degrees for Obscura; Mix to the clip in code values;
       alpha mix( a, 1, Mix ).

    The taps live in one RGBA32F texture, a row a level: texel 2t is the
    tap's offset in that level's texels, 2t + 1 its R G B weights.
    tools/glslc.sh's ASSEMBLED table mirrors Assemble().
*/
namespace pinhole::shaders
{

extern const char* const kVersion;
extern const char* const kQuadVertex;
extern const char* const kCommon;
extern const char* const kEdges;
extern const char* const kGather;
extern const char* const kLinearFragment;
extern const char* const kDownFragment;
extern const char* const kRingFragment;
extern const char* const kFinalFragment;

enum class Pass
{
	Linear,
	Down,
	Ring,
	Final
};
std::string AssembleVertex( Pass pass );
std::string Assemble( Pass pass );

} // namespace pinhole::shaders
