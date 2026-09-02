#ifndef _position_service_
#define _position_service_

// ============================================================
// PositionService
//
// Layer di calcolo stateless: JD + lat/lon/height -> posizione
// dei corpi celesti (AR, Dec, altezza, azimut, elongazione,
// distanza). Estratto e adattato da
// WSsrv::computeCelestialPositions (BALDOWEB/cpp/libPlanetarium/
// WSsrv.cpp), rimuovendo la dipendenza da sessione/socket
// (clientData, sockfd) e da SkyGrid (costellazioni/stelle, che
// richiede MariaDB e non serve alle funzioni base di questa API).
// ============================================================

#include <string>
#include <vector>
#include <stdexcept>

#include <solarSystem.hpp>

struct BodyPosition {

	std::string name;

	double ra_deg  = 0;   // ascensione retta, gradi [0,360)
	std::string ra_hms;   // ascensione retta, formato hh:mm:ss

	double dec_deg = 0;   // declinazione, gradi [-90,+90]
	std::string dec_dms;  // declinazione, formato gradi/primi/secondi

	double altitude_deg = 0;   // altezza sull'orizzonte locale, gradi
	double azimuth_deg  = 0;   // azimut locale, gradi [0,360)

	double elongation_deg = 0; // elongazione rispetto al Sole, gradi [-180,+180]

	double distance_au = 0;    // distanza dalla Terra, unità astronomiche
	double distance_km = 0;    // distanza dalla Terra, km

	bool visible = false;      // altitude_deg > 0
};

struct Observer {
	double latitude  = 0;   // gradi, positivo = Nord
	double longitude = 0;   // gradi, positivo = Est
	double height    = 0;   // metri sul livello del mare
};

// Elenco dei nomi di corpo accettati (tutti quelli gestiti da SolarSystem
// tranne "earth", che è l'osservatore stesso e non ha senso restituire).
const std::vector<std::string> & supportedBodies();

// Calcola la posizione di uno o più corpi celesti per una data data
// giuliana (JD) e una posizione dell'osservatore sulla Terra.
//
// - JD: giorno giuliano (es. 2460600.5)
// - observer: lat/lon/height dell'osservatore
// - bodies: nomi richiesti (minuscolo, es. "sun","moon","mars"...).
//           Se vuoto, vengono calcolati tutti i corpi supportati.
// - system: istanza di SolarSystem già inizializzata (VSOP_DIR impostata)
//
// Lancia std::invalid_argument se un nome in "bodies" non è riconosciuto.
std::vector<BodyPosition> computeBodyPositions (
	double JD,
	const Observer & observer,
	const std::vector<std::string> & bodies,
	SolarSystem & system
);

#endif // _position_service_
