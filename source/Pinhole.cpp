#include "Pinhole.h"

#include "Diag.h"
#include "GLState.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace pinhole
{
namespace
{
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

int ceilShift( int a, int k )
{
	return ( a + ( 1 << k ) - 1 ) >> k;
}
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount, "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
PinholePlugin::PinholePlugin()
{
	SetMinInputs( 1 );
	SetMaxInputs( 1 );

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
		params[ id ] = InfoOf( id ).defaultValue;

	for( unsigned int id = 0; id < PT_ABOUT_TEXT; ++id )
	{
		const ControlInfo& info = InfoOf( id );
		switch( info.kind )
		{
		case Kind::Standard: SetParamInfo( id, info.name, FF_TYPE_STANDARD, params[ id ] ); break;
		case Kind::Option:
			SetOptionParamInfo( id, info.name, static_cast< unsigned int >( info.optionCount ), params[ id ] );
			for( int i = 0; i < info.optionCount; ++i )
				SetParamElementInfo( id, static_cast< unsigned int >( i ), info.options[ i ], static_cast< float >( i ) );
			break;
		case Kind::Integer:
			//Only FF_TYPE_STANDARD has its default clamped into 0..1, so an
			//integer is declared with its real default and range (boreal's).
			SetParamInfo( id, info.name, FF_TYPE_INTEGER, params[ id ] );
			SetParamRange( id, info.low, info.high );
			break;
		}
		SetParamGroup( id, info.group );
	}

	// The About block, last. Inline rather than through a helper: SetParamInfo
	// is protected on CFFGLPlugin, so nothing outside the class can call it.
	SetParamInfo( PT_ABOUT_TEXT, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
	{
		FFUInt32 aboutId = PT_ABOUT_TEXT + 1;
		for( const auto& b : stoatworks::about::buttons() )
			SetParamInfo( aboutId++, b.label, FF_TYPE_EVENT, false );
	}
	for( FFUInt32 i = PT_ABOUT_TEXT; i < PT_COUNT; ++i )
		SetParamGroup( i, "About" );
}

//---------------------------------------------------------------------------
GLint PinholePlugin::where( const FFGLShader& shader, const char* name )
{
	//Keyed by the literal's address: every name here is a string literal.
	const auto key = std::make_pair( shader.GetGLID(), name );
	for( const auto& entry : uniformCache )
		if( entry.first == key )
			return entry.second;
	const GLint location = glGetUniformLocation( shader.GetGLID(), name );
	uniformCache.emplace_back( key, location );
	return location;
}

//---------------------------------------------------------------------------
FFResult PinholePlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer=" + glStringOrUnknown( GL_RENDERER )
	            + " version=" + glStringOrUnknown( GL_VERSION ) );

	struct Stage
	{
		FFGLShader* shader;
		shaders::Pass pass;
		const char* name;
	};
	const Stage stages[] = {
		{ &linearShader, shaders::Pass::Linear, "linear" },
		{ &downShader, shaders::Pass::Down, "down" },
		{ &ringShader, shaders::Pass::Ring, "ring" },
		{ &finalShader, shaders::Pass::Final, "final" },
	};
	for( const Stage& stage : stages )
	{
		const std::string vertex   = shaders::AssembleVertex( stage.pass );
		const std::string fragment = shaders::Assemble( stage.pass );
		if( stage.shader->Compile( vertex.c_str(), fragment.c_str() ) )
			continue;
		//Returning FF_FAIL is invisible to the operator: the plugin simply
		//does nothing in Resolume. These lines are the only record of which
		//pass it was.
		diag::error( std::string( "the " ) + stage.name + " shader failed to compile - the plugin will do nothing" );
		FFGLLog::LogToHost( "Pinhole: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}

	//A core profile draws nothing without a vertex array bound; the
	//triangle needs no attributes.
	glGenVertexArrays( 1, &vao );
	glGenTextures( 1, &tapTexture );
	haveKernel = false;
	diag::info( "initialised" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
Settings PinholePlugin::CurrentSettings() const
{
	Settings s;
	s.aperture   = OptionIndex( params[ PT_APERTURE ], 2 );
	s.diameter   = DiameterFromParam( params[ PT_DIAMETER ] );
	s.zones      = ZonesFromParam( params[ PT_ZONES ] );
	s.zoneFocus  = ZoneFocusFromParam( params[ PT_ZONE_FOCUS ] );
	s.distance   = DistanceFromParam( params[ PT_DISTANCE ] );
	s.exposureEV = ExposureFromParam( params[ PT_EXPOSURE ] );
	s.view       = OptionIndex( params[ PT_VIEW ], 2 );
	s.edge       = OptionIndex( params[ PT_EDGE ], 2 );
	s.mix        = std::clamp( static_cast< double >( params[ PT_MIX ] ), 0.0, 1.0 );
	return s;
}

optics::Aperture PinholePlugin::ApertureFor( const Settings& s ) const
{
	return s.aperture == kZonePlate ? optics::MakeZonePlate( s.zones, s.zoneFocus, s.distance, hooks.optics )
	                                : optics::MakePinhole( s.diameter, hooks.optics );
}

//---------------------------------------------------------------------------
void PinholePlugin::rebuild( const Settings& s, int height )
{
	Key key;
	key.aperture = s.aperture;
	key.height   = height;
	key.distance = s.distance;
	//Only what the chosen aperture reads: the other's controls rebuild nothing.
	if( s.aperture == kZonePlate )
	{
		key.zones = s.zones;
		key.focus = s.zoneFocus;
	}
	else
		key.diameter = s.diameter;
	lastRebuildMs = 0.0;
	if( haveKernel && key == built )
		return;
	if( haveKernel && hooks.staleOnResize )
	{
		Key same   = key;
		same.height = built.height;
		if( same == built )
			return;
	}
	const auto start      = std::chrono::steady_clock::now();
	const double pixel    = optics::kFrameHeight / height;
	profile               = optics::BuildProfile( ApertureFor( s ), s.distance, pixel, static_cast< double >( height ), hooks.optics );
	kernel                = optics::BuildKernel( profile );
	uploadTaps();
	lastRebuildMs = std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count();
	built         = key;
	haveKernel    = true;
	++rebuilds;
}

void PinholePlugin::uploadTaps()
{
	const int width  = static_cast< int >( 2 * std::max< size_t >( 1, kernel.MaxTaps() ) );
	const int height = kernel.levels + 1;
	std::vector< float > data( static_cast< size_t >( width ) * height * 4, 0.0f );
	for( int k = 0; k <= kernel.levels; ++k )
		for( size_t t = 0; t < kernel.taps[ k ].size(); ++t )
		{
			const optics::Tap& tap = kernel.taps[ k ][ t ];
			float* o               = &data[ ( static_cast< size_t >( k ) * width + 2 * t ) * 4 ];
			o[ 0 ]                 = static_cast< float >( tap.i );
			o[ 1 ]                 = static_cast< float >( tap.j );
			o[ 4 ]                 = tap.w[ 0 ];
			o[ 5 ]                 = tap.w[ 1 ];
			o[ 6 ]                 = tap.w[ 2 ];
		}
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, tapTexture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, data.data() );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
}

//---------------------------------------------------------------------------
bool PinholePlugin::ensureBuffers( int width, int height )
{
	using S = PassBuffer::Sampling;
	bool ok = true;
	for( int k = 0; k <= kernel.levels; ++k )
	{
		ok = ok && level[ k ].Ensure( ceilShift( width, k ), ceilShift( height, k ), GL_RGBA16F, S::Nearest );
		if( k > 0 )
			ok = ok && sum[ k ].Ensure( ceilShift( width, k ), ceilShift( height, k ), GL_RGBA16F, S::Nearest );
	}
	return ok;
}

void PinholePlugin::draw( PassBuffer& target )
{
	glBindFramebuffer( GL_FRAMEBUFFER, target.GetGLID() );
	glViewport( 0, 0, static_cast< GLsizei >( target.GetWidth() ), static_cast< GLsizei >( target.GetHeight() ) );
	glDrawArrays( GL_TRIANGLES, 0, 3 );
}

//---------------------------------------------------------------------------
void PinholePlugin::render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height, const Settings& s )
{
	if( !ensureBuffers( width, height ) )
	{
		diag::error( "could not allocate the pyramid's buffers" );
		return;
	}
	glBindVertexArray( vao );
	const int K        = kernel.levels;
	const int edgeMode = ( s.edge == kBlack ) != hooks.edgeSwapped ? 1 : 0;
	auto size          = [ & ]( const PassBuffer& b, GLint location ) {
        glUniform2i( location, static_cast< GLint >( b.GetWidth() ), static_cast< GLint >( b.GetHeight() ) );
	};
	auto tapCount = [ & ]( int k ) { return k == hooks.dropLevel ? 0 : static_cast< GLint >( kernel.taps[ k ].size() ); };

	//1. linear: level 0
	glUseProgram( linearShader.GetGLID() );
	bindUnit( 0, input->Handle );
	glUniform1i( where( linearShader, "InputTexture" ), 0 );
	glUniform1i( where( linearShader, "Premultiply" ), hooks.premultiply ? 1 : 0 );
	draw( level[ 0 ] );

	//2. down: levels 1..K
	glUseProgram( downShader.GetGLID() );
	glUniform1i( where( downShader, "Finer" ), 0 );
	glUniform1i( where( downShader, "EdgeMode" ), edgeMode );
	for( int k = 1; k <= K; ++k )
	{
		bindUnit( 0, level[ k - 1 ].TextureID() );
		size( level[ k - 1 ], where( downShader, "FinerSize" ) );
		draw( level[ k ] );
	}

	//3. rings, from the coarsest level down
	glUseProgram( ringShader.GetGLID() );
	glUniform1i( where( ringShader, "Level" ), 0 );
	glUniform1i( where( ringShader, "Taps" ), 1 );
	glUniform1i( where( ringShader, "Coarser" ), 2 );
	glUniform1i( where( ringShader, "EdgeMode" ), edgeMode );
	bindUnit( 1, tapTexture );
	for( int k = K; k >= 1; --k )
	{
		bindUnit( 0, level[ k ].TextureID() );
		size( level[ k ], where( ringShader, "LevelSize" ) );
		glUniform1i( where( ringShader, "TapRow" ), k );
		glUniform1i( where( ringShader, "TapCount" ), tapCount( k ) );
		const bool coarser = k < K;
		bindUnit( 2, coarser ? sum[ k + 1 ].TextureID() : level[ k ].TextureID() );
		size( coarser ? sum[ k + 1 ] : level[ k ], where( ringShader, "CoarserSize" ) );
		glUniform1i( where( ringShader, "HasCoarser" ), coarser ? 1 : 0 );
		draw( sum[ k ] );
	}

	//4. final, into the host's framebuffer and viewport
	glBindFramebuffer( GL_FRAMEBUFFER, hostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	glUseProgram( finalShader.GetGLID() );
	bindUnit( 0, input->Handle );
	bindUnit( 1, level[ 0 ].TextureID() );
	bindUnit( 2, tapTexture );
	bindUnit( 3, K >= 1 ? sum[ 1 ].TextureID() : level[ 0 ].TextureID() );
	glUniform1i( where( finalShader, "InputTexture" ), 0 );
	glUniform1i( where( finalShader, "Level" ), 1 );
	glUniform1i( where( finalShader, "Taps" ), 2 );
	glUniform1i( where( finalShader, "Coarser" ), 3 );
	glUniform1i( where( finalShader, "EdgeMode" ), edgeMode );
	size( level[ 0 ], where( finalShader, "LevelSize" ) );
	glUniform1i( where( finalShader, "TapRow" ), 0 );
	glUniform1i( where( finalShader, "TapCount" ), tapCount( 0 ) );
	size( K >= 1 ? sum[ 1 ] : level[ 0 ], where( finalShader, "CoarserSize" ) );
	glUniform1i( where( finalShader, "HasCoarser" ), K >= 1 ? 1 : 0 );
	glUniform2i( where( finalShader, "ViewOrigin" ), hostViewport[ 0 ], hostViewport[ 1 ] );
	glUniform2i( where( finalShader, "Size" ), width, height );
	glUniform1i( where( finalShader, "ViewMode" ), s.view == kObscura ? ( hooks.mirrorView ? 2 : 1 ) : 0 );
	glUniform1f( where( finalShader, "PixelOverDistance" ), static_cast< float >( optics::kFrameHeight / height / s.distance ) );
	glUniform1f( where( finalShader, "CosPower" ), static_cast< float >( hooks.cosPower ) );
	glUniform1f( where( finalShader, "ExposureGain" ), static_cast< float >( std::exp2( s.exposureEV ) ) );
	glUniform1f( where( finalShader, "MixAmount" ), static_cast< float >( s.mix ) );
	glUniform1i( where( finalShader, "AlphaModel" ), hooks.alphaModel );
	glDrawArrays( GL_TRIANGLES, 0, 3 );

	unbindTextureUnits( 4 );
	glActiveTexture( GL_TEXTURE0 );
	glUseProgram( 0 );
}

//---------------------------------------------------------------------------
FFResult PinholePlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr || pgl->numInputTextures < 1 || pgl->inputTextures[ 0 ] == nullptr )
		return FF_FAIL;
	const FFGLTextureStruct* input = pgl->inputTextures[ 0 ];

	//The host's state, put back however this returns.
	ScopedGLState restore;
	restore.restoreOnExit     = !hooks.leakGLState;
	const GLint* hostViewport = restore.saved.viewport;
	glDisable( GL_BLEND );

	const int width  = static_cast< int >( input->Width );
	const int height = static_cast< int >( input->Height );
	if( width <= 0 || height <= 0 || vao == 0 )
		return FF_FAIL;

	const Settings s = CurrentSettings();
	rebuild( s, height );
	render( input, hostViewport, pgl->HostFBO, width, height, s );
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult PinholePlugin::DeInitGL()
{
	uniformCache.clear();
	for( FFGLShader* s : { &linearShader, &downShader, &ringShader, &finalShader } )
		s->FreeGLResources();
	if( vao != 0 )
		glDeleteVertexArrays( 1, &vao );
	vao = 0;
	if( tapTexture != 0 )
		glDeleteTextures( 1, &tapTexture );
	tapTexture = 0;
	for( PassBuffer& b : level )
		b.Destroy();
	for( PassBuffer& b : sum )
		b.Destroy();
	haveKernel = false;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
char* PinholePlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult PinholePlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult PinholePlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;

	// The About buttons open a browser and store nothing.
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;

	params[ index ] = value;
	return FF_SUCCESS;
}

float PinholePlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

} // namespace pinhole
