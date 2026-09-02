#!/usr/bin/env node
// Adapter MCP per planetarium-api.
//
// Non reimplementa alcun calcolo astronomico: è un client HTTP sottile
// verso il servizio REST (vedi ../../service/main.cpp). Traduce le
// chiamate degli agenti/LLM (tool MCP) in richieste HTTP contro
// PLANETARIUM_API_URL e ne ritorna il JSON.

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { z } from "zod";

const API_URL = process.env.PLANETARIUM_API_URL ?? "http://127.0.0.1:8484";
const API_KEY = process.env.PLANETARIUM_API_KEY; // opzionale, header X-API-Key

async function callApi ( path: string, params: Record<string, string | undefined> ): Promise<unknown> {
	const url = new URL( path, API_URL );
	for ( const [ key, value ] of Object.entries( params ) ) {
		if ( value !== undefined ) url.searchParams.set( key, value );
	}

	const headers: Record<string, string> = {};
	if ( API_KEY ) headers[ "X-API-Key" ] = API_KEY;

	const res = await fetch( url, { headers } );
	const body = await res.json();

	if ( !res.ok ) {
		const message = ( body as { error?: string } )?.error ?? res.statusText;
		throw new Error( `planetarium-api ${res.status}: ${message}` );
	}
	return body;
}

const server = new McpServer( {
	name: "planetarium-mcp",
	version: "0.1.0"
} );

server.registerTool(
	"get_celestial_positions",
	{
		title: "Posizione dei corpi celesti",
		description:
			"Calcola AR, Dec, altezza, azimut, elongazione e distanza dei corpi " +
			"celesti richiesti, per un dato giorno giuliano (JD) e una posizione " +
			"dell'osservatore sulla Terra (lat/lon).",
		inputSchema: {
			jd: z.number().describe( "giorno giuliano, es. 2460600.5" ),
			latitude: z.number().min( -90 ).max( 90 ),
			longitude: z.number().min( -180 ).max( 180 ),
			height: z.number().optional().describe( "altezza sul livello del mare, metri" ),
			bodies: z
				.array( z.enum( [
					"sun", "moon", "mercury", "venus", "mars",
					"jupiter", "saturn", "uranus", "neptune"
				] ) )
				.optional()
				.describe( "sottoinsieme di corpi richiesti; omesso = tutti" )
		}
	},
	async ( { jd, latitude, longitude, height, bodies } ) => {
		const result = await callApi( "/api/v1/positions", {
			jd: String( jd ),
			lat: String( latitude ),
			lon: String( longitude ),
			height: height !== undefined ? String( height ) : undefined,
			bodies: bodies?.join( "," )
		} );
		return { content: [ { type: "text", text: JSON.stringify( result, null, 2 ) } ] };
	}
);

server.registerTool(
	"list_supported_bodies",
	{
		title: "Elenco corpi celesti supportati",
		description: "Ritorna l'elenco dei nomi di corpo accettati da get_celestial_positions.",
		inputSchema: {}
	},
	async () => {
		const result = await callApi( "/api/v1/bodies", {} );
		return { content: [ { type: "text", text: JSON.stringify( result, null, 2 ) } ] };
	}
);

const transport = new StdioServerTransport();
await server.connect( transport );
