// PLANETARIUM-API - servizio HTTP stateless
//
// Espone via REST il motore astronomico C++ (VSOP87 + ELP82B) per
// calcolare la posizione dei corpi celesti (AR, Dec, altezza, azimut,
// elongazione, distanza) dato un giorno giuliano (JD) e la posizione
// dell'osservatore sulla Terra (lat/lon/height).

#include <cstdlib>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "vendor/httplib.h"
#include "vendor/json.hpp"

#include <solarSystem.hpp>
#include "PositionService.hpp"

using json = nlohmann::json;

// cpp-httplib serve le richieste con un pool di thread. Le funzioni di
// PositionService scrivono su membri di SolarSystem (es. system.mercury.
// position in computeBodyPositions) quindi l'istanza condivisa non è
// safe per accessi concorrenti: un mutex globale serializza le chiamate
// al motore. Il calcolo costa sub-millisecondo, seriale va benissimo.
std::mutex g_engineMutex;

namespace {

std::vector<std::string> splitCsv ( const std::string & s ) {
	std::vector<std::string> out;
	std::stringstream ss( s );
	std::string item;
	while ( std::getline( ss, item, ',' ) ) {
		// rimuove spazi ai bordi
		size_t a = item.find_first_not_of( " \t" );
		size_t b = item.find_last_not_of( " \t" );
		if ( a != std::string::npos ) out.push_back( item.substr( a, b - a + 1 ) );
	}
	return out;
}

json sunEventsToJson ( const SunEvents & e ) {
	json out;
	out[ "sunrise" ] = e.sunrise_found
		? json{ { "jd", e.next_sunrise_jd }, { "found", true } }
		: json{ { "found", false } };
	out[ "sunset" ] = e.sunset_found
		? json{ { "jd", e.next_sunset_jd }, { "found", true } }
		: json{ { "found", false } };
	return out;
}

json seasonsToJson ( const SeasonEvents & s ) {
	return json{
		{ "year", s.year },
		{ "march_equinox_jd",     s.march_equinox_jd },
		{ "june_solstice_jd",     s.june_solstice_jd },
		{ "september_equinox_jd", s.september_equinox_jd },
		{ "december_solstice_jd", s.december_solstice_jd }
	};
}

json moonPhaseToJson ( const MoonPhaseInfo & m ) {
	return json{
		{ "angle_me_deg",  m.angle_me_deg },
		{ "illumination",  m.illumination },
		{ "phase_name",    m.phase_name },
		{ "next_new_moon_jd",      m.next_new_moon_jd },
		{ "next_first_quarter_jd", m.next_first_quarter_jd },
		{ "next_full_moon_jd",     m.next_full_moon_jd },
		{ "next_last_quarter_jd",  m.next_last_quarter_jd }
	};
}

json bodyToJson ( const BodyPosition & b ) {
	return json{
		{ "name",            b.name },
		{ "ra_deg",          b.ra_deg },
		{ "ra_hms",          b.ra_hms },
		{ "dec_deg",         b.dec_deg },
		{ "dec_dms",         b.dec_dms },
		{ "altitude_deg",    b.altitude_deg },
		{ "azimuth_deg",     b.azimuth_deg },
		{ "elongation_deg",  b.elongation_deg },
		{ "distance_au",     b.distance_au },
		{ "distance_km",     b.distance_km },
		{ "visible",         b.visible }
	};
}

void sendError ( httplib::Response & res, int status, const std::string & message ) {
	json j = { { "error", message } };
	res.status = status;
	res.set_content( j.dump(), "application/json" );
}

} // namespace


int main () {

	const char * vsopDir = std::getenv( "VSOP_DIR" );
	if ( !vsopDir || std::string( vsopDir ).empty() ) {
		std::cerr << "ERRORE: variabile d'ambiente VSOP_DIR non impostata "
		          << "(deve puntare alla cartella con i sottodir vsop87/ ed elp/, "
		          << "con lo slash finale, es. /app/data/)" << std::endl;
		return 1;
	}

	// SolarSystem: nessun XML di configurazione necessario, ci servono
	// solo i corpi predefiniti (sun/earth/moon/pianeti) per il calcolo
	// orbitale, non le proprietà di rendering 3D.
	SolarSystem system( "" );

	int port = 8484;
	if ( const char * portEnv = std::getenv( "PLANETARIUM_PORT" ) ) {
		port = std::atoi( portEnv );
	}

	// Se impostata, richiede l'header X-API-Key su ogni chiamata (tranne /health)
	const char * apiKey = std::getenv( "PLANETARIUM_API_KEY" );

	httplib::Server svr;

	svr.set_pre_routing_handler( [ apiKey ]( const httplib::Request & req, httplib::Response & res ) {
		if ( req.path == "/api/v1/health" ) return httplib::Server::HandlerResponse::Unhandled;
		if ( apiKey && *apiKey ) {
			auto it = req.headers.find( "X-API-Key" );
			if ( it == req.headers.end() || it->second != apiKey ) {
				sendError( res, 401, "API key mancante o non valida" );
				return httplib::Server::HandlerResponse::Handled;
			}
		}
		return httplib::Server::HandlerResponse::Unhandled;
	} );

	svr.Get( "/api/v1/health", []( const httplib::Request &, httplib::Response & res ) {
		res.set_content( json{ { "status", "ok" } }.dump(), "application/json" );
	} );

	svr.Get( "/api/v1/bodies", []( const httplib::Request &, httplib::Response & res ) {
		res.set_content( json{ { "bodies", supportedBodies() } }.dump(), "application/json" );
	} );

	svr.Get( "/api/v1/positions", [ &system ]( const httplib::Request & req, httplib::Response & res ) {

		if ( !req.has_param( "jd" ) ) {
			sendError( res, 400, "parametro obbligatorio mancante: jd" );
			return;
		}
		if ( !req.has_param( "lat" ) || !req.has_param( "lon" ) ) {
			sendError( res, 400, "parametri obbligatori mancanti: lat, lon" );
			return;
		}

		try {
			double jd  = std::stod( req.get_param_value( "jd" ) );
			Observer observer;
			observer.latitude  = std::stod( req.get_param_value( "lat" ) );
			observer.longitude = std::stod( req.get_param_value( "lon" ) );
			if ( req.has_param( "height" ) ) {
				observer.height = std::stod( req.get_param_value( "height" ) );
			}

			std::vector<std::string> bodies;
			if ( req.has_param( "bodies" ) ) {
				bodies = splitCsv( req.get_param_value( "bodies" ) );
			}

			std::vector<BodyPosition> positions;
			{
				std::lock_guard<std::mutex> lock( g_engineMutex );
				positions = computeBodyPositions( jd, observer, bodies, system );
			}

			json bodiesJson = json::array();
			for ( const auto & b : positions ) bodiesJson.push_back( bodyToJson( b ) );

			json out = {
				{ "jd", jd },
				{ "observer", {
					{ "lat",    observer.latitude },
					{ "lon",    observer.longitude },
					{ "height", observer.height }
				} },
				{ "bodies", bodiesJson }
			};

			res.set_content( out.dump(), "application/json" );

		} catch ( const std::invalid_argument & e ) {
			sendError( res, 400, e.what() );
		} catch ( const std::exception & e ) {
			sendError( res, 500, std::string( "errore interno: " ) + e.what() );
		}
	} );

	svr.Get( "/api/v1/sun/events", [ &system ]( const httplib::Request & req, httplib::Response & res ) {

		if ( !req.has_param( "jd" ) || !req.has_param( "lat" ) || !req.has_param( "lon" ) ) {
			sendError( res, 400, "parametri obbligatori mancanti: jd, lat, lon" );
			return;
		}

		try {
			double jd = std::stod( req.get_param_value( "jd" ) );
			Observer observer;
			observer.latitude  = std::stod( req.get_param_value( "lat" ) );
			observer.longitude = std::stod( req.get_param_value( "lon" ) );

			SunEvents events;
			{
				std::lock_guard<std::mutex> lock( g_engineMutex );
				events = computeSunEvents( jd, observer, system );
			}

			json out = sunEventsToJson( events );
			out[ "jd" ] = jd;
			out[ "observer" ] = { { "lat", observer.latitude }, { "lon", observer.longitude } };

			res.set_content( out.dump(), "application/json" );

		} catch ( const std::exception & e ) {
			sendError( res, 500, std::string( "errore interno: " ) + e.what() );
		}
	} );

	svr.Get( "/api/v1/sun/seasons", [ &system ]( const httplib::Request & req, httplib::Response & res ) {

		if ( !req.has_param( "year" ) ) {
			sendError( res, 400, "parametro obbligatorio mancante: year" );
			return;
		}

		try {
			short year = static_cast<short>( std::stoi( req.get_param_value( "year" ) ) );
			SeasonEvents seasons;
			{
				std::lock_guard<std::mutex> lock( g_engineMutex );
				seasons = computeSeasons( year, system );
			}
			res.set_content( seasonsToJson( seasons ).dump(), "application/json" );

		} catch ( const std::exception & e ) {
			sendError( res, 500, std::string( "errore interno: " ) + e.what() );
		}
	} );

	svr.Get( "/api/v1/moon/phase", [ &system ]( const httplib::Request & req, httplib::Response & res ) {

		if ( !req.has_param( "jd" ) ) {
			sendError( res, 400, "parametro obbligatorio mancante: jd" );
			return;
		}

		try {
			double jd = std::stod( req.get_param_value( "jd" ) );
			MoonPhaseInfo info;
			{
				std::lock_guard<std::mutex> lock( g_engineMutex );
				info = computeMoonPhase( jd, system );
			}

			json out = moonPhaseToJson( info );
			out[ "jd" ] = jd;

			res.set_content( out.dump(), "application/json" );

		} catch ( const std::exception & e ) {
			sendError( res, 500, std::string( "errore interno: " ) + e.what() );
		}
	} );

	std::cout << "planetarium-api in ascolto sulla porta " << port
	          << " (VSOP_DIR=" << vsopDir << ")" << std::endl;

	svr.listen( "0.0.0.0", port );

	return 0;
}
