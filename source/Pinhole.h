#pragma once

#include "Controls.h"
#include "Optics.h"
#include "PassBuffer.h"

#include <FFGLSDK.h>
// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace pinhole
{

/// The harness's negative controls that reach the shaders or the plugin's
/// own bookkeeping. All off in the plugin.
struct PluginHooks
{
	int alphaModel     = 0;    ///< 1 the clip's alpha passed through
	bool premultiply   = true; ///< false: the clip's light not weighted by its alpha
	double cosPower    = 4.0;  ///< the falloff's power of cos
	bool edgeSwapped   = false;///< Mirror draws Black and Black Mirror
	bool edgeClamped   = false;///< Mirror repeats the edge pixel instead of reflecting
	int dropLevel      = -1;   ///< this pyramid level's ring left out (0 the core)
	bool staleOnResize = false;///< the taps not rebuilt when the raster changes
	bool mirrorView    = false;///< Obscura mirrors instead of turning
	bool leakGLState   = false;///< the host's GL state not put back
	optics::Hooks optics;
};

/**
    The plugin, `SW Pinhole`: the clip is the scene's geometric image on the
    screen of a pinhole camera (or a zone-plate one), and what comes out is
    the picture the hole really makes: the clip convolved, per colour band,
    with the Fresnel pattern of the aperture. The patterns and the pyramid's
    taps are built on the CPU (Optics.h) when a control that changes them, or
    the raster, changes; every frame runs the passes of Shaders.h.

    Host indices are the ParamIds, About last.
*/
class PinholePlugin : public CFFGLPlugin
{
public:
	PinholePlugin();

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

	void SetById( unsigned int id, float value )
	{
		SetFloatParameter( id, value );
	}
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	/// The controls in physical units.
	Settings CurrentSettings() const;

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	PluginHooks& HooksForTest()
	{
		return hooks;
	}
	const optics::Profile& LastProfile() const
	{
		return profile;
	}
	const optics::Kernel& LastKernel() const
	{
		return kernel;
	}
	/// ms the last rebuild of the patterns and taps took on the CPU (0 when
	/// the last frame needed none).
	double LastRebuildMs() const
	{
		return lastRebuildMs;
	}
	int Rebuilds() const
	{
		return rebuilds;
	}
	/// The aperture the settings make (the harness's geometry checks).
	optics::Aperture ApertureFor( const Settings& s ) const;

private:
	struct Key
	{
		int aperture = -1, zones = 0, height = 0;
		double diameter = 0, focus = 0, distance = 0;
		bool operator==( const Key& o ) const
		{
			return aperture == o.aperture && zones == o.zones && height == o.height && diameter == o.diameter && focus == o.focus
			       && distance == o.distance;
		}
	};

	bool ensureBuffers( int width, int height );
	void rebuild( const Settings& s, int height );
	void uploadTaps();
	void draw( PassBuffer& target );
	void render( const FFGLTextureStruct* input, const GLint* hostViewport, GLuint hostFBO, int width, int height, const Settings& s );

	GLint where( const ffglex::FFGLShader& shader, const char* name );

	float params[ PT_COUNT ] = {};
	PluginHooks hooks;

	optics::Profile profile;
	optics::Kernel kernel;
	Key built;
	bool haveKernel      = false;
	double lastRebuildMs = 0.0;
	int rebuilds         = 0;

	ffglex::FFGLShader linearShader, downShader, ringShader, finalShader;
	GLuint vao = 0, tapTexture = 0;
	std::vector< std::pair< std::pair< GLuint, const char* >, GLint > > uniformCache;

	PassBuffer level[ optics::kMaxLevels + 1 ];
	PassBuffer sum[ optics::kMaxLevels + 1 ];
};

} // namespace pinhole
