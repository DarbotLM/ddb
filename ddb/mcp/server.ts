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
const server = new McpServer({ name: "DarbotDB MCP Server", version: "0.4.0" });

async function ddbFetch(endpoint: string, options?: RequestInit) {
  const url = `${DDB_API}${endpoint}`;
  const res = await fetch(url, { headers: { "Content-Type": "application/json" }, ...options });
  return res.json();
}

// ─── Reusable AG-UI Schemas ─────────────────────────────────────────────────

const AGUIMessageSchema = z.object({
  role: z.enum(["user", "assistant", "system", "tool", "developer", "activity", "reasoning"]),
  messageId: z.string().optional(),
  content: z.string().nullable().optional(),
  toolCallId: z.string().nullable().optional(),
  toolCallName: z.string().nullable().optional(),
  timestamp: z.string().optional(),
});

const AGUIToolSchema = z.object({
  name: z.string(),
  description: z.string(),
  parameters: z.record(z.string(), z.unknown()).optional(),
});

const AGUIContextSchema = z.object({
  description: z.string(),
  value: z.string(),
});

// ─── UI Resource URIs ───────────────────────────────────────────────────────

const cardViewerUri = "ui://ddb/card-viewer.html";
const memoryDashUri = "ui://ddb/memory-dashboard.html";
const graphExplorerUri = "ui://ddb/graph-explorer.html";
const threeDkgViewerUri = "ui://ddb/3dkg-viewer.html";

// ═══════════════════════════════════════════════════════════════════════════
// registerAppTool tools (4 tools with UI resources)
// ═══════════════════════════════════════════════════════════════════════════

// 1. POST /v1/cards — Create Card
registerAppTool(server, "ddb-card-render", {
  title: "Render DDB Card",
  description: "Render a DDB adaptive card with inspector metadata.",
  inputSchema: {
    db_id: z.string().nullable().optional().describe("Optional micro DB ID"),
    card_type: z.string().default("memory").describe("Card type"),
    title: z.string().describe("Card title"),
    content: z.string().default("").describe("Card body content"),
    agent_id: z.string().nullable().optional().describe("Optional agent ID"),
    zone: z.string().nullable().optional().describe("Radial data zone"),
    tags: z.array(z.string()).optional().describe("Tags"),
    parent_card_id: z.string().nullable().optional().describe("Optional parent card ID"),
    facts: z.record(z.string(), z.unknown()).optional().describe("Optional facts object"),
    manifest_id: z.string().nullable().optional().describe("Optional manifest ID"),
    view_id: z.string().nullable().optional().describe("Optional view ID"),
    entity_id: z.string().nullable().optional().describe("Optional semantic entity ID"),
    entity_kind: z.string().nullable().optional().describe("Optional semantic entity kind"),
    spatial: z.record(z.string(), z.unknown()).optional().describe("Optional spatial data"),
    view_hints: z.record(z.string(), z.unknown()).optional().describe("Optional view hints"),
  },
  _meta: { ui: { resourceUri: cardViewerUri } },
}, async (args) => {
  const result = await ddbFetch("/v1/cards", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 2. POST /v1/memory/recall — Memory Recall
registerAppTool(server, "ddb-memory-recall", {
  title: "Remember Forward",
  description: "Recall relevant patterns, cards, and triples from the DDB knowledge graph.",
  inputSchema: {
    db_id: z.string().nullable().optional().describe("Optional micro DB ID"),
    query: z.string().describe("Search query"),
    zone: z.string().nullable().optional().describe("Optional zone filter"),
    agent_id: z.string().nullable().optional().describe("Optional agent ID"),
    depth: z.number().default(3).describe("Traversal depth"),
    limit: z.number().default(20).describe("Max results"),
  },
  _meta: { ui: { resourceUri: memoryDashUri } },
}, async (args) => {
  const result = await ddbFetch("/v1/memory/recall", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 3. POST /v1/graph/traverse — Graph Traverse
registerAppTool(server, "ddb-graph-explore", {
  title: "Explore DDB Graph",
  description: "Traverse the DDB knowledge graph to discover relationships between agents, cards, patterns, and manifests.",
  inputSchema: {
    start_key: z.string().describe("Starting node key"),
    collection: z.string().default("cards").describe("Collection name"),
    edge_collection: z.string().default("card_to_card").describe("Edge collection name"),
    depth: z.number().default(3).describe("Traversal depth"),
    direction: z.string().default("outbound").describe("Traversal direction"),
  },
  _meta: { ui: { resourceUri: graphExplorerUri } },
}, async (args) => {
  const result = await ddbFetch("/v1/graph/traverse", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 4. GET /v1/scene/{manifest_id} — Get Scene
registerAppTool(server, "ddb-3dkg-render", {
  title: "Render 3DKG Scene",
  description: "Render a 3DKG manifest as a 3D scene with inspector support.",
  inputSchema: {
    manifest_id: z.string().describe("Manifest ID"),
    db_id: z.string().nullable().optional().describe("Optional micro DB ID"),
  },
  _meta: { ui: { resourceUri: threeDkgViewerUri } },
}, async (args) => {
  const params = new URLSearchParams();
  if (args.db_id) params.set("db_id", String(args.db_id));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/scene/${encodeURIComponent(String(args.manifest_id))}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ═══════════════════════════════════════════════════════════════════════════
// server.tool tools (54 tools without UI)
// ═══════════════════════════════════════════════════════════════════════════

// ─── AQL ─────────────────────────────────────────────────────────────────────

// 5. POST /v1/aql — Run AQL
server.tool("ddb-aql-query", "Run an AQL query against the DarbotDB graph database", {
  query: z.string().describe("AQL query string"),
  bind_vars: z.record(z.string(), z.unknown()).nullable().optional().describe("Bind variables"),
  batch_size: z.number().nullable().optional().describe("Batch size"),
}, async (args) => {
  const result = await ddbFetch("/v1/aql", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Micro DB ────────────────────────────────────────────────────────────────

// 6. POST /v1/micro/create — Create Micro
server.tool("ddb-micro-create", "Create a new portable micro database for an agent or session", {
  agent_id: z.string().nullable().optional().describe("Agent identifier"),
  session_id: z.string().nullable().optional().describe("Session identifier"),
  zone: z.string().nullable().optional().describe("Zone name"),
  db_type: z.string().default("agent").describe("Type: agent, session, zone"),
  meta: z.record(z.string(), z.unknown()).optional().describe("Optional metadata"),
}, async (args) => {
  const result = await ddbFetch("/v1/micro/create", { method: "POST", body: JSON.stringify(args) });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 7. POST /v1/triad/process — Triad Process
server.tool("ddb-triad-process", "Submit an event to the Observer/Orchestrator/Synthesizer engine for pattern detection", {
  db_id: z.string().nullable().optional().describe("Optional micro DB ID"),
  event_type: z.string().describe("Event type"),
  source_agent: z.string().nullable().optional().describe("Source agent ID"),
  payload: z.record(z.string(), z.unknown()).optional().describe("Event payload"),
}, async (args) => {
  const result = await ddbFetch("/v1/triad/process", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 8. GET /v1/sessions — List Sessions
server.tool("ddb-session-list", "List DDB micro sessions", {}, async () => {
  const result = await ddbFetch("/v1/sessions", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 9. POST /v1/manifests/project — Project Manifest
server.tool("ddb-manifest-project", "Project a micro DB into a 3DKG manifest", {
  db_id: z.string().describe("Micro DB ID"),
  title: z.string().default("3DKG Scene").describe("Manifest title"),
  include_cards: z.boolean().default(true).describe("Include cards"),
  include_triples: z.boolean().default(true).describe("Include triples"),
  include_events: z.boolean().default(true).describe("Include events"),
  include_patterns: z.boolean().default(true).describe("Include patterns"),
  persist: z.boolean().default(true).describe("Persist to graph"),
}, async (args) => {
  const result = await ddbFetch("/v1/manifests/project", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 10. GET /v1/micro/list — List Micros
server.tool("ddb-micro-list", "List all portable micro databases", {}, async () => {
  const result = await ddbFetch("/v1/micro/list", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 11. GET /v1/micro/{db_id}/status — Micro Status
server.tool("ddb-micro-status", "Get status of a micro database", {
  db_id: z.string().describe("Micro DB ID"),
}, async (args) => {
  const result = await ddbFetch(`/v1/micro/${encodeURIComponent(args.db_id)}/status`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 12. POST /v1/micro/{db_id}/query — Micro Query
server.tool("ddb-micro-query", "Run a SQL query against a micro database", {
  db_id: z.string().describe("Micro DB ID"),
  sql: z.string().describe("SQL query string"),
  params: z.array(z.unknown()).optional().describe("Bind parameters"),
}, async (args) => {
  const { db_id, ...body } = args;
  const result = await ddbFetch(`/v1/micro/${encodeURIComponent(db_id)}/query`, {
    method: "POST",
    body: JSON.stringify(body),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 13. DELETE /v1/micro/{db_id} — Delete Micro
server.tool("ddb-micro-delete", "Delete a micro database", {
  db_id: z.string().describe("Micro DB ID"),
}, async (args) => {
  const result = await ddbFetch(`/v1/micro/${encodeURIComponent(args.db_id)}`, { method: "DELETE" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Cards ───────────────────────────────────────────────────────────────────

// 14. POST /v1/cards/search — Search Cards
server.tool("ddb-card-search", "Search adaptive cards by query, zone, or type", {
  db_id: z.string().describe("Micro DB ID"),
  query: z.string().describe("Search query"),
  zone: z.string().nullable().optional().describe("Optional zone filter"),
  card_type: z.string().nullable().optional().describe("Optional card type filter"),
  limit: z.number().default(50).describe("Max results"),
}, async (args) => {
  const result = await ddbFetch("/v1/cards/search", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 15. POST /v1/cards/compose — Compose Cards
server.tool("ddb-card-compose", "Compose multiple cards into a single composite card", {
  card_ids: z.array(z.string()).describe("Card IDs to compose"),
  title: z.string().describe("Title for the composite card"),
  zone: z.string().nullable().optional().describe("Optional zone"),
  manifest_id: z.string().nullable().optional().describe("Optional manifest ID"),
}, async (args) => {
  const result = await ddbFetch("/v1/cards/compose", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 16. GET /v1/cards/{db_id}/{card_id}/tree — Card Tree
server.tool("ddb-card-tree", "Get the tree structure of a card and its children", {
  db_id: z.string().describe("Micro DB ID"),
  card_id: z.string().describe("Card ID"),
  depth: z.number().optional().describe("Traversal depth"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.depth != null) params.set("depth", String(args.depth));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/cards/${encodeURIComponent(args.db_id)}/${encodeURIComponent(args.card_id)}/tree${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Memory ──────────────────────────────────────────────────────────────────

// 17. POST /v1/memory/store — Memory Store
server.tool("ddb-memory-store", "Store a memory card into the knowledge graph", {
  db_id: z.string().nullable().optional().describe("Optional micro DB ID"),
  card_type: z.string().default("memory").describe("Card type"),
  title: z.string().describe("Memory title"),
  content: z.string().default("").describe("Memory content"),
  zone: z.string().nullable().optional().describe("Optional zone"),
  tags: z.array(z.string()).optional().describe("Tags"),
  agent_id: z.string().nullable().optional().describe("Optional agent ID"),
}, async (args) => {
  const result = await ddbFetch("/v1/memory/store", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 18. GET /v1/memory/zones — List Zones
server.tool("ddb-memory-zones", "List all memory zones", {}, async () => {
  const result = await ddbFetch("/v1/memory/zones", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 19. POST /v1/memory/zones — Create Zone
server.tool("ddb-memory-zone-create", "Create a new memory zone", {
  name: z.string().describe("Zone name"),
  description: z.string().default("").describe("Zone description"),
  visibility: z.string().default("shared").describe("Visibility: shared or private"),
}, async (args) => {
  const result = await ddbFetch("/v1/memory/zones", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 20. GET /v1/memory/zones/{zone_name} — Get Zone
server.tool("ddb-memory-zone-get", "Get details of a specific memory zone", {
  zone_name: z.string().describe("Zone name"),
}, async (args) => {
  const result = await ddbFetch(`/v1/memory/zones/${encodeURIComponent(args.zone_name)}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Graph ───────────────────────────────────────────────────────────────────

// 21. GET /v1/graph/agents — List Agents
server.tool("ddb-graph-agents", "List all agents in the knowledge graph", {}, async () => {
  const result = await ddbFetch("/v1/graph/agents", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 22. POST /v1/graph/link — Create Link
server.tool("ddb-graph-link", "Create a link (edge) between two nodes in the graph", {
  from_collection: z.string().describe("Source collection name"),
  from_key: z.string().describe("Source node key"),
  to_collection: z.string().describe("Target collection name"),
  to_key: z.string().describe("Target node key"),
  edge_collection: z.string().describe("Edge collection name"),
  metadata: z.record(z.string(), z.unknown()).optional().describe("Edge metadata"),
}, async (args) => {
  const result = await ddbFetch("/v1/graph/link", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 23. POST /v1/graph/pattern — Discover Patterns
server.tool("ddb-graph-pattern", "Discover patterns in the knowledge graph", {
  min_confidence: z.number().default(0.5).describe("Minimum confidence threshold"),
  since: z.string().nullable().optional().describe("ISO timestamp filter"),
  limit: z.number().default(20).describe("Max results"),
}, async (args) => {
  const result = await ddbFetch("/v1/graph/pattern", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 24. GET /v1/graph/manifests — List Manifests (graph)
server.tool("ddb-graph-manifests", "List manifests in the knowledge graph", {
  source_db_id: z.string().nullable().optional().describe("Optional source micro DB ID filter"),
  limit: z.number().optional().describe("Max results"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.source_db_id) params.set("source_db_id", args.source_db_id);
  if (args.limit != null) params.set("limit", String(args.limit));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/graph/manifests${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 25. GET /v1/graph/manifests/{manifest_id} — Get Manifest Scene
server.tool("ddb-graph-manifest-scene", "Get a manifest scene from the knowledge graph", {
  manifest_id: z.string().describe("Manifest ID"),
  db_id: z.string().nullable().optional().describe("Optional micro DB ID"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.db_id) params.set("db_id", args.db_id);
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/graph/manifests/${encodeURIComponent(args.manifest_id)}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 26. GET /v1/graph/zones/{zone_name} — Zone Contents
server.tool("ddb-graph-zone", "Get contents of a graph zone", {
  zone_name: z.string().describe("Zone name"),
}, async (args) => {
  const result = await ddbFetch(`/v1/graph/zones/${encodeURIComponent(args.zone_name)}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Triad (Observer/Orchestrator/Synthesizer) ──────────────────────────────

// 27. GET /v1/triad/thoughts/{db_id} — Get Thoughts
server.tool("ddb-triad-thoughts", "Get thoughts from a micro DB's triad engine", {
  db_id: z.string().describe("Micro DB ID"),
  perspective: z.string().nullable().optional().describe("Optional perspective filter"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.perspective) params.set("perspective", args.perspective);
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/triad/thoughts/${encodeURIComponent(args.db_id)}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 28. GET /v1/triad/events/{db_id} — Get Events
server.tool("ddb-triad-events", "Get events from a micro DB's triad engine", {
  db_id: z.string().describe("Micro DB ID"),
  event_type: z.string().nullable().optional().describe("Optional event type filter"),
  triad_status: z.string().nullable().optional().describe("Optional triad status filter"),
  limit: z.number().optional().describe("Max results"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.event_type) params.set("event_type", args.event_type);
  if (args.triad_status) params.set("triad_status", args.triad_status);
  if (args.limit != null) params.set("limit", String(args.limit));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/triad/events/${encodeURIComponent(args.db_id)}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 29. GET /v1/triad/patterns — List Patterns
server.tool("ddb-triad-patterns", "List detected patterns from the triad engine", {
  min_confidence: z.number().optional().describe("Minimum confidence threshold"),
  limit: z.number().optional().describe("Max results"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.min_confidence != null) params.set("min_confidence", String(args.min_confidence));
  if (args.limit != null) params.set("limit", String(args.limit));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/triad/patterns${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Sessions ────────────────────────────────────────────────────────────────

// 30. POST /v1/sessions — Create Or Open Session
server.tool("ddb-session-create", "Create or open a DDB session", {
  db_id: z.string().nullable().optional().describe("Optional micro DB ID"),
  session_id: z.string().nullable().optional().describe("Optional session ID"),
  agent_id: z.string().nullable().optional().describe("Optional agent ID"),
  zone: z.string().nullable().optional().describe("Optional zone"),
  scope: z.enum(["agent", "session", "zone", "memory"]).optional().describe("Session scope"),
  metadata: z.record(z.string(), z.unknown()).optional().describe("Session metadata"),
}, async (args) => {
  const result = await ddbFetch("/v1/sessions", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 31. GET /v1/sessions/{db_id} — Session Status
server.tool("ddb-session-status", "Get status of a specific session", {
  db_id: z.string().describe("Session/micro DB ID"),
}, async (args) => {
  const result = await ddbFetch(`/v1/sessions/${encodeURIComponent(args.db_id)}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Manifests ───────────────────────────────────────────────────────────────

// 32. GET /v1/manifests — List Manifests
server.tool("ddb-manifest-list", "List all manifests", {
  db_id: z.string().nullable().optional().describe("Optional micro DB ID filter"),
  limit: z.number().optional().describe("Max results"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.db_id) params.set("db_id", args.db_id);
  if (args.limit != null) params.set("limit", String(args.limit));
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/manifests${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 33. POST /v1/manifests — Save Manifest
server.tool("ddb-manifest-save", "Save a manifest to the graph", {
  db_id: z.string().describe("Micro DB ID"),
  persist_to_graph: z.boolean().default(true).describe("Persist to ArangoDB graph"),
  manifest: z.record(z.string(), z.unknown()).describe("Manifest object"),
}, async (args) => {
  const result = await ddbFetch("/v1/manifests", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 34. GET /v1/manifests/{manifest_id} — Get Manifest
server.tool("ddb-manifest-get", "Get a specific manifest by ID", {
  manifest_id: z.string().describe("Manifest ID"),
  db_id: z.string().nullable().optional().describe("Optional micro DB ID"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.db_id) params.set("db_id", args.db_id);
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/manifests/${encodeURIComponent(args.manifest_id)}${qs}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Scene ───────────────────────────────────────────────────────────────────

// 35. POST /v1/scene/materialize — Materialize Scene
server.tool("ddb-scene-materialize", "Materialize a micro DB into a 3DKG scene", {
  db_id: z.string().describe("Micro DB ID"),
  title: z.string().default("3DKG Scene").describe("Scene title"),
  include_cards: z.boolean().default(true).describe("Include cards"),
  include_triples: z.boolean().default(true).describe("Include triples"),
  include_events: z.boolean().default(true).describe("Include events"),
  include_patterns: z.boolean().default(true).describe("Include patterns"),
  persist: z.boolean().default(true).describe("Persist to graph"),
}, async (args) => {
  const result = await ddbFetch("/v1/scene/materialize", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── 3DKG Spatial ────────────────────────────────────────────────────────────

// 36. GET /v1/3dkg/snapshot — Snapshot
server.tool("ddb-3dkg-snapshot", "Get a snapshot of the 3DKG spatial graph", {
  db_id: z.string().describe("Micro DB ID"),
}, async (args) => {
  const result = await ddbFetch(`/v1/3dkg/snapshot?db_id=${encodeURIComponent(args.db_id)}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 37. GET /v1/3dkg/node/{node_id} — Get Node
server.tool("ddb-3dkg-node", "Get a specific node from the 3DKG spatial graph", {
  node_id: z.string().describe("Node ID"),
  db_id: z.string().describe("Micro DB ID"),
}, async (args) => {
  const result = await ddbFetch(`/v1/3dkg/node/${encodeURIComponent(args.node_id)}?db_id=${encodeURIComponent(args.db_id)}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 38. POST /v1/3dkg/nearest — Nearest Neighbors
server.tool("ddb-3dkg-nearest", "Find nearest neighbors in 3D space", {
  db_id: z.string().describe("Micro DB ID"),
  x: z.number().describe("X coordinate"),
  y: z.number().describe("Y coordinate"),
  z: z.number().describe("Z coordinate"),
  k: z.number().default(10).describe("Number of neighbors"),
  node_type: z.string().nullable().optional().describe("Optional node type filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/nearest", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 39. POST /v1/3dkg/bbox — Bounding Box
server.tool("ddb-3dkg-bbox", "Query nodes within a 3D bounding box", {
  db_id: z.string().describe("Micro DB ID"),
  min_x: z.number().describe("Min X"),
  min_y: z.number().describe("Min Y"),
  min_z: z.number().describe("Min Z"),
  max_x: z.number().describe("Max X"),
  max_y: z.number().describe("Max Y"),
  max_z: z.number().describe("Max Z"),
  node_type: z.string().nullable().optional().describe("Optional node type filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/bbox", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 40. POST /v1/3dkg/path — Shortest Path
server.tool("ddb-3dkg-path", "Find shortest path between two nodes in the 3DKG", {
  db_id: z.string().describe("Micro DB ID"),
  from_id: z.string().describe("Start node ID"),
  to_id: z.string().describe("End node ID"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/path", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 41. POST /v1/3dkg/layout — Recompute Layout
server.tool("ddb-3dkg-layout", "Recompute the spatial layout of the 3DKG", {
  db_id: z.string().describe("Micro DB ID"),
  iterations: z.number().default(50).describe("Layout iterations"),
  jitter: z.boolean().default(true).describe("Add jitter for overlap prevention"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/layout", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 42. POST /v1/3dkg/sync — Sync Graph
server.tool("ddb-3dkg-sync", "Sync the 3DKG spatial index with ArangoDB", {
  db_id: z.string().describe("Micro DB ID"),
  use_arango: z.boolean().default(false).describe("Use ArangoDB as source"),
}, async (args) => {
  const result = await ddbFetch("/v1/3dkg/sync", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 43. GET /v1/3dkg/stats — Graph Stats
server.tool("ddb-3dkg-stats", "Get statistics for the 3DKG spatial graph", {
  db_id: z.string().describe("Micro DB ID"),
}, async (args) => {
  const result = await ddbFetch(`/v1/3dkg/stats?db_id=${encodeURIComponent(args.db_id)}`, { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Collection ──────────────────────────────────────────────────────────────

// 44. POST /v1/db/{db_name}/collections — Create Collection
server.tool("ddb-collection-create", "Create a new collection in a named database", {
  db_name: z.string().describe("Database name"),
  name: z.string().describe("Collection name"),
  type: z.string().default("document").describe("Collection type: document or edge"),
  key_options: z.record(z.string(), z.unknown()).nullable().optional().describe("Key options"),
  schema: z.record(z.string(), z.unknown()).nullable().optional().describe("Validation schema"),
}, async (args) => {
  const { db_name, ...body } = args;
  const result = await ddbFetch(`/v1/db/${encodeURIComponent(db_name)}/collections`, {
    method: "POST",
    body: JSON.stringify(body),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── AG-UI ───────────────────────────────────────────────────────────────────

// 45. POST /v1/agui/run — Agui Run
server.tool("ddb-agui-run", "Run an AG-UI agent conversation", {
  threadId: z.string().describe("Thread ID"),
  runId: z.string().describe("Run ID"),
  parentRunId: z.string().nullable().optional().describe("Optional parent run ID"),
  state: z.record(z.string(), z.unknown()).optional().describe("State object"),
  messages: z.array(AGUIMessageSchema).optional().describe("Messages"),
  tools: z.array(AGUIToolSchema).optional().describe("Tool definitions"),
  context: z.array(AGUIContextSchema).optional().describe("Context items"),
  forwardedProps: z.record(z.string(), z.unknown()).optional().describe("Forwarded properties"),
}, async (args) => {
  const result = await ddbFetch("/v1/agui/run", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 46. POST /v1/agui/replay/{conversation_id} — Agui Replay
server.tool("ddb-agui-replay", "Replay an AG-UI conversation", {
  conversation_id: z.string().describe("Conversation ID to replay"),
  agent_id: z.string().nullable().optional().describe("Optional agent ID"),
}, async (args) => {
  const params = new URLSearchParams();
  if (args.agent_id) params.set("agent_id", args.agent_id);
  const qs = params.toString() ? `?${params}` : "";
  const result = await ddbFetch(`/v1/agui/replay/${encodeURIComponent(args.conversation_id)}${qs}`, { method: "POST" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 47. POST /v1/agui/ingest — Agui Ingest
server.tool("ddb-agui-ingest", "Ingest messages into an AG-UI thread", {
  threadId: z.string().describe("Thread ID"),
  messages: z.array(AGUIMessageSchema).describe("Messages to ingest"),
}, async (args) => {
  const result = await ddbFetch("/v1/agui/ingest", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Txt2KG ──────────────────────────────────────────────────────────────────

// 48. GET /v1/txt2kg/status — Status
server.tool("ddb-txt2kg-status", "Get Txt2KG pipeline status", {}, async () => {
  const result = await ddbFetch("/v1/txt2kg/status", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 49. GET /v1/txt2kg/models — List Models
server.tool("ddb-txt2kg-models", "List available LLM models for Txt2KG extraction", {}, async () => {
  const result = await ddbFetch("/v1/txt2kg/models", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 50. GET /v1/txt2kg/stats — Graph Stats
server.tool("ddb-txt2kg-stats", "Get Txt2KG graph statistics", {}, async () => {
  const result = await ddbFetch("/v1/txt2kg/stats", { method: "GET" });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 51. POST /v1/txt2kg/extract — Extract Triples
server.tool("ddb-txt2kg-extract", "Extract triples from text using LLM", {
  text: z.string().describe("Text to extract triples from"),
  model: z.string().nullable().optional().describe("Optional model override"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/extract", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 52. POST /v1/txt2kg/store — Store Triples
server.tool("ddb-txt2kg-store", "Store triples into the knowledge graph", {
  triples: z.array(z.record(z.string(), z.unknown())).describe("Triple objects"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/store", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 53. POST /v1/txt2kg/rag — Rag Search
server.tool("ddb-txt2kg-rag", "Search the knowledge graph using RAG", {
  query: z.string().describe("Search query"),
  top_k: z.number().default(5).describe("Number of results"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/rag", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 54. POST /v1/txt2kg/rag/answer — Rag Answer
server.tool("ddb-txt2kg-rag-answer", "Get a RAG-generated answer from the knowledge graph", {
  query: z.string().describe("Question to answer"),
  top_k: z.number().default(5).describe("Number of context results"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/rag/answer", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ─── Txt2KG Bridge ──────────────────────────────────────────────────────────

// 55. POST /v1/txt2kg/bridge/push — Bridge Push Cards
server.tool("ddb-txt2kg-bridge-push", "Push cards from a micro DB to the Txt2KG graph", {
  db_id: z.string().describe("Micro DB ID"),
  zone: z.string().default("txt2kg").describe("Zone name"),
  query: z.string().nullable().optional().describe("Optional query filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/bridge/push", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 56. POST /v1/txt2kg/bridge/pull — Bridge Pull Triples
server.tool("ddb-txt2kg-bridge-pull", "Pull triples from the Txt2KG graph into a micro DB", {
  db_id: z.string().describe("Micro DB ID"),
  zone: z.string().default("txt2kg").describe("Zone name"),
  query: z.string().nullable().optional().describe("Optional query filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/bridge/pull", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 57. POST /v1/txt2kg/bridge/recall — Bridge Recall
server.tool("ddb-txt2kg-bridge-recall", "Recall knowledge from the Txt2KG bridge", {
  query: z.string().describe("Recall query"),
  top_k: z.number().default(5).describe("Number of results"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/bridge/recall", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// 58. POST /v1/txt2kg/bridge/thoughts — Bridge Thoughts To KG
server.tool("ddb-txt2kg-bridge-thoughts", "Bridge thoughts from a micro DB to the Txt2KG knowledge graph", {
  db_id: z.string().describe("Micro DB ID"),
  zone: z.string().default("txt2kg").describe("Zone name"),
  query: z.string().nullable().optional().describe("Optional query filter"),
}, async (args) => {
  const result = await ddbFetch("/v1/txt2kg/bridge/thoughts", {
    method: "POST",
    body: JSON.stringify(args),
  });
  return { content: [{ type: "text" as const, text: JSON.stringify(result, null, 2) }] };
});

// ═══════════════════════════════════════════════════════════════════════════
// UI Resources & Transport
// ═══════════════════════════════════════════════════════════════════════════

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
