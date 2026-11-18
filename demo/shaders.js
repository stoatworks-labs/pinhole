// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape is \` for a backtick inside a comment.
export const VERSION = '#version 410 core\n';

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
void main()
{
	vec2 p = vec2( float( ( gl_VertexID << 1 ) & 2 ), float( gl_VertexID & 2 ) );
	gl_Position = vec4( p * 2.0 - 1.0, 0.0, 1.0 );
}
`;

// kCommon, source/Shaders.cpp
export const COMMON = `
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
`;

// kEdges, source/Shaders.cpp
export const EDGES = `
uniform int EdgeMode;// 0 Mirror, 1 Black, 2 the edge pixel repeated (a wrong model)
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
	if( EdgeMode == 2 )
		return texelFetch( tex, clamp( q, ivec2( 0 ), size - 1 ), 0 ).rgb;
	return texelFetch( tex, ivec2( fold( q.x, size.x ), fold( q.y, size.y ) ), 0 ).rgb;
}
`;

// kGather, source/Shaders.cpp
export const GATHER = `
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
`;

// kLinearFragment, source/Shaders.cpp
export const LINEAR_FRAGMENT = `
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
`;

// kDownFragment, source/Shaders.cpp
export const DOWN_FRAGMENT = `
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
`;

// kRingFragment, source/Shaders.cpp
export const RING_FRAGMENT = `
out vec4 FragColor;

void main()
{
	ivec2 at  = ivec2( gl_FragCoord.xy ) - 1;
	FragColor = vec4( gatherTaps( at ) + upsample( at ), 1.0 );
}
`;

// kFinalFragment, source/Shaders.cpp
export const FINAL_FRAGMENT = `
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
	// Written out rather than mix(): Apple's software renderer computes mix as
	// x + ( y - x ) a, which at Mix 1 lets the clip's colour under a
	// transparent pixel through in the last bit.
	float keep = 1.0 - MixAmount;
	FragColor  = vec4( src.rgb * keep + rgb * MixAmount, src.a * keep + outA * MixAmount );
}
`;

// Each stage's source, piece by piece, in the order the plugin joins them:
// every pass as Pinhole.cpp's InitGL compiles it, its vertex stage from
// Shaders.cpp's AssembleVertex() and its fragment stage from Assemble().
export const ASSEMBLY = {
  linear_vertex: ['VERSION', 'QUAD_VERTEX'],
  linear_fragment: ['VERSION', 'COMMON', 'LINEAR_FRAGMENT'],
  down_vertex: ['VERSION', 'QUAD_VERTEX'],
  down_fragment: ['VERSION', 'EDGES', 'DOWN_FRAGMENT'],
  ring_vertex: ['VERSION', 'QUAD_VERTEX'],
  ring_fragment: ['VERSION', 'EDGES', 'GATHER', 'RING_FRAGMENT'],
  final_vertex: ['VERSION', 'QUAD_VERTEX'],
  final_fragment: ['VERSION', 'COMMON', 'EDGES', 'GATHER', 'FINAL_FRAGMENT'],
};
