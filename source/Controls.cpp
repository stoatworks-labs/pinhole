#include "Controls.h"

#include "Optics.h"

#include <algorithm>
#include <cmath>

namespace pinhole
{
namespace
{
double clamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double geometric( float v, double low, double high )
{
	return low * std::pow( high / low, clamp01( v ) );
}
float inverseGeometric( double value, double low, double high )
{
	const double lo = std::min( low, high ), hi = std::max( low, high );
	return static_cast< float >( std::log( std::clamp( value, lo, hi ) / low ) / std::log( high / low ) );
}
double linear( float v, double low, double high )
{
	return low + ( high - low ) * clamp01( v );
}
float inverseLinear( double value, double low, double high )
{
	return static_cast< float >( std::clamp( ( value - low ) / ( high - low ), 0.0, 1.0 ) );
}

const char* const kApertureNames[] = { "Pinhole", "Zone Plate" };
const char* const kViewNames[]     = { "Upright", "Obscura" };
const char* const kEdgeNames[]     = { "Mirror", "Black" };

ControlInfo standard( unsigned int id, const char* name, const char* group, float value )
{
	return { id, name, group, Kind::Standard, value, nullptr, 0, 0.0f, 1.0f };
}
ControlInfo option( unsigned int id, const char* name, const char* group, float value, const char* const* names, int count )
{
	return { id, name, group, Kind::Option, value, names, count, 0.0f, static_cast< float >( count - 1 ) };
}
ControlInfo integer( unsigned int id, const char* name, const char* group, int value, int low, int high )
{
	return { id, name, group, Kind::Integer, static_cast< float >( value ), nullptr, 0, static_cast< float >( low ), static_cast< float >( high ) };
}

//---------------------------------------------------------------------------
// The defaults: a pinhole at the optimum for a 50 mm box in green light
// (0.306 mm), four open zones focusing 550 nm when the plate is chosen,
// upright, mirrored at the edges, and an Exposure of +0.2 EV, an averaging
// meter's on Resolume's demo clips through --pipe (Controls.h,
// tools/clips.py defaults).
//---------------------------------------------------------------------------
const ControlInfo kTable[] = {
	option( PT_APERTURE, "Aperture", "Camera", static_cast< float >( kPinhole ), kApertureNames, 2 ),
	standard( PT_DIAMETER, "Diameter", "Camera", ParamFromDiameter( DefaultDiameter() ) ),
	integer( PT_ZONES, "Open Zones", "Camera", kDefaultZones, kZonesLow, kZonesHigh ),
	standard( PT_ZONE_FOCUS, "Zone Focus", "Camera", ParamFromZoneFocus( kDefaultFocus ) ),
	standard( PT_DISTANCE, "Distance", "Camera", ParamFromDistance( kDefaultDistance ) ),
	standard( PT_EXPOSURE, "Exposure", "Picture", ParamFromExposure( kDefaultExposure ) ),
	option( PT_VIEW, "View", "Picture", static_cast< float >( kUpright ), kViewNames, 2 ),
	option( PT_EDGE, "Edge", "Picture", static_cast< float >( kMirror ), kEdgeNames, 2 ),
	standard( PT_MIX, "Mix", "Look", 1.0f ),
};
static_assert( sizeof( kTable ) / sizeof( kTable[ 0 ] ) == PT_ABOUT_TEXT, "every control needs a row in the table" );
} // namespace

double DefaultDiameter()
{
	return std::sqrt( 2.0 * optics::kOptimumU / optics::kPi * kDefaultFocus * kDefaultDistance );
}

const ControlInfo& InfoOf( unsigned int id )
{
	return kTable[ std::min< unsigned int >( id, PT_ABOUT_TEXT - 1 ) ];
}

int OptionIndex( float value, int count )
{
	const long i = std::lround( static_cast< double >( value ) );
	return static_cast< int >( std::clamp< long >( i, 0, count - 1 ) );
}

double DiameterFromParam( float v )
{
	return geometric( v, kDiameterLow, kDiameterHigh );
}
float ParamFromDiameter( double metres )
{
	return inverseGeometric( metres, kDiameterLow, kDiameterHigh );
}
int ZonesFromParam( float v )
{
	const long n = std::lround( static_cast< double >( v ) );
	return static_cast< int >( std::clamp< long >( n, kZonesLow, kZonesHigh ) );
}
double ZoneFocusFromParam( float v )
{
	return linear( v, kZoneFocusLow, kZoneFocusHigh );
}
float ParamFromZoneFocus( double metres )
{
	return inverseLinear( metres, kZoneFocusLow, kZoneFocusHigh );
}
double DistanceFromParam( float v )
{
	return geometric( v, kDistanceLow, kDistanceHigh );
}
float ParamFromDistance( double metres )
{
	return inverseGeometric( metres, kDistanceLow, kDistanceHigh );
}
double ExposureFromParam( float v )
{
	return linear( v, kExposureLow, kExposureHigh );
}
float ParamFromExposure( double stops )
{
	return inverseLinear( stops, kExposureLow, kExposureHigh );
}

} // namespace pinhole
