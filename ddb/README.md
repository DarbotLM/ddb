# DarbotDB API

FastAPI service layer for DarbotDB / DDB / DarbotLM workflows.

This service fronts the DarbotDB graph engine. It is not a wire-compatible replacement for clients that speak the native storage engine HTTP API directly.

## Quick start

```bash
cp .env.example .env
docker compose -f ../../deploy/compose.yaml up -d
# or run locally:
uvicorn darbotdb.api.main:app --reload --port 8080
```

## Standard topology

- Raw darbotdb engine: `8529`
- DarbotDB API wrapper: `8530`
- Default database: `txt2kg`
- MCP server: `3001`
- Optional 3DKG viewer dev host: `http://localhost:5000`

## Layer mapping

| Layer | Responsibility | Key files |
|---|---|---|
| `darango` | transport / integration adapters | `darango/api/routers/agui_router.py`, `darango/api/routers/txt2kg_router.py` |
| `darbotdb` | domain API, session orchestration, manifests, scene materialization | `darbotdb/api/*`, `darbotdb/session/*` |
| `DDB` | persistence and graph substrate | `micro/*`, `graph/*` |

### Mapping rules

- `darango` owns AG-UI and txt2kg protocol-facing adapters.
- `darbotdb` owns session APIs, memory APIs, manifest APIs, scene APIs, and MCP-facing orchestration.
- `DDB` owns SQLite `.ddb` files, Arango collections/edges, indexes, and graph queries.

## 3DKG architecture

### Session abstraction

The 3DKG flow is routed through `darbotdb/session/`:

- `models.py` -- session context and request models
- `backends.py` -- micro DB backend, DDB graph backend, composite backend
- `service.py` -- high-level manifest projection, scene lookup, recall, and session status orchestration

### Manifest schema

`graph/manifest.py` defines the renderable scene model:

- `Manifest`
- `ManifestNode`
- `ManifestEdge`
- `SceneView`
- `SpatialHint`

Semantic truth stays in cards, triples, patterns, events, agents, and zones.
Manifest data is the projection layer used to assemble and render a 3DKG scene.

### Projection pipeline

`graph/projection.py` projects:

- cards into evidence / inspector nodes
- triples into semantic entity nodes and relation edges
- events into provenance nodes
- patterns into cluster / hypothesis nodes
- zones into scope nodes

### Adaptive Card integration

`cards/schema.py` and `cards/builder.py` now support additive 3DKG metadata in `_ddb`:

- `entity_id`
- `entity_kind`
- `manifest_id`
- `view_id`
- `spatial`
- `view_hints`
- `inspector_for`
- `projection_source`

Cards remain backward-compatible and continue to function as the inspector payload format for scene nodes.

## Storage model

### SQLite micro DB (`micro/schema.py`)

Portable `.ddb` files now persist:

- cards
- turns
- thoughts
- tool calls
- links
- events
- triples
- embeddings
- manifests
- manifest nodes
- manifest edges
- session views

### Arango / DDB (`graph/setup.py`)

Shared graph state now includes:

- `manifests`
- `manifest_nodes`
- `manifest_edges`
- `session_views`
- `manifest_to_node`
- `manifest_to_edge`
- `session_to_manifest`
- `view_to_manifest`

## API surface

### Core endpoints

- `GET /health` — basic round-trip to the DarbotDB graph engine
- `POST /v1/aql` — run an AQL query with optional bind vars and batch size
- `POST /v1/db/{db}/collections` — create a document/edge collection
- `POST /v1/cards` — build an adaptive card, optionally persisting to a micro DB
- `POST /v1/cards/search` — search cards by query, zone, or type
- `POST /v1/cards/compose` — compose multiple cards into a composite
- `GET /v1/cards/{db_id}/{card_id}/tree` — card hierarchy traversal
- `POST /v1/memory/recall` — recall cards, triples, and graph patterns
- `POST /v1/memory/store` — store a memory card
- `GET|POST /v1/memory/zones` — list or create memory zones
- `POST /v1/triad/process` — process triad events and capture provenance
- `POST /v1/graph/traverse` — graph traversal for cards, agents, and manifests
- `POST /v1/graph/link` — create edges between nodes
- `POST /v1/graph/pattern` — discover patterns
- `GET /v1/graph/agents` — list agents
- `GET /v1/graph/manifests` — list manifests with optional filters
- `GET /v1/graph/zones/{zone_name}` — zone contents

### Session & manifest endpoints

- `GET /v1/sessions` — list micro sessions
- `POST /v1/sessions` — create or open a session context (scope: agent, session, zone, memory)
- `GET /v1/sessions/{db_id}` — session status and counts
- `GET /v1/manifests` — list manifests
- `POST /v1/manifests/project` — build a manifest from a micro DB
- `POST /v1/manifests` — persist a manifest
- `GET /v1/manifests/{manifest_id}` — load a manifest
- `POST /v1/scene/materialize` — project a micro DB into a scene
- `GET /v1/scene/{manifest_id}` — fetch a scene by manifest ID

### Micro DB endpoints

- `POST /v1/micro/create` — create a portable micro database
- `GET /v1/micro/list` — list all micro databases
- `GET /v1/micro/{db_id}/status` — micro DB status
- `POST /v1/micro/{db_id}/query` — run SQL against a micro DB
- `DELETE /v1/micro/{db_id}` — delete a micro DB

### 3DKG spatial endpoints

- `GET /v1/3dkg/snapshot` — spatial graph snapshot
- `GET /v1/3dkg/node/{node_id}` — get a node
- `POST /v1/3dkg/nearest` — nearest neighbors in 3D space
- `POST /v1/3dkg/bbox` — bounding box query
- `POST /v1/3dkg/path` — shortest path
- `POST /v1/3dkg/layout` — recompute layout
- `POST /v1/3dkg/sync` — sync spatial index with ArangoDB
- `GET /v1/3dkg/stats` — graph statistics

### AG-UI endpoints

- `POST /v1/agui/run` — run an AG-UI agent conversation
- `POST /v1/agui/replay/{conversation_id}` — replay a conversation
- `POST /v1/agui/ingest` — ingest messages into a thread

### Txt2KG endpoints

- `GET /v1/txt2kg/status` — pipeline status
- `GET /v1/txt2kg/models` — available LLM models
- `GET /v1/txt2kg/stats` — graph statistics
- `POST /v1/txt2kg/extract` — extract triples from text
- `POST /v1/txt2kg/store` — store triples
- `POST /v1/txt2kg/rag` — RAG search
- `POST /v1/txt2kg/rag/answer` — RAG-generated answer
- `POST /v1/txt2kg/bridge/push` — push cards to Txt2KG graph
- `POST /v1/txt2kg/bridge/pull` — pull triples into a micro DB
- `POST /v1/txt2kg/bridge/recall` — recall from bridge
- `POST /v1/txt2kg/bridge/thoughts` — bridge thoughts to KG

**59 endpoints across 56 paths.** Full OpenAPI spec at `GET /docs` or `GET /openapi.json`.

## MCP server

`mcp/server.ts` exposes **58 tools** covering the entire API surface. See [mcp/README.md](mcp/README.md) for the complete tool reference.

### Interactive UI apps (4 tools with ext-apps)

- `ddb-card-render` → `card-viewer.html`
- `ddb-memory-recall` → `memory-dashboard.html`
- `ddb-graph-explore` → `graph-explorer.html`
- `ddb-3dkg-render` → `3dkg-viewer.html`

### API-only tools (54 tools)

Organized by domain: Micro DB (5), Cards (3), Memory (4), Graph (6), Triad (4), Sessions (3), Manifests (4), Scene (1), 3DKG Spatial (8), AQL & Collections (2), AG-UI (3), Txt2KG (7), Txt2KG Bridge (4).

All tools use complete Zod 4 schemas with proper types — no JSON-stringified workarounds. Enums, arrays, records, defaults, and nullable optionals match the OpenAPI spec exactly.

```bash
# Start in HTTP mode (port 3001)
npm run serve

# Start in stdio mode (for MCP clients)
npm run serve:stdio
```

## Verification

Repository validation after the 3DKG/session changes:

- full `pytest -q` suite passing
- explicit tests added for manifest serialization, semantic projection, card metadata propagation, and micro DB manifest round-trips