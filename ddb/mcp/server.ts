/**
 * DarbotDB MCP Server
 */

import { McpServer } from "@modelcontextprotocol/sdk/server/mcp.js";
import { StreamableHTTPServerTransport } from "@modelcontextprotocol/sdk/server/streamableHttp.js";
import { StdioServerTransport } from "@modelcontextprotocol/sdk/server/stdio.js";
import { registerAppResource, registerAppTool, RESOURCE_MIME_TYPE } from "@modelcontextprotocol/ext-apps/server";
import cors from "cors";
import express from "express";
import fs from "node:fs/promises";
import path from "node:path";

const DDB_API = process.env.DDB_API_URL || process.env.DDB_URL || "http://localhost:8080";
const server = new McpServer({ name: "DarbotDB MCP Server", version: "0.2.0" });

async function ddbFetch(endpoint: string, options?: RequestInit) {
  const url = `${DDB_API}${endpoint}`;
  const res = await fetch(url, { headers: { "Content-Type": "application/json" }, ...options });
  return res.json();
}

const cardViewerUri = "ui://ddb/card-viewer.html";
const memoryDashUri = "ui://ddb/memory-dashboard.html";
const graphExplorerUri = "ui://ddb/graph-explorer.html";
const threeDkgViewerUri = "ui://ddb/3dkg-viewer.html";

registerAppTool(server, "ddb-card-render", {
  title: "Render DDB Card",
  description: "Render a DDB adaptive card with inspector metadata.",
  inputSchema: {
    type: "object" as const,
    properties: {
      db_id: { type: "string", description: "Optional micro DB ID to persist the card into" },
      card_type: { type: "string", description: "Card type: memory, task, observation, pattern, index" },
      title: { type: "string", description: "Card title" },
      content: { type: "string", description: "Card body content" },
      zone: { type: "string", description: "Radial data zone" },
      tags: { type: "string", description: "Comma-separated tags" },
      manifest_id: { type: "string", description: "Optional manifest ID for inspector binding" },
      entity_id: { type: "string", description: "Optional semantic entity ID" },
      entity_kind: { type: "string", description: "Optional semantic entity kind" },
    },
    required: ["card_type", "title"],
  },
  _meta: { ui: { resourceUri: cardViewerUri } },
}, async (args: Record<string, unknown>) => {
  const result = await ddbFetch("/v1/cards", {
    method: "POST",
    body: JSON.stringify({
      db_id: args.db_id,
      card_type: args.card_type,
      title: args.title,
      content: args.content || "",
      zone: args.zone,
      tags: typeof args.tags === "string" ? (args.tags as string).split(",").map((t: string) => t.trim()) : [],
      manifest_id: args.manifest_id,
      entity_id: args.entity_id,
      entity_kind: args.entity_kind,
    }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

registerAppTool(server, "ddb-memory-recall", {
  title: "Remember Forward",
  description: "Recall relevant patterns, cards, and triples from the DDB knowledge graph.",
  inputSchema: {
    type: "object" as const,
    properties: {
      db_id: { type: "string", description: "Optional micro DB ID" },
      query: { type: "string", description: "Search query" },
      zone: { type: "string", description: "Optional zone filter" },
      depth: { type: "number", description: "Traversal depth (default 3)" },
    },
    required: ["query"],
  },
  _meta: { ui: { resourceUri: memoryDashUri } },
}, async (args: Record<string, unknown>) => {
  const result = await ddbFetch("/v1/memory/recall", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, query: args.query, zone: args.zone, depth: args.depth || 3 }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

registerAppTool(server, "ddb-graph-explore", {
  title: "Explore DDB Graph",
  description: "Traverse the DDB knowledge graph to discover relationships between agents, cards, patterns, and manifests.",
  inputSchema: {
    type: "object" as const,
    properties: {
      start_key: { type: "string", description: "Starting node key" },
      collection: { type: "string", description: "Collection: agents, cards, patterns, manifests" },
      depth: { type: "number", description: "Traversal depth (default 3)" },
    },
    required: ["start_key"],
  },
  _meta: { ui: { resourceUri: graphExplorerUri } },
}, async (args: Record<string, unknown>) => {
  const result = await ddbFetch("/v1/graph/traverse", {
    method: "POST",
    body: JSON.stringify({ start_key: args.start_key, collection: args.collection || "cards", depth: args.depth || 3 }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

registerAppTool(server, "ddb-3dkg-render", {
  title: "Render 3DKG Scene",
  description: "Render a 3DKG manifest as a 3D scene with inspector support.",
  inputSchema: {
    type: "object" as const,
    properties: {
      manifest_id: { type: "string", description: "Manifest ID" },
      db_id: { type: "string", description: "Optional micro DB ID for local manifest lookup" },
    },
    required: ["manifest_id"],
  },
  _meta: { ui: { resourceUri: threeDkgViewerUri } },
}, async (args: Record<string, unknown>) => {
  const endpoint = `/v1/scene/${encodeURIComponent(args.manifest_id as string)}${args.db_id ? `?db_id=${encodeURIComponent(args.db_id as string)}` : ""}`;
  const result = await ddbFetch(endpoint, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-aql-query", "Run an AQL query against the DarbotDB graph database", {
  query: { type: "string", description: "AQL query string" },
  bind_vars: { type: "string", description: "JSON bind variables (optional)" },
}, async (args: Record<string, unknown>) => {
  const result = await ddbFetch("/v1/aql", {
    method: "POST",
    body: JSON.stringify({ query: args.query, bind_vars: args.bind_vars ? JSON.parse(args.bind_vars as string) : {} }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-micro-create", "Create a new portable micro database for an agent or session", {
  agent_id: { type: "string", description: "Agent identifier" },
  db_type: { type: "string", description: "Type: agent, session, zone" },
  zone: { type: "string", description: "Zone name (for zone type)" },
}, async (args: Record<string, unknown>) => {
  const result = await ddbFetch("/v1/micro/create", { method: "POST", body: JSON.stringify(args) });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-triad-process", "Submit an event to the Observer/Orchestrator/Synthesizer engine for pattern detection", {
  db_id: { type: "string", description: "Optional micro DB ID" },
  event_type: { type: "string", description: "Event type (turn, tool_call, card_created)" },
  source_agent: { type: "string", description: "Source agent ID" },
  payload: { type: "string", description: "JSON event payload" },
}, async (args: Record<string, unknown>) => {
  const result = await ddbFetch("/v1/triad/process", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, event_type: args.event_type, source_agent: args.source_agent, payload: args.payload ? JSON.parse(args.payload as string) : {} }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-session-list", "List DDB micro sessions", {}, async () => {
  const result = await ddbFetch("/v1/sessions", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-manifest-project", "Project a micro DB into a 3DKG manifest", {
  db_id: { type: "string", description: "Micro DB ID" },
  title: { type: "string", description: "Manifest title" },
}, async (args: Record<string, unknown>) => {
  const result = await ddbFetch("/v1/manifests/project", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, title: args.title || "3DKG Scene", persist: true, include_cards: true, include_triples: true, include_events: true, include_patterns: true }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

async function loadHtml(filename: string): Promise<string> {
  try {
    return await fs.readFile(path.join(import.meta.dirname, "dist", filename), "utf-8");
  } catch {
    return await fs.readFile(path.join(import.meta.dirname, "resources", filename), "utf-8");
  }
}

for (const [uri, file] of [
  [cardViewerUri, "card-viewer.html"],
  [memoryDashUri, "memory-dashboard.html"],
  [graphExplorerUri, "graph-explorer.html"],
  [threeDkgViewerUri, "3dkg-viewer.html"],
] as const) {
  registerAppResource(server, uri, uri, { mimeType: RESOURCE_MIME_TYPE }, async () => ({
    contents: [{ uri, mimeType: RESOURCE_MIME_TYPE, text: await loadHtml(file) }],
  }));
}

const useStdio = process.argv.includes("--stdio");
if (useStdio) {
  const transport = new StdioServerTransport();
  await server.connect(transport);
  console.error("DDB MCP server running on stdio");
} else {
  const expressApp = express();
  expressApp.use(cors());
  expressApp.use(express.json());
  expressApp.post("/mcp", async (req, res) => {
    const transport = new StreamableHTTPServerTransport({ sessionIdGenerator: undefined, enableJsonResponse: true });
    res.on("close", () => transport.close());
    await server.connect(transport);
    await transport.handleRequest(req, res, req.body);
  });
  const port = parseInt(process.env.MCP_PORT || "3001");
  expressApp.listen(port, () => console.log(`DDB MCP server listening on http://localhost:${port}/mcp`));
}