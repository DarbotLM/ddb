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

### Existing endpoints

- `GET /health` -- basic round-trip to the DarbotDB graph engine
- `POST /v1/aql` -- run an AQL query with optional bind vars
- `POST /v1/db/{db}/collections` -- create a document/edge collection
- `POST /v1/cards` -- build an adaptive card, optionally persisting to a micro DB
- `POST /v1/memory/recall` -- recall cards, triples, and graph patterns
- `POST /v1/triad/process` -- process triad events and capture provenance
- `POST /v1/graph/traverse` -- graph traversal for cards, agents, and manifests

### New 3DKG endpoints

- `GET /v1/sessions` -- list micro sessions
- `POST /v1/sessions` -- create or open a session context
- `GET /v1/sessions/{db_id}` -- session status and counts
- `GET /v1/manifests` -- list manifests
- `POST /v1/manifests/project` -- build a manifest from a micro DB
- `POST /v1/manifests` -- persist a manifest
- `GET /v1/manifests/{manifest_id}` -- load a manifest
- `POST /v1/scene/materialize` -- project a micro DB into a scene
- `GET /v1/scene/{manifest_id}` -- fetch a scene by manifest ID

## MCP and UI

`mcp/server.ts` exposes:

- `ddb-card-render`
- `ddb-memory-recall`
- `ddb-graph-explore`
- `ddb-3dkg-render`
- `ddb-session-list`
- `ddb-manifest-project`
- `ddb-micro-create`
- `ddb-triad-process`

UI resources:

- `mcp/resources/card-viewer.html`
- `mcp/resources/memory-dashboard.html`
- `mcp/resources/graph-explorer.html`
- `mcp/resources/3dkg-viewer.html`

The existing card and memory views remain available. The new `3dkg-viewer.html` is the manifest-driven scene surface.

## Verification

Repository validation after the 3DKG/session changes:

- full `pytest -q` suite passing
- explicit tests added for manifest serialization, semantic projection, card metadata propagation, and micro DB manifest round-trips