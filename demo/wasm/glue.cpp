/*
    The page's way in to the plugin: what an FFGL host does to a plugin
    instance, as C functions JavaScript can call.

    Nothing here is the plugin, and nothing here does the plugin's work. The
    plugin is `source/Pinhole.cpp` and everything it calls, compiled
    UNMODIFIED into the same WebAssembly module as this file (see
    demo/tools/build-wasm.sh for the list). This file only plays the host:

      - it constructs a `PinholePlugin`, as EffectPlugin.cpp's registration
        would, and calls InitGL / DeInitGL on it;
      - it reads the parameter declarations back through the FFGL SDK's own
        host-facing getters (CFFGLPluginManager::GetParamName and the rest,
        the SDK compiled unmodified too), so the page's panel is built from
        the plugin's constructor rather than from a copy of it;
      - it forwards the host's calls: SetFloatParameter, and ProcessOpenGL
        with ONE input texture (an effect), as tools/phtest's Rig does.

    The rest are read-outs for the line under the picture: the plugin's own
    state through the accessors its harness reads (LastKernel(),
    LastProfile(), LastRebuildMs(), Rebuilds(), CurrentSettings(),
    ApertureFor()), and Controls.cpp's conversions for the panel. None of
    them changes what the plugin draws.

    GL calls go to the page's WebGL2 context through emscripten's GL library;
    demo/wasm/gl_shim.cpp replaces the four entry points that cannot be passed
    straight through.
*/
#include "Controls.h"
#include "Diag.h"
#include "Optics.h"
#include "Pinhole.h"

#include <emscripten/emscripten.h>

#include <sys/stat.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

using namespace pinhole;

namespace
{
/// One plugin instance and the host-side structs it is handed.
struct Instance
{
	PinholePlugin plugin;
	bool initialised = false;

	FFGLTextureStruct input {};
	FFGLTextureStruct* inputs[ 1 ] = { &input };
	ProcessOpenGLStruct process {};

	std::string scratch;///< a string handed back to JS lives here until the next call
};

float asFloat( FFMixed mixed )
{
	float value = 0.0f;
	static_assert( sizeof( value ) == sizeof( mixed.UIntValue ), "FFMixed carries a float in its bits" );
	std::memcpy( &value, &mixed.UIntValue, sizeof( value ) );
	return value;
}

const char* hold( Instance* instance, std::string text )
{
	instance->scratch = std::move( text );
	return instance->scratch.c_str();
}

/// The ParamId behind a host index, or PT_COUNT. Host indices ARE the
/// ParamIds here (Pinhole.h), but the page asks by index and this keeps the
/// mapping in one place should that ever change.
unsigned int idOf( int index )
{
	return index >= 0 && index < static_cast< int >( PT_ABOUT_TEXT ) ? static_cast< unsigned int >( index ) : PT_COUNT;
}
} // namespace

extern "C"
{
//---------------------------------------------------------------------------
// Lifetime.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE Instance* ph_new()
{
	return new Instance();
}

EMSCRIPTEN_KEEPALIVE void ph_delete( Instance* instance )
{
	if( instance == nullptr )
		return;
	if( instance->initialised )
		instance->plugin.DeInitGL();
	delete instance;
}

/// InitGL, with the viewport a host would pass. 1 on success.
EMSCRIPTEN_KEEPALIVE int ph_init_gl( Instance* instance, int width, int height )
{
	FFGLViewportStruct viewport {};
	viewport.width        = static_cast< GLuint >( width );
	viewport.height       = static_cast< GLuint >( height );
	instance->initialised = instance->plugin.InitGL( &viewport ) == FF_SUCCESS;
	return instance->initialised ? 1 : 0;
}

//---------------------------------------------------------------------------
// The declarations, read as a host reads them.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ph_param_count( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetNumParams() );
}

EMSCRIPTEN_KEEPALIVE const char* ph_param_name( Instance* instance, int index )
{
	const char* name = instance->plugin.GetParamName( static_cast< unsigned int >( index ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE int ph_param_type( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetParamType( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ph_param_group( Instance* instance, int index )
{
	return hold( instance, instance->plugin.GetParamGroup( static_cast< unsigned int >( index ) ) );
}

/// The declared default of a numeric parameter (FF_TYPE_STANDARD already
/// clamped into 0..1 by the SDK, as a host sees it; FF_TYPE_INTEGER the count).
EMSCRIPTEN_KEEPALIVE float ph_param_default( Instance* instance, int index )
{
	return asFloat( instance->plugin.GetParamDefault( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE int ph_param_element_count( Instance* instance, int index )
{
	return static_cast< int >( instance->plugin.GetNumParamElements( static_cast< unsigned int >( index ) ) );
}

EMSCRIPTEN_KEEPALIVE const char* ph_param_element_name( Instance* instance, int index, int element )
{
	const char* name = instance->plugin.GetParamElementName( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) );
	return name ? name : "";
}

EMSCRIPTEN_KEEPALIVE float ph_param_element_value( Instance* instance, int index, int element )
{
	return asFloat( instance->plugin.GetParamElementDefault( static_cast< unsigned int >( index ), static_cast< unsigned int >( element ) ) );
}

/// An FF_TYPE_INTEGER's declared range (SetParamRange).
EMSCRIPTEN_KEEPALIVE float ph_param_range_min( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).min;
}

EMSCRIPTEN_KEEPALIVE float ph_param_range_max( Instance* instance, int index )
{
	return instance->plugin.GetParamRange( static_cast< unsigned int >( index ) ).max;
}

EMSCRIPTEN_KEEPALIVE int ph_min_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMinInputs() );
}

EMSCRIPTEN_KEEPALIVE int ph_max_inputs( Instance* instance )
{
	return static_cast< int >( instance->plugin.GetMaxInputs() );
}

//---------------------------------------------------------------------------
// The host's calls.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ph_set_float( Instance* instance, int index, float value )
{
	return instance->plugin.SetFloatParameter( static_cast< unsigned int >( index ), value ) == FF_SUCCESS ? 1 : 0;
}

EMSCRIPTEN_KEEPALIVE float ph_get_float( Instance* instance, int index )
{
	return instance->plugin.GetFloatParameter( static_cast< unsigned int >( index ) );
}

/// One frame: ProcessOpenGL into whatever the page has bound (the canvas,
/// HostFBO 0, at the viewport the page set), handed one input of
/// clipWidth x clipHeight whose GL name is `clip` (a texture the page
/// registered with emscripten's GL tables). Width and HardwareWidth are the
/// same: a browser texture is not padded. 1 on success.
EMSCRIPTEN_KEEPALIVE int ph_process( Instance* instance, int clip, int clipWidth, int clipHeight )
{
	instance->input.Width = instance->input.HardwareWidth = static_cast< FFUInt32 >( clipWidth );
	instance->input.Height = instance->input.HardwareHeight = static_cast< FFUInt32 >( clipHeight );
	instance->input.Handle                                 = static_cast< GLuint >( clip );
	instance->process.numInputTextures                     = 1;
	instance->process.inputTextures                        = instance->inputs;
	instance->process.HostFBO                              = 0;
	return instance->plugin.ProcessOpenGL( &instance->process ) == FF_SUCCESS ? 1 : 0;
}

//---------------------------------------------------------------------------
// For the panel's read-outs: Controls.cpp's conversion for a parameter, the
// one CurrentSettings() applies to it. NaN where the plugin uses the host's
// value as it is (Mix, clamped), an index, or a count.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE double ph_convert( int index, float v )
{
	switch( idOf( index ) )
	{
	case PT_DIAMETER: return DiameterFromParam( v );
	case PT_ZONE_FOCUS: return ZoneFocusFromParam( v );
	case PT_DISTANCE: return DistanceFromParam( v );
	case PT_EXPOSURE: return ExposureFromParam( v );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

/// And back: the host's 0..1 for a value in the plugin's units, by the
/// inverse Controls.cpp pairs with each conversion (the one its constructor
/// sets the defaults with). The page's presets are written in metres and
/// stops and go through this. NaN for a parameter with no conversion.
EMSCRIPTEN_KEEPALIVE double ph_param_from( int index, double value )
{
	switch( idOf( index ) )
	{
	case PT_DIAMETER: return ParamFromDiameter( value );
	case PT_ZONE_FOCUS: return ParamFromZoneFocus( value );
	case PT_DISTANCE: return ParamFromDistance( value );
	case PT_EXPOSURE: return ParamFromExposure( value );
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}

//---------------------------------------------------------------------------
// What the line under the picture reports: the plugin's own state, through
// the accessors its harness reads. It reports; it measures nothing.
//---------------------------------------------------------------------------
EMSCRIPTEN_KEEPALIVE int ph_kernel_levels( Instance* instance )
{
	return instance->plugin.LastKernel().levels;
}
EMSCRIPTEN_KEEPALIVE int ph_kernel_max_taps( Instance* instance )
{
	return static_cast< int >( instance->plugin.LastKernel().MaxTaps() );
}
EMSCRIPTEN_KEEPALIVE int ph_kernel_taps( Instance* instance )
{
	size_t n = 0;
	for( int k = 0; k <= instance->plugin.LastKernel().levels; ++k )
		n += instance->plugin.LastKernel().taps[ k ].size();
	return static_cast< int >( n );
}
EMSCRIPTEN_KEEPALIVE double ph_kernel_support( Instance* instance )
{
	return instance->plugin.LastKernel().support;
}
EMSCRIPTEN_KEEPALIVE double ph_profile_kept( Instance* instance, int channel )
{
	return channel >= 0 && channel < 3 ? instance->plugin.LastProfile().kept[ channel ] : 0.0;
}
EMSCRIPTEN_KEEPALIVE double ph_last_rebuild_ms( Instance* instance )
{
	return instance->plugin.LastRebuildMs();
}
EMSCRIPTEN_KEEPALIVE int ph_rebuilds( Instance* instance )
{
	return instance->plugin.Rebuilds();
}
/// The settings in the plugin's units, as the frame used them.
EMSCRIPTEN_KEEPALIVE double ph_setting( Instance* instance, int which )
{
	const Settings s = instance->plugin.CurrentSettings();
	switch( which )
	{
	case 0: return s.aperture;
	case 1: return s.diameter;
	case 2: return s.zones;
	case 3: return s.zoneFocus;
	case 4: return s.distance;
	case 5: return s.exposureEV;
	case 6: return s.view;
	case 7: return s.edge;
	case 8: return s.mix;
	default: return std::numeric_limits< double >::quiet_NaN();
	}
}
/// Lommel's u = 2 pi R^2 / ( lambda f ) of the aperture the settings make, at
/// `wavelength` (m): the plugin's ApertureFor() for the outer radius.
EMSCRIPTEN_KEEPALIVE double ph_fresnel_u( Instance* instance, double wavelength )
{
	const Settings s          = instance->plugin.CurrentSettings();
	const optics::Aperture ap = instance->plugin.ApertureFor( s );
	return 2.0 * optics::kPi * ap.radius * ap.radius / ( wavelength * s.distance );
}
/// The screen's height, m (Optics.h: a 135 frame).
EMSCRIPTEN_KEEPALIVE double ph_frame_height()
{
	return optics::kFrameHeight;
}

/// Give the plugin's log (Diag.cpp) somewhere to go: a directory in the
/// page's in-memory file system, named through the override Diag.cpp reads.
/// Its own `mkdir -p` goes through system(), which a browser does not have.
/// Call before the first InitGL, which is when the log opens.
EMSCRIPTEN_KEEPALIVE void ph_prepare_log()
{
	setenv( "PINHOLE_LOG_DIR", "/pinhole/logs", 1 );
	mkdir( "/pinhole", 0755 );
	mkdir( "/pinhole/logs", 0755 );
}

/// The plugin's log file (Diag.cpp), in the page's in-memory file system.
EMSCRIPTEN_KEEPALIVE const char* ph_log_path()
{
	static std::string path;
	path = diag::logPath();
	return path.c_str();
}

} // extern "C"
