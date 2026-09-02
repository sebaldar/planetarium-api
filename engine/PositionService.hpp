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


// ------------------------------------------------------------------
// Alba / tramonto
// ------------------------------------------------------------------
//
// A differenza di Earth::sunRise (che cerca uno zero entro +-0.01 giorni
// dal JD passato -- pensata per essere richiamata con una stima già
// vicina, dal loop del visualizzatore interattivo), qui la ricerca è
// fatta da zero: scansione grossolana in avanti da JD (fino a +2 giorni)
// per trovare i cambi di segno di sunAngleOverOrizont(t,lat,lon)+0.83
// (la costante di rifrazione/semidiametro usata dal motore originale),
// poi bisezione. Deterministico, nessuna dipendenza da orologio di
// sistema o da CMat.
struct SunEvents {
	bool   sunrise_found = false;
	double next_sunrise_jd = 0;   // prossima alba a partire da JD

	bool   sunset_found = false;
	double next_sunset_jd = 0;    // prossimo tramonto a partire da JD
};

SunEvents computeSunEvents (
	double JD,
	const Observer & observer,
	SolarSystem & system
);


// ------------------------------------------------------------------
// Equinozi e solstizi
// ------------------------------------------------------------------
//
// Wrapper diretto di Earth::marchEquinox/juneSolstice/septemberEquinox/
// decemberSolstice (root-finding via CMat, già presente nel motore).
// Lancia std::runtime_error se il motore non riesce a convergere.
struct SeasonEvents {
	short  year = 0;
	double march_equinox_jd = 0;
	double june_solstice_jd = 0;
	double september_equinox_jd = 0;
	double december_solstice_jd = 0;
};

SeasonEvents computeSeasons ( short year, SolarSystem & system );


// ------------------------------------------------------------------
// Fase lunare
// ------------------------------------------------------------------
//
// angle_me_deg segue la convenzione di Moon::angleME:
// 0° = luna nuova, 90° = primo quarto, 180° = luna piena, 270° = ultimo quarto.
// Le prossime occorrenze sono trovate stimando un JD di innesco (velocità
// sinodica media ~12.19°/giorno) e rifinendo con Moon::phases (CMat).
struct MoonPhaseInfo {
	double angle_me_deg = 0;      // fase attuale
	double illumination = 0;      // frazione illuminata del disco, 0-1
	std::string phase_name;       // nome qualitativo (italiano)

	double next_new_moon_jd = 0;
	double next_first_quarter_jd = 0;
	double next_full_moon_jd = 0;
	double next_last_quarter_jd = 0;
};

// Lancia std::runtime_error se il motore non riesce a convergere su una
// delle quattro fasi.
MoonPhaseInfo computeMoonPhase ( double JD, SolarSystem & system );

#endif // _position_service_
