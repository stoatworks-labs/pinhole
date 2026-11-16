#pragma once

#include <vector>

/**
    The optics, on the CPU and with no GL: the Fresnel pattern of an aperture
    at one wavelength, each colour band's mean pattern on the screen, and the
    pyramid's taps. Everything the shaders are told about the blur comes from
    here; the shaders only gather.

    **The pattern.** For an aperture of outer radius R at distance f from the
    screen, lit by a plane wave of wavelength lambda (the scene is at
    infinity), with Lommel's variables

        u = 2 pi R^2 / ( lambda f ),    v = 2 pi R c / ( lambda f )

    (c the distance from the pattern's centre on the screen), the field is the
    Fresnel integral over the open rings (Rayleigh 1891 eqs 4-8),

        U( v ) = Int Int_open exp( i u rho^2 / 2 ) exp( -i v x ) dx dy

    in units of R, and the irradiance relative to the open wave is
    |U|^2 u^2 / ( 4 pi^2 ). The pattern is radially symmetric, so by the
    projection-slice theorem U( v ) is the 1D Fourier transform of the
    pupil's projection

        p( x ) = exp( i u x^2 / 2 ) Sum_rings 2 [ G( sqrt( b^2 - x^2 ) ) - G( sqrt( a^2 - x^2 ) ) ],
        G( Y ) = Int_0^Y exp( i u y^2 / 2 ) dy,

    and that transform is one FFT. No Fraunhofer approximation, no
    disc-plus-Airy shortcut: Fresnel diffraction as far as the sampling goes.
    On the axis it is 4 sin^2( u / 4 ) (Rayleigh eq. 20); as u -> 0 it is the
    Airy pattern; as u -> infinity the geometric disc.

    **The screen** is 24 mm tall (a 135 frame), so a pixel is 24 mm / H.

    **The bands.** Channel 0 (R) is 600-700 nm, 1 (G) 500-600, 2 (B) 400-500,
    each the mean of six wavelengths at its sub-bands' midpoints, equal energy
    per nanometre. A decision, not physics: an sRGB channel is not a band.
*/
namespace pinhole::optics
{
constexpr double kPi = 3.14159265358979323846;

/// m: the screen's height, a 135 film frame's 24 mm (24 x 36 mm).
constexpr double kFrameHeight = 24e-3;

/// m: each channel's band, R G B.
constexpr double kBandLow[ 3 ]  = { 600e-9, 500e-9, 400e-9 };
constexpr double kBandHigh[ 3 ] = { 700e-9, 600e-9, 500e-9 };
constexpr int kSamplesPerBand   = 6;

/// The plugin's optimum: the area-width minimum of Mielenz (1999) eq. 9,
/// u = 1.7 pi, so d = sqrt( 2 u / pi ) sqrt( lambda f ) = 1.844 sqrt( lambda f ).
constexpr double kOptimumU = 1.7 * kPi;

/// The share of each channel's light the kernel keeps (then renormalises).
constexpr double kEnergyKept = 0.99;

/// The harness's wrong models. All off in the plugin.
struct Hooks
{
	bool fraunhofer       = false;///< the chirp dropped: the Fraunhofer (Airy) pattern at every u
	double chirpScale     = 1.0;  ///< u detuned in the chirp's phase
	bool discAiry         = false;///< the geometric disc convolved with the Airy pattern (a pinhole only)
	bool oneWavelength    = false;///< every band at 550 nm
	bool diameterAsRadius = false;///< the hole twice as wide as asked
	double zoneScale      = 1.0;  ///< every zone radius squared scaled
};

/// One open ring, in units of the aperture's outer radius.
struct Ring
{
	double inner, outer;
};

struct Aperture
{
	double radius = 0.0;///< m, the outermost open edge
	std::vector< Ring > rings;
	bool zonePlate = false;

	/// The open area over pi R^2.
	double OpenFraction() const;
};

/// A round hole of this diameter, m.
Aperture MakePinhole( double diameter, const Hooks& hooks = {} );
/// A binary zone plate of `zones` zones (counted separately, the centre one
/// open), made to focus `designWavelength` at `distance`: r_n^2 = n lambda_d f
/// (X-Ray Data Booklet 4.4 eq. 1 without its n^2 lambda^2 / 4 term).
Aperture MakeZonePlate( int zones, double designWavelength, double distance, const Hooks& hooks = {} );

/// One aperture at one wavelength: E / E_open at v_k = k dv.
struct Pattern
{
	double u  = 0.0;///< 2 pi R^2 / ( lambda f ), the geometric one (Hooks::chirpScale is not in it)
	double dv = 0.0;
	std::vector< double > e;

	/// Linear between samples, 0 past the end.
	double At( double v ) const;
	double VMax() const
	{
		return e.empty() ? 0.0 : dv * static_cast< double >( e.size() - 1 );
	}
};

/// The pattern by the projection-slice evaluation above. `vMax` and `dv`
/// override the plugin's choices (the harness's tables want finer ones).
Pattern Monochromatic( const Aperture& aperture, double wavelength, double distance, const Hooks& hooks = {}, double vMax = 0.0,
                       double dv = 0.0 );

/// The k-th wavelength of a channel's band, m.
double Wavelength( int channel, int sample, const Hooks& hooks = {} );

/// Each channel's band-mean pattern on the screen, in pixels.
struct Profile
{
	double dr = 0.0;            ///< px between samples
	std::vector< double > e[ 3 ];  ///< E / E_open, the band's mean, at r = k dr
	std::vector< double > cum[ 3 ];///< Int_0^r E 2 pi r dr, px^2, exact for e linear between samples
	double total    = 0.0;      ///< Int E dA over the whole screen: the open area, px^2 (exact)
	double support  = 0.0;      ///< px: the radius holding kEnergyKept of the light, the widest channel's
	double tableEnd = 0.0;      ///< px: where the computed patterns end
	double kept[ 3 ] = {};      ///< each channel's share of its light inside `support`

	/// Int_0^r E 2 pi r dr for channel c.
	double CumAt( int c, double r ) const;
};

/// `pixel` m per px; `maxRadius` px caps the support (the plugin passes the
/// frame's height).
Profile BuildProfile( const Aperture& aperture, double distance, double pixel, double maxRadius, const Hooks& hooks = {} );

//---------------------------------------------------------------------------
// The pyramid. The core, r <= kCoreRadius px, is gathered at full resolution;
// the ring kCoreRadius 2^( k - 1 ) < r <= kCoreRadius 2^k from level k (2^k px
// texels, 2 x 2 box averages of level k - 1), at level k's resolution, and
// each level adds a bilinear sample of the level above. A tap's weight is
// the pattern integrated exactly over its texel square inside its ring (by
// arc length), per channel; all of them sum to 1 per channel.
//---------------------------------------------------------------------------
constexpr int kCoreRadius = 8;
constexpr int kMaxLevels  = 12;

struct Tap
{
	int i, j;      ///< offset in the level's texels
	float w[ 3 ];  ///< R G B
};

struct Kernel
{
	int levels = 0;///< coarse levels: 0 means the core alone
	std::vector< Tap > taps[ kMaxLevels + 1 ];
	double support   = 0.0;
	double sum[ 3 ]  = {};///< before normalising: the light inside the support, px^2
	size_t MaxTaps() const;
};

Kernel BuildKernel( const Profile& profile );

/// The angle of the circle of radius r about the origin that lies inside the
/// rectangle [ x0, x1 ] x [ y0, y1 ], radians.
double AngleInside( double r, double x0, double x1, double y0, double y1 );

} // namespace pinhole::optics
