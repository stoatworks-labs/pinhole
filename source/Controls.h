#pragma once

#include "StoatworksAboutLinks.h"

/**
    The host's parameters, and what they mean in physical units.

    Every ranged STANDARD parameter the host sees is 0..1, because
    `SetParamInfo` clamps an `FF_TYPE_STANDARD` default into 0..1 before
    `SetParamRange` could widen it. The conversions live in Controls.cpp, one
    function per control, each with its inverse where a default or the
    harness needs it. Option parameters hold the element index (an option
    reads back 0..1 whatever its count: map by index). Open Zones is an
    `FF_TYPE_INTEGER` holding the real count, declared with its range.

    Units: metres for Diameter and Distance (shown in millimetres), metres
    for Zone Focus (a wavelength, shown in nanometres), stops for Exposure.
*/
namespace pinhole
{

/// Every control, by what it is. The host sees them in this order, with the
/// About block last.
enum ParamId : unsigned int
{
	// -- Camera ------------------------------------------------------------------
	PT_APERTURE = 0,
	PT_DIAMETER,
	PT_ZONES,
	PT_ZONE_FOCUS,
	PT_DISTANCE,

	// -- Picture -----------------------------------------------------------------
	PT_EXPOSURE,
	PT_VIEW,
	PT_EDGE,

	// -- Look --------------------------------------------------------------------
	PT_MIX,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

enum class Kind
{
	Standard,
	Option,
	Integer
};

/// One control as the host is told about it.
struct ControlInfo
{
	unsigned int id;
	const char* name;///< at most 16 characters: FFGL's name field is not terminated
	const char* group;
	Kind kind;
	float defaultValue;///< the host's 0..1, the element index, or the integer
	const char* const* options;
	int optionCount;
	float low, high;///< an Integer's range
};

/// The table, indexed by ParamId, for every id below PT_ABOUT_TEXT.
const ControlInfo& InfoOf( unsigned int id );

/// An option's index from the value the host stored.
int OptionIndex( float value, int count );

enum ApertureKind
{
	kPinhole   = 0,
	kZonePlate = 1
};
enum View
{
	kUpright = 0,
	kObscura = 1///< turned 180 degrees, as on the wall of a camera obscura
};
enum Edge
{
	kMirror = 0,
	kBlack  = 1
};

/// The controls in physical units.
struct Settings
{
	int aperture      = kPinhole;
	double diameter   = 0.0;///< m
	int zones         = 4;  ///< open zones
	double zoneFocus  = 0.0;///< m, the wavelength the plate focuses on the screen
	double distance   = 0.0;///< m, the aperture to the screen
	double exposureEV = 0.0;
	int view          = kUpright;
	int edge          = kMirror;
	double mix        = 1.0;
};

//---------------------------------------------------------------------------
// Conversions, 0..1 to physical and back.
//---------------------------------------------------------------------------
double DiameterFromParam( float v );///< m, 0.1..2 mm, geometric
float ParamFromDiameter( double metres );
int ZonesFromParam( float v );///< 1..16 open zones (the integer as stored)
double ZoneFocusFromParam( float v );///< m, 400..700 nm, linear
float ParamFromZoneFocus( double metres );
double DistanceFromParam( float v );///< m, 10..500 mm, geometric
float ParamFromDistance( double metres );
double ExposureFromParam( float v );///< stops, -4..+4, linear
float ParamFromExposure( double stops );

/// The ranges, for the harness and the docs.
constexpr double kDiameterLow = 0.1e-3, kDiameterHigh = 2e-3;
constexpr int kZonesLow = 1, kZonesHigh = 16;
constexpr double kZoneFocusLow = 400e-9, kZoneFocusHigh = 700e-9;
constexpr double kDistanceLow = 10e-3, kDistanceHigh = 500e-3;
constexpr double kExposureLow = -4.0, kExposureHigh = 4.0;

/// The defaults in physical units.
constexpr double kDefaultDistance  = 50e-3;
constexpr double kDefaultFocus     = 550e-9;
constexpr int kDefaultZones        = 4;
/// Stops: what an averaging meter sets, from Resolume's demo clips
/// (tools/clips.py defaults). The blur keeps the light, so the frame's only
/// loss is the cos^4 falloff: at 0 EV the 33 clips' output means keep 0.736
/// to 0.968 of their own (640x360, the default 50 mm), and the median clip
/// (Bass 003) is brought back by +0.174 EV, to 0.1 stop +0.2.
constexpr double kDefaultExposure  = 0.2;
/// The optimum at the default distance in 550 nm light, Mielenz's u = 1.7 pi.
double DefaultDiameter();

} // namespace pinhole
