// The checks. Included into main.cpp's anonymous namespace; see main.cpp for
// the Rig and the pictures.

//===========================================================================
// The harness's own physics, restated from SPEC-pinhole.md and its sources,
// not from the plugin's source. Every constant is given again with where it
// comes from.
//===========================================================================
constexpr double hPi    = 3.14159265358979323846;
constexpr double hFrame = 24e-3;///< m, the screen's height: a 135 frame is 24 x 36 mm (Wikipedia, "135 film")
/// m, each channel's band, R G B: the spec's decision (an sRGB channel is not a band).
constexpr double hBandLow[ 3 ]  = { 600e-9, 500e-9, 400e-9 };
constexpr double hBandHigh[ 3 ] = { 700e-9, 600e-9, 500e-9 };
/// Mielenz (1999) eq. 9: the area width of a point's image is least at
/// u = 2 pi a^2 / ( lambda f ) = 1.7 pi, given to two figures.
constexpr double hMielenzU        = 1.7 * hPi;
constexpr double hMielenzRounding = 0.05 * hPi;
/// The visibility tolerance for what is a numerical evaluation of an exact
/// formula: a quarter of an 8-bit code step on a full-scale picture.
constexpr double kVisible = 1.0 / 1020.0;
/// IEEE half precision, the pyramid's storage: 2^-11 relative a rounding.
/// Four roundings (level, ring sum, the next ring's sum, the core) bound a
/// flat field's level.
constexpr double kHalfFloat = 4.0 * 0.00048828125;
/// The same light encoded at two field angles and decoded: float32's
/// rounding in the encode, decode and cos^4 (2^-23 relative each, times the
/// sRGB curve's steepest slope 12.92), with room: 1e-5.
constexpr double kFloatCode = 1e-5;

const char* const kChannel[ 3 ] = { "R", "G", "B" };

/// The larger of two errors, NaN winning: std::max( worst, NaN ) keeps
/// worst, so a model that produced NaN would pass every "worst <= tolerance".
double worse( double a, double b )
{
	return ( a != a || b != b ) ? std::numeric_limits< double >::quiet_NaN() : std::max( a, b );
}
template< typename... T >
double worse( double a, double b, T... rest )
{
	return worse( worse( a, b ), rest... );
}

//---------------------------------------------------------------------------
// The controls, restated from the spec's table.
//---------------------------------------------------------------------------
double hClamp01( float v )
{
	return std::clamp( static_cast< double >( v ), 0.0, 1.0 );
}
double hDiameter( float v )///< m: 0.1 to 2 mm, geometric
{
	return 0.1e-3 * std::pow( 20.0, hClamp01( v ) );
}
double hDistance( float v )///< m: 10 to 500 mm, geometric
{
	return 10e-3 * std::pow( 50.0, hClamp01( v ) );
}
double hFocus( float v )///< m: 400 to 700 nm, linear
{
	return 400e-9 + 300e-9 * hClamp01( v );
}
double hStops( float v )///< -4 to +4, linear
{
	return -4.0 + 8.0 * hClamp01( v );
}

double hPixel( int h )///< m a pixel on the screen
{
	return hFrame / h;
}
/// cos^4 of the angle between the axis and the line from the hole to pixel
/// ( x, y )'s centre (Aggarwal, Hua & Ahuja 2001, sec. 1): cos theta =
/// f / sqrt( f^2 + r^2 ), r the pixel's distance from the frame's centre.
double hCos4( int x, int y, int w, int h, double f )
{
	const double p  = hPixel( h ) / f;
	const double tx = ( x + 0.5 - 0.5 * w ) * p, ty = ( y + 0.5 - 0.5 * h ) * p;
	const double c2 = 1.0 / ( 1.0 + tx * tx + ty * ty );
	return c2 * c2;
}

/// The radius of the pyramid level that holds radius r: its texel, px (the
/// spec: the core to 8 px, then rings 8 2^( k - 1 ) to 8 2^k at 2^k px).
double hTexelAt( double r )
{
	if( r <= 8.0 )
		return 1.0;
	return std::ldexp( 1.0, static_cast< int >( std::ceil( std::log2( r / 8.0 ) ) ) );
}

//---------------------------------------------------------------------------
// The Airy pattern's edge spread, by the harness's own route: the pattern
// ( 2 J1( v ) / v )^2 (Mielenz, under eq. 7a) has for transfer function the
// unit pupil's autocorrelation over its area, T( q ) = ( 2 / pi )( acos( q/2 )
// - ( q/2 ) sqrt( 1 - q^2/4 ) ) on 0 <= q <= 2 (two unit circles' overlap),
// so its edge spread is ESF( v ) = 1/2 + ( 1/pi ) Int_0^2 T( q ) sin( q v ) / q dq.
// Nothing here is the plugin's FFT.
//---------------------------------------------------------------------------
double hAiryEsf( double v )
{
	const int n = 4000;
	const double h = 2.0 / n;
	double sum     = 0.0;
	for( int i = 0; i <= n; ++i )
	{
		const double q = i * h, s = q / 2.0;
		const double T = ( 2.0 / hPi ) * ( std::acos( std::min( s, 1.0 ) ) - s * std::sqrt( std::max( 0.0, 1.0 - s * s ) ) );
		const double f = q == 0.0 ? v : std::sin( q * v ) / q;
		const double w = ( i == 0 || i == n ) ? 1.0 : ( i % 2 == 1 ? 4.0 : 2.0 );
		sum += w * T * f;
	}
	return 0.5 + sum * h / 3.0 / hPi;
}

/// A channel's band-mean Airy edge spread at X m from the edge, for a hole of
/// diameter d at f: the band as 40 equal sub-bands, each at its midpoint
/// (the plugin samples six; the difference is the quadrature's).
double hBandAiryEsf( int c, double X, double d, double f )
{
	const int n = 40;
	double sum  = 0.0;
	for( int j = 0; j < n; ++j )
	{
		const double lambda = hBandLow[ c ] + ( j + 0.5 ) / n * ( hBandHigh[ c ] - hBandLow[ c ] );
		sum += hAiryEsf( 2.0 * hPi * ( 0.5 * d ) * X / ( lambda * f ) );
	}
	return sum / n;
}

/// The interquartile width of a channel's band-mean Airy edge spread, m.
double hBandAiryIqr( int c, double d, double f )
{
	double lo = 0.0, hi = 50.0 * hBandHigh[ c ] * f / d;
	for( int i = 0; i < 60; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( hBandAiryEsf( c, mid, d, f ) < 0.75 ? lo : hi ) = mid;
	}
	return lo + hi;//2 x the upper quartile: the edge spread is odd about 1/2
}

/// A uniform disc's edge spread: its line spread is a semicircle, and the
/// quartile x / a solves x sqrt( 1 - x^2 ) + asin x = pi / 4.
double hDiscIqrOverRadius()
{
	double lo = 0.0, hi = 1.0;
	for( int i = 0; i < 60; ++i )
	{
		const double x = 0.5 * ( lo + hi );
		( x * std::sqrt( 1.0 - x * x ) + std::asin( x ) < 0.25 * hPi ? lo : hi ) = x;
	}
	return lo + hi;
}

//---------------------------------------------------------------------------
// Rayleigh (1891) pp. 94-96, deduced from Lommel's tables III-VI: s M^2 at
// integer z for y = s pi (s = 1 ... 4), Rayleigh's own s = 1/2, and the
// tables' extremum rows. M^2 = ( E / E_open ) 4 / y^2 with y = u, z = v.
//---------------------------------------------------------------------------
struct LommelRow
{
	double s, z, value;
	bool extremum;
};

const std::vector< LommelRow >& lommelRows()
{
	static std::vector< LommelRow > rows;
	if( !rows.empty() )
		return rows;
	const double integer[ 4 ][ 13 ] = {
		{ .8106, .6286, .2772, .0623, .0269, .0306, .0121, .0018, .0051, .0037, .0004, .0013, .0016 },
		{ .8106, .6316, .3117, .1560, .1438, .1077, .0426, .0200, .0227, .0125, .0034, .0053, .0046 },
		{ .2702, .2159, .1631, .2110, .2449, .1734, .0916, .0739, .0651, .0335, .0156, .0178, .0122 },
		{ 0.0, .0056, .0609, .1594, .1947, .1515, .1293, .1399, .1148, .0658, .0484, .0458, .0280 },
	};
	for( int s = 1; s <= 4; ++s )
		for( int z = 0; z <= 12; ++z )
			rows.push_back( { static_cast< double >( s ), static_cast< double >( z ), integer[ s - 1 ][ z ], false } );
	const double half[ 5 ] = { .4748, .3679, .1590, .0272, .0041 };
	for( int z = 0; z <= 4; ++z )
		rows.push_back( { 0.5, static_cast< double >( z ), half[ z ], false } );
	const double ext[][ 3 ] = {
		{ 1, 3.8317, .0263 }, { 1, 4.7153, .0320 }, { 1, 7.0156, .0018 }, { 1, 8.3060, .0055 }, { 1, 10.1735, .0003 }, { 1, 11.5785, .0019 },
		{ 2, 3.5977, .1440 }, { 2, 3.8317, .1442 }, { 2, 7.0156, .0198 }, { 2, 7.8879, .0229 }, { 2, 10.1735, .0032 }, { 2, 11.4135, .0059 },
		{ 3, 1.9969, .1631 }, { 3, 3.8317, .2467 }, { 3, 7.0156, .0737 }, { 3, 7.0878, .0739 }, { 3, 10.1735, .0154 }, { 3, 11.0361, .0178 },
		{ 4, 3.8317, .1961 }, { 4, 5.8978, .1291 }, { 4, 7.0156, .1399 }, { 4, 10.1735, .0483 }, { 4, 10.3861, .0483 },
	};
	for( const auto& e : ext )
		rows.push_back( { e[ 0 ], e[ 1 ], e[ 2 ], true } );
	return rows;
}

//---------------------------------------------------------------------------
// Pattern arithmetic, on the plugin's builder's output (the offline checks).
//---------------------------------------------------------------------------
/// A pinhole whose u is `u` at 550 nm and 50 mm.
optics::Aperture holeForU( double u, const optics::Hooks& hooks )
{
	const double a = std::sqrt( u * 550e-9 * 0.05 / ( 2.0 * hPi ) );
	return optics::MakePinhole( 2.0 * a, hooks );
}

/// The builder at four times its own sampling (x samples x 4, v step / 4).
optics::Hooks finer( optics::Hooks hooks )
{
	hooks.sampling = 4.0;
	return hooks;
}

/// A profile's channel at radius r px, linear between samples.
double profileAt( const optics::Profile& p, int c, double r )
{
	const double x = r / p.dr;
	const size_t k = static_cast< size_t >( x );
	if( k + 1 >= p.e[ c ].size() )
		return k < p.e[ c ].size() ? p.e[ c ][ k ] : 0.0;
	return p.e[ c ][ k ] + ( p.e[ c ][ k + 1 ] - p.e[ c ][ k ] ) * ( x - static_cast< double >( k ) );
}

/// Int_0^VMax E 2 pi v dv by the trapezoid rule.
double patternFlux( const optics::Pattern& p )
{
	double sum = 0.0;
	for( size_t k = 0; k + 1 < p.e.size(); ++k )
		sum += 0.5 * p.dv * ( p.e[ k ] * k * p.dv + p.e[ k + 1 ] * ( k + 1 ) * p.dv ) * 2.0 * hPi;
	return sum;
}

/// The light past VMax: far from the image the field is the edges'
/// diffraction, an edge at rho (units of the outer radius) giving
/// |U|^2 = 4 pi rho / v^3 on average (the disc's 2 pi rho J1( rho v ) / v at
/// large v), so E / E_open = u^2 Sum rho / ( pi v^3 ) and the light past V is
/// 2 u^2 Sum rho / V.
double patternTail( const optics::Aperture& a, const optics::Pattern& p )
{
	double edges = 0.0;
	for( const optics::Ring& r : a.rings )
		edges += r.outer + ( r.inner > 0.0 ? r.inner : 0.0 );
	return 2.0 * p.u * p.u * edges / p.VMax();
}

/// The share of a radial pattern's light inside the strip |x| < q (v units):
/// the circle of radius v lies inside it entirely for v <= q, and for a
/// share 2 asin( q / v ) / pi beyond.
double stripShare( const optics::Pattern& p, double q, double total )
{
	auto weight = [ q ]( double v ) { return v <= q ? 2.0 * hPi * v : 4.0 * v * std::asin( q / v ); };
	double sum  = 0.0;
	for( size_t k = 0; k + 1 < p.e.size(); ++k )
		sum += 0.5 * p.dv * ( p.e[ k ] * weight( k * p.dv ) + p.e[ k + 1 ] * weight( ( k + 1 ) * p.dv ) );
	return sum / total;
}

/// The edge spread's interquartile width, v units.
double patternIqr( const optics::Pattern& p, double total )
{
	double lo = 0.0, hi = p.VMax();
	for( int i = 0; i < 60; ++i )
	{
		const double mid = 0.5 * ( lo + hi );
		( stripShare( p, mid, total ) < 0.5 ? lo : hi ) = mid;
	}
	return lo + hi;
}

/// The area width of a point's image (Mielenz): the light along a line
/// through the centre over the peak, 2 Int_0^inf E dv / E( 0 ), v units.
double areaWidth( const optics::Pattern& p )
{
	double sum = 0.0;
	for( size_t k = 0; k + 1 < p.e.size(); ++k )
		sum += 0.5 * p.dv * ( p.e[ k ] + p.e[ k + 1 ] );
	return 2.0 * sum / p.e[ 0 ];
}

//===========================================================================
// Rigs and pictures.
//===========================================================================
/// Every check sets every control it depends on (honeydew's trap).
void defaults( Rig& rig )
{
	rig.Set( PT_APERTURE, static_cast< int >( kPinhole ) );
	rig.Set( PT_DIAMETER, ParamFromDiameter( std::sqrt( 2.0 * 1.7 * 550e-9 * 50e-3 ) ) );
	rig.Set( PT_ZONES, 4.0f );
	rig.Set( PT_ZONE_FOCUS, ParamFromZoneFocus( 550e-9 ) );
	rig.Set( PT_DISTANCE, ParamFromDistance( 50e-3 ) );
	rig.Set( PT_EXPOSURE, ParamFromExposure( 0.0 ) );
	rig.Set( PT_VIEW, static_cast< int >( kUpright ) );
	rig.Set( PT_EDGE, static_cast< int >( kMirror ) );
	rig.Set( PT_MIX, 1.0f );
}

bool startRig( Rig& rig, int w, int h, const Perturb& perturb, const Floats* picture = nullptr )
{
	if( !rig.Init( w, h, picture ) )
		return false;
	rig.plugin.HooksForTest() = perturb;
	defaults( rig );
	return true;
}

Floats grey( int w, int h, double code, double alpha = 1.0 )
{
	return flatCard( w, h, static_cast< float >( code ), static_cast< float >( code ), static_cast< float >( code ), static_cast< float >( alpha ) );
}

/// Black, with one white pixel at ( x, y ) (rows bottom first).
Floats pointCard( int w, int h, int x, int y )
{
	Floats card = grey( w, h, 0.0 );
	float* o    = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
	o[ 0 ] = o[ 1 ] = o[ 2 ] = 1.0f;
	return card;
}

/// The picture's light: linear (IEC 61966-2-1, the harness's own decoding),
/// with cos^4 and 2^Exposure divided out. Rows bottom first.
struct Light
{
	int w = 0, h = 0;
	std::vector< double > c[ 3 ];
	double at( int ch, int x, int y ) const
	{
		return c[ ch ][ static_cast< size_t >( y ) * w + x ];
	}
};

Light lightOf( const Floats& out, int w, int h, double f, double stops = 0.0 )
{
	Light l;
	l.w = w;
	l.h = h;
	const double gain = std::exp2( stops );
	for( int ch = 0; ch < 3; ++ch )
		l.c[ ch ].resize( static_cast< size_t >( w ) * h );
	for( int y = 0; y < h; ++y )
		for( int x = 0; x < w; ++x )
		{
			const size_t p    = static_cast< size_t >( y ) * w + x;
			const double fall = hCos4( x, y, w, h, f ) * gain;
			for( int ch = 0; ch < 3; ++ch )
				l.c[ ch ][ p ] = linearOf( out[ 4 * p + ch ] ) / fall;
		}
	return l;
}

/// Where a rising row of samples first reaches `level`, linear between
/// samples; positions are pixel centres less `origin`. NaN if it never does.
double crossing( const std::vector< double >& row, double level, double origin )
{
	for( size_t i = 1; i < row.size(); ++i )
		if( row[ i - 1 ] < level && row[ i ] >= level )
			return ( i - 1 ) + 0.5 - origin + ( level - row[ i - 1 ] ) / ( row[ i ] - row[ i - 1 ] );
	return std::numeric_limits< double >::quiet_NaN();
}

//===========================================================================
// --fresnel (offline): the pattern builder against the sources.
//===========================================================================
int runFresnel( const Perturb& perturb )
{
	std::printf( "\n=== fresnel: the plugin's Fresnel builder against Lommel's tables (as Rayleigh prints them), Rayleigh's axis 4 sin^2( u/4 ),\n"
	             "    the flux through the aperture, the Airy limit (libm's j1), the geometric limit, Mielenz's optimum, and a zone plate's axis\n" );
	const optics::Hooks& hooks = perturb.optics;

	//1. Lommel. Four times the builder's own x sampling, v to 400 in steps of
	//   0.01 (the plugin's own sampling is checked against the finer one below).
	{
		std::map< double, optics::Pattern > patterns;
		int agree = 0, total = 0;
		std::string disagreements;
		for( const LommelRow& row : lommelRows() )
		{
			const double u = row.s * hPi;
			if( patterns.find( row.s ) == patterns.end() )
				patterns[ row.s ] = optics::Monochromatic( holeForU( u, hooks ), 550e-9, 0.05, finer( hooks ), 400.0, 0.01 );
			const double mine = patterns[ row.s ].At( row.z ) * 4.0 / ( u * u ) * row.s;
			++total;
			if( std::fabs( mine - row.value ) <= 5e-5 )
				++agree;
			else
				disagreements += fmt( " %s%g pi z=%g: %.4f printed, %.5f computed;", row.extremum ? "(ext) " : "", row.s, row.z, row.value, mine );
		}
		const int need = ( 3 * total + 3 ) / 4;
		Check( agree >= need, fmt( "Lommel via Rayleigh: %d of %d printed values to 5e-5 (half the last printed digit); at least %d required. Off:%s", agree,
		                           total, need, disagreements.empty() ? " none" : disagreements.c_str() ) );
	}

	//2. The axis and the flux over u, at four times the builder's sampling.
	{
		double worstAxis = 0.0, worstFlux = 0.0;
		std::string lines;
		for( const double s : { 0.5, 1.0, 1.7, 2.0, 3.0, 4.0, 10.0, 40.0 } )
		{
			const double u             = s * hPi;
			const optics::Aperture a   = holeForU( u, hooks );
			const optics::Pattern fine = optics::Monochromatic( a, 550e-9, 0.05, finer( hooks ) );
			const double axis          = 4.0 * std::pow( std::sin( u / 4.0 ), 2.0 );
			const double peak          = *std::max_element( fine.e.begin(), fine.e.end() );
			worstAxis                  = worse( worstAxis, std::fabs( fine.e[ 0 ] - axis ) / peak );
			const double flux          = ( patternFlux( fine ) + patternTail( a, fine ) ) / ( hPi * u * u ) - 1.0;
			worstFlux                  = worse( worstFlux, std::fabs( flux ) );
			lines += fmt( " %g pi: axis %.6f (%.6f), flux %+.1e;", s, fine.e[ 0 ], axis, flux );
		}
		Check( worstAxis <= kVisible && worstFlux <= kVisible,
		       fmt( "the axis against Rayleigh's 4 sin^2( u/4 ) (eq. 20): worst %.2g of the peak; the light through the hole against pi u^2 (Parseval), "
		            "the tail past the table added: worst %.2g (tolerance %.2g each, a quarter of an 8-bit code step).%s",
		            worstAxis, worstFlux, kVisible, lines.c_str() ) );
	}

	//2b. The plugin's own sampling: each channel's band-mean profile on the
	//    screen, as the plugin builds it, against the same at four times the
	//    sampling.
	{
		struct Case
		{
			const char* name;
			optics::Aperture a;
			double f;
		};
		const Case cases[] = {
			{ "0.31 mm at 50 mm", optics::MakePinhole( std::sqrt( 2.0 * 1.7 * 550e-9 * 50e-3 ), hooks ), 50e-3 },
			{ "0.1 mm at 500 mm", optics::MakePinhole( 0.1e-3, hooks ), 500e-3 },
			{ "2 mm at 10 mm", optics::MakePinhole( 2e-3, hooks ), 10e-3 },
			{ "0.6 mm at 10 mm", optics::MakePinhole( 0.6e-3, hooks ), 10e-3 },
			{ "4 zones for 550 nm at 50 mm", optics::MakeZonePlate( 4, 550e-9, 50e-3, hooks ), 50e-3 },
			{ "16 zones for 400 nm at 500 mm", optics::MakeZonePlate( 16, 400e-9, 500e-3, hooks ), 500e-3 },
		};
		double worst = 0.0;
		std::string lines;
		for( const Case& c : cases )
		{
			const double pixel        = hFrame / 720.0;
			const optics::Profile own = optics::BuildProfile( c.a, c.f, pixel, 720.0, hooks );
			const optics::Profile fin = optics::BuildProfile( c.a, c.f, pixel, 720.0, finer( hooks ) );
			double here               = 0.0;
			for( int ch = 0; ch < 3; ++ch )
			{
				const double peak = *std::max_element( fin.e[ ch ].begin(), fin.e[ ch ].end() );
				for( size_t k = 0; k < own.e[ ch ].size(); ++k )
				{
					const double r = k * own.dr;
					if( r > fin.tableEnd )
						break;
					here = worse( here, std::fabs( own.e[ ch ][ k ] - profileAt( fin, ch, r ) ) / peak );
				}
			}
			worst = worse( worst, here );
			lines += fmt( " %s %.1e;", c.name, here );
		}
		Check( worst <= kVisible, fmt( "the plugin's own sampling: each band-mean profile at 720 lines against four times the sampling, worst %.2g of its peak "
		                               "(tolerance %.2g).%s",
		                               worst, kVisible, lines.c_str() ) );
	}

	//3. The Airy limit: a hole of u = 0.01, normalised at the centre.
	{
		const optics::Pattern p = optics::Monochromatic( holeForU( 0.01, hooks ), 550e-9, 0.05, hooks, 300.0, 0.01 );
		double worst            = 0.0;
		for( double v = 0.0; v <= 30.0; v += 0.05 )
		{
			const double airy = v < 1e-9 ? 1.0 : std::pow( 2.0 * ::j1( v ) / v, 2.0 );
			worst             = worse( worst, std::fabs( p.At( v ) / p.e[ 0 ] - airy ) );
		}
		Check( worst <= kVisible, fmt( "u = 0.01: the pattern over its centre against ( 2 J1( v ) / v )^2 (Mielenz, under eq. 7a; libm's j1) for v <= 30: "
		                               "worst %.2g (tolerance %.2g)",
		                               worst, kVisible ) );
	}

	//4. The geometric limit: u = 400 pi, the edge spread a disc's.
	{
		const double u          = 400.0 * hPi;
		const optics::Pattern p = optics::Monochromatic( holeForU( u, hooks ), 550e-9, 0.05, hooks );
		const double iqr        = patternIqr( p, hPi * u * u ) / u;
		const double disc       = hDiscIqrOverRadius();
		//The edge's Fresnel scale is sqrt( lambda f ); what it spills across a
		//straight edge it takes from the other side, so the quartile moves
		//only at second order: ( sqrt( lambda f ) )^2 / a = 2 pi a / u.
		const double second = 2.0 * hPi / u;
		Check( std::fabs( iqr - disc ) <= second,
		       fmt( "u = 400 pi: the edge spread's interquartile width %.5f a against a uniform disc's %.5f a: off %.2g a (tolerance %.4f a, the edge's "
		            "Fresnel scale squared over the radius, lambda f / a)",
		            iqr, disc, iqr - disc, second ) );
	}

	//5. Mielenz's optimum: the least area width (physical: over sqrt u at
	//   fixed lambda f) by golden section on u / pi in [ 1.4, 2.0 ].
	{
		auto width = [ & ]( double s ) {
			const double u = s * hPi;
			return areaWidth( optics::Monochromatic( holeForU( u, hooks ), 550e-9, 0.05, hooks, 400.0, 0.01 ) ) / std::sqrt( u );
		};
		const double g = 0.5 * ( std::sqrt( 5.0 ) - 1.0 );
		double a = 1.4, b = 2.0, c = b - g * ( b - a ), d = a + g * ( b - a );
		double fc = width( c ), fd = width( d );
		while( b - a > 1e-3 )
		{
			if( fc < fd )
			{
				b  = d;
				d  = c;
				fd = fc;
				c  = b - g * ( b - a );
				fc = width( c );
			}
			else
			{
				a  = c;
				c  = d;
				fc = fd;
				d  = a + g * ( b - a );
				fd = width( d );
			}
		}
		const double best = 0.5 * ( a + b ) * hPi;
		Check( std::fabs( best - hMielenzU ) <= hMielenzRounding,
		       fmt( "the area width is least at u = %.4f pi, d = %.4f sqrt( lambda f ): Mielenz eq. 9 gives 1.7 pi (tolerance 0.05 pi, his two figures)",
		            best / hPi, std::sqrt( 2.0 * best / hPi ) ) );
	}

	//6. Zone plates: the axis at the design wavelength is ( 2M )^2 -- Rayleigh's
	//   eq. 20 says an aperture's axial field is 1 - exp( i u / 2 ), so an
	//   annulus gives exp( i u_in / 2 ) - exp( i u_out / 2 ), 2 for every open
	//   Fresnel zone -- and at any wavelength the same sum over the rings; the
	//   light through the plate is its open area.
	{
		double worstAxis = 0.0, worstOther = 0.0, worstFlux = 0.0;
		std::string lines;
		for( const int M : { 1, 2, 4, 8, 16 } )
		{
			const double f = 0.5, design = 550e-9;
			const optics::Aperture plate = optics::MakeZonePlate( M, design, f, hooks );
			const optics::Pattern p      = optics::Monochromatic( plate, design, f, finer( hooks ) );
			const double want            = 4.0 * M * M;
			worstAxis                    = worse( worstAxis, std::fabs( p.e[ 0 ] / want - 1.0 ) );
			//r_n^2 = n lambda_d f (X-Ray Data Booklet 4.4 eq. 1), open zones
			//1, 3, ... 2M - 1: the harness's own rings.
			for( const double lambda : { 450e-9, 650e-9 } )
			{
				std::complex< double > U( 0.0, 0.0 );
				for( int n = 1; n <= 2 * M - 1; n += 2 )
				{
					const double uin = 2.0 * hPi * ( n - 1 ) * design * f / ( lambda * f ), uout = 2.0 * hPi * n * design * f / ( lambda * f );
					U += std::polar( 1.0, 0.5 * uin ) - std::polar( 1.0, 0.5 * uout );
				}
				const optics::Pattern q = optics::Monochromatic( plate, lambda, f, hooks );
				worstOther              = worse( worstOther, std::fabs( q.e[ 0 ] - std::norm( U ) ) / want );
			}
			const double open = hPi * p.u * p.u * static_cast< double >( M ) / ( 2.0 * M - 1.0 );
			const double flux = ( patternFlux( p ) + patternTail( plate, p ) ) / open - 1.0;
			worstFlux         = worse( worstFlux, std::fabs( flux ) );
			lines += fmt( " M=%d: axis %.4f (%g), first order %.4f of a lens's;", M, p.e[ 0 ], want, p.e[ 0 ] / std::pow( 0.5 * p.u, 2.0 ) );
		}
		Check( worstAxis <= kVisible && worstOther <= kVisible && worstFlux <= kVisible,
		       fmt( "zone plates of M open zones, 550 nm at 500 mm: the axis against ( 2M )^2, worst %.2g relative; at 450 and 650 nm against the rings' "
		            "sum, worst %.2g of ( 2M )^2; the light against the open area, worst %.2g (tolerance %.2g each). The axis over a perfect lens's "
		            "( u/2 )^2 tends to 1/pi^2 = %.4f, the booklet's first-order efficiency.%s",
		            worstAxis, worstOther, worstFlux, kVisible, 1.0 / ( hPi * hPi ), lines.c_str() ) );
	}

	//7. The conversions: the plugin's against the spec's ranges.
	{
		double worst = 0.0;
		for( int i = 0; i <= 100; ++i )
		{
			const float v    = i / 100.0f;
			const double d[] = { std::fabs( DiameterFromParam( v ) / hDiameter( v ) - 1.0 ), std::fabs( DistanceFromParam( v ) / hDistance( v ) - 1.0 ),
				                 std::fabs( ZoneFocusFromParam( v ) / hFocus( v ) - 1.0 ), std::fabs( ExposureFromParam( v ) - hStops( v ) ),
				                 std::fabs( static_cast< double >( ParamFromDiameter( hDiameter( v ) ) ) - v ),
				                 std::fabs( static_cast< double >( ParamFromDistance( hDistance( v ) ) ) - v ),
				                 std::fabs( static_cast< double >( ParamFromZoneFocus( hFocus( v ) ) ) - v ),
				                 std::fabs( static_cast< double >( ParamFromExposure( hStops( v ) ) ) - v ) };
			for( const double x : d )
				worst = worse( worst, x );
		}
		const double optimum = std::sqrt( 2.0 * 1.7 * 550e-9 * 50e-3 );
		Check( worst <= 1e-6 && std::fabs( DefaultDiameter() / optimum - 1.0 ) <= 1e-9,
		       fmt( "the conversions against the spec's ranges, both ways: worst %.2g (tolerance 1e-6, float); the default Diameter %.4f mm is "
		            "1.844 sqrt( 550 nm x 50 mm )",
		            worst, DefaultDiameter() * 1e3 ) );
	}

	//Not a check: how far the disc-plus-Airy shortcut is from the pattern.
	{
		const double u               = hMielenzU;
		optics::Hooks shortcut       = hooks;
		shortcut.discAiry            = true;
		const optics::Aperture a     = holeForU( u, hooks );
		const optics::Pattern truth  = optics::Monochromatic( a, 550e-9, 0.05, hooks, 400.0, 0.01 );
		const optics::Pattern approx = optics::Monochromatic( a, 550e-9, 0.05, shortcut, 400.0, 0.01 );
		std::printf( "  --    at u = 1.7 pi the disc convolved with the Airy pattern has a peak %.3f of Fresnel's and an edge spread %.3f times as wide\n",
		             approx.e[ 0 ] / truth.e[ 0 ], patternIqr( approx, hPi * u * u ) / patternIqr( truth, hPi * u * u ) );
	}
	return Verdict();
}

//===========================================================================
// --blur: an edge's spread read off the picture.
//===========================================================================
int runBlur( const Perturb& perturb )
{
	std::printf( "\n=== blur: an edge's spread off the picture, per channel -- a uniform disc's 0.80795 a for a big hole near the screen, a band-mean Airy\n"
	             "    pattern's for a small hole far from it (red against blue ~ 650/450). Tolerance: a texel of the pyramid level holding the quartile,\n"
	             "    plus a pixel for the two crossings\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Floats card = grey( w, h, 0.0 );
		for( int y = 0; y < h; ++y )
			for( int x = w / 2; x < w; ++x )
			{
				float* o = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
				o[ 0 ] = o[ 1 ] = o[ 2 ] = 1.0f;
			}
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		struct Case
		{
			const char* name;
			double d, f;
			bool airy;
		};
		const Case cases[] = { { "geometric: 2 mm at 10 mm", 2e-3, 10e-3, false }, { "diffraction: 0.1 mm at 500 mm", 0.1e-3, 500e-3, true } };
		for( const Case& c : cases )
		{
			const float dp = ParamFromDiameter( c.d ), fp = ParamFromDistance( c.f );
			rig.Set( PT_DIAMETER, dp );
			rig.Set( PT_DISTANCE, fp );
			if( !rig.Render( 1 ) )
				return 1;
			const double d = hDiameter( dp ), f = hDistance( fp );
			const Light light = lightOf( rig.Output(), w, h, f );
			double iqr[ 3 ], want[ 3 ], tol[ 3 ];
			bool ok = true;
			std::string line;
			for( int ch = 0; ch < 3; ++ch )
			{
				std::vector< double > row( static_cast< size_t >( w ) );
				for( int x = 0; x < w; ++x )
					row[ static_cast< size_t >( x ) ] = light.at( ch, x, h / 2 );
				iqr[ ch ]  = crossing( row, 0.75, 0.5 * w ) - crossing( row, 0.25, 0.5 * w );
				want[ ch ] = ( c.airy ? hBandAiryIqr( ch, d, f ) : hDiscIqrOverRadius() * 0.5 * d ) / hPixel( h );
				tol[ ch ]  = hTexelAt( 0.5 * want[ ch ] ) + 1.0;
				ok         = ok && std::fabs( iqr[ ch ] - want[ ch ] ) <= tol[ ch ];
				line += fmt( " %s %.2f px (%.2f +- %.0f);", kChannel[ ch ], iqr[ ch ], want[ ch ], tol[ ch ] );
			}
			Check( ok, fmt( "%4dx%-4d %s, interquartile width%s R/B %.3f (%.3f)", w, h, c.name, line.c_str(), iqr[ 0 ] / iqr[ 2 ], want[ 0 ] / want[ 2 ] ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --optimum: the sharpest hole, off the picture.
//===========================================================================
int runOptimum( const Perturb& perturb )
{
	std::printf( "\n=== optimum: a point's image over a ladder of diameters at 500 mm; its area width (the row through the peak over the peak) is least at\n"
	             "    d = 1.844 sqrt( lambda f ) for each band's centre (Mielenz eq. 9). Tolerance: Mielenz's two figures (+-0.05 pi in u, 1.5%% in d) plus\n"
	             "    half a ladder step (2^( 1/32 ), 2.2%%)\n" );
	bool any = false;
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		if( w != 1280 || h != 720 )
		{
			std::printf( "  --    %dx%d: not run here. At 500 mm the optimum's area width is 0.38 mm, 2.8 px at 180 lines, too few to place a minimum "
			             "this shallow; it needs 1280x720\n",
			             w, h );
			continue;
		}
		any                = true;
		const Floats card  = pointCard( w, h, w / 2, h / 2 );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		const float fp = ParamFromDistance( 500e-3 );
		rig.Set( PT_DISTANCE, fp );
		const double f  = hDistance( fp );
		const double d0 = std::sqrt( 2.0 * 1.7 * 550e-9 * f );
		std::vector< double > logD;
		std::vector< double > width[ 3 ];
		for( int k = -12; k <= 12; ++k )
		{
			const float dp = ParamFromDiameter( d0 * std::exp2( k / 16.0 ) );
			rig.Set( PT_DIAMETER, dp );
			if( !rig.Render( 1 ) )
				return 1;
			logD.push_back( std::log( hDiameter( dp ) ) );
			const Light light = lightOf( rig.Output(), w, h, f );
			for( int ch = 0; ch < 3; ++ch )
			{
				double row = 0.0;
				for( int x = 0; x < w; ++x )
					row += light.at( ch, x, h / 2 );
				width[ ch ].push_back( row / light.at( ch, w / 2, h / 2 ) );
			}
		}
		std::string line;
		bool ok = true;
		for( int ch = 0; ch < 3; ++ch )
		{
			const auto& wv = width[ ch ];
			size_t m       = static_cast< size_t >( std::min_element( wv.begin(), wv.end() ) - wv.begin() );
			double best    = logD[ m ];
			if( m > 0 && m + 1 < wv.size() )
			{
				//The parabola through the least and its neighbours.
				const double x0 = logD[ m - 1 ], x1 = logD[ m ], x2 = logD[ m + 1 ];
				const double y0 = wv[ m - 1 ], y1 = wv[ m ], y2 = wv[ m + 1 ];
				const double num = ( x1 - x0 ) * ( x1 - x0 ) * ( y1 - y2 ) - ( x1 - x2 ) * ( x1 - x2 ) * ( y1 - y0 );
				const double den = ( x1 - x0 ) * ( y1 - y2 ) - ( x1 - x2 ) * ( y1 - y0 );
				if( den != 0.0 )
					best = x1 - 0.5 * num / den;
			}
			const double centre = 0.5 * ( hBandLow[ ch ] + hBandHigh[ ch ] );
			const double want   = std::sqrt( 2.0 * 1.7 * centre * f );
			const double off    = std::exp( best ) / want - 1.0;
			const double tol    = ( std::sqrt( 1.75 / 1.7 ) - 1.0 ) + ( std::exp2( 1.0 / 32.0 ) - 1.0 );
			ok                  = ok && m > 0 && m + 1 < wv.size() && std::fabs( off ) <= tol;
			line += fmt( " %s least at %.4f mm (%.4f, %+.2f%%; width %.2f px);", kChannel[ ch ], std::exp( best ) * 1e3, want * 1e3, 100.0 * off, wv[ m ] );
		}
		Check( ok, fmt( "%4dx%-4d f = %.0f mm, 25 diameters 2^( k/16 ) about %.4f mm:%s", w, h, f * 1e3, d0 * 1e3, line.c_str() ) );
	}
	if( !any )
		std::printf( "  --    optimum: no 1280x720 raster in this run, so nothing was measured\n" );
	return Verdict();
}

//===========================================================================
// --falloff: cos^4 on a flat field.
//===========================================================================
int runFalloff( const Perturb& perturb )
{
	std::printf( "\n=== falloff: a flat field's light over the frame is cos^4 of the angle off the axis (Aggarwal, Hua & Ahuja), from the screen's geometry,\n"
	             "    and the metered centre keeps the field's own level; at three distances\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const double code = 0.5, level = linearOf( code );
		const Floats card = grey( w, h, code );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		for( const double distance : { 10e-3, 25e-3, 50e-3 } )
		{
			const float fp = ParamFromDistance( distance );
			rig.Set( PT_DISTANCE, fp );
			if( !rig.Render( 1 ) )
				return 1;
			const double f   = hDistance( fp );
			const Floats out = rig.Output();
			double worst     = 0.0;
			for( int y = 0; y < h; ++y )
				for( int x = 0; x < w; ++x )
					worst = worse( worst, std::fabs( linearOf( out[ 4 * ( static_cast< size_t >( y ) * w + x ) + 1 ] ) / ( level * hCos4( x, y, w, h, f ) ) - 1.0 ) );
			Check( worst <= kHalfFloat, fmt( "%4dx%-4d f = %4.1f mm: every pixel against level x cos^4 (corner %.4f of the centre): worst %.2g (tolerance %.2g, "
			                                 "four half-float roundings)",
			                                 w, h, f * 1e3, hCos4( 0, 0, w, h, f ), worst, kHalfFloat ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --zone: a zone plate focuses one colour.
//===========================================================================
int runZone( const Perturb& perturb )
{
	std::printf( "\n=== zone: a plate of 8 open zones at 500 mm, Zone Focus 400-700 nm in 10 nm steps: each channel's point image is sharpest (the most\n"
	             "    light in the point's own pixel) when the plate is made for a wavelength in that channel's band -- blue below green below red\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		const Floats card = pointCard( w, h, w / 2, h / 2 );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		const float fp = ParamFromDistance( 500e-3 );
		rig.Set( PT_APERTURE, static_cast< int >( kZonePlate ) );
		rig.Set( PT_ZONES, 8.0f );
		rig.Set( PT_DISTANCE, fp );
		const double f = hDistance( fp );
		double bestFocus[ 3 ] = {}, bestShare[ 3 ] = {};
		for( int nm = 400; nm <= 700; nm += 10 )
		{
			const float zp = ParamFromZoneFocus( nm * 1e-9 );
			rig.Set( PT_ZONE_FOCUS, zp );
			if( !rig.Render( 1 ) )
				return 1;
			const Light light = lightOf( rig.Output(), w, h, f );
			for( int ch = 0; ch < 3; ++ch )
			{
				double total = 0.0;
				for( const double v : light.c[ ch ] )
					total += v;
				const double share = light.at( ch, w / 2, h / 2 ) / total;
				if( share > bestShare[ ch ] )
				{
					bestShare[ ch ] = share;
					bestFocus[ ch ] = hFocus( zp );
				}
			}
		}
		bool ok = true;
		std::string line;
		for( int ch = 0; ch < 3; ++ch )
		{
			//Half a ladder step either side of the band.
			ok = ok && bestFocus[ ch ] >= hBandLow[ ch ] - 5.01e-9 && bestFocus[ ch ] <= hBandHigh[ ch ] + 5.01e-9;
			line += fmt( " %s sharpest at %.0f nm (band %.0f-%.0f, %.3f of its light in one pixel);", kChannel[ ch ], bestFocus[ ch ] * 1e9, hBandLow[ ch ] * 1e9,
			             hBandHigh[ ch ] * 1e9, bestShare[ ch ] );
		}
		Check( ok, fmt( "%4dx%-4d%s", w, h, line.c_str() ) );
	}
	return Verdict();
}

//===========================================================================
// --edges: Mirror and Black.
//===========================================================================
int runEdges( const Perturb& perturb )
{
	std::printf( "\n=== edges: the clip's left half white. Mirror: the white runs flat to the frame's edge, and the card comes out as the card with its\n"
	             "    mirror image appended does. Black: the frame's edge is a step to black exactly as the clip's own edge is, so the white strip is\n"
	             "    symmetric about its middle. A 0.1 mm hole at 50 mm\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Floats card = grey( w, h, 0.0 );
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w / 2; ++x )
			{
				float* o = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
				o[ 0 ] = o[ 1 ] = o[ 2 ] = 1.0f;
			}
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		rig.Set( PT_DIAMETER, ParamFromDiameter( 0.1e-3 ) );
		const float fp = ParamFromDistance( 50e-3 );
		rig.Set( PT_DISTANCE, fp );
		const double f = hDistance( fp );

		rig.Set( PT_EDGE, static_cast< int >( kMirror ) );
		if( !rig.Render( 1 ) )
			return 1;
		const double support = rig.plugin.LastKernel().support;
		const int reach      = static_cast< int >( std::ceil( support ) ) + 1;
		Light light          = lightOf( rig.Output(), w, h, f );
		double flat          = 0.0;
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w / 2 - reach; ++x )
				for( int ch = 0; ch < 3; ++ch )
					flat = worse( flat, std::fabs( light.at( ch, x, y ) - 1.0 ) );
		Check( flat <= kHalfFloat, fmt( "%4dx%-4d Mirror: the white more than the kernel's %.0f px from the clip's edge is flat to the frame's edge: worst %.2g "
		                                "(tolerance %.2g, four half-float roundings)",
		                                w, h, support, flat, kHalfFloat ) );

		//Mirror against the clip reflected by hand. The kernel depends on the
		//raster's height alone (the screen is 24 mm tall), so the card with
		//its mirror image appended on the right, 2w wide, is blurred with the
		//same taps: Mirror on the w-wide card must give the left half of
		//that, where the frame's right edge lies inside the wider one. Exact
		//only where w is a whole number of the coarsest level's texels (a
		//reflection about the edge then maps texel blocks onto texel blocks);
		//otherwise the coarse rings see the reflection up to half a coarse
		//texel out (AGENTS.md), so the check refuses such a raster.
		{
			const Floats scene = buildCard( w, h );
			Floats doubled( static_cast< size_t >( 2 * w ) * h * 4 );
			for( int y = 0; y < h; ++y )
				for( int x = 0; x < 2 * w; ++x )
				{
					const int from = x < w ? x : 2 * w - 1 - x;
					for( int ch = 0; ch < 4; ++ch )
						doubled[ ( static_cast< size_t >( y ) * 2 * w + x ) * 4 + ch ] = scene[ ( static_cast< size_t >( y ) * w + from ) * 4 + ch ];
				}
			Rig one, two;
			if( !startRig( one, w, h, perturb, &scene ) || !startRig( two, 2 * w, h, perturb, &doubled ) )
				return 1;
			for( Rig* r : { &one, &two } )
			{
				r->Set( PT_DIAMETER, ParamFromDiameter( 0.1e-3 ) );
				r->Set( PT_DISTANCE, fp );
				r->Set( PT_EDGE, static_cast< int >( kMirror ) );
				if( !r->Render( 1 ) )
					return 1;
			}
			const int levels  = one.plugin.LastKernel().levels;
			const bool whole  = w % ( 1 << levels ) == 0;
			const Light mine  = lightOf( one.Output(), w, h, f );
			const Light by    = lightOf( two.Output(), 2 * w, h, f );
			double worst      = 0.0;
			for( int y = 0; y < h; ++y )
				for( int x = 0; x < w; ++x )
					for( int ch = 0; ch < 3; ++ch )
						worst = worse( worst, std::fabs( mine.at( ch, x, y ) - by.at( ch, x, y ) ) );
			Check( whole && worst <= kFloatCode,
			       fmt( "%4dx%-4d Mirror: the card against its left half with its mirror image appended (%dx%d, the same taps): worst %.2g (tolerance "
			            "%.2g, float rounding of the encode at two field angles); %d levels, %d px a texel at the coarsest, %s",
			            w, h, 2 * w, h, worst, kFloatCode, levels, 1 << levels, whole ? "a whole number across the frame" : "NOT a whole number across the frame: no measurement" ) );
		}

		rig.Set( PT_EDGE, static_cast< int >( kBlack ) );
		if( !rig.Render( 1 ) )
			return 1;
		light            = lightOf( rig.Output(), w, h, f );
		double asymmetry = 0.0, dip = 1.0;
		for( int y = 0; y < h; ++y )
			for( int x = 0; x < w / 2; ++x )
				for( int ch = 0; ch < 3; ++ch )
				{
					asymmetry = worse( asymmetry, std::fabs( light.at( ch, x, y ) - light.at( ch, w / 2 - 1 - x, y ) ) );
					dip       = std::min( dip, light.at( ch, 0, h / 2 ) );
				}
		Check( asymmetry <= kVisible && dip < 0.75,
		       fmt( "%4dx%-4d Black: the white strip against its mirror image about its middle: worst %.2g (tolerance %.2g, a quarter of an 8-bit code "
		            "step); the frame's edge falls to %.3f",
		            w, h, asymmetry, kVisible, dip ) );
	}
	return Verdict();
}

//===========================================================================
// --energy: nothing made or lost.
//===========================================================================
int runEnergy( const Perturb& perturb )
{
	std::printf( "\n=== energy: a 9 x 9 white square in the middle: the picture's light, cos^4 divided out, is the clip's (the kernel sums to one)\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		Floats card = grey( w, h, 0.0 );
		for( int y = h / 2 - 4; y <= h / 2 + 4; ++y )
			for( int x = w / 2 - 4; x <= w / 2 + 4; ++x )
			{
				float* o = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
				o[ 0 ] = o[ 1 ] = o[ 2 ] = 1.0f;
			}
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		rig.Set( PT_EDGE, static_cast< int >( kBlack ) );
		struct Case
		{
			const char* name;
			int aperture;
			double d;
		};
		for( const Case& c : { Case { "the default hole", kPinhole, std::sqrt( 2.0 * 1.7 * 550e-9 * 50e-3 ) }, Case { "a 0.15 mm hole", kPinhole, 0.15e-3 },
		                       Case { "4 open zones", kZonePlate, 0.0 } } )
		{
			rig.Set( PT_APERTURE, c.aperture );
			if( c.d > 0.0 )
				rig.Set( PT_DIAMETER, ParamFromDiameter( c.d ) );
			if( !rig.Render( 1 ) )
				return 1;
			const double support = rig.plugin.LastKernel().support;
			const Light light    = lightOf( rig.Output(), w, h, hDistance( ParamFromDistance( 50e-3 ) ) );
			double worst         = 0.0;
			std::string line;
			for( int ch = 0; ch < 3; ++ch )
			{
				double total = 0.0;
				for( const double v : light.c[ ch ] )
					total += v;
				worst = worse( worst, std::fabs( total / 81.0 - 1.0 ) );
				line += fmt( " %s %.5f", kChannel[ ch ], total / 81.0 );
			}
			const bool inside = support + 4.5 < 0.5 * h;
			Check( worst <= kHalfFloat && inside, fmt( "%4dx%-4d %s (kernel %.0f px, %d levels): the picture's light over the clip's:%s; worst %.2g (tolerance %.2g, "
			                                           "four half-float roundings)%s",
			                                           w, h, c.name, support, rig.plugin.LastKernel().levels, line.c_str(), worst, kHalfFloat,
			                                           inside ? "" : " -- the kernel reaches the frame's edge, so light left it" ) );
		}
	}
	return Verdict();
}

//===========================================================================
// --view: Obscura turns the picture.
//===========================================================================
int runView( const Perturb& perturb )
{
	std::printf( "\n=== view: a marked pixel an eighth of the way in from the bottom-left corner. Upright leaves the brightest pixel there; Obscura moves it to\n"
	             "    the same place from the top-right (turned 180 degrees, as on a camera obscura's wall)\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h, x0 = w / 8, y0 = h / 8;
		const Floats card = pointCard( w, h, x0, y0 );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &card ) )
			return 1;
		int found[ 2 ][ 2 ] = {};
		for( int view = 0; view < 2; ++view )
		{
			rig.Set( PT_VIEW, view );
			if( !rig.Render( 1 ) )
				return 1;
			const Light light = lightOf( rig.Output(), w, h, hDistance( ParamFromDistance( 50e-3 ) ) );
			const auto& g     = light.c[ 1 ];
			const size_t p    = static_cast< size_t >( std::max_element( g.begin(), g.end() ) - g.begin() );
			found[ view ][ 0 ] = static_cast< int >( p % w );
			found[ view ][ 1 ] = static_cast< int >( p / w );
		}
		const bool ok = found[ 0 ][ 0 ] == x0 && found[ 0 ][ 1 ] == y0 && found[ 1 ][ 0 ] == w - 1 - x0 && found[ 1 ][ 1 ] == h - 1 - y0;
		Check( ok, fmt( "%4dx%-4d the mark at (%d, %d): Upright's brightest pixel at (%d, %d), Obscura's at (%d, %d) (want (%d, %d); exact)", w, h, x0, y0,
		                found[ 0 ][ 0 ], found[ 0 ][ 1 ], found[ 1 ][ 0 ], found[ 1 ][ 1 ], w - 1 - x0, h - 1 - y0 ) );
	}
	return Verdict();
}

//===========================================================================
// --alpha
//===========================================================================
int runAlpha( const Perturb& perturb )
{
	std::printf( "\n=== alpha: output alpha = mix( a, 1, Mix ); white under transparent pixels changes nothing (the light is weighted by alpha before the\n"
	             "    blur); Mix 0 hands the clip through\n" );
	for( const Raster& raster : kRasters )
	{
		const int w = raster.w, h = raster.h;
		//Left half: white (or black) at alpha 0. Right half: a grey under an alpha ramp.
		auto make = [ & ]( float under ) {
			Floats card = grey( w, h, 0.5 );
			for( int y = 0; y < h; ++y )
				for( int x = 0; x < w; ++x )
				{
					float* o = &card[ ( static_cast< size_t >( y ) * w + x ) * 4 ];
					if( x < w / 2 )
						o[ 0 ] = o[ 1 ] = o[ 2 ] = under, o[ 3 ] = 0.0f;
					else
						o[ 3 ] = std::round( 255.0f * ( 2.0f * x / w - 1.0f ) ) / 255.0f;
				}
			return card;
		};
		const Floats white = make( 1.0f ), black = make( 0.0f );
		Rig rig;
		if( !startRig( rig, w, h, perturb, &white ) )
			return 1;
		double worst[ 2 ]      = {};
		const float mixes[ 2 ] = { 1.0f, 0.5f };
		Floats atOne;
		for( int k = 0; k < 2; ++k )
		{
			rig.Set( PT_MIX, mixes[ k ] );
			if( !rig.Render( 1 ) )
				return 1;
			const Floats out = rig.Output();
			if( k == 0 )
				atOne = out;
			for( size_t p = 0; p < out.size() / 4; ++p )
			{
				const double a = white[ 4 * p + 3 ];
				worst[ k ]     = worse( worst[ k ], std::fabs( out[ 4 * p + 3 ] - ( a + ( 1.0 - a ) * mixes[ k ] ) ) );
			}
		}
		rig.Set( PT_MIX, 0.0f );
		if( !rig.Render( 1 ) )
			return 1;
		const size_t through = differing( rig.Output(), white );
		rig.Upload( black );
		rig.Set( PT_MIX, 1.0f );
		if( !rig.Render( 1 ) )
			return 1;
		//Not bit for bit: Apple's software renderer does not repeat itself to
		//the last bit (the same frame twice differed by 1e-10 in one value).
		const Floats blackOut = rig.Output();
		double under          = 0.0;
		for( size_t i = 0; i < blackOut.size(); ++i )
			under = worse( under, std::fabs( static_cast< double >( blackOut[ i ] ) - atOne[ i ] ) );
		Check( worst[ 0 ] <= 1e-6 && worst[ 1 ] <= 1e-6 && through == 0 && under <= 1e-6,
		       fmt( "%4dx%-4d alpha at Mix 1 off mix( a, 1, Mix ) by %.2g, at Mix 0.5 by %.2g (tolerance 1e-6); white against black under alpha 0 at Mix 1: "
		            "worst %.2g (tolerance 1e-6); Mix 0: %zu values differ from the clip (bit-exact)",
		            w, h, worst[ 0 ], worst[ 1 ], under, through ) );
	}
	return Verdict();
}

//===========================================================================
// --resize
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: after the host's raster changes mid-run the picture is a fresh instance's at the new raster (the kernel is rebuilt for the\n"
	             "    new pixel size: the screen is 24 mm tall at every raster)\n" );
	const Raster to   = kRasters.back();
	const Raster from = { 2 * to.w, 2 * to.h };
	Rig running;
	if( !startRig( running, from.w, from.h, perturb ) || !running.Render( 1 ) )
		return 1;
	const Floats card = buildCard( to.w, to.h );
	if( !running.Resize( to.w, to.h, &card ) || !running.Render( 1 ) )
		return 1;
	const Floats resized = running.Output();
	Rig fresh;
	if( !startRig( fresh, to.w, to.h, perturb, &card ) || !fresh.Render( 1 ) )
		return 1;
	const Floats clean = fresh.Output();
	double worst       = 0.0;
	for( size_t i = 0; i < clean.size(); ++i )
		worst = worse( worst, std::fabs( static_cast< double >( resized[ i ] ) - clean[ i ] ) );
	Check( worst <= 1e-6, fmt( "%dx%d -> %dx%d mid-run against a fresh %dx%d: worst %.2g (tolerance 1e-6)", from.w, from.h, to.w, to.h, to.w, to.h, worst ) );
	return Verdict();
}

//===========================================================================
// --glstate
//===========================================================================
int runGLState( const Perturb& perturb )
{
	std::printf( "\n=== glstate: the host's GL state comes back as it was handed over\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !startRig( rig, raster.w, raster.h, perturb ) )
			return 1;
		GLuint vao = 0, buffer = 0;
		glGenVertexArrays( 1, &vao );
		glGenBuffers( 1, &buffer );
		int wrong = 0;
		for( int frame = 0; frame < 3; ++frame )
		{
			glBindFramebuffer( GL_FRAMEBUFFER, rig.outputFBO );
			glViewport( 3, 5, raster.w - 7, raster.h - 9 );
			glEnable( GL_BLEND );
			glBlendFuncSeparate( GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO );
			glClearColor( 0.1f, 0.2f, 0.3f, 0.4f );
			glBindVertexArray( vao );
			glBindBuffer( GL_ARRAY_BUFFER, buffer );
			glActiveTexture( GL_TEXTURE3 );
			if( rig.plugin.ProcessOpenGL( &rig.process ) != FF_SUCCESS )
				return 1;
			GLint vp[ 4 ], src = 0, dst = 0, srcA = 0, dstA = 0, va = 0, ab = 0, at = 0;
			GLfloat cc[ 4 ];
			glGetIntegerv( GL_VIEWPORT, vp );
			glGetIntegerv( GL_BLEND_SRC_RGB, &src );
			glGetIntegerv( GL_BLEND_DST_RGB, &dst );
			glGetIntegerv( GL_BLEND_SRC_ALPHA, &srcA );
			glGetIntegerv( GL_BLEND_DST_ALPHA, &dstA );
			glGetFloatv( GL_COLOR_CLEAR_VALUE, cc );
			glGetIntegerv( GL_VERTEX_ARRAY_BINDING, &va );
			glGetIntegerv( GL_ARRAY_BUFFER_BINDING, &ab );
			glGetIntegerv( GL_ACTIVE_TEXTURE, &at );
			wrong += vp[ 0 ] != 3 || vp[ 1 ] != 5 || vp[ 2 ] != raster.w - 7 || vp[ 3 ] != raster.h - 9;
			wrong += !glIsEnabled( GL_BLEND ) || src != GL_SRC_ALPHA || dst != GL_ONE || srcA != GL_ONE || dstA != GL_ZERO;
			wrong += cc[ 0 ] != 0.1f || cc[ 1 ] != 0.2f || cc[ 2 ] != 0.3f || cc[ 3 ] != 0.4f;
			wrong += va != static_cast< GLint >( vao ) || ab != static_cast< GLint >( buffer ) || at != GL_TEXTURE3;
			wrong += glGetError() != GL_NO_ERROR;
		}
		glBindVertexArray( 0 );
		glDisable( GL_BLEND );
		glActiveTexture( GL_TEXTURE0 );
		glDeleteVertexArrays( 1, &vao );
		glDeleteBuffers( 1, &buffer );
		Check( wrong == 0, fmt( "%4dx%-4d three frames with a host viewport, blend, clear colour, vertex array, buffer and texture unit of its own: %d "
		                        "things not put back, GL errors included",
		                        raster.w, raster.h, wrong ) );
	}
	return Verdict();
}

//===========================================================================
// The registry.
//===========================================================================
struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "fresnel", runFresnel, true }, { "blur", runBlur, false },   { "optimum", runOptimum, false }, { "falloff", runFalloff, false },
		{ "zone", runZone, false },      { "edges", runEdges, false }, { "energy", runEnergy, false },   { "view", runView, false },
		{ "alpha", runAlpha, false },    { "resize", runResize, false }, { "glstate", runGLState, false },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "fresnel", runFresnel, "the chirp dropped: the Fraunhofer (Airy) pattern at every u", []( Perturb& p ) { p.optics.fraunhofer = true; } );
	add( "fresnel", runFresnel, "u detuned 2% in the chirp", []( Perturb& p ) { p.optics.chirpScale = 1.02; } );
	add( "fresnel", runFresnel, "the geometric disc convolved with the Airy pattern", []( Perturb& p ) { p.optics.discAiry = true; } );
	add( "blur", runBlur, "the diameter taken as the radius", []( Perturb& p ) { p.optics.diameterAsRadius = true; } );
	add( "blur", runBlur, "every band at 550 nm", []( Perturb& p ) { p.optics.oneWavelength = true; } );
	add( "optimum", runOptimum, "u doubled in the chirp", []( Perturb& p ) { p.optics.chirpScale = 2.0; } );
	add( "falloff", runFalloff, "cos^3", []( Perturb& p ) { p.cosPower = 3.0; } );
	add( "zone", runZone, "every band at 550 nm", []( Perturb& p ) { p.optics.oneWavelength = true; } );
	add( "zone", runZone, "every zone radius squared 1.2 times too large", []( Perturb& p ) { p.optics.zoneScale = 1.2; } );
	add( "edges", runEdges, "Mirror and Black swapped", []( Perturb& p ) { p.edgeSwapped = true; } );
	add( "edges", runEdges, "Mirror repeating the edge pixel instead of reflecting", []( Perturb& p ) { p.edgeClamped = true; } );
	add( "energy", runEnergy, "the pyramid's level-1 ring left out", []( Perturb& p ) { p.dropLevel = 1; } );
	add( "view", runView, "Obscura mirrors instead of turning", []( Perturb& p ) { p.mirrorView = true; } );
	add( "alpha", runAlpha, "the clip's alpha passed through", []( Perturb& p ) { p.alphaModel = 1; } );
	add( "alpha", runAlpha, "the clip's light not weighted by its alpha", []( Perturb& p ) { p.premultiply = false; } );
	add( "resize", runResize, "the kernel not rebuilt when the raster changes", []( Perturb& p ) { p.staleOnResize = true; } );
	add( "glstate", runGLState, "the host's GL state not put back", []( Perturb& p ) { p.leakGLState = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}

int runOffline()
{
	int failed = 0;
	for( const CheckEntry& check : checks() )
		if( check.offline )
			failed |= check.run( Perturb {} );
	failed |= runNegative( true );
	std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
	             "  tools/verify.sh runs them against a real driver, at 1280x720 and 320x180, and again on the software renderer.\n"
	             "\n  %s\n",
	             failed == 0 ? "PASS" : "FAIL" );
	return failed == 0 ? 0 : 1;
}

//===========================================================================
// --bench: GPU time per frame from GL_TIME_ELAPSED, and the CPU rebuild.
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
		bool largest;
		int warm, timed;
	};
	const Size sizes[] = { { 1280, 720, "720p", false, 10, 60 },  { 1280, 720, "720p", true, 10, 60 },  { 1920, 1080, "1080p", false, 10, 60 },
		                   { 1920, 1080, "1080p", true, 10, 60 }, { 3840, 2160, "4K", true, 5, 20 } };
	std::printf( "\n=== bench: GPU time per frame (GL_TIME_ELAPSED) on the card -- the defaults (0.31 mm at 50 mm), and the largest blur the controls\n"
	             "    reach (0.1 mm at 500 mm: an Airy pattern wider than the frame, cut at its height); the CPU rebuild of the patterns and taps\n" );
	GLuint query = 0;
	glGenQueries( 1, &query );
	for( const Size& size : sizes )
	{
		Rig rig;
		if( !rig.Init( size.w, size.h ) )
			return 1;
		defaults( rig );
		if( size.largest )
		{
			rig.Set( PT_DIAMETER, ParamFromDiameter( 0.1e-3 ) );
			rig.Set( PT_DISTANCE, ParamFromDistance( 500e-3 ) );
		}
		if( !rig.Render( 1 ) )
			return 1;
		const double rebuild = rig.plugin.LastRebuildMs();
		if( !rig.Render( size.warm ) )
			return 1;
		std::vector< double > gpu;
		for( int f = 0; f < size.timed; ++f )
		{
			glBeginQuery( GL_TIME_ELAPSED, query );
			if( !rig.Render( 1 ) )
				return 1;
			glEndQuery( GL_TIME_ELAPSED );
			GLuint64 ns = 0;
			glGetQueryObjectui64v( query, GL_QUERY_RESULT, &ns );
			gpu.push_back( static_cast< double >( ns ) * 1e-6 );
		}
		std::sort( gpu.begin(), gpu.end() );
		const optics::Kernel& k = rig.plugin.LastKernel();
		std::printf( "  %-6s %-8s kernel %6.1f px, %2d levels, %3zu taps at most   GPU median %7.2f ms, worst %7.2f (%5.1f%% of a 60 fps frame)   "
		             "CPU rebuild %6.1f ms\n",
		             size.name, size.largest ? "largest" : "defaults", k.support, k.levels, k.MaxTaps(), gpu[ gpu.size() / 2 ], gpu.back(),
		             100.0 * gpu[ gpu.size() / 2 ] / ( 1000.0 / 60.0 ), rebuild );
	}
	glDeleteQueries( 1, &query );
	return 0;
}
