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
import { z } from "zod";

const DDB_API = process.env.DDB_API_URL || process.env.DDB_URL || "http://localhost:8080";
const server = new McpServer({ name: "DarbotDB MCP Server", version: "0.3.0" });

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
  query: z.string().describe("AQL query string"),
  bind_vars: z.string().optional().describe("JSON bind variables (optional)"),
}, async (args) => {
  const result = await ddbFetch("/v1/aql", {
    method: "POST",
    body: JSON.stringify({ query: args.query, bind_vars: args.bind_vars ? JSON.parse(args.bind_vars) : {} }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-micro-create", "Create a new portable micro database for an agent or session", {
  agent_id: z.string().optional().describe("Agent identifier"),
  db_type: z.string().optional().describe("Type: agent, session, zone"),
  zone: z.string().optional().describe("Zone name (for zone type)"),
}, async (args) => {
  const result = await ddbFetch("/v1/micro/create", { method: "POST", body: JSON.stringify(args) });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-triad-process", "Submit an event to the Observer/Orchestrator/Synthesizer engine for pattern detection", {
  db_id: z.string().optional().describe("Optional micro DB ID"),
  event_type: z.string().describe("Event type (turn, tool_call, card_created)"),
  source_agent: z.string().describe("Source agent ID"),
  payload: z.string().optional().describe("JSON event payload"),
}, async (args) => {
  const result = await ddbFetch("/v1/triad/process", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, event_type: args.event_type, source_agent: args.source_agent, payload: args.payload ? JSON.parse(args.payload) : {} }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-session-list", "List DDB micro sessions", {}, async () => {
  const result = await ddbFetch("/v1/sessions", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-manifest-project", "Project a micro DB into a 3DKG manifest", {
  db_id: z.string().describe("Micro DB ID"),
  title: z.string().optional().describe("Manifest title"),
}, async (args) => {
  const result = await ddbFetch("/v1/manifests/project", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, title: args.title || "3DKG Scene", persist: true, include_cards: true, include_triples: true, include_events: true, include_patterns: true }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Micro DB ────────────────────────────────────────────────────────────────

server.tool("ddb-micro-list", "List all portable micro databases", {}, async () => {
  const result = await ddbFetch("/v1/micro/list", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-micro-status", "Get status of a micro database", {
  db_id: z.string().describe("Micro DB ID"),
}, async (args) => {
  const result = await ddbFetch(`/v1/micro/${encodeURIComponent(args.db_id)}/status`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-micro-query", "Run a SQL query against a micro database", {
  db_id: z.string().describe("Micro DB ID"),
  sql: z.string().describe("SQL query string"),
  params: z.string().optional().describe("JSON array of bind parameters (optional)"),
}, async (args) => {
  const result = await ddbFetch(`/v1/micro/${encodeURIComponent(args.db_id)}/query`, {
    method: "POST",
    body: JSON.stringify({ sql: args.sql, params: args.params ? JSON.parse(args.params) : [] }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-micro-delete", "Delete a micro database", {
  db_id: z.string().describe("Micro DB ID"),
}, async (args) => {
  const result = await ddbFetch(`/v1/micro/${encodeURIComponent(args.db_id)}`, { method: "DELETE" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Cards ───────────────────────────────────────────────────────────────────

server.tool("ddb-card-search", "Search adaptive cards by query, zone, or type", {
  db_id: z.string().describe("Micro DB ID"),
  query: z.string().describe("Search query"),
  zone: z.string().optional().describe("Optional zone filter"),
  card_type: z.string().optional().describe("Optional card type filter"),
  limit: z.number().optional().describe("Max results (default 50)"),
}, async (args) => {
  const result = await ddbFetch("/v1/cards/search", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, query: args.query, zone: args.zone, card_type: args.card_type, limit: args.limit || 50 }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-card-compose", "Compose multiple cards into a single composite card", {
  card_ids: z.string().describe("JSON array of card IDs to compose"),
  title: z.string().describe("Title for the composite card"),
  zone: z.string().optional().describe("Optional zone"),
  manifest_id: z.string().optional().describe("Optional manifest ID"),
}, async (args) => {
  const result = await ddbFetch("/v1/cards/compose", {
    method: "POST",
    body: JSON.stringify({ card_ids: JSON.parse(args.card_ids), title: args.title, zone: args.zone, manifest_id: args.manifest_id }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-card-tree", "Get the tree structure of a card and its children", {
  db_id: z.string().describe("Micro DB ID"),
  card_id: z.string().describe("Card ID"),
  depth: z.number().optional().describe("Traversal depth (default 3)"),
}, async (args) => {
  const params = args.depth ? `?depth=${args.depth}` : "";
  const result = await ddbFetch(`/v1/cards/${encodeURIComponent(args.db_id)}/${encodeURIComponent(args.card_id)}/tree${params}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Memory ──────────────────────────────────────────────────────────────────

server.tool("ddb-memory-store", "Store a memory card into the knowledge graph", {
  db_id: z.string().optional().describe("Optional micro DB ID"),
  card_type: z.string().optional().describe("Card type (default: memory)"),
  title: z.string().describe("Memory title"),
  content: z.string().optional().describe("Memory content"),
  zone: z.string().optional().describe("Optional zone"),
  tags: z.string().optional().describe("Comma-separated tags"),
  agent_id: z.string().optional().describe("Optional agent ID"),
}, async (args) => {
  const result = await ddbFetch("/v1/memory/store", {
    method: "POST",
    body: JSON.stringify({
      db_id: args.db_id, card_type: args.card_type || "memory", title: args.title, content: args.content || "",
      zone: args.zone, tags: typeof args.tags === "string" ? args.tags.split(",").map((t: string) => t.trim()) : [],
      agent_id: args.agent_id,
    }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-memory-zones", "List all memory zones", {}, async () => {
  const result = await ddbFetch("/v1/memory/zones", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-memory-zone-create", "Create a new memory zone", {
  name: z.string().describe("Zone name"),
  description: z.string().optional().describe("Zone description"),
  visibility: z.string().optional().describe("Visibility: shared or private (default: shared)"),
}, async (args) => {
  const result = await ddbFetch("/v1/memory/zones", {
    method: "POST",
    body: JSON.stringify({ name: args.name, description: args.description || "", visibility: args.visibility || "shared" }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-memory-zone-get", "Get details of a specific memory zone", {
  zone_name: z.string().describe("Zone name"),
}, async (args) => {
  const result = await ddbFetch(`/v1/memory/zones/${encodeURIComponent(args.zone_name)}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Graph ───────────────────────────────────────────────────────────────────

server.tool("ddb-graph-agents", "List all agents in the knowledge graph", {}, async () => {
  const result = await ddbFetch("/v1/graph/agents", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-graph-link", "Create a link (edge) between two nodes in the graph", {
  from_collection: z.string().describe("Source collection name"),
  from_key: z.string().describe("Source node key"),
  to_collection: z.string().describe("Target collection name"),
  to_key: z.string().describe("Target node key"),
  edge_collection: z.string().describe("Edge collection name"),
  metadata: z.string().optional().describe("JSON metadata for the edge (optional)"),
}, async (args) => {
  const result = await ddbFetch("/v1/graph/link", {
    method: "POST",
    body: JSON.stringify({
      from_collection: args.from_collection, from_key: args.from_key,
      to_collection: args.to_collection, to_key: args.to_key,
      edge_collection: args.edge_collection,
      metadata: args.metadata ? JSON.parse(args.metadata) : {},
    }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-graph-pattern", "Discover patterns in the knowledge graph", {
  min_confidence: z.number().optional().describe("Minimum confidence threshold (default 0.5)"),
  since: z.string().optional().describe("ISO timestamp to filter recent patterns"),
  limit: z.number().optional().describe("Max results (default 20)"),
}, async (args) => {
  const result = await ddbFetch("/v1/graph/pattern", {
    method: "POST",
    body: JSON.stringify({ min_confidence: args.min_confidence ?? 0.5, since: args.since, limit: args.limit || 20 }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-graph-manifests", "List manifests in the knowledge graph", {
  source_db_id: z.string().optional().describe("Optional source micro DB ID filter"),
  limit: z.number().optional().describe("Max results (default 20)"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.source_db_id) params.set("source_db_id", args.source_db_id);
  if (args.limit) params.set("limit", String(args.limit));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/graph/manifests${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-graph-manifest-scene", "Get a manifest scene from the knowledge graph", {
  manifest_id: z.string().describe("Manifest ID"),
  db_id: z.string().optional().describe("Optional micro DB ID"),
}, async (args) => {
  const qs = args.db_id ? `?db_id=${encodeURIComponent(args.db_id)}` : "";
  const result = await ddbFetch(`/v1/graph/manifests/${encodeURIComponent(args.manifest_id)}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-graph-zone", "Get contents of a graph zone", {
  zone_name: z.string().describe("Zone name"),
}, async (args) => {
  const result = await ddbFetch(`/v1/graph/zones/${encodeURIComponent(args.zone_name)}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Triad (Observer/Orchestrator/Synthesizer) ──────────────────────────────

server.tool("ddb-triad-thoughts", "Get thoughts from a micro DB's triad engine", {
  db_id: z.string().describe("Micro DB ID"),
  perspective: z.string().optional().describe("Optional perspective filter"),
}, async (args) => {
  const qs = args.perspective ? `?perspective=${encodeURIComponent(args.perspective)}` : "";
  const result = await ddbFetch(`/v1/triad/thoughts/${encodeURIComponent(args.db_id)}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-triad-events", "Get events from a micro DB's triad engine", {
  db_id: z.string().describe("Micro DB ID"),
  event_type: z.string().optional().describe("Optional event type filter"),
  triad_status: z.string().optional().describe("Optional triad status filter"),
  limit: z.number().optional().describe("Max results"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.event_type) params.set("event_type", args.event_type);
  if (args.triad_status) params.set("triad_status", args.triad_status);
  if (args.limit) params.set("limit", String(args.limit));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/triad/events/${encodeURIComponent(args.db_id)}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-triad-patterns", "List detected patterns from the triad engine", {
  min_confidence: z.number().optional().describe("Minimum confidence threshold"),
  limit: z.number().optional().describe("Max results"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.min_confidence != null) params.set("min_confidence", String(args.min_confidence));
  if (args.limit) params.set("limit", String(args.limit));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/triad/patterns${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Sessions ────────────────────────────────────────────────────────────────

server.tool("ddb-session-create", "Create or open a DDB session", {
  db_id: z.string().optional().describe("Optional micro DB ID"),
  session_id: z.string().optional().describe("Optional session ID"),
  agent_id: z.string().optional().describe("Optional agent ID"),
  zone: z.string().optional().describe("Optional zone"),
  scope: z.string().optional().describe("Session scope"),
  metadata: z.string().optional().describe("JSON metadata (optional)"),
}, async (args) => {
  const result = await ddbFetch("/v1/sessions", {
    method: "POST",
    body: JSON.stringify({
      db_id: args.db_id, session_id: args.session_id, agent_id: args.agent_id,
      zone: args.zone, scope: args.scope,
      metadata: args.metadata ? JSON.parse(args.metadata) : undefined,
    }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-session-status", "Get status of a specific session", {
  db_id: z.string().describe("Session/micro DB ID"),
}, async (args) => {
  const result = await ddbFetch(`/v1/sessions/${encodeURIComponent(args.db_id)}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Manifests ───────────────────────────────────────────────────────────────

server.tool("ddb-manifest-list", "List all manifests", {
  db_id: z.string().optional().describe("Optional micro DB ID filter"),
  limit: z.number().optional().describe("Max results"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.db_id) params.set("db_id", args.db_id);
  if (args.limit) params.set("limit", String(args.limit));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/manifests${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-manifest-save", "Save a manifest to the graph", {
  db_id: z.string().describe("Micro DB ID"),
  manifest: z.string().describe("JSON manifest object"),
  persist_to_graph: z.boolean().optional().describe("Persist to ArangoDB graph (default true)"),
}, async (args) => {
  const result = await ddbFetch("/v1/manifests", {
    method: "POST",
    body: JSON.stringify({
      db_id: args.db_id,
      manifest: JSON.parse(args.manifest),
      persist_to_graph: args.persist_to_graph ?? true,
    }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-manifest-get", "Get a specific manifest by ID", {
  manifest_id: z.string().describe("Manifest ID"),
  db_id: z.string().optional().describe("Optional micro DB ID"),
}, async (args) => {
  const qs = args.db_id ? `?db_id=${encodeURIComponent(args.db_id)}` : "";
  const result = await ddbFetch(`/v1/manifests/${encodeURIComponent(args.manifest_id)}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Scene ───────────────────────────────────────────────────────────────────

server.tool("ddb-scene-materialize", "Materialize a micro DB into a 3DKG scene", {
  db_id: z.string().describe("Micro DB ID"),
  title: z.string().optional().describe("Scene title (default: 3DKG Scene)"),
  include_cards: z.boolean().optional().describe("Include cards (default true)"),
  include_triples: z.boolean().optional().describe("Include triples (default true)"),
  include_events: z.boolean().optional().describe("Include events (default true)"),
  include_patterns: z.boolean().optional().describe("Include patterns (default true)"),
  persist: z.boolean().optional().describe("Persist to graph (default true)"),
}, async (args) => {
  const result = await ddbFetch("/v1/scene/materialize", {
    method: "POST",
    body: JSON.stringify({
      db_id: args.db_id, title: args.title || "3DKG Scene",
      include_cards: args.include_cards ?? true, include_triples: args.include_triples ?? true,
      include_events: args.include_events ?? true, include_patterns: args.include_patterns ?? true,
      persist: args.persist ?? true,
    }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── 3DKG Spatial ────────────────────────────────────────────────────────────

server.tool("ddb-3dkg-snapshot", "Get a snapshot of the 3DKG spatial graph", {
  db_id: z.string().optional().describe("Optional micro DB ID"),
}, async (args) => {
  const qs = args.db_id ? `?db_id=${encodeURIComponent(args.db_id)}` : "";
  const result = await ddbFetch(`/v1/3dkg/snapshot${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-3dkg-node", "Get a specific node from the 3DKG spatial graph", {
  node_id: z.string().describe("Node ID"),
  db_id: z.string().optional().describe("Optional micro DB ID"),
}, async (args) => {
  const qs = args.db_id ? `?db_id=${encodeURIComponent(args.db_id)}` : "";
  const result = await ddbFetch(`/v1/3dkg/node/${encodeURIComponent(args.node_id)}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-3dkg-nearest", "Find nearest neighbors in 3D space", {
  db_id: z.string().describe("Micro DB ID"),
  x: z.number().describe("X coordinate"),
  y: z.number().describe("Y coordinate"),
  z: z.number().describe("Z coordinate"),
  k: z.number().optional().describe("Number of neighbors (default 10)"),
  node_type: z.string().optional().describe("Optional node type filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/nearest", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, x: args.x, y: args.y, z: args.z, k: args.k || 10, node_type: args.node_type }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-3dkg-bbox", "Query nodes within a 3D bounding box", {
  db_id: z.string().describe("Micro DB ID"),
  min_x: z.number().describe("Min X"),
  min_y: z.number().describe("Min Y"),
  min_z: z.number().describe("Min Z"),
  max_x: z.number().describe("Max X"),
  max_y: z.number().describe("Max Y"),
  max_z: z.number().describe("Max Z"),
  node_type: z.string().optional().describe("Optional node type filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/bbox", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, min_x: args.min_x, min_y: args.min_y, min_z: args.min_z, max_x: args.max_x, max_y: args.max_y, max_z: args.max_z, node_type: args.node_type }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-3dkg-path", "Find shortest path between two nodes in the 3DKG", {
  db_id: z.string().describe("Micro DB ID"),
  from_id: z.string().describe("Start node ID"),
  to_id: z.string().describe("End node ID"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/path", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, from_id: args.from_id, to_id: args.to_id }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-3dkg-layout", "Recompute the spatial layout of the 3DKG", {
  db_id: z.string().describe("Micro DB ID"),
  iterations: z.number().optional().describe("Layout iterations (default 50)"),
  jitter: z.boolean().optional().describe("Add jitter for overlap prevention (default true)"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/layout", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, iterations: args.iterations ?? 50, jitter: args.jitter ?? true }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-3dkg-sync", "Sync the 3DKG spatial index with ArangoDB", {
  db_id: z.string().describe("Micro DB ID"),
  use_arango: z.boolean().optional().describe("Use ArangoDB as source (default false)"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/sync", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, use_arango: args.use_arango ?? false }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-3dkg-stats", "Get statistics for the 3DKG spatial graph", {
  db_id: z.string().optional().describe("Optional micro DB ID"),
}, async (args) => {
  const qs = args.db_id ? `?db_id=${encodeURIComponent(args.db_id)}` : "";
  const result = await ddbFetch(`/v1/3dkg/stats${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Collection ──────────────────────────────────────────────────────────────

server.tool("ddb-collection-create", "Create a new collection in a named database", {
  db_name: z.string().describe("Database name"),
}, async (args) => {
  const result = await ddbFetch(`/v1/db/${encodeURIComponent(args.db_name)}/collections`, { method: "POST" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── AG-UI ───────────────────────────────────────────────────────────────────

server.tool("ddb-agui-run", "Run an AG-UI agent conversation", {
  threadId: z.string().describe("Thread ID"),
  runId: z.string().describe("Run ID"),
  parentRunId: z.string().optional().describe("Optional parent run ID"),
  state: z.string().optional().describe("JSON state object (optional)"),
  messages: z.string().optional().describe("JSON array of messages (optional)"),
  tools: z.string().optional().describe("JSON array of tool definitions (optional)"),
  context: z.string().optional().describe("JSON array of context items (optional)"),
  forwardedProps: z.string().optional().describe("JSON forwarded properties (optional)"),
}, async (args) => {
  const result = await ddbFetch("/v1/agui/run", {
    method: "POST",
    body: JSON.stringify({
      threadId: args.threadId, runId: args.runId, parentRunId: args.parentRunId,
      state: args.state ? JSON.parse(args.state) : undefined,
      messages: args.messages ? JSON.parse(args.messages) : undefined,
      tools: args.tools ? JSON.parse(args.tools) : undefined,
      context: args.context ? JSON.parse(args.context) : undefined,
      forwardedProps: args.forwardedProps ? JSON.parse(args.forwardedProps) : undefined,
    }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-agui-replay", "Replay an AG-UI conversation", {
  conversation_id: z.string().describe("Conversation ID to replay"),
  agent_id: z.string().optional().describe("Optional agent ID"),
}, async (args) => {
  const qs = args.agent_id ? `?agent_id=${encodeURIComponent(args.agent_id)}` : "";
  const result = await ddbFetch(`/v1/agui/replay/${encodeURIComponent(args.conversation_id)}${qs}`, { method: "POST" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-agui-ingest", "Ingest messages into an AG-UI thread", {
  threadId: z.string().describe("Thread ID"),
  messages: z.string().describe("JSON array of messages"),
}, async (args) => {
  const result = await ddbFetch("/v1/agui/ingest", {
    method: "POST",
    body: JSON.stringify({ threadId: args.threadId, messages: JSON.parse(args.messages) }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Txt2KG ──────────────────────────────────────────────────────────────────

server.tool("ddb-txt2kg-status", "Get Txt2KG pipeline status", {}, async () => {
  const result = await ddbFetch("/v1/txt2kg/status", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-txt2kg-extract", "Extract triples from text using LLM", {
  text: z.string().describe("Text to extract triples from"),
  model: z.string().optional().describe("Optional model override"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/extract", {
    method: "POST",
    body: JSON.stringify({ text: args.text, model: args.model }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-txt2kg-store", "Store triples into the knowledge graph", {
  triples: z.string().describe("JSON array of triple objects [{subject, predicate, object}]"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/store", {
    method: "POST",
    body: JSON.stringify({ triples: JSON.parse(args.triples) }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-txt2kg-rag", "Search the knowledge graph using RAG", {
  query: z.string().describe("Search query"),
  top_k: z.number().optional().describe("Number of results (default 5)"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/rag", {
    method: "POST",
    body: JSON.stringify({ query: args.query, top_k: args.top_k || 5 }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-txt2kg-rag-answer", "Get a RAG-generated answer from the knowledge graph", {
  query: z.string().describe("Question to answer"),
  top_k: z.number().optional().describe("Number of context results (default 5)"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/rag/answer", {
    method: "POST",
    body: JSON.stringify({ query: args.query, top_k: args.top_k || 5 }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-txt2kg-stats", "Get Txt2KG graph statistics", {}, async () => {
  const result = await ddbFetch("/v1/txt2kg/stats", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-txt2kg-models", "List available LLM models for Txt2KG extraction", {}, async () => {
  const result = await ddbFetch("/v1/txt2kg/models", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Txt2KG Bridge ──────────────────────────────────────────────────────────

server.tool("ddb-txt2kg-bridge-push", "Push cards from a micro DB to the Txt2KG graph", {
  db_id: z.string().describe("Micro DB ID"),
  zone: z.string().optional().describe("Zone name (default: txt2kg)"),
  query: z.string().optional().describe("Optional query filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/bridge/push", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, zone: args.zone || "txt2kg", query: args.query }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-txt2kg-bridge-pull", "Pull triples from the Txt2KG graph into a micro DB", {
  db_id: z.string().describe("Micro DB ID"),
  zone: z.string().optional().describe("Zone name (default: txt2kg)"),
  query: z.string().optional().describe("Optional query filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/bridge/pull", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, zone: args.zone || "txt2kg", query: args.query }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-txt2kg-bridge-recall", "Recall knowledge from the Txt2KG bridge", {
  query: z.string().describe("Recall query"),
  top_k: z.number().optional().describe("Number of results (default 5)"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/bridge/recall", {
    method: "POST",
    body: JSON.stringify({ query: args.query, top_k: args.top_k || 5 }),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

server.tool("ddb-txt2kg-bridge-thoughts", "Bridge thoughts from a micro DB to the Txt2KG knowledge graph", {
  db_id: z.string().describe("Micro DB ID"),
  zone: z.string().optional().describe("Zone name (default: txt2kg)"),
  query: z.string().optional().describe("Optional query filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/bridge/thoughts", {
    method: "POST",
    body: JSON.stringify({ db_id: args.db_id, zone: args.zone || "txt2kg", query: args.query }),
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