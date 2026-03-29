# DarbotDB 3DKG Implementation Map

## Executive Summary

This is a comprehensive 3D Knowledge Graph (3DKG) feature set implementation plan for DarbotDB (DDB). 

**Key Finding**: DDB is architecturally ready for 3DKG. Core components exist (micro DB schema with triples/embeddings, graph layer, txt2kg bridge). Main gaps: modular DB session abstraction, graph manifests, 3D HTML viewer, adaptive card 3D modeling, and schema sync.

---

## Part 1: Architecture Mapping

### 1.1 Layer Responsibilities

#### Layer 0: darango (External Gateway)
- **Location**: \./darango/api/routers/\
- **Role**: Standalone REST to txt2kg (10.1.8.69:3001)
- **Files**: agui_router.py, txt2kg_router.py

#### Layer 1: darbotdb API (FastAPI Wrapper)
- **Location**: \./darbotdb/api/main.py\ + \./darbotdb/api/routers/\

| Router | File | Key Endpoints | Calls |
|--------|------|---------------|-------|
| health | health.py | GET /health | ArangoClient.db().info() |
| aql | aql.py | POST /v1/aql | db.aql.execute() |
| cards | cards_router.py | POST /v1/cards | CardBuilder, MicroDB |
| memory | memory.py | POST /v1/memory/recall | DDBQueries, MicroDB |
| graph | graph_router.py | POST /v1/graph/traverse | DDBQueries |
| micro | micro.py | POST /v1/micro/create | MicroDBManager |
| agui | agui_router.py | POST /v1/agui/run | AGUISession |
| txt2kg | txt2kg_router.py | POST /v1/txt2kg/extract | Txt2KGBridge |

#### Layer 2: DDB Core Engines

**Graph Engine** (\./graph/\):
- setup.py: DDBGraph, collections, edge definitions
- queries.py: DDBQueries (AQL builders)
- sync.py: GraphSync (bidirectional micro ↔ graph)

**Micro DB Engine** (\./micro/\):
- engine.py: MicroDB async SQLite (cards, turns, thoughts, triples, embeddings)
- manager.py: MicroDBManager (factory, directory layout)
- schema.py: SQLite DDL + SCHEMA_VERSION = "v1.1.0"

**Card System** (\./cards/\):
- schema.py: AdaptiveCard, DDBMeta, CardType, LinkType
- builder.py: CardBuilder fluent API
- templates.py: Card presets (memory, task, observation, pattern, index)
- validator.py: Card validation, hash computation

**Triad Engine** (\./triad/\):
- engine.py: TriadEngine (Observer/Orchestrator/Synthesizer)
- models.py: DDBEvent, Observation, Correction, Pattern
- observer.py, orchestrator.py, synthesizer.py: perspectives

**AG-UI Session** (\./agui/\):
- session.py: AGUISession (thread/run ↔ micro DB)
- models.py: AG-UI protocol (RunAgentInput, AGUIEvent)
- adapter.py: Message ↔ turn conversion
- emitter.py: Event emission

**txt2kg Bridge** (\./txt2kg/\):
- bridge.py: Txt2KGBridge (cards ↔ triples)
- client.py: Txt2KGClient (HTTP to txt2kg)
- models.py: Triple, ExtractionResult

### 1.2 HTTP Endpoint → Module Call Chain Example

**POST /v1/memory/recall**
1. memory.py:memory_recall()
2. MicroDBManager.list_dbs() → discover micro DB
3. MicroDB.search_cards(query) → FTS5
4. DDBQueries.patterns_by_confidence() → AQL
5. MicroDB.search_triples() → SQLite
6. Returns: {local_cards, graph_patterns, triples}

### 1.3 Database Session Management (CRITICAL ISSUE)

**Problem**: All routers instantiate ArangoClient directly with hardcoded config.

From \ql.py\ lines 10-14:
\\\python
def get_db():
    client = ArangoClient(hosts=settings.DDB_HOSTS)
    return client.db(settings.DDB_DATABASE, ...)

@router.post("")
def run_aql(payload: AQLRequest, db = Depends(get_db)):
    # No session abstraction; raw db object
\\\

**Issues**:
- No connection pooling (new client per request)
- No context manager pattern
- Hardcoded credentials
- No transaction support
- No timeout/retry/circuit-breaker

---

## Part 2: Identified Gaps for 3DKG Features

### 2.1 Modular Database Session Abstraction ⭐ CRITICAL

**Gap**: Tight coupling to ArangoClient.

**Missing**:
- Session factory with connection pool
- Context manager for cleanup
- Per-operation configuration
- Middleware hooks (logging, metrics, auth)
- Multi-backend support

**Solution**: Create \darbotdb/core/session.py\:
\\\python
class DBSession(ABC):
    async def aql(self, query: str, bind_vars: dict)
    async def collection(self, name: str)
    async def close(self)

class ArangoDBSession(DBSession):
    # Connection pool, logging, retry

class DBSessionFactory:
    async def create_session() -> DBSession
    async def get_pool() -> Pool[DBSession]
\\\

### 2.2 Graph Manifests (NO EXISTING FEATURE)

**Gap**: No structure for graph topology, formats, 3D properties, visualization metadata.

**Needed**:
- **Schema Definition**: YAML/JSON manifests declaring:
  - Vertex types (agents, cards, triples, patterns, zones)
  - Edge types with constraints
  - Properties, indexes, hints
  
- **Visualization Metadata**:
  - 3D positions (x, y, z, scale, rotation)
  - Color/icon mapping
  - Grouping rules
  - Layout parameters

**Solution**: Create \darbotdb/manifests/\ package:
- schema.py: SchemaManifest, VisualizerManifest (Pydantic)
- manifest_router.py: CRUD endpoints

**Example**:
\\\yaml
version: "1.0"
vertices:
  - name: "agents"
    shape: "cube"
    color: "#4CAF50"
  - name: "cards"
    shape: "sphere"
    color_by_property: "card_type"
    color_map: {memory: "#2196F3", pattern: "#FF9800"}
edges:
  - name: "card_to_card"
    style: "arrow"
    color: "#757575"
layout:
  algorithm: "force-directed"
  charge: -300
  distance: 150
\\\

### 2.3 3D HTML Viewer Integration (NO EXISTING FEATURE)

**Gap**: No visualization endpoint or embedded viewer.

**Needed**:
- REST endpoint to serve 3D graph data
- HTML5/WebGL viewer (Three.js)
- Interactive features: pan, zoom, rotate, inspect, filter, search, highlight
- Real-time updates (WebSocket)

**Solution**: Create \darbotdb/visualizer/\ and \darbotdb/api/routers/viewer_router.py\

**Endpoints**:
\\\python
GET /v1/viewer/graph/{graph_id}?manifest={id}&filter={filter}
GET /v1/viewer/card/{card_id}/neighborhood?depth=3
GET /v1/viewer/zone/{zone_name}
POST /v1/viewer/search?query=...
WS /v1/viewer/stream  # real-time updates
\\\

### 2.4 Adaptive Card 3D Modeling (PARTIAL)

**Current State**: Cards define structure but lack 3D geometry.

**Gap**: Cards missing:
- 3D position/rotation
- Bounding volume (scale, shape)
- Visual style hints
- Clustering metadata

**Solution**: Extend \DDBMeta\ in \cards/schema.py\:
\\\python
class Geometry3D(BaseModel):
    position: tuple[float, float, float] = (0, 0, 0)
    rotation: tuple[float, float, float] = (0, 0, 0)
    scale: float = 1.0
    shape: Literal["cube", "sphere", "pyramid"] = "sphere"

class DDBMeta(BaseModel):
    # ... existing ...
    geometry_3d: Geometry3D | None = None
    layout_group: str | None = None
\\\

Update \CardBuilder\: add .position(), .rotation(), .shape() methods.

### 2.5 Schema Sync (PARTIAL)

**Current State**: 
- GraphSync has push_cards(), push_links(), pull_patterns()
- Txt2KGBridge bridges cards ↔ triples

**Gaps**:
- No bidirectional micro ↔ graph schema auto-update
- No version tracking in graph collections
- No merge strategy for conflicts
- No change propagation hooks (manual API only)

**Solution**: Create \darbotdb/core/sync.py\:
\\\python
class SchemaSync:
    async def detect_drift(self) -> SchemaDrift
    async def auto_migrate(self, direction, strategy)
    async def continuous_sync(interval_sec=60)

class ReplicationStrategy(Enum):
    MICRO_WINS | GRAPH_WINS | MERGE | MANUAL
\\\

---

## Part 3: Concrete Todo List (with Dependencies)

### Phase 1: Foundation (Weeks 1-2)

**1.1 Modular Session Abstraction** ⭐ BLOCKING
- [ ] Create \darbotdb/core/session.py\
- [ ] Implement DBSession ABC, ArangoDBSession, DBSessionFactory
- [ ] Update \darbotdb/config.py\ (pool settings)
- [ ] Update all routers: aql.py, collections.py, graph_router.py, memory.py
- **Test**: \	ests/test_session.py\
- **Blocker**: All other work depends on this

**1.2 Schema Sync Detection**
- [ ] Create \darbotdb/core/sync.py\
- [ ] Implement detect_drift(), auto_migrate()
- [ ] Add MigrationResult model
- **Test**: \	ests/test_schema_sync.py\
- **Depends on**: 1.1

### Phase 2: Graph Manifests (Weeks 2-3)

**2.1 Manifest Models & API**
- [ ] Create \darbotdb/manifests/schema.py\ (VertexType, EdgeType, SchemaManifest, VisualizerManifest)
- [ ] Create \darbotdb/api/routers/manifest_router.py\
  - POST /v1/manifest/create
  - GET /v1/manifest/{id}
  - PUT /v1/manifest/{id}
  - GET /v1/manifest/{id}/validate
- [ ] Add default manifest file
- **Test**: \	ests/test_manifests.py\, \	ests/test_manifest_router.py\
- **Depends on**: 2.1

### Phase 3: 3D Viewer (Weeks 3-4)

**3.1 Graph → 3D Format Converter**
- [ ] Create \darbotdb/visualizer/converter.py\ (GraphToThreeJS)
- [ ] Support filters: type, zone, agent, confidence, depth
- [ ] Return {vertices, edges, metadata}
- **Test**: \	ests/test_converter.py\
- **Depends on**: 1.1, 2.1

**3.2 Layout Algorithms**
- [ ] Create \darbotdb/visualizer/layout.py\
- [ ] Force-directed, hierarchical, radial layouts
- [ ] Manifest-driven parameters
- **Test**: \	ests/test_layout.py\

**3.3 Viewer Endpoints**
- [ ] Create \darbotdb/api/routers/viewer_router.py\
- [ ] GET /v1/viewer/graph/{id}, /v1/viewer/card/{id}/neighborhood, /v1/viewer/zone/{zone}
- [ ] POST /v1/viewer/search, WS /v1/viewer/stream
- **Test**: \	ests/test_viewer_router.py\

**3.4 HTML5 Viewer UI** (Optional, can be external)
- [ ] Create \darbotdb/ui/index.html\ (Three.js scene)
- [ ] Pan/zoom/rotate controls, filtering, card inspection, search

### Phase 4: 3D Card Modeling (Weeks 4-5)

**4.1 Extend AdaptiveCard Schema**
- [ ] Modify \cards/schema.py\: Add Geometry3D, extend DDBMeta
- **Test**: Update \	ests/test_cards.py\

**4.2 CardBuilder 3D Methods**
- [ ] Modify \cards/builder.py\: Add .position(), .rotation(), .scale(), .shape(), .layout_group()
- **Test**: Update \	ests/test_cards.py\

**4.3 Card Templates with 3D Presets**
- [ ] Modify \cards/templates.py\: Add 3D geometry presets
- **Test**: Update \	ests/test_cards.py\

### Phase 5: Continuous Schema Sync (Weeks 5-6)

**5.1 Auto-Sync Implementation**
- [ ] Extend \darbotdb/core/sync.py\: Implement auto_migrate(), continuous_sync()
- [ ] Add hooks for card insert/update, graph changes
- **Test**: Update \	ests/test_schema_sync.py\

**5.2 Integration**
- [ ] Modify \darbotdb/api/main.py\: Start sync task on startup
- [ ] Add endpoints: POST /v1/sync, GET /v1/sync/status

### Phase 6: Integration & Polish (Weeks 6-7)

**6.1 End-to-End Tests**
- [ ] Test: Create card → sync to graph → visualize in 3D
- [ ] Test: Extract triples from txt2kg → sync to DDB → visualize
- [ ] Test: Triad event → creates cards → appear in 3D view
- **Test**: \	ests/test_e2e_3dkg.py\

**6.2 Documentation**
- [ ] Create \docs/3DKG_ARCHITECTURE.md\
- [ ] Create \docs/MANIFEST_GUIDE.md\
- [ ] Create \docs/VIEWER_API.md\
- [ ] Add Swagger annotations to viewer_router

---

## Part 4: Tests to Add/Update

### New Test Files

1. **\	ests/test_session.py\**
   - DBSessionFactory creation, pooling, cleanup
   - Retry logic
   - Context manager semantics

2. **\	ests/test_schema_sync.py\**
   - detect_drift() accuracy
   - auto_migrate() with different strategies
   - Round-trip sync

3. **\	ests/test_manifests.py\**
   - SchemaManifest validation
   - Color/shape mapping
   - File I/O

4. **\	ests/test_converter.py\**
   - GraphToThreeJS output format
   - Filtering (type, zone, agent)
   - Layout algorithms produce valid positions

5. **\	ests/test_layout.py\**
   - Force-directed convergence
   - Hierarchical respects parent-child
   - Radial clusters by zone

6. **\	ests/test_viewer_router.py\**
   - GET /v1/viewer/graph/{id} returns valid 3D data
   - Filtering parameters
   - Search endpoint

7. **\	ests/test_manifest_router.py\**
   - CRUD operations
   - Validation endpoint

8. **\	ests/test_e2e_3dkg.py\**
   - Create card with 3D → fetch via viewer
   - Extract triples → import as cards → visualize
   - Triad event → pattern card → appears in graph
   - Update card → auto-synced → 3D position updated
   - Filter by zone → only zone cards returned

### Modified Test Files

1. **\	ests/test_cards.py\**
   - Add Geometry3D tests
   - Add CardBuilder.position(), .rotation(), .shape() tests
   - Add 3D template tests

2. **\	ests/test_health.py\**
   - Update to use session abstraction
   - Test session pool health

3. **\	ests/test_micro.py\**
   - Add continuous sync hook tests
   - Test: insert card in micro → auto-synced to graph

---

## Part 5: Current Code Inconsistencies & Risks

### Critical Risks

**5.1 Direct ArangoClient Instantiation (HIGH)**
- **Location**: aql.py:10-14, collections.py:7, graph_router.py:18, memory.py:23
- **Risk**: New client per request → no pooling → connection exhaustion under load
- **Impact**: 100+ concurrent requests hit connection limits
- **Mitigation**: Complete Phase 1.1 (session abstraction)

**5.2 No Transaction Support**
- **Location**: graph/sync.py:40-45 (GraphSync.push_cards loop)
- **Risk**: If one insert fails mid-loop, orphaned data
- **Impact**: Inconsistent graph state after network interruption
- **Mitigation**: Wrap in ArangoDB transactions or batch operations

**5.3 Hard-Coded Credentials**
- **Location**: darbotdb/config.py (DDB_USERNAME, DDB_PASSWORD)
- **Risk**: Credentials in .env files, not secrets management
- **Impact**: Credential leak if .env committed
- **Mitigation**: Use environment-only secret injection, Vault, or K8s Secrets

### Medium Risks

**5.4 No Query Validation on AQL**
- **Location**: aql.py:20 (db.aql.execute(payload.query, ...))
- **Risk**: User can submit arbitrary AQL, including mutations
- **Impact**: Unauthorized graph mutations
- **Mitigation**: Query whitelist or AQL analyzer

**5.5 Circular Dependencies**
- **Location**: txt2kg/bridge.py imports cards.builder; cards/schema.py defines LinkType
- **Risk**: Refactoring cards breaks txt2kg
- **Mitigation**: Extract schema to darbotdb/core/models.py

**5.6 AGUISession.replay_as_agui() Assumes Consistent Data**
- **Location**: agui/session.py:180-200
- **Risk**: Missing/malformed thoughts cause silent failures
- **Mitigation**: Add try/except + logging, yield error events

### Low Risks

**5.7 FTS5 Search May Be Slow**
- **Location**: micro/engine.py:search_cards()
- **Risk**: MATCH queries on >100K cards slow
- **Mitigation**: Add query timeout, index hints

**5.8 No Index Stats**
- **Location**: micro/schema.py (CREATE_TRIPLES_IDX_GRAPH_KEY)
- **Risk**: graph_key lookups slow without stats
- **Mitigation**: Add ANALYZE after bulk inserts

---

## Part 6: File Tree (Updated)

\\\
darbotdb/
├── api/
│   ├── main.py (MODIFY: start sync task)
│   └── routers/
│       ├── viewer_router.py (NEW)
│       ├── manifest_router.py (NEW)
│       ├── aql.py (MODIFY)
│       ├── collections.py (MODIFY)
│       ├── graph_router.py (MODIFY)
│       └── memory.py (MODIFY)
├── core/
│   ├── session.py (NEW: DBSession, ArangoDBSession, DBSessionFactory)
│   ├── sync.py (NEW: SchemaSync, ReplicationStrategy)
│   └── models.py (NEW OPTIONAL)
├── manifests/
│   ├── schema.py (NEW: SchemaManifest, VisualizerManifest)
│   └── defaults.yaml (NEW)
├── visualizer/
│   ├── converter.py (NEW: GraphToThreeJS)
│   ├── layout.py (NEW: force-directed, hierarchical, radial)
│   └── filters.py (NEW)
├── ui/
│   ├── index.html (NEW)
│   ├── graph.js (NEW)
│   └── styles.css (NEW)
└── config.py (MODIFY)

cards/
├── schema.py (MODIFY: +Geometry3D)
├── builder.py (MODIFY: +3D methods)
└── templates.py (MODIFY: +3D presets)

tests/
├── test_session.py (NEW)
├── test_schema_sync.py (NEW)
├── test_manifests.py (NEW)
├── test_converter.py (NEW)
├── test_layout.py (NEW)
├── test_viewer_router.py (NEW)
├── test_manifest_router.py (NEW)
├── test_cards.py (MODIFY)
└── test_e2e_3dkg.py (NEW)
\\\

---

## Appendix: Performance Targets

- **Viewer latency**: < 500ms for 1,000 vertices
- **Layout computation**: < 1s for 1,000 vertices (100 iterations)
- **Sync throughput**: > 100 cards/sec
- **Search latency**: < 100ms for FTS5 on 10K cards

