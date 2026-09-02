#include "PositionService.hpp"

#include <algorithm>
#include <cctype>

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
