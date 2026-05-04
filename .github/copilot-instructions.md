# Copilot Instructions for DarbotDB (ddb)

## Repository Overview

**DarbotDB (ddb)** is an agent-composable, graph-backed database platform inspired by ArangoDB. It combines:

- A portable **SQLite micro-DB** layer (`.ddb` files) for isolated, verifiable agent memory
- An **ArangoDB/DarbotDB graph engine** for shared graph state
- A **FastAPI service layer** (`ddb/` directory) that wraps both backends
- A **58-tool MCP server** (TypeScript) exposing the full API surface to AI agents
- A **3D Knowledge Graph (3DKG)** spatial engine
- **Txt2KG** LLM-powered triple-extraction pipeline
- **AG-UI** conversation threading

The C++/CMake files in the repository root are the upstream ArangoDB engine (base for darbotdb). Active development is inside the `ddb/` subdirectory.

---

## Repository Layout

```
/                          # Upstream ArangoDB C++ engine (base only — do not modify directly)
  CMakeLists.txt
  CMakePresets.json
  CONTRIBUTING.md
  STYLEGUIDE.md
  ...

ddb/                       # Active DarbotDB Python/TypeScript work
  pyproject.toml           # Python package: "darbotdb" (pip install -e .)
  Dockerfile               # python:3.11-slim, exposes 8080
  .env.example             # Environment variable reference

  darbotdb/                # Domain API, session orchestration, manifests, scenes
    api/                   # FastAPI routers (main.py is the entrypoint)
    config.py              # Settings via pydantic-settings (.env / env vars)
    session/               # Session models, backends (micro/graph/composite), service

  darango/                 # Transport / integration adapters (AG-UI, Txt2KG routers)
    api/routers/

  cards/                   # Adaptive card schema and builder (schema.py, builder.py)
  graph/                   # ArangoDB graph layer: setup, queries, manifest, projection
  graph3d/                 # 3DKG spatial engine
  micro/                   # SQLite micro-DB: schema.py, engine.py, manager.py
  session/                 # Session context models and backends
  triad/                   # Triad event processing and provenance
  agui/                    # AG-UI protocol
  txt2kg/                  # Txt2KG triple extraction and RAG

  mcp/                     # TypeScript MCP server (58 tools)
    server.ts              # Main server entry point
    package.json
    tsconfig.json

  tests/                   # pytest test suite
    test_cards.py
    test_graph3d.py
    test_health.py
    test_manifest.py
    test_micro.py
    test_session.py

deploy/
  compose.yaml             # Docker Compose: darbotdb-api on port 8530

.github/
  workflows/
    darango-api.yml        # CI: Python 3.11, pip install -e ., pytest -q
```

---

## Key Service Ports

| Service                  | Port  |
|--------------------------|-------|
| ArangoDB / DarbotDB engine | 8529 |
| DarbotDB API (FastAPI)   | 8530  |
| MCP server (HTTP)        | 3001  |
| 3DKG viewer dev server   | 5000  |

Default database: `txt2kg`

---

## Environment Variables

Copy `ddb/.env.example` to `ddb/.env`. Key variables:

| Variable              | Description                                  |
|-----------------------|----------------------------------------------|
| `DDB_HOSTS`           | ArangoDB engine URL (default: `http://darbotdb:8529`) |
| `DDB_DATABASE`        | Database name (default: `txt2kg`)            |
| `DDB_USERNAME`        | ArangoDB username (default: `root`)          |
| `DDB_PASSWORD`        | ArangoDB password                            |
| `API_PORT`            | FastAPI listen port (default: `8080`)        |
| `MICRO_DATA_ROOT`     | SQLite `.ddb` file storage path (default: `data`) |
| `TXT2KG_URL`          | Txt2KG service URL                           |
| `TXT2KG_OLLAMA_URL`   | Ollama LLM URL                               |
| `TXT2KG_MODEL`        | Ollama model name (e.g. `qwen3:32b`)         |

---

## How to Run

### Python API (local)

```bash
cd ddb
cp .env.example .env          # Edit as needed
pip install -e .
uvicorn darbotdb.api.main:app --reload --port 8080
```

OpenAPI docs available at `http://localhost:8080/docs`.

### Docker Compose (with remote DarbotDB engine)

```bash
docker compose -f deploy/compose.yaml up -d
```

### MCP Server

```bash
cd ddb/mcp
npm install
npm run serve          # HTTP on port 3001
npm run serve:stdio    # stdio mode for Claude Desktop / VS Code
```

---

## How to Test

Tests live in `ddb/tests/` and use **pytest**.

```bash
cd ddb
pip install -e ".[dev]"      # or: pip install pytest httpx
pytest -q                    # run all tests (quiet output)
```

CI runs the same command via `.github/workflows/darango-api.yml` on every push/PR that touches the `ddb/` directory.

Tests do **not** require a live ArangoDB instance — all graph calls are mocked.

---

## Architecture: Layer Responsibilities

| Layer      | Owns                                                              |
|------------|-------------------------------------------------------------------|
| `darango`  | AG-UI and Txt2KG protocol-facing HTTP adapters                    |
| `darbotdb` | Session orchestration, memory, manifest, scene, MCP-facing APIs  |
| `DDB`      | SQLite `.ddb` persistence, ArangoDB collections/edges/indexes     |

### Projection pipeline (3DKG)

`graph/projection.py` projects:
- **cards** → evidence / inspector nodes
- **triples** → semantic entity nodes + relation edges
- **events** → provenance nodes
- **patterns** → cluster / hypothesis nodes
- **zones** → scope nodes

Semantic truth lives in cards, triples, patterns, events, agents, and zones.  
Manifest data (`graph/manifest.py`) is the projection layer for rendering a 3DKG scene.

---

## Key Files to Know

| File | Purpose |
|------|---------|
| `ddb/darbotdb/api/main.py` | FastAPI app entrypoint; mounts all routers |
| `ddb/darbotdb/config.py` | Pydantic settings (reads from env / `.env`) |
| `ddb/darbotdb/session/service.py` | High-level manifest projection, scene lookup, session status |
| `ddb/darbotdb/session/backends.py` | Micro DB backend, DDB graph backend, composite backend |
| `ddb/graph/manifest.py` | `Manifest`, `ManifestNode`, `ManifestEdge`, `SceneView`, `SpatialHint` models |
| `ddb/graph/projection.py` | Projects micro-DB data into a renderable 3DKG scene |
| `ddb/micro/schema.py` | SQLite schema for `.ddb` files (cards, turns, triples, manifests, …) |
| `ddb/cards/schema.py` | Adaptive card Pydantic models including `_ddb` 3DKG metadata |
| `ddb/mcp/server.ts` | All 58 MCP tool definitions (Zod 4 schemas, HTTP calls to the API) |

---

## Coding Conventions

- **Python**: Pydantic v2 models everywhere; `pydantic-settings` for config; `orjson` for fast serialization; `aiosqlite` for async SQLite access.
- **FastAPI**: All routers return Pydantic models. Use `response_model=` and `status_code=` explicitly.
- **TypeScript (MCP server)**: Zod 4 schemas for all tool inputs/outputs. No JSON-stringified workarounds — use proper typed schemas.
- **Tests**: `pytest` with `httpx.AsyncClient` + `pytest-asyncio`. Mock external dependencies (ArangoDB, Ollama) in tests — no live services required.
- **Packages**: All Python sub-packages listed in `pyproject.toml` under `[tool.setuptools] packages`. Add new sub-packages there.

---

## Common Errors and Workarounds

### ArangoDB connection refused during tests
Tests mock the ArangoDB driver. If you see connection errors in tests, check that the relevant module's graph calls are patched. Do not point tests at a real engine.

### `MICRO_DATA_ROOT` path not found
The micro DB manager auto-creates the data directory. If running in Docker, ensure the `micro-data` volume is mounted at `/app/data`.

### MCP server `zod` version mismatch
The MCP server uses **Zod 4** (`zod@^4.x`). The `@modelcontextprotocol/sdk` peer may expect Zod 3. Run `npm install` inside `ddb/mcp/` and confirm `zod` resolves to `^4.3.x`.

### `pytest` import errors for sub-packages
`pyproject.toml` sets `pythonpath = ["."]` for pytest. Always run pytest from the `ddb/` directory (not the repo root) to pick up the correct `pythonpath`.

---

## CI Pipeline

`.github/workflows/darango-api.yml` triggers on:
- **push** or **PR** touching `ddb/**`

Steps: checkout → Python 3.11 → `pip install -e . pytest httpx` → `pytest -q`

No build step for the MCP TypeScript server is included in CI yet.
