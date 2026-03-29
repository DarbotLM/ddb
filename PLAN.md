# DDB Architecture Plan — DarangoDB + SQLite Micro DBs + MCP Apps

## Vision

DDB is a **composable agent database** that combines:
- **ArangoDB** (graph/document/search backbone) for deep relationship queries
- **SQLite micro DBs** (portable, embeddable, per-agent/session files) for agent-composable state
- **MCP Apps** (adaptive cards rendered inline in Claude/Copilot/Goose) as the UI + agent schema layer
- **Observer/Orchestrator/Synthesizer** parallel triad for "remember forward" pattern recognition

---

## Layer Architecture

```
┌─────────────────────────────────────────────────────────────────────┐
│                     MCP Apps Layer (UI + Schema)                    │
│  Adaptive Cards: human-friendly UI ↔ agent-friendly JSON schema    │
│  Rendered inline in Claude, Copilot, Goose, Claude Desktop         │
├─────────────────────────────────────────────────────────────────────┤
│                     DDB MCP Server (FastAPI + MCP)                  │
│  registerAppTool() + registerAppResource() + REST endpoints        │
│  Tools: query, write, card-render, memory-recall, graph-traverse   │
├─────────────────────────────────────────────────────────────────────┤
│                     Darango API (existing FastAPI)                  │
│  /v1/aql, /v1/db/{db}/collections, /health                        │
│  Extended: /v1/cards, /v1/memory, /v1/micro, /v1/graph             │
├──────────────────────┬──────────────────────────────────────────────┤
│   SQLite Micro DBs   │          ArangoDB Graph Engine               │
│   (per-agent state)  │     (shared graph, search, relations)        │
│   .ddb files         │     collections, edges, AQL, IResearch       │
│   portable, isolated │     cluster-capable, full-text search        │
├──────────────────────┴──────────────────────────────────────────────┤
│                     Storage Providers (pluggable)                    │
│  FileSystem │ Git │ Azure Blob │ SQLite │ ArangoDB │ Hybrid         │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 1. SQLite Micro DB Layer (`ddb/micro/`)

Each agent, session, or memory zone gets its own `.ddb` SQLite file.
Follows the same JSONL-like patterns as Claude Code sessions but in SQLite for queryability.

### Schema (per `.ddb` file)

```sql
-- Core identity
CREATE TABLE meta (
    key     TEXT PRIMARY KEY,
    value   TEXT NOT NULL
);
-- Populated with: schema_version, agent_id, created_at, owner,
--                 visibility (isolated|shared), auth_mode (none|key|aad),
--                 parent_graph_db, linked_agents[]

-- Conversation turns (mirrors darbot-memory-mcp ConversationTurn)
CREATE TABLE turns (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT NOT NULL,
    turn_number     INTEGER NOT NULL,
    timestamp_utc   TEXT NOT NULL,  -- ISO 8601
    role            TEXT NOT NULL,  -- user|assistant|system|tool
    content         TEXT NOT NULL,
    model           TEXT,
    tools_used      TEXT,           -- JSON array
    hash            TEXT,           -- SHA256 for integrity
    schema_version  TEXT DEFAULT 'v1.0.0',
    UNIQUE(conversation_id, turn_number)
);
CREATE INDEX idx_turns_conv ON turns(conversation_id);
CREATE INDEX idx_turns_ts ON turns(timestamp_utc);

-- Adaptive memory cards (the flash card system)
CREATE TABLE cards (
    id              TEXT PRIMARY KEY,  -- uuid
    card_type       TEXT NOT NULL,     -- memory|task|observation|pattern|index
    title           TEXT NOT NULL,
    schema_json     TEXT NOT NULL,     -- AdaptiveCard JSON (agent-readable)
    content_md      TEXT,              -- Markdown summary (human-readable)
    embedding       BLOB,             -- Optional vector for semantic search
    parent_card_id  TEXT,             -- Composable hierarchy
    tags            TEXT,             -- JSON array for indexing
    zone            TEXT,             -- radial data zone identifier
    created_at      TEXT NOT NULL,
    updated_at      TEXT NOT NULL,
    expires_at      TEXT,             -- TTL for ephemeral cards
    hash            TEXT,
    FOREIGN KEY (parent_card_id) REFERENCES cards(id)
);
CREATE INDEX idx_cards_type ON cards(card_type);
CREATE INDEX idx_cards_zone ON cards(zone);
CREATE INDEX idx_cards_tags ON cards(tags);
CREATE INDEX idx_cards_parent ON cards(parent_card_id);

-- Full-text search on cards (SQLite FTS5)
CREATE VIRTUAL TABLE cards_fts USING fts5(
    title, content_md, tags,
    content='cards',
    content_rowid='rowid'
);

-- Tool call log (audit trail)
CREATE TABLE tool_calls (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    turn_id         INTEGER,
    tool_name       TEXT NOT NULL,
    input_json      TEXT,
    output_json     TEXT,
    duration_ms     INTEGER,
    timestamp_utc   TEXT NOT NULL,
    FOREIGN KEY (turn_id) REFERENCES turns(id)
);

-- Observer/Orchestrator/Synthesizer thought log
CREATE TABLE thoughts (
    id                  INTEGER PRIMARY KEY AUTOINCREMENT,
    thought_number      INTEGER NOT NULL,
    total_thoughts      INTEGER NOT NULL,
    perspective         TEXT NOT NULL,  -- observer|orchestrator|synthesizer
    thought             TEXT NOT NULL,
    assumptions         TEXT,          -- JSON array
    observations        TEXT,          -- JSON array
    verification_level  TEXT NOT NULL, -- none|execution|artifact|ingestion|function
    is_revision         INTEGER DEFAULT 0,
    revises_thought     INTEGER,
    branch_id           TEXT,
    timestamp_utc       TEXT NOT NULL
);
CREATE INDEX idx_thoughts_perspective ON thoughts(perspective);
CREATE INDEX idx_thoughts_branch ON thoughts(branch_id);

-- Links to other micro DBs and ArangoDB collections
CREATE TABLE links (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    link_type       TEXT NOT NULL,  -- agent|card|graph_node|graph_edge|micro_db|external
    source_id       TEXT NOT NULL,
    target_uri      TEXT NOT NULL,  -- ddb://agent-id/card-id OR arango://db/collection/key
    metadata        TEXT,           -- JSON
    created_at      TEXT NOT NULL
);
CREATE INDEX idx_links_type ON links(link_type);
CREATE INDEX idx_links_source ON links(source_id);
```

### Micro DB File Conventions

```
ddb/data/
├── agents/
│   ├── {agent-id}.ddb              # Per-agent isolated micro DB
│   └── {agent-id}.ddb-journal      # WAL journal
├── sessions/
│   ├── {session-id}.ddb            # Per-session ephemeral
│   └── shared/
│       └── {zone-name}.ddb         # Shared radial data zones
├── memory/
│   ├── {agent-id}_memory.ddb       # Long-term agent memory
│   └── swarm_{swarm-id}.ddb        # Shared swarm memory
└── index/
    └── master_index.ddb            # Index of indexes
```

---

## 2. ArangoDB Graph Layer (existing + extensions)

The ArangoDB instance remains the **relationship engine**. SQLite micro DBs hold local state; ArangoDB holds the **cross-agent graph**.

### New Collections

```
agents              (document)   - Agent identity, capabilities, status
cards               (document)   - Master card registry (synced from micro DBs)
memory_zones        (document)   - Radial data zone definitions
sessions            (document)   - Active session metadata
patterns            (document)   - Discovered patterns from O/O/S triad

agent_to_agent      (edge)       - Agent relationships
agent_to_card       (edge)       - Agent ↔ card ownership/access
card_to_card        (edge)       - Card composition hierarchy
card_to_zone        (edge)       - Card ↔ zone membership
pattern_to_card     (edge)       - Pattern evidence links
session_to_agent    (edge)       - Session participants
```

### Named Graph: `ddb_knowledge_graph`

```
Vertex collections: agents, cards, memory_zones, sessions, patterns
Edge collections:   agent_to_agent, agent_to_card, card_to_card,
                    card_to_zone, pattern_to_card, session_to_agent
```

### Key AQL Queries

```aql
// "Remember forward" - find patterns across agents
FOR p IN patterns
  FOR c IN 1..3 OUTBOUND p pattern_to_card
    FOR a IN 1..1 INBOUND c agent_to_card
      FILTER p.discovered_at > @since
      RETURN { pattern: p, evidence: c, discoveredBy: a }

// Composable card tree
FOR c IN 1..10 OUTBOUND @rootCard card_to_card
  RETURN { depth: LENGTH(EDGES), card: c }

// Cross-agent shared memory in zone
FOR z IN memory_zones FILTER z.name == @zone
  FOR c IN 1..1 INBOUND z card_to_zone
    FOR a IN 1..1 INBOUND c agent_to_card
      RETURN { card: c, agent: a.name }
```

---

## 3. Adaptive Card / Flash Card Schema (`ddb/cards/`)

Each card is DUAL-PURPOSE:
- **Human view**: Rendered as MCP App (interactive HTML via `ui://` resource)
- **Agent view**: Raw AdaptiveCard JSON for schema-first subclass queries

### Base Card Schema

```json
{
  "$schema": "http://adaptivecards.io/schemas/adaptive-card.json",
  "type": "AdaptiveCard",
  "version": "1.5",
  "_ddb": {
    "id": "card-uuid",
    "card_type": "memory|task|observation|pattern|index",
    "zone": "zone-identifier",
    "agent_id": "owning-agent",
    "tags": ["semantic", "tags"],
    "parent_card_id": null,
    "hash": "sha256-...",
    "schema_version": "v1.0.0",
    "expires_at": null,
    "links": [
      { "type": "graph_node", "uri": "arango://ddb/cards/card-key" },
      { "type": "micro_db", "uri": "ddb://agent-id" }
    ]
  },
  "body": [
    {
      "type": "TextBlock",
      "text": "Memory: API Rate Limiting Pattern",
      "weight": "Bolder",
      "size": "Medium"
    },
    {
      "type": "FactSet",
      "facts": [
        { "title": "Discovered", "value": "2026-03-08T20:00:00Z" },
        { "title": "Confidence", "value": "0.87" },
        { "title": "Evidence Count", "value": "12" }
      ]
    },
    {
      "type": "Container",
      "items": [
        { "type": "TextBlock", "text": "Pattern details...", "wrap": true }
      ]
    }
  ],
  "actions": [
    {
      "type": "Action.Execute",
      "title": "Recall Full Context",
      "verb": "ddb.recall",
      "data": { "card_id": "card-uuid", "depth": 3 }
    },
    {
      "type": "Action.Execute",
      "title": "Link to Current Task",
      "verb": "ddb.link",
      "data": { "card_id": "card-uuid" }
    }
  ]
}
```

### Card Types

| Type | Purpose | Composable With |
|------|---------|----------------|
| `memory` | Long-term learned fact/pattern | Other memories, patterns |
| `task` | Actionable work item | Agents, sessions |
| `observation` | Raw observation from O/O/S | Patterns, memories |
| `pattern` | Synthesized pattern ("remember forward") | Memories, evidence cards |
| `index` | Index-of-indexes, cross-references | Any card type |

---

## 4. MCP Apps Integration (`ddb/mcp/`)

DDB exposes itself as an MCP server with MCP Apps for interactive card rendering.

### MCP Server Structure

```
ddb/mcp/
├── server.ts                    # MCP server entry point
├── tools/
│   ├── query.ts                 # AQL + SQLite query tool
│   ├── card-render.ts           # Render adaptive card as MCP App
│   ├── card-create.ts           # Create new card
│   ├── memory-recall.ts         # "Remember forward" recall
│   ├── micro-db.ts              # Manage micro DB lifecycle
│   ├── graph-traverse.ts        # Graph traversal tool
│   └── thought.ts               # O/O/S thought recording
├── resources/
│   ├── card-viewer.html         # MCP App: interactive card viewer
│   ├── graph-explorer.html      # MCP App: graph visualization
│   ├── memory-dashboard.html    # MCP App: memory zone browser
│   └── query-results.html       # MCP App: AQL result viewer
└── package.json
```

### Tool Registration Pattern

```typescript
// card-render tool with MCP App UI
registerAppTool(
  server,
  "ddb-card-render",
  {
    title: "Render DDB Card",
    description: "Renders an adaptive memory card with interactive UI",
    inputSchema: {
      type: "object",
      properties: {
        card_id: { type: "string", description: "Card UUID to render" },
        depth: { type: "number", description: "Composition depth", default: 1 }
      },
      required: ["card_id"]
    },
    _meta: { ui: { resourceUri: "ui://ddb/card-viewer.html" } }
  },
  async ({ card_id, depth }) => {
    // Fetch card from SQLite micro DB or ArangoDB
    // Return card JSON + related cards as tool result
    // MCP App renders it interactively
  }
);

// memory-recall tool for "remember forward"
registerAppTool(
  server,
  "ddb-memory-recall",
  {
    title: "Remember Forward",
    description: "Recall relevant patterns and memories from the knowledge graph",
    inputSchema: {
      type: "object",
      properties: {
        query: { type: "string" },
        zone: { type: "string" },
        agent_id: { type: "string" },
        depth: { type: "number", default: 3 }
      },
      required: ["query"]
    },
    _meta: { ui: { resourceUri: "ui://ddb/memory-dashboard.html" } }
  },
  async ({ query, zone, agent_id, depth }) => {
    // 1. FTS5 search in SQLite micro DB
    // 2. Graph traversal in ArangoDB for related patterns
    // 3. Return ranked results with composition tree
  }
);
```

---

## 5. Observer/Orchestrator/Synthesizer Engine (`ddb/triad/`)

Three parallel "views" of incoming data, running behind the scenes:

```
                    ┌─────────────────┐
                    │   Incoming Data  │
                    │  (turns, tools,  │
                    │   observations)  │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
      ┌──────────────┐ ┌──────────────┐ ┌──────────────┐
      │   Observer   │ │ Orchestrator │ │ Synthesizer  │
      │              │ │              │ │              │
      │ Watches what │ │ Sees system  │ │ Identifies   │
      │ IS. Reports  │ │ patterns.    │ │ early cross- │
      │ measured     │ │ Designs      │ │ patterns.    │
      │ state only.  │ │ corrections. │ │ Creates new  │
      │              │ │              │ │ memory cards │
      └──────┬───────┘ └──────┬───────┘ └──────┬───────┘
             │                │                │
             └────────────────┼────────────────┘
                              ▼
                    ┌─────────────────┐
                    │  Adaptive Card  │
                    │   Generation    │
                    │ (pattern → card │
                    │  → micro DB →   │
                    │  graph edge)    │
                    └─────────────────┘
```

### Implementation

```python
# ddb/triad/engine.py

class TriadEngine:
    """Parallel O/O/S processor for incoming data streams."""

    def __init__(self, micro_db: MicroDB, graph: ArangoGraph):
        self.micro_db = micro_db
        self.graph = graph

    async def process(self, event: DDBEvent) -> list[AdaptiveCard]:
        """Run all three perspectives in parallel."""
        observer_task = asyncio.create_task(self._observe(event))
        orchestrator_task = asyncio.create_task(self._orchestrate(event))
        synthesizer_task = asyncio.create_task(self._synthesize(event))

        observations, corrections, patterns = await asyncio.gather(
            observer_task, orchestrator_task, synthesizer_task
        )

        # Synthesizer creates new cards from discovered patterns
        new_cards = []
        for pattern in patterns:
            card = self._create_pattern_card(pattern, observations, corrections)
            self.micro_db.insert_card(card)
            self.graph.link_pattern(card)
            new_cards.append(card)

        return new_cards

    async def _observe(self, event: DDBEvent) -> list[Observation]:
        """Observer: What IS. Measured state only. No assumptions."""
        # Record in thoughts table with perspective='observer'
        ...

    async def _orchestrate(self, event: DDBEvent) -> list[Correction]:
        """Orchestrator: System patterns. Design corrections."""
        # Record in thoughts table with perspective='orchestrator'
        ...

    async def _synthesize(self, event: DDBEvent) -> list[Pattern]:
        """Synthesizer: Cross-pattern identification. Remember forward."""
        # Query existing cards for semantic similarity
        # Identify emerging patterns across agents/zones
        # Record in thoughts table with perspective='synthesizer'
        ...
```

---

## 6. Extended Darango API Routes

### New Endpoints (added to existing FastAPI app)

```python
# ddb/darango/api/routers/micro.py
POST   /v1/micro/create          # Create new micro DB for agent/session
GET    /v1/micro/{id}/status     # Micro DB health + stats
POST   /v1/micro/{id}/query      # SQL query against micro DB
DELETE /v1/micro/{id}            # Drop micro DB

# ddb/darango/api/routers/cards.py
POST   /v1/cards                 # Create adaptive card
GET    /v1/cards/{id}            # Get card (JSON or rendered)
PUT    /v1/cards/{id}            # Update card
DELETE /v1/cards/{id}            # Delete card
POST   /v1/cards/search          # FTS5 + semantic search
POST   /v1/cards/compose         # Compose card tree
GET    /v1/cards/{id}/tree       # Get composition tree

# ddb/darango/api/routers/memory.py
POST   /v1/memory/recall         # "Remember forward" query
POST   /v1/memory/store          # Store memory card
GET    /v1/memory/zones          # List radial data zones
POST   /v1/memory/zones          # Create zone
GET    /v1/memory/zones/{zone}   # Get zone contents

# ddb/darango/api/routers/graph.py  (extends existing AQL)
POST   /v1/graph/traverse        # Named graph traversal
POST   /v1/graph/pattern         # Pattern discovery query
GET    /v1/graph/agents          # List registered agents
POST   /v1/graph/link            # Create cross-entity link

# ddb/darango/api/routers/triad.py
POST   /v1/triad/process         # Submit event to O/O/S engine
GET    /v1/triad/thoughts/{id}   # Get thought log
GET    /v1/triad/patterns        # List discovered patterns
```

---

## 7. Implementation Steps

### Phase 1: SQLite Micro DB Core
**Files:** `ddb/micro/`

1. `ddb/micro/__init__.py` — Module init
2. `ddb/micro/schema.py` — SQLite schema definitions (CREATE TABLE statements)
3. `ddb/micro/engine.py` — MicroDB class: create, open, close, query `.ddb` files
4. `ddb/micro/manager.py` — Lifecycle: create per-agent, per-session, per-zone micro DBs
5. `ddb/micro/sync.py` — Sync cards/patterns from micro DB ↔ ArangoDB
6. Add `aiosqlite>=0.20` to `pyproject.toml` dependencies
7. Unit tests: `ddb/tests/test_micro.py`

### Phase 2: Adaptive Card Schema
**Files:** `ddb/cards/`

1. `ddb/cards/__init__.py` — Module init
2. `ddb/cards/schema.py` — Pydantic models for AdaptiveCard + `_ddb` extension
3. `ddb/cards/builder.py` — Fluent builder: `CardBuilder().memory().title("...").fact("k","v").build()`
4. `ddb/cards/templates.py` — Pre-built templates: memory, task, observation, pattern, index
5. `ddb/cards/validator.py` — Validate against AdaptiveCard JSON schema + DDB extensions
6. Unit tests: `ddb/tests/test_cards.py`

### Phase 3: Extended Darango API Routes
**Files:** `ddb/darango/api/routers/`

1. `micro.py` — SQLite micro DB CRUD + query endpoints
2. `cards.py` — Card CRUD + search + compose endpoints
3. `memory.py` — Memory recall + zone management endpoints
4. `graph.py` — Graph traversal + pattern + link endpoints
5. `triad.py` — O/O/S engine + thought log endpoints
6. Update `main.py` to register new routers
7. Integration tests: `ddb/tests/test_api_*.py`

### Phase 4: ArangoDB Graph Collections
**Files:** `ddb/graph/`

1. `ddb/graph/__init__.py` — Module init
2. `ddb/graph/setup.py` — Create collections, edges, named graph, indexes
3. `ddb/graph/queries.py` — Pre-built AQL queries (remember forward, compose, cross-agent)
4. `ddb/graph/sync.py` — Bidirectional sync micro DB ↔ ArangoDB
5. Migration script: `ddb/graph/migrate.py`
6. Unit tests: `ddb/tests/test_graph.py`

### Phase 5: Observer/Orchestrator/Synthesizer Engine
**Files:** `ddb/triad/`

1. `ddb/triad/__init__.py` — Module init
2. `ddb/triad/engine.py` — TriadEngine with parallel async processing
3. `ddb/triad/observer.py` — Observer perspective logic
4. `ddb/triad/orchestrator.py` — Orchestrator perspective logic
5. `ddb/triad/synthesizer.py` — Synthesizer with pattern detection
6. `ddb/triad/models.py` — Pydantic models for thoughts, events, patterns
7. Unit tests: `ddb/tests/test_triad.py`

### Phase 6: MCP Apps Layer
**Files:** `ddb/mcp/`

1. `ddb/mcp/server.ts` — MCP server with StreamableHTTPServerTransport
2. `ddb/mcp/tools/` — Tool registrations (query, card-render, memory-recall, etc.)
3. `ddb/mcp/resources/card-viewer.html` — Interactive card viewer MCP App
4. `ddb/mcp/resources/graph-explorer.html` — Graph visualization MCP App
5. `ddb/mcp/resources/memory-dashboard.html` — Memory zone browser MCP App
6. `ddb/mcp/package.json` — Dependencies (`@modelcontextprotocol/ext-apps`, etc.)
7. `ddb/mcp/tsconfig.json` + `vite.config.ts` — Build config
8. Integration with Darango API via HTTP calls

### Phase 7: ddb-py Client Extensions
**Files:** Updates to `//10.1.7.0/github/dayour-repos/ddb-py/`

1. `arango/micro.py` — Client methods for micro DB operations
2. `arango/cards.py` — Client methods for card CRUD + search
3. `arango/memory.py` — Client methods for memory recall + zones
4. `arango/triad.py` — Client methods for O/O/S submission + retrieval
5. Update `ddb/cli.py` — CLI commands for micro DB management

### Phase 8: Docker + Deployment
**Files:** `deploy/`

1. Update `compose.yaml` — Add MCP server service, volumes for micro DBs
2. `deploy/Dockerfile.mcp` — MCP server container
3. `deploy/init-graph.py` — Bootstrap ArangoDB collections + graph on first run
4. Volume mounts for `ddb/data/` (micro DB persistence)

---

## 8. Key Design Principles

1. **Schema-first, schema-versioned** — Every record includes `schema_version`
2. **SHA256 integrity** — Every card and turn is hash-verified
3. **Dual representation** — Every card is both human-readable (MCP App HTML) and agent-readable (AdaptiveCard JSON)
4. **Composable** — Cards link to cards, micro DBs link to graph nodes, agents link to agents
5. **Isolated by default, shareable by design** — Micro DBs are per-agent; sharing requires explicit zone membership
6. **Index of indexes** — `index` card type creates cross-reference cards that point to other cards
7. **Remember forward** — O/O/S engine proactively creates pattern cards from incoming data
8. **Pluggable storage** — IStorageProvider pattern from darbot-memory-mcp for backend flexibility
9. **Portable** — A `.ddb` file is a complete, self-contained, queryable agent state
10. **MCP-native** — Every operation is a tool call; every visualization is an MCP App

---

## 9. Module Dependency Map

```
ddb/micro/     ← standalone, depends only on aiosqlite
ddb/cards/     ← depends on micro/ (card storage in SQLite)
ddb/graph/     ← depends on cards/ (sync to ArangoDB), python-arango
ddb/triad/     ← depends on micro/, cards/, graph/ (reads + writes all)
ddb/darango/   ← depends on all above (exposes via REST)
ddb/mcp/       ← depends on darango/ API (calls via HTTP)
ddb-py client  ← depends on darango/ API (calls via HTTP)
```

Build order: `micro → cards → graph → triad → darango routes → mcp → ddb-py`
