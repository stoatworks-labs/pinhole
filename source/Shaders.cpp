#include "Shaders.h"

namespace pinhole::shaders
{

const char* const kVersion = "#version 410 core\n";

/// The full-raster triangle, from gl_VertexID alone.
const char* const kQuadVertex = R"(
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
)";

/// IEC 61966-2-1, both ways.
const char* const kCommon = R"(
vec3 toLinear( vec3 c )
{
	c       = max( c, vec3( 0.0 ) );
	vec3 lo = c / 12.92;
	vec3 hi = pow( ( c + 0.055 ) / 1.055, vec3( 2.4 ) );
	return mix( lo, hi, step( vec3( 0.04045 ), c ) );
}
vec3 toCode( vec3 c )
{
	c       = clamp( c, 0.0, 1.0 );
	vec3 lo = c * 12.92;
	vec3 hi = 1.055 * pow( c, vec3( 1.0 / 2.4 ) ) - 0.055;
	return mix( lo, hi, step( vec3( 0.0031308 ), c ) );
}
)";

/// What lies past a level's edge. Mirror: reflected about the edge (period
/// 2n, so an offset wider than the frame still lands inside). Black: nothing.
/// (GLSL's % is undefined for negative operands: the reflection at 0 comes
/// first.)
const char* const kEdges = R"(
uniform int EdgeMode;// 0 Mirror, 1 Black
int fold( int i, int n )
{
	if( i < 0 )
		i = -1 - i;
	i = i % ( 2 * n );
	return i < n ? i : 2 * n - 1 - i;
}
vec3 fetchEdge( sampler2D tex, ivec2 q, ivec2 size )
{
	if( q.x >= 0 && q.y >= 0 && q.x < size.x && q.y < size.y )
		return texelFetch( tex, q, 0 ).rgb;
	if( EdgeMode == 1 )
		return vec3( 0.0 );
	return texelFetch( tex, ivec2( fold( q.x, size.x ), fold( q.y, size.y ) ), 0 ).rgb;
}
)";

/// A level's taps, and the level above's sum sampled bilinearly at a texel's
/// centre (by hand: the weights are always 1/4 and 3/4). A sum buffer keeps a
/// one-texel margin all round -- the level's texel i is the buffer's i + 1 --
/// so a texel at the frame's edge reads the sum just past it as computed
/// there, under the edge mode, and not a copy of the edge's own.
const char* const kGather = R"(
uniform sampler2D Level;
uniform ivec2 LevelSize;
uniform sampler2D Taps;
uniform int TapRow;
uniform int TapCount;
uniform sampler2D Coarser;
uniform ivec2 CoarserSize;// the sum buffer's, margin included
uniform int HasCoarser;

vec3 gatherTaps( ivec2 at )
{
	vec3 sum = vec3( 0.0 );
	for( int t = 0; t < TapCount; ++t )
	{
		vec4 o = texelFetch( Taps, ivec2( 2 * t, TapRow ), 0 );
		vec4 w = texelFetch( Taps, ivec2( 2 * t + 1, TapRow ), 0 );
		sum += w.rgb * fetchEdge( Level, at + ivec2( o.xy ), LevelSize );
	}
	return sum;
}

vec3 upsample( ivec2 at )
{
	if( HasCoarser == 0 )
		return vec3( 0.0 );
	vec2 c     = ( vec2( at ) + 0.5 ) * 0.5 - 0.5;
	vec2 base  = floor( c );
	vec2 f     = c - base;
	ivec2 b    = ivec2( base ) + 1;
	ivec2 top  = CoarserSize - 1;
	vec3 c00   = texelFetch( Coarser, clamp( b, ivec2( 0 ), top ), 0 ).rgb;
	vec3 c10   = texelFetch( Coarser, clamp( b + ivec2( 1, 0 ), ivec2( 0 ), top ), 0 ).rgb;
	vec3 c01   = texelFetch( Coarser, clamp( b + ivec2( 0, 1 ), ivec2( 0 ), top ), 0 ).rgb;
	vec3 c11   = texelFetch( Coarser, clamp( b + ivec2( 1, 1 ), ivec2( 0 ), top ), 0 ).rgb;
	return mix( mix( c00, c10, f.x ), mix( c01, c11, f.x ), f.y );
}
)";

/// Level 0: the clip's light, linear, premultiplied (a transparent pixel
/// sends nothing).
const char* const kLinearFragment = R"(
uniform sampler2D InputTexture;
uniform int Premultiply;
out vec4 FragColor;

void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy );
	vec4 src = texelFetch( InputTexture, at, 0 );
	float a  = clamp( src.a, 0.0, 1.0 );
	vec3 lin = toLinear( src.rgb );
	FragColor = vec4( Premultiply == 1 ? lin * a : lin, a );
}
)";

/// Level k from level k - 1: the mean of a 2 x 2 block.
const char* const kDownFragment = R"(
uniform sampler2D Finer;
uniform ivec2 FinerSize;
out vec4 FragColor;

void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy ) * 2;
	vec3 sum = fetchEdge( Finer, at, FinerSize ) + fetchEdge( Finer, at + ivec2( 1, 0 ), FinerSize )
	         + fetchEdge( Finer, at + ivec2( 0, 1 ), FinerSize ) + fetchEdge( Finer, at + ivec2( 1, 1 ), FinerSize );
	FragColor = vec4( 0.25 * sum, 1.0 );
}
)";

/// Level k's ring, plus everything coarser, into a sum buffer with its
/// margin: the buffer's texel i is the level's i - 1.
const char* const kRingFragment = R"(
out vec4 FragColor;

void main()
{
	ivec2 at  = ivec2( gl_FragCoord.xy ) - 1;
	FragColor = vec4( gatherTaps( at ) + upsample( at ), 1.0 );
}
)";

/// To the host. The core plus everything coarser, at the picture's pixel
/// (turned 180 degrees for Obscura); the cos^4 falloff of the field angle;
/// the exposure; encoded; Mix to the clip in code values; alpha
/// mix( a, 1, Mix ): the screen is lit, or dark, everywhere. Mix 0 hands the
/// clip through untouched.
const char* const kFinalFragment = R"(
uniform sampler2D InputTexture;
uniform ivec2 ViewOrigin;
uniform ivec2 Size;
uniform int ViewMode;// 0 upright; 1 turned 180 degrees; 2 mirrored (a wrong model)
uniform float PixelOverDistance;
uniform float CosPower;
uniform float ExposureGain;
uniform float MixAmount;
uniform int AlphaModel;// 0 the plugin; 1 the clip's alpha passed through
out vec4 FragColor;

void main()
{
	ivec2 at = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	vec4 src = texelFetch( InputTexture, at, 0 );
	if( MixAmount <= 0.0 )
	{
		FragColor = src;
		return;
	}
	ivec2 pic = at;
	if( ViewMode == 1 )
		pic = Size - 1 - at;
	else if( ViewMode == 2 )
		pic = ivec2( Size.x - 1 - at.x, at.y );
	vec3 light = gatherTaps( pic ) + upsample( pic );
	// tan of the field angle on each axis: the pixel's offset over the distance.
	vec2 t     = ( vec2( at ) + 0.5 - 0.5 * vec2( Size ) ) * PixelOverDistance;
	float c2   = 1.0 / ( 1.0 + dot( t, t ) );
	float fall = pow( c2, 0.5 * CosPower );
	vec3 rgb   = toCode( light * fall * ExposureGain );
	float outA = AlphaModel == 1 ? src.a : 1.0;
	FragColor  = vec4( mix( src.rgb, rgb, MixAmount ), mix( src.a, outA, MixAmount ) );
}
)";

std::string AssembleVertex( Pass )
{
	return std::string( kVersion ) + kQuadVertex;
}

std::string Assemble( Pass pass )
{
	switch( pass )
	{
	case Pass::Linear: return std::string( kVersion ) + kCommon + kLinearFragment;
	case Pass::Down: return std::string( kVersion ) + kEdges + kDownFragment;
	case Pass::Ring: return std::string( kVersion ) + kEdges + kGather + kRingFragment;
	case Pass::Final: return std::string( kVersion ) + kCommon + kEdges + kGather + kFinalFragment;
	}
	return std::string();
}

} // namespace pinhole::shaders
