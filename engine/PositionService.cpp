#include "PositionService.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <stdexcept>

#include <celestial_body.hpp>
#include <orbit.hpp>
#include <horizont.hpp>
#include <point.hpp>
#include <angolo.hpp>

namespace {

std::string toLower ( std::string s ) {
	std::transform( s.begin(), s.end(), s.begin(),
		[]( unsigned char c ) { return std::tolower(c); } );
	return s;
}

const double PI = 3.14159265358979323846;

// Bisezione generica: richiede f(a) e f(b) di segno opposto.
template <typename F>
double bisect ( F f, double a, double b, int iterations = 30 ) {
	double fa = f( a );
	for ( int i = 0; i < iterations; i++ ) {
		double m  = 0.5 * ( a + b );
		double fm = f( m );
		if ( ( fa < 0 ) == ( fm < 0 ) ) { a = m; fa = fm; }
		else                            { b = m; }
	}
	return 0.5 * ( a + b );
}

std::string phaseName ( double angleDeg ) {
	double a = std::fmod( angleDeg, 360.0 );
	if ( a < 0 ) a += 360.0;

	if ( a <  22.5 ) return "nuova";
	if ( a <  67.5 ) return "crescente";
	if ( a < 112.5 ) return "primo quarto";
	if ( a < 157.5 ) return "gibbosa crescente";
	if ( a < 202.5 ) return "piena";
	if ( a < 247.5 ) return "gibbosa calante";
	if ( a < 292.5 ) return "ultimo quarto";
	if ( a < 337.5 ) return "calante";
	return "nuova";
}

// Costruisce il BodyPosition a partire dalle coordinate equatoriali/XYZ
// già calcolate per il corpo, replicando la logica di
// WSsrv::computeCelestialPositions (buildBodyJson), meno costellazioni/stelle.
// Nota: OrbitEqu::alfa/delta sono di tipo Grade, la cui conversione a
// double (operator double()) non è const nel codice originale; stessa
// cosa per Horizont::azimut/height. Per questo i parametri qui sotto
// sono riferimenti non-const (fedele al codice legacy, non una scelta
// nostra) invece di "const &".
BodyPosition buildBody (
	const std::string & name,
	OrbitEqu & o_eq,
	OrbitXYZ & o_xyz,
	Horizont & horizont,
	double sun_ar_deg,
	bool is_moon
) {
	BodyPosition b;
	b.name = name;

	double ar  = o_eq.alfa;
	double dec = o_eq.delta;
	if ( ar < 0 ) ar += 360.0;

	b.ra_deg  = ar;
	b.ra_hms  = Grade( o_eq.alfa ).toGradeHMS();
	b.dec_deg = dec;
	b.dec_dms = Grade( dec ).toGradeGPS();

	Point p( o_xyz.x, o_xyz.y, o_xyz.z );
	b.azimuth_deg  = horizont.azimut( p );
	b.altitude_deg = horizont.height( p );

	double elong = ar - sun_ar_deg;
	if ( elong >  180.0 ) elong -= 360.0;
	if ( elong < -180.0 ) elong += 360.0;
	b.elongation_deg = elong;

	double R = o_eq.R; // AU (o unità interna per la luna, come nel codice originale)
	if ( is_moon ) {
		b.distance_km = R * CelestialBody::AU;
		b.distance_au = b.distance_km / CelestialBody::AU;
	} else {
		b.distance_au = R;
		b.distance_km = R * CelestialBody::AU;
	}

	b.visible = b.altitude_deg > 0;

	return b;
}

} // namespace


const std::vector<std::string> & supportedBodies () {
	static const std::vector<std::string> bodies = {
		"sun", "moon", "mercury", "venus", "mars",
		"jupiter", "saturn", "uranus", "neptune"
	};
	return bodies;
}


std::vector<BodyPosition> computeBodyPositions (
	double JD,
	const Observer & observer,
	const std::vector<std::string> & bodiesIn,
	SolarSystem & system
) {
	// Corpi effettivamente richiesti (default: tutti quelli supportati)
	std::vector<std::string> requested;
	if ( bodiesIn.empty() ) {
		requested = supportedBodies();
	} else {
		for ( const auto & raw : bodiesIn ) {
			std::string name = toLower( raw );
			const auto & all = supportedBodies();
			if ( std::find( all.begin(), all.end(), name ) == all.end() ) {
				throw std::invalid_argument( "corpo celeste non supportato: " + raw );
			}
			requested.push_back( name );
		}
	}

	// --- Terra: rotazione (tempo siderale locale) e obliquità ---
	Earth & earth = system.earth;
	double rot     = earth.rotation( JD );
	Grade  epsilon = earth.obliquity( JD );

	// --- Sole: posizione equatoriale e XYZ (usata anche come riferimento
	//     per la conversione eliocentrica -> geocentrica dei pianeti) ---
	Sun & sun = system.sun;
	OrbitEcli orbit_sun = sun.orbitLBR( JD );
	OrbitEqu  o_sun( JD );
	o_sun = orbit_sun;
	OrbitXYZ o_sun_xyz = o_sun;
	sun.position = o_sun_xyz;

	double sun_ar = o_sun.alfa;
	if ( sun_ar < 0 ) sun_ar += 360.0;

	// posizione eliocentrica della Terra, usata da orbit_eq(...) per
	// convertire le posizioni eliocentriche dei pianeti in geocentriche
	OrbitXYZ orbit_sun_xyz = sun.orbit( MainBody::earth_data, JD );

	// --- Luna ---
	Moon & moon = system.moon;
	OrbitEcli orbit_moon = moon.orbitAlfaELP( JD );
	OrbitEqu  o_moon( JD );
	o_moon = orbit_moon;
	OrbitXYZ  o_moon_xyz = o_moon;
	moon.position = o_moon_xyz;

	// --- Pianeti ---
	system.mercury.position = system.mercury.orbit_eq( MainBody::mercury_data, orbit_sun_xyz, epsilon, JD );
	system.venus.position   = system.venus.orbit_eq  ( MainBody::venus_data,   orbit_sun_xyz, epsilon, JD );
	system.mars.position    = system.mars.orbit_eq   ( MainBody::mars_data,    orbit_sun_xyz, epsilon, JD );
	system.jupiter.position = system.jupiter.orbit_eq( MainBody::jupiter_data, orbit_sun_xyz, epsilon, JD );
	system.saturn.position  = system.saturn.orbit_eq ( MainBody::saturn_data,  orbit_sun_xyz, epsilon, JD );
	system.uranus.position  = system.uranus.orbit_eq ( MainBody::uranus_data,  orbit_sun_xyz, epsilon, JD );
	system.neptune.position = system.neptune.orbit_eq( MainBody::neptune_data, orbit_sun_xyz, epsilon, JD );

	// --- Orizzonte locale dell'osservatore ---
	Horizont horizont( observer.latitude, observer.longitude + rot );

	std::vector<BodyPosition> out;
	out.reserve( requested.size() );

	for ( const auto & name : requested ) {

		if ( name == "sun" ) {
			out.push_back( buildBody( name, o_sun, o_sun_xyz, horizont, sun_ar, false ) );
		} else if ( name == "moon" ) {
			out.push_back( buildBody( name, o_moon, o_moon_xyz, horizont, sun_ar, true ) );
		} else if ( name == "mercury" ) {
			OrbitEqu eq = system.mercury.position;
			out.push_back( buildBody( name, eq, system.mercury.position, horizont, sun_ar, false ) );
		} else if ( name == "venus" ) {
			OrbitEqu eq = system.venus.position;
			out.push_back( buildBody( name, eq, system.venus.position, horizont, sun_ar, false ) );
		} else if ( name == "mars" ) {
			OrbitEqu eq = system.mars.position;
			out.push_back( buildBody( name, eq, system.mars.position, horizont, sun_ar, false ) );
		} else if ( name == "jupiter" ) {
			OrbitEqu eq = system.jupiter.position;
			out.push_back( buildBody( name, eq, system.jupiter.position, horizont, sun_ar, false ) );
		} else if ( name == "saturn" ) {
			OrbitEqu eq = system.saturn.position;
			out.push_back( buildBody( name, eq, system.saturn.position, horizont, sun_ar, false ) );
		} else if ( name == "uranus" ) {
			OrbitEqu eq = system.uranus.position;
			out.push_back( buildBody( name, eq, system.uranus.position, horizont, sun_ar, false ) );
		} else if ( name == "neptune" ) {
			OrbitEqu eq = system.neptune.position;
			out.push_back( buildBody( name, eq, system.neptune.position, horizont, sun_ar, false ) );
		}
	}

	return out;
}


SunEvents computeSunEvents (
	double JD,
	const Observer & observer,
	SolarSystem & system
) {
	Earth & earth = system.earth;

	// stessa correzione (rifrazione + semidiametro) usata da Earth::sunRise
	auto f = [&]( double t ) -> double {
		return earth.sunAngleOverOrizont( t, observer.latitude, observer.longitude ) + 0.83;
	};

	const double stepDays   = 10.0 / 1440.0; // passo di scansione: 10 minuti
	const double windowDays = 2.0;           // cerca fino a 2 giorni in avanti
	const int    steps      = static_cast<int>( windowDays / stepDays );

	SunEvents result;

	double tPrev = JD;
	double fPrev = f( tPrev );

	for ( int i = 1; i <= steps && !( result.sunrise_found && result.sunset_found ); i++ ) {

		double tCur = JD + i * stepDays;
		double fCur = f( tCur );

		if ( fPrev < 0 && fCur >= 0 && !result.sunrise_found ) {
			result.next_sunrise_jd = bisect( f, tPrev, tCur );
			result.sunrise_found   = true;
		} else if ( fPrev >= 0 && fCur < 0 && !result.sunset_found ) {
			result.next_sunset_jd = bisect( f, tPrev, tCur );
			result.sunset_found   = true;
		}

		tPrev = tCur;
		fPrev = fCur;
	}

	return result;
}


SeasonEvents computeSeasons ( short year, SolarSystem & system ) {

	Earth & earth = system.earth;
	SeasonEvents result;
	result.year = year;

	// Earth::marchEquinox/juneSolstice/... (motore CMat) lanciano, a
	// seconda del punto di fallimento, std::string oppure const char*
	// (stile legacy, non std::exception): li normalizziamo qui in
	// std::runtime_error così il chiamante (l'endpoint HTTP) ha un
	// solo tipo da intercettare.
	try {
		result.march_equinox_jd     = earth.marchEquinox( year );
		result.june_solstice_jd     = earth.juneSolstice( year );
		result.september_equinox_jd = earth.septemberEquinox( year );
		result.december_solstice_jd = earth.decemberSolstice( year );
	}
	catch ( const std::string & e ) { throw std::runtime_error( e ); }
	catch ( const char * e )        { throw std::runtime_error( e ); }

	return result;
}


// NOTA IMPORTANTE: qui NON usiamo Moon::angleME / Moon::phases del motore
// originale. Verificato empiricamente (campionando angleME su un mese
// sinodico intero) che quella funzione non è monotona: usa fabs() sulla
// differenza di due longitudini normalizzate indipendentemente in
// [0,360), il che la fa "ripiegare" invece di progredire 0->360 nel
// ciclo (es. valori che dovrebbero salire restano bloccati vicino a 0
// e poi saltano di colpo a ~360). Come conseguenza anche Moon::phases
// (che fa root-finding su quella stessa funzione via Newton-Raphson,
// CMat) converge in modo inaffidabile: un seed "5 giorni nel futuro" ha
// prodotto un risultato "5 giorni nel passato" durante il test.
//
// La grandezza corretta è ricavata direttamente dalle longitudini
// eclittiche di Sole e Luna (le stesse, già validate, usate in
// computeBodyPositions): fase = (L_luna - L_sole) mod 360. Campionata
// sullo stesso mese sinodico risulta monotona e pulita, un solo
// passaggio per 0/90/180/270 come atteso. La prossima occorrenza di
// ciascuna fase è quindi trovata con scansione grossolana + bisezione
// (stesso schema di computeSunEvents), non con il risolutore del motore.
double phaseAngle ( double t, SolarSystem & system ) {
	OrbitEcli sunEc  = system.sun.orbitLBR( t );
	OrbitEcli moonEc = system.moon.orbitAlfaELP( t );
	return std::fmod( moonEc.L - sunEc.L + 720.0, 360.0 ); // [0,360)
}

MoonPhaseInfo computeMoonPhase ( double JD, SolarSystem & system ) {

	MoonPhaseInfo info;

	double angle = phaseAngle( JD, system );
	info.angle_me_deg = angle;
	info.illumination = ( 1.0 - std::cos( angle * PI / 180.0 ) ) / 2.0;
	info.phase_name   = phaseName( angle );

	const double synodicMonth = 29.530588853; // giorni, media

	// distanza angolare firmata (fase(t) - target), avvolta in (-180,180],
	// così attraversa lo zero esattamente quando fase(t) supera target
	auto signedDist = [&]( double t, double target ) -> double {
		double d = std::fmod( phaseAngle( t, system ) - target + 540.0, 360.0 ) - 180.0;
		return d;
	};

	auto nextOccurrence = [&]( double target ) -> double {
		const double stepDays = 6.0 / 24.0; // 6 ore: << 1/4 di mese sinodico
		const int    steps    = static_cast<int>( ( synodicMonth + 2.0 ) / stepDays );

		double tPrev = JD;
		double dPrev = signedDist( tPrev, target );

		for ( int i = 1; i <= steps; i++ ) {
			double tCur = JD + i * stepDays;
			double dCur = signedDist( tCur, target );

			if ( dPrev < 0 && dCur >= 0 ) {
				return bisect( [&]( double t ) { return signedDist( t, target ); }, tPrev, tCur );
			}

			tPrev = tCur;
			dPrev = dCur;
		}

		throw std::runtime_error( "impossibile trovare la prossima occorrenza della fase richiesta" );
	};

	info.next_new_moon_jd      = nextOccurrence(   0.0 );
	info.next_first_quarter_jd = nextOccurrence(  90.0 );
	info.next_full_moon_jd     = nextOccurrence( 180.0 );
	info.next_last_quarter_jd  = nextOccurrence( 270.0 );

	return info;
}
