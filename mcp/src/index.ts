#!/usr/bin/env node
// Adapter MCP per planetarium-api.
//
// Non reimplementa alcun calcolo astronomico: è un client HTTP sottile
// verso il servizio REST (vedi ../../service/main.cpp). Traduce le
// chiamate degli agenti/LLM (tool MCP) in richieste HTTP contro
// PLANETARIUM_API_URL e ne ritorna il JSON.
//
// Transport selezionabile via MCP_TRANSPORT:
//   - "stdio" (default): il processo viene spawnato dall'host MCP
//     (Claude Code/Desktop) e comunica su stdin/stdout. Nessuna rete,
//     nessuna configurazione, adatto a uso locale sulla stessa macchina.
//   - "http": server HTTP persistente (Streamable HTTP, spec MCP
//     corrente), adatto a girare come servizio di rete (es. su dedalus,
//     dietro lo stesso tunnel WireGuard di planetarium-api) e farsi
//     raggiungere da un host MCP remoto.

import * as http from "node:http";
import { randomUUID } from "node:crypto";

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { isInitializeRequest } from "@modelcontextprotocol/sdk/types.js";
import { z } from "zod";

const API_URL = process.env.PLANETARIUM_API_URL ?? "http://127.0.0.1:8484";
const API_KEY = process.env.PLANETARIUM_API_KEY; // opzionale, header X-API-Key verso planetarium-api

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

// Un McpServer nuovo per ogni sessione (stdio: una sola, per tutta la
// vita del processo; http: una per client connesso), coerente con il
// pattern raccomandato dal SDK per il transport Streamable HTTP.
function createServer (): McpServer {

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

	server.registerTool(
		"get_sun_events",
		{
			title: "Prossima alba/tramonto",
			description: "Prossima alba e tramonto a partire da un giorno giuliano, per una posizione data.",
			inputSchema: {
				jd: z.number(),
				latitude: z.number().min( -90 ).max( 90 ),
				longitude: z.number().min( -180 ).max( 180 )
			}
		},
		async ( { jd, latitude, longitude } ) => {
			const result = await callApi( "/api/v1/sun/events", {
				jd: String( jd ), lat: String( latitude ), lon: String( longitude )
			} );
			return { content: [ { type: "text", text: JSON.stringify( result, null, 2 ) } ] };
		}
	);

	server.registerTool(
		"get_sun_seasons",
		{
			title: "Equinozi e solstizi",
			description: "Date (JD) di equinozi e solstizi per un anno solare.",
			inputSchema: { year: z.number().int() }
		},
		async ( { year } ) => {
			const result = await callApi( "/api/v1/sun/seasons", { year: String( year ) } );
			return { content: [ { type: "text", text: JSON.stringify( result, null, 2 ) } ] };
		}
	);

	server.registerTool(
		"get_moon_phase",
		{
			title: "Fase lunare",
			description: "Fase lunare corrente e prossime occorrenze di nuova/primo quarto/piena/ultimo quarto.",
			inputSchema: { jd: z.number() }
		},
		async ( { jd } ) => {
			const result = await callApi( "/api/v1/moon/phase", { jd: String( jd ) } );
			return { content: [ { type: "text", text: JSON.stringify( result, null, 2 ) } ] };
		}
	);

	return server;
}

async function runStdio (): Promise<void> {
	const server = createServer();
	const transport = new StdioServerTransport();
	await server.connect( transport );
}

async function runHttp (): Promise<void> {
	const port = Number( process.env.MCP_HTTP_PORT ?? 8485 );
	const bindAddr = process.env.MCP_BIND_ADDR ?? "127.0.0.1";
	// Segreto opzionale sull'endpoint MCP stesso (stesso pattern di
	// PLANETARIUM_API_KEY): se impostato, richiede l'header X-API-Key.
	const mcpApiKey = process.env.MCP_API_KEY;

	const sessions: Record<string, StreamableHTTPServerTransport> = {};

	const httpServer = http.createServer( async ( req, res ) => {

		if ( req.url !== "/mcp" ) {
			res.writeHead( 404 ).end();
			return;
		}

		if ( mcpApiKey && req.headers[ "x-api-key" ] !== mcpApiKey ) {
			res.writeHead( 401, { "Content-Type": "application/json" } )
				.end( JSON.stringify( { error: "API key mancante o non valida" } ) );
			return;
		}

		const sessionId = req.headers[ "mcp-session-id" ] as string | undefined;

		if ( req.method === "POST" ) {
			let transport = sessionId ? sessions[ sessionId ] : undefined;

			if ( !transport ) {
				const chunks: Buffer[] = [];
				for await ( const chunk of req ) chunks.push( chunk as Buffer );
				const raw = Buffer.concat( chunks ).toString( "utf-8" );
				const parsedBody = raw.length ? JSON.parse( raw ) : undefined;

				if ( !sessionId && isInitializeRequest( parsedBody ) ) {
					transport = new StreamableHTTPServerTransport( {
						sessionIdGenerator: () => randomUUID(),
						onsessioninitialized: ( sid ) => { sessions[ sid ] = transport!; }
					} );
					transport.onclose = () => {
						if ( transport!.sessionId ) delete sessions[ transport!.sessionId ];
					};

					const server = createServer();
					await server.connect( transport );
				} else {
					res.writeHead( 400, { "Content-Type": "application/json" } ).end( JSON.stringify( {
						jsonrpc: "2.0",
						error: { code: -32000, message: "Sessione mancante o non valida (serve initialize)" },
						id: null
					} ) );
					return;
				}

				await transport.handleRequest( req, res, parsedBody );
				return;
			}

			await transport.handleRequest( req, res );
			return;
		}

		// GET (stream SSE server->client) e DELETE (chiusura sessione)
		// richiedono una sessione già inizializzata.
		if ( !sessionId || !sessions[ sessionId ] ) {
			res.writeHead( 400 ).end( "Sessione mancante o non valida" );
			return;
		}
		await sessions[ sessionId ].handleRequest( req, res );
	} );

	httpServer.listen( port, bindAddr, () => {
		console.error( `planetarium-mcp (streamable http) in ascolto su ${bindAddr}:${port}/mcp` );
	} );
}

const transportMode = process.env.MCP_TRANSPORT ?? "stdio";

if ( transportMode === "http" ) {
	await runHttp();
} else {
	await runStdio();
}
