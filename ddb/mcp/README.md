# DarbotDB MCP Server

Model Context Protocol server exposing **58 tools** across the full DarbotDB API surface — adaptive cards, memory recall, graph traversal, 3DKG spatial queries, sessions, manifests, AG-UI agent conversations, and Txt2KG knowledge extraction.

Built on MCP SDK 1.28 with Zod 4 schemas for every tool. Four tools ship with interactive HTML UI apps (ext-apps) for card rendering, memory dashboards, graph exploration, and 3D scene visualization.

## Quick Start

```bash
# HTTP transport (default port 3001)
npm run serve

# stdio transport (for MCP clients like Claude Desktop)
npm run serve:stdio
```

### Environment Variables

| Variable | Default | Description |
|---|---|---|
| `DDB_API_URL` or `DDB_URL` | `http://localhost:8080` | DarbotDB API base URL |
| `MCP_PORT` | `3001` | HTTP server port |

### Prerequisites

The DarbotDB API must be running. From the repo root:

```bash
docker compose -f deploy/compose.yaml up -d
```

Standard topology: engine on `8529`, API on `8530` (mapped to `8080` inside container), MCP server on `3001`.

## Architecture

```
┌─────────────────────────────────────────────────────┐
│  MCP Client (Claude, VS Code, custom agent)         │
└──────────────────────┬──────────────────────────────┘
                       │ MCP protocol (HTTP or stdio)
┌──────────────────────▼──────────────────────────────┐
│  DarbotDB MCP Server  (server.ts)                   │
│  • 4 registerAppTool (with UI resources)            │
│  • 54 server.tool (API-only)                        │
│  • Zod 4 schemas on every input                     │
└──────────────────────┬──────────────────────────────┘
                       │ HTTP / JSON
┌──────────────────────▼──────────────────────────────┐
│  DarbotDB API  (FastAPI on port 8530)               │
│  59 endpoints across cards, memory, graph, 3DKG,    │
│  sessions, manifests, AG-UI, Txt2KG                 │
└─────────────────────────────────────────────────────┘
```

### Interactive UI Apps

Four tools use `registerAppTool` from `@modelcontextprotocol/ext-apps` to serve interactive HTML applications:

| Tool | UI Resource | Description |
|---|---|---|
| `ddb-card-render` | `card-viewer.html` | Adaptive card renderer with inspector metadata |
| `ddb-memory-recall` | `memory-dashboard.html` | Memory recall dashboard for patterns and triples |
| `ddb-graph-explore` | `graph-explorer.html` | Interactive graph traversal visualization |
| `ddb-3dkg-render` | `3dkg-viewer.html` | 3D knowledge graph scene viewer |

## Tool Reference

### Micro DB (5 tools)

| Tool | Description |
|---|---|
| `ddb-micro-create` | Create a portable micro database for an agent or session |
| `ddb-micro-list` | List all portable micro databases |
| `ddb-micro-status` | Get status of a micro database |
| `ddb-micro-query` | Run a SQL query against a micro database |
| `ddb-micro-delete` | Delete a micro database |

### Cards (4 tools)

| Tool | Description |
|---|---|
| `ddb-card-render` | Create and render an adaptive card (with UI) |
| `ddb-card-search` | Search cards by query, zone, or type |
| `ddb-card-compose` | Compose multiple cards into a composite card |
| `ddb-card-tree` | Get the tree structure of a card and its children |

### Memory (5 tools)

| Tool | Description |
|---|---|
| `ddb-memory-recall` | Recall patterns, cards, and triples (with UI) |
| `ddb-memory-store` | Store a memory card into the knowledge graph |
| `ddb-memory-zones` | List all memory zones |
| `ddb-memory-zone-create` | Create a new memory zone |
| `ddb-memory-zone-get` | Get details of a specific zone |

### Graph (7 tools)

| Tool | Description |
|---|---|
| `ddb-graph-explore` | Traverse the knowledge graph (with UI) |
| `ddb-graph-agents` | List all agents |
| `ddb-graph-link` | Create an edge between two nodes |
| `ddb-graph-pattern` | Discover patterns in the graph |
| `ddb-graph-manifests` | List manifests |
| `ddb-graph-manifest-scene` | Get a manifest scene |
| `ddb-graph-zone` | Get zone contents |

### Triad — Observer/Orchestrator/Synthesizer (4 tools)

| Tool | Description |
|---|---|
| `ddb-triad-process` | Submit an event for pattern detection |
| `ddb-triad-thoughts` | Get thoughts from a micro DB's triad engine |
| `ddb-triad-events` | Get events from a micro DB's triad engine |
| `ddb-triad-patterns` | List detected patterns |

### Sessions (3 tools)

| Tool | Description |
|---|---|
| `ddb-session-list` | List micro sessions |
| `ddb-session-create` | Create or open a session (scope: agent, session, zone, memory) |
| `ddb-session-status` | Get session status |

### Manifests (4 tools)

| Tool | Description |
|---|---|
| `ddb-manifest-list` | List all manifests |
| `ddb-manifest-save` | Save a manifest to the graph |
| `ddb-manifest-get` | Get a manifest by ID |
| `ddb-manifest-project` | Project a micro DB into a 3DKG manifest |

### Scene (1 tool)

| Tool | Description |
|---|---|
| `ddb-scene-materialize` | Materialize a micro DB into a 3DKG scene |

### 3DKG Spatial (9 tools)

| Tool | Description |
|---|---|
| `ddb-3dkg-render` | Render a manifest as a 3D scene (with UI) |
| `ddb-3dkg-snapshot` | Get a snapshot of the spatial graph |
| `ddb-3dkg-node` | Get a specific node |
| `ddb-3dkg-nearest` | Find nearest neighbors in 3D space |
| `ddb-3dkg-bbox` | Query nodes within a bounding box |
| `ddb-3dkg-path` | Find shortest path between nodes |
| `ddb-3dkg-layout` | Recompute the spatial layout |
| `ddb-3dkg-sync` | Sync spatial index with ArangoDB |
| `ddb-3dkg-stats` | Get spatial graph statistics |

### AQL & Collections (2 tools)

| Tool | Description |
|---|---|
| `ddb-aql-query` | Run an AQL query with bind variables |
| `ddb-collection-create` | Create a document or edge collection |

### AG-UI (3 tools)

| Tool | Description |
|---|---|
| `ddb-agui-run` | Run an AG-UI agent conversation |
| `ddb-agui-replay` | Replay a conversation |
| `ddb-agui-ingest` | Ingest messages into a thread |

### Txt2KG (7 tools)

| Tool | Description |
|---|---|
| `ddb-txt2kg-status` | Pipeline status |
| `ddb-txt2kg-models` | List available LLM models |
| `ddb-txt2kg-stats` | Graph statistics |
| `ddb-txt2kg-extract` | Extract triples from text |
| `ddb-txt2kg-store` | Store triples |
| `ddb-txt2kg-rag` | RAG search |
| `ddb-txt2kg-rag-answer` | RAG-generated answer |

### Txt2KG Bridge (4 tools)

| Tool | Description |
|---|---|
| `ddb-txt2kg-bridge-push` | Push cards to the Txt2KG graph |
| `ddb-txt2kg-bridge-pull` | Pull triples into a micro DB |
| `ddb-txt2kg-bridge-recall` | Recall knowledge from the bridge |
| `ddb-txt2kg-bridge-thoughts` | Bridge thoughts to the knowledge graph |

## Schema Design

All 58 tools use complete Zod 4 schemas matching the OpenAPI spec exactly:

- **Required fields** are not optional; **optional fields** use `.optional()` or `.nullable().optional()`
- **Arrays** use `z.array()` — no JSON-stringified workarounds
- **Objects** use `z.record(z.string(), z.unknown())` — no `JSON.parse` hacks
- **Enums** use `z.enum()` (e.g., session scope: `agent | session | zone | memory`)
- **Defaults** use `.default()` where the API specifies them
- **Reusable schemas**: `AGUIMessageSchema`, `AGUIToolSchema`, `AGUIContextSchema` are defined once and shared

## Development

```bash
npm install          # install dependencies
npm run build:views  # build HTML UI apps with Vite
npm run build        # build views + compile TypeScript
npx tsc --noEmit     # type-check without emitting
```

### Stack

| Component | Version |
|---|---|
| MCP SDK | 1.28.0 |
| ext-apps | 1.3.2 |
| Zod | 4.3.6 |
| Express | 5.x |
| TypeScript | 5.6+ |
| Node.js | 22+ (ES2022 target) |
