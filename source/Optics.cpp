#include "Optics.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace pinhole::optics
{
namespace
{
using Complex = std::complex< double >;

/// Bessel J0 and J1, for the disc-plus-Airy shortcut only (a wrong model the
/// harness feeds the checks). POSIX names them j0/j1; MSVC _j0/_j1.
double besselJ0( double x )
{
#ifdef _MSC_VER
	return _j0( x );
#else
	return ::j0( x );
#endif
}
double besselJ1( double x )
{
#ifdef _MSC_VER
	return _j1( x );
#else
	return ::j1( x );
#endif
}

/// In place, radix 2, e^( -2 pi i j k / N ). N a power of two.
void fft( std::vector< Complex >& a )
{
	const size_t n = a.size();
	for( size_t i = 1, j = 0; i < n; ++i )
	{
		size_t bit = n >> 1;
		for( ; j & bit; bit >>= 1 )
			j ^= bit;
		j ^= bit;
		if( i < j )
			std::swap( a[ i ], a[ j ] );
	}
	for( size_t len = 2; len <= n; len <<= 1 )
	{
		const double angle = -2.0 * kPi / static_cast< double >( len );
		const Complex step( std::cos( angle ), std::sin( angle ) );
		for( size_t i = 0; i < n; i += len )
		{
			Complex w( 1.0, 0.0 );
			for( size_t k = 0; k < len / 2; ++k )
			{
				const Complex x = a[ i + k ], y = a[ i + k + len / 2 ] * w;
				a[ i + k ]           = x + y;
				a[ i + k + len / 2 ] = x - y;
				//Recomputed every 64 steps: a long run of multiplications drifts.
				w = ( ( k + 1 ) % 64 == 0 ) ? Complex( std::cos( angle * static_cast< double >( k + 1 ) ), std::sin( angle * static_cast< double >( k + 1 ) ) )
				                            : w * step;
			}
		}
	}
}

/// G( Y ) = Int_0^Y exp( i u y^2 / 2 ) dy for Y in [ 0, 1 ]: a table of the
/// integral at nodes h apart by 3-point Gauss-Legendre (the phase moves at
/// most u h <= 0.5 rad across a step), and from the nearest node to Y by
/// 4-point Gauss-Legendre.
class FresnelG
{
public:
	explicit FresnelG( double u ) : u( u )
	{
		steps = std::max( 512, static_cast< int >( std::ceil( 2.0 * u ) ) + 64 );
		h     = 1.0 / steps;
		table.resize( static_cast< size_t >( steps ) + 1 );
		const double x3[ 3 ] = { -0.774596669241483377, 0.0, 0.774596669241483377 };
		const double w3[ 3 ] = { 5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0 };
		Complex sum( 0.0, 0.0 );
		table[ 0 ] = sum;
		for( int k = 0; k < steps; ++k )
		{
			const double a = k * h;
			Complex part( 0.0, 0.0 );
			for( int q = 0; q < 3; ++q )
			{
				const double y = a + 0.5 * h * ( 1.0 + x3[ q ] );
				part += w3[ q ] * std::polar( 1.0, 0.5 * u * y * y );
			}
			sum += part * ( 0.5 * h );
			table[ static_cast< size_t >( k ) + 1 ] = sum;
		}
	}

	Complex operator()( double y ) const
	{
		if( y <= 0.0 )
			return Complex( 0.0, 0.0 );
		y            = std::min( y, 1.0 );
		const int k  = std::min( steps - 1, static_cast< int >( y / h ) );
		const double a = k * h, d = y - a;
		const double x4[ 4 ] = { -0.861136311594052575, -0.339981043584856265, 0.339981043584856265, 0.861136311594052575 };
		const double w4[ 4 ] = { 0.347854845137453857, 0.652145154862546143, 0.652145154862546143, 0.347854845137453857 };
		Complex part( 0.0, 0.0 );
		for( int q = 0; q < 4; ++q )
		{
			const double t = a + 0.5 * d * ( 1.0 + x4[ q ] );
			part += w4[ q ] * std::polar( 1.0, 0.5 * u * t * t );
		}
		return table[ static_cast< size_t >( k ) ] + part * ( 0.5 * d );
	}

private:
	double u;
	int steps = 0;
	double h  = 0.0;
	std::vector< Complex > table;
};

/// The disc of radius u (in v) convolved with the Airy pattern, through their
/// transfer functions: 2 J1( u q ) / ( u q ) times the diffraction-limited
/// pupil's chat( q / 2 ), cut off at q = 2. Normalised to the same light as
/// the true pattern, pi u^2.
Pattern discAiry( double u, double vMax, double dv )
{
	Pattern p;
	p.u               = u;
	p.dv              = dv;
	const int count   = static_cast< int >( std::ceil( vMax / dv ) ) + 1;
	const double step = std::min( 0.002, 0.1 / std::max( u, 1e-9 ) );
	const int nq      = static_cast< int >( std::ceil( 2.0 / step ) );
	std::vector< double > weight( static_cast< size_t >( nq ) );
	for( int k = 0; k < nq; ++k )
	{
		const double q    = ( k + 0.5 ) * 2.0 / nq;
		const double s    = q / 2.0;
		const double chat = ( 2.0 / kPi ) * ( std::acos( s ) - s * std::sqrt( 1.0 - s * s ) );
		const double x    = u * q;
		const double disc = x < 1e-8 ? 1.0 : 2.0 * besselJ1( x ) / x;
		weight[ static_cast< size_t >( k ) ] = chat * disc * q * ( 2.0 / nq );
	}
	p.e.resize( static_cast< size_t >( count ) );
	for( int i = 0; i < count; ++i )
	{
		const double v = i * dv;
		double sum     = 0.0;
		for( int k = 0; k < nq; ++k )
			sum += weight[ static_cast< size_t >( k ) ] * besselJ0( ( k + 0.5 ) * 2.0 / nq * v );
		p.e[ static_cast< size_t >( i ) ] = sum;
	}
	//To pi u^2 in all (the open hole's light, in v units).
	double total = 0.0;
	for( int i = 0; i + 1 < count; ++i )
		total += 0.5 * ( p.e[ i ] * i + p.e[ i + 1 ] * ( i + 1 ) ) * dv * dv * 2.0 * kPi;
	const double scale = total > 0.0 ? kPi * u * u / total : 0.0;
	for( double& value : p.e )
		value *= scale;
	return p;
}
} // namespace

//---------------------------------------------------------------------------
double Aperture::OpenFraction() const
{
	double sum = 0.0;
	for( const Ring& ring : rings )
		sum += ring.outer * ring.outer - ring.inner * ring.inner;
	return sum;
}

Aperture MakePinhole( double diameter, const Hooks& hooks )
{
	Aperture a;
	a.radius = hooks.diameterAsRadius ? diameter : 0.5 * diameter;
	a.rings  = { { 0.0, 1.0 } };
	return a;
}

Aperture MakeZonePlate( int openZones, double designWavelength, double distance, const Hooks& hooks )
{
	//Open zones 1, 3, 5 ... 2M - 1: the centre is open and so is the edge.
	const int open  = std::max( 1, openZones );
	const int zones = 2 * open - 1;
	Aperture a;
	a.zonePlate = true;
	a.radius    = std::sqrt( zones * designWavelength * distance * hooks.zoneScale );
	for( int n = 1; n <= zones; n += 2 )
		a.rings.push_back( { std::sqrt( static_cast< double >( n - 1 ) / zones ), std::sqrt( static_cast< double >( n ) / zones ) } );
	return a;
}

double Wavelength( int channel, int sample, const Hooks& hooks )
{
	if( hooks.oneWavelength )
		return 550e-9;
	const int c = std::clamp( channel, 0, 2 );
	return kBandLow[ c ] + ( sample + 0.5 ) / kSamplesPerBand * ( kBandHigh[ c ] - kBandLow[ c ] );
}

//---------------------------------------------------------------------------
double Pattern::At( double v ) const
{
	if( e.empty() || !( v >= 0.0 ) )
		return 0.0;
	const double x = v / dv;
	const size_t k = static_cast< size_t >( x );
	if( k + 1 >= e.size() )
		return k + 1 == e.size() && x == static_cast< double >( k ) ? e.back() : 0.0;
	const double t = x - static_cast< double >( k );
	return e[ k ] + ( e[ k + 1 ] - e[ k ] ) * t;
}

Pattern Monochromatic( const Aperture& aperture, double wavelength, double distance, const Hooks& hooks, double vMax, double dv )
{
	const double R = aperture.radius;
	const double u = 2.0 * kPi * R * R / ( wavelength * distance );
	if( vMax <= 0.0 )
		vMax = ( aperture.zonePlate ? 8.0 * u : 2.0 * u ) + 300.0;
	if( dv <= 0.0 )
		dv = ( aperture.zonePlate || u < 8.0 * kPi ) ? 0.2 : 0.5;
	if( hooks.discAiry && !aperture.zonePlate )
		return discAiry( u, vMax, dv );

	//The chirp's u: the geometric one unless a wrong model detunes or drops it.
	const double uc = hooks.fraunhofer ? 0.0 : u * hooks.chirpScale;

	//p( x ) on [ 0, 1 ] at M + 1 points: fine enough for the chirp (u x dx
	//below pi / 2) and for v up to vMax (pi / dx).
	const int M = std::max( { 256, static_cast< int >( std::ceil( vMax / kPi ) ), static_cast< int >( std::ceil( 2.2 * uc / kPi ) ) } );
	const double dx = 1.0 / M;
	size_t N        = 1;
	const double want = std::max( 2.0 * M + 2.0, 2.0 * kPi / ( dv * dx ) );
	while( static_cast< double >( N ) < want )
		N <<= 1;

	const FresnelG G( uc );
	std::vector< Complex > buffer( N, Complex( 0.0, 0.0 ) );
	for( int j = 0; j <= M; ++j )
	{
		const double x  = j * dx;
		const double xx = x * x;
		Complex sum( 0.0, 0.0 );
		for( const Ring& ring : aperture.rings )
		{
			const double b2 = ring.outer * ring.outer - xx, a2 = ring.inner * ring.inner - xx;
			if( b2 <= 0.0 )
				continue;
			sum += 2.0 * ( G( std::sqrt( b2 ) ) - ( a2 > 0.0 ? G( std::sqrt( a2 ) ) : Complex( 0.0, 0.0 ) ) );
		}
		buffer[ static_cast< size_t >( j ) ] = std::polar( 1.0, 0.5 * uc * xx ) * sum;
	}
	const Complex p0 = buffer[ 0 ];
	fft( buffer );

	//U( v_k ) = 2 Int_0^1 p cos( v x ) dx by the trapezoid rule (p( 1 ) = 0):
	//dx ( F_k + F_( N - k ) - p_0 ).
	Pattern pattern;
	pattern.u          = u;
	pattern.dv         = 2.0 * kPi / ( static_cast< double >( N ) * dx );
	const size_t count = std::min( N / 2, static_cast< size_t >( std::ceil( vMax / pattern.dv ) ) + 1 );
	pattern.e.resize( count );
	const double toOpen = u * u / ( 4.0 * kPi * kPi );
	for( size_t k = 0; k < count; ++k )
	{
		const Complex U = dx * ( buffer[ k ] + buffer[ ( N - k ) % N ] - p0 );
		pattern.e[ k ]  = std::norm( U ) * toOpen;
	}
	return pattern;
}

//---------------------------------------------------------------------------
double Profile::CumAt( int c, double r ) const
{
	const std::vector< double >& ev = e[ c ];
	const std::vector< double >& cv = cum[ c ];
	if( ev.empty() || r <= 0.0 )
		return 0.0;
	const double x = r / dr;
	size_t k       = static_cast< size_t >( x );
	if( k + 1 >= ev.size() )
		return cv.back();
	const double rk    = static_cast< double >( k ) * dr;
	const double t     = r - rk;
	const double slope = ( ev[ k + 1 ] - ev[ k ] ) / dr;
	//Int_rk^( rk + t ) 2 pi r' ( e_k + slope ( r' - rk ) ) dr'
	return cv[ k ] + 2.0 * kPi * ( ev[ k ] * ( rk * t + 0.5 * t * t ) + slope * ( 0.5 * rk * t * t + t * t * t / 3.0 ) );
}

Profile BuildProfile( const Aperture& aperture, double distance, double pixel, double maxRadius, const Hooks& hooks )
{
	Profile profile;
	const double R = aperture.radius;
	Pattern patterns[ 3 ][ kSamplesPerBand ];
	double vPerPx[ 3 ][ kSamplesPerBand ];
	double tableEnd = std::numeric_limits< double >::infinity();
	double dr       = 0.25;
	for( int c = 0; c < 3; ++c )
		for( int s = 0; s < kSamplesPerBand; ++s )
		{
			const double lambda = Wavelength( c, s, hooks );
			if( hooks.oneWavelength && ( c > 0 || s > 0 ) )
				patterns[ c ][ s ] = patterns[ 0 ][ 0 ];
			else
				patterns[ c ][ s ] = Monochromatic( aperture, lambda, distance, hooks );
			vPerPx[ c ][ s ] = 2.0 * kPi * R * pixel / ( lambda * distance );
			tableEnd         = std::min( tableEnd, patterns[ c ][ s ].VMax() / vPerPx[ c ][ s ] );
			dr               = std::min( dr, patterns[ c ][ s ].dv / vPerPx[ c ][ s ] );
		}
	const double end = std::min( tableEnd, maxRadius );
	//Never more than ~200k samples a channel: past that the patterns are
	//finer than any texel can show.
	dr                = std::max( dr, end / 200000.0 );
	const size_t n    = static_cast< size_t >( std::ceil( end / dr ) ) + 1;
	profile.dr        = dr;
	profile.tableEnd  = tableEnd;
	profile.total     = kPi * R * R * aperture.OpenFraction() / ( pixel * pixel );
	for( int c = 0; c < 3; ++c )
	{
		std::vector< double >& ev = profile.e[ c ];
		ev.assign( n, 0.0 );
		for( size_t k = 0; k < n; ++k )
		{
			const double r = static_cast< double >( k ) * dr;
			double sum     = 0.0;
			for( int s = 0; s < kSamplesPerBand; ++s )
				sum += patterns[ c ][ s ].At( r * vPerPx[ c ][ s ] );
			ev[ k ] = sum / kSamplesPerBand;
		}
		std::vector< double >& cv = profile.cum[ c ];
		cv.assign( n, 0.0 );
		for( size_t k = 0; k + 1 < n; ++k )
		{
			const double rk = static_cast< double >( k ) * dr, slope = ( ev[ k + 1 ] - ev[ k ] ) / dr;
			cv[ k + 1 ]     = cv[ k ] + 2.0 * kPi * ( ev[ k ] * ( rk * dr + 0.5 * dr * dr ) + slope * ( 0.5 * rk * dr * dr + dr * dr * dr / 3.0 ) );
		}
	}
	//The support: the widest channel's radius holding kEnergyKept of its light
	//(of the exact total, so a pattern cut short by its table counts as short).
	double support = 0.0;
	for( int c = 0; c < 3; ++c )
	{
		const double target = kEnergyKept * profile.total;
		const std::vector< double >& cv = profile.cum[ c ];
		double r = end;
		const auto it = std::lower_bound( cv.begin(), cv.end(), target );
		if( it != cv.end() )
		{
			const size_t k = static_cast< size_t >( it - cv.begin() );
			double lo = k > 0 ? static_cast< double >( k - 1 ) * dr : 0.0, hi = static_cast< double >( k ) * dr;
			for( int iteration = 0; iteration < 50; ++iteration )
			{
				const double mid = 0.5 * ( lo + hi );
				( profile.CumAt( c, mid ) < target ? lo : hi ) = mid;
			}
			r = hi;
		}
		support = std::max( support, r );
	}
	profile.support = std::min( support, end );
	for( int c = 0; c < 3; ++c )
		profile.kept[ c ] = profile.total > 0.0 ? profile.CumAt( c, profile.support ) / profile.total : 0.0;
	return profile;
}

//---------------------------------------------------------------------------
double AngleInside( double r, double x0, double x1, double y0, double y1 )
{
	if( !( r > 0.0 ) )
		return ( x0 <= 0.0 && x1 >= 0.0 && y0 <= 0.0 && y1 >= 0.0 ) ? 2.0 * kPi : 0.0;
	//One quadrant's rectangle, 0 <= a0 < a1, 0 <= b0 < b1: theta in [ 0, pi/2 ]
	//with x = r cos theta falling and y = r sin theta rising.
	auto quadrant = [ r ]( double a0, double a1, double b0, double b1 ) {
		if( a1 <= a0 || b1 <= b0 )
			return 0.0;
		const double lo = std::max( std::acos( std::min( 1.0, a1 / r ) ), std::asin( std::min( 1.0, b0 / r ) ) );
		const double hi = std::min( std::acos( std::min( 1.0, a0 / r ) ), std::asin( std::min( 1.0, b1 / r ) ) );
		return std::max( 0.0, hi - lo );
	};
	struct Span
	{
		double lo, hi;
	};
	//Each axis split at 0, the negative part mirrored.
	Span xs[ 2 ] = { { std::max( x0, 0.0 ), x1 }, { std::max( -x1, 0.0 ), -x0 } };
	Span ys[ 2 ] = { { std::max( y0, 0.0 ), y1 }, { std::max( -y1, 0.0 ), -y0 } };
	double total = 0.0;
	for( const Span& x : xs )
		for( const Span& y : ys )
			total += quadrant( x.lo, x.hi, y.lo, y.hi );
	return total;
}

size_t Kernel::MaxTaps() const
{
	size_t most = 0;
	for( int k = 0; k <= levels; ++k )
		most = std::max( most, taps[ k ].size() );
	return most;
}

Kernel BuildKernel( const Profile& profile )
{
	Kernel kernel;
	const double support = std::max( profile.support, 0.5 );
	kernel.support       = support;
	int levels           = 0;
	while( kCoreRadius * std::ldexp( 1.0, levels ) < support && levels < kMaxLevels )
		++levels;
	kernel.levels = levels;

	for( int level = 0; level <= levels; ++level )
	{
		const double s    = std::ldexp( 1.0, level );
		const double rIn  = level == 0 ? 0.0 : kCoreRadius * std::ldexp( 1.0, level - 1 );
		const double rOut = std::min( kCoreRadius * s, support );
		if( rOut <= rIn )
			continue;
		const int reach = static_cast< int >( std::ceil( rOut / s + 0.75 ) );
		for( int j = -reach; j <= reach; ++j )
			for( int i = -reach; i <= reach; ++i )
			{
				const double x0 = i * s - 0.5 * s, x1 = i * s + 0.5 * s;
				const double y0 = j * s - 0.5 * s, y1 = j * s + 0.5 * s;
				const double nx = std::max( 0.0, std::fabs( i * s ) - 0.5 * s ), ny = std::max( 0.0, std::fabs( j * s ) - 0.5 * s );
				const double fx = std::fabs( i * s ) + 0.5 * s, fy = std::fabs( j * s ) + 0.5 * s;
				const double a = std::max( rIn, std::sqrt( nx * nx + ny * ny ) );
				const double b = std::min( rOut, std::sqrt( fx * fx + fy * fy ) );
				if( b <= a )
					continue;
				//The angle inside the square is smooth between its kinks; the
				//pattern's own structure is in the cumulative, exactly.
				const int steps = std::max( 8, static_cast< int >( std::ceil( ( b - a ) / ( s / 32.0 ) ) ) );
				const double h  = ( b - a ) / steps;
				double w[ 3 ]   = {};
				double before[ 3 ];
				for( int c = 0; c < 3; ++c )
					before[ c ] = profile.CumAt( c, a );
				for( int m = 0; m < steps; ++m )
				{
					const double r1    = a + ( m + 1 ) * h;
					const double share = AngleInside( a + ( m + 0.5 ) * h, x0, x1, y0, y1 ) / ( 2.0 * kPi );
					for( int c = 0; c < 3; ++c )
					{
						const double after = profile.CumAt( c, r1 );
						w[ c ] += share * ( after - before[ c ] );
						before[ c ] = after;
					}
				}
				if( w[ 0 ] <= 0.0 && w[ 1 ] <= 0.0 && w[ 2 ] <= 0.0 )
					continue;
				Tap tap { i, j, { static_cast< float >( w[ 0 ] ), static_cast< float >( w[ 1 ] ), static_cast< float >( w[ 2 ] ) } };
				kernel.taps[ level ].push_back( tap );
				for( int c = 0; c < 3; ++c )
					kernel.sum[ c ] += w[ c ];
			}
	}
	//To unit sum per channel: a flat field is exact.
	for( int level = 0; level <= levels; ++level )
		for( Tap& tap : kernel.taps[ level ] )
			for( int c = 0; c < 3; ++c )
				tap.w[ c ] = kernel.sum[ c ] > 0.0 ? static_cast< float >( tap.w[ c ] / kernel.sum[ c ] ) : 0.0f;
	return kernel;
}

} // namespace pinhole::optics
