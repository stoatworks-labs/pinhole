#include "Pinhole.h"

/**
    The registration. `pinhole_core` is an OBJECT library so this file-scope
    constructor, which nothing references by name, is linked into the bundle
    (an archive would let the linker drop it, and the bundle would load and
    report no plugins).
*/
static CFFGLPluginInfo PluginInfo(
	PluginFactory< pinhole::PinholePlugin >,// Create method
	"PC01",                                 // Plugin unique ID of maximum length 4
	"SW Pinhole",                           // Plugin name
	2,                                      // API major version number
	1,                                      // API minor version number
	0,                                      // Plugin major version number
	1,                                      // Plugin minor version number
	FF_EFFECT,                              // Plugin type
	"A pinhole camera, or a Fresnel zone plate, looking at the clip. Every point of the picture lands as the Fresnel "
	"diffraction pattern of the aperture, one per colour band: a picture sharpest at one hole size and softer either side, "
	"red softer than blue for small holes, a faint veil round highlights, cos^4 dark corners, and a zone plate's sharp colour "
	"sitting in the glow of the light it does not focus.",
	"Pinhole FFGL effect"// About
);

extern "C" const char* PinholeBuildStamp()
{
	return "pinhole " PINHOLE_VERSION ", built " __DATE__ " " __TIME__;
}
