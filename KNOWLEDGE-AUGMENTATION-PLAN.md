# DarbotDB Knowledge Augmentation Plan

## Current State (Post-Import Analysis)

### Graph Topology
```
321 nodes  ·  475 edges (named graph view, 250 limit)
408 cards  ·  71 agents  ·  12 zones

Edge Distribution:
  cross-domain    1,441   (semantic links between domains)
  sibling           914   (same-level relationships)
  references        350   (source citations)
  deck_member       232   (deck composition)
  composition        73   (parent-child containment)
  template_instance  52   (layout template usage)
  belongs_to_zone    35   (zone assignment)
  part_of            15
  indexes            11
  + 19 other semantic edge types (1-8 each)
```

### Pattern Findings

#### 1. ZONE IMBALANCE
**agentcards** dominates (348/408 cards = 85%). Six zones are completely empty:
```
agentcards    348 cards  ·  3,023 internal edges  ·  density 0.05
engineering    22 cards  ·     20 internal edges  ·  density 0.09
darbotlm       16 cards  ·     27 internal edges  ·  density 0.23
audit           12 cards  ·     14 internal edges  ·  density 0.21
analysis         7 cards  ·      6 internal edges  ·  density 0.29
knowledge        6 cards  ·      1 internal edge   ·  density 0.07
architecture     0 cards                            EMPTY
customer         0 cards                            EMPTY
platform         0 cards                            EMPTY
operations       0 cards                            EMPTY
data             0 cards                            EMPTY
security         0 cards                            EMPTY
```

#### 2. AGENT ROUTING GAP
22 of 24 decks have **zero** `route_to` agent assignments. Only `mcp-apps` and `genaiscript` route to specific agents. The remaining 232+ cards are unreachable by the dayourbot fleet unless discovered via graph traversal.

#### 3. ORPHAN AGENTS
11 agents in the `agents` collection have no card ownership or routing edges. These are fleet members with no knowledge cards assigned.

#### 4. NO CROSS-DECK LINKING
Every deck is an island — zero `cross_deck` links between any deck pair. The 24 decks cover overlapping topics (e.g., `copilot-cowork` and `copilot-cowork-dev`, three OTel decks, two session-audit decks) but none reference each other.

#### 5. TEMPLATE USAGE IS SHALLOW
Only 52 template→instance edges exist. 10 templates are used, but many deck cards don't declare their `template_id`, so the template lineage is lost.

#### 6. HIGH-DENSITY SMALL ZONES
`darbotlm` (0.23) and `audit` (0.21) and `analysis` (0.29) have high density — every card connects to multiple others. These are tightly scoped knowledge clusters that work well. The model to replicate.

---

## Augmentation Plan

### Phase A: Zone Rebalancing (Fill Empty Zones)

**Goal:** Populate the 6 empty zones by reassigning existing cards and generating new ones.

| Zone | Source | Action |
|------|--------|--------|
| **platform** | Decks: `copilot-cowork`, `copilot-cowork-dev`, `copilot-studio-api-deck`, `pac-cli-mcp`, `power-pages-copilot-chat` | Reassign ~50 cards from `agentcards` to `platform` |
| **architecture** | Decks: `headless-agent-lifecycle`, `openai-agents-js-sdk`, `mcp-apps`, `agency-playground` | Reassign ~35 cards covering system design patterns |
| **engineering** | Decks: `genaiscript`, `codex-windows-app`, `github-spark`, `fluent-ui-react-v9` | Move ~40 cards to `engineering` (some already there) |
| **security** | Decks: `gim-policy-service`, `session-audit`, `session-audit-api` | Reassign ~37 cards covering audit, policy, compliance |
| **operations** | Decks: `best-practices-deck`, `otel-genai-ops` | Reassign ~22 cards covering SRE, monitoring, operations |
| **data** | Decks: `otel-genai-semconv`, `otel-genai-impl`, `llm-library`, `foundry` | Reassign ~42 cards covering data, AI, and ML |
| **customer** | Root cards: `23-wave3-frontier-transformation`, workshop hub cards (24-30) | Move ~15 cards covering enterprise strategy, workshops |

**AQL to execute:**
```aql
-- Example: Move copilot decks to platform zone
FOR c IN cards
  FILTER c.deck_id IN ['deck-copilot-cowork', 'deck-copilot-cowork-dev',
                        'deck-copilot-studio-api-deck', 'deck-pac-cli-mcp',
                        'deck-power-pages-copilot-chat']
  UPDATE c WITH { zone: 'platform' } IN cards
```

### Phase B: Agent Routing Augmentation

**Goal:** Connect every deck to at least 3 relevant dayourbot agents.

Routing matrix based on deck content analysis:

| Deck | Agents to Route |
|------|----------------|
| `copilot-cowork` | dayour-copilot, dayour-m365, dayour-studio |
| `copilot-cowork-dev` | dayour-copilot, dayour-swe, dayour-agentbuilder |
| `copilot-studio-api-deck` | dayour-studio, dayour-bat, dayour-agentbuilder |
| `session-audit` | dayour-sre, dayour-analyst, dayour-bat |
| `session-audit-api` | dayour-sre, dayour-bat, dayour-swe |
| `best-practices-deck` | dayour-studio, dayour-architect, dayour-bat |
| `gim-policy-service` | dayour-security, dayour-azure, dayour-sre |
| `pac-cli-mcp` | dayour-mcp, dayour-studio, dayour-bat |
| `headless-agent-lifecycle` | dayour-agentbuilder, dayour-architect, dayour-swe |
| `otel-genai-*` (3 decks) | dayour-sre, dayour-architect, dayour-analyst |
| `llm-library` | dayour-ai, dayour-azure, dayour-architect |
| `codex-windows-app` | dayour-swe, dayour-dev, dayour-bat |
| `github-spark` | dayour-swe, dayour-dev, dayour-agentbuilder |
| `fluent-ui-react-v9` | dayour-design, dayour-swe, dayour-dev |
| `openai-agents-js-sdk` | dayour-agentbuilder, dayour-swe, dayour-mcp |
| `power-pages-copilot-chat` | dayour-studio, dayour-dataverse, dayour-swe |
| `self-flow-bfl` | dayour-ai, dayour-researcher, dayour-architect |
| `agency-playground` | dayour-agentbuilder, dayour-bat, dayour-mcp |
| `foundry` | dayour-azure, dayour-ai, dayour-architect |

### Phase C: Cross-Deck Semantic Linking

**Goal:** Create edges between decks that cover related topics. Currently zero cross-deck links.

Discovery strategy — use txt2kg LLM extraction on card summaries to find semantic overlaps:

| Link | Type | Rationale |
|------|------|-----------|
| `deck-copilot-cowork` ↔ `deck-copilot-cowork-dev` | `companion` | Same topic, user vs dev perspective |
| `deck-session-audit` ↔ `deck-session-audit-api` | `companion` | Concept vs implementation |
| `deck-otel-genai-semconv` ↔ `deck-otel-genai-impl` ↔ `deck-otel-genai-ops` | `trilogy` | Three facets of OTel GenAI |
| `deck-mcp-apps` ↔ `deck-pac-cli-mcp` | `shared_protocol` | Both use MCP |
| `deck-copilot-studio-api-deck` ↔ `deck-headless-agent-lifecycle` | `extends` | API deck extends lifecycle patterns |
| `deck-genaiscript` ↔ `deck-openai-agents-js-sdk` | `complementary` | Both JS/TS agent tooling |
| `deck-llm-library` ↔ `deck-foundry` | `stack` | Foundry uses LLM library under the hood |
| `deck-gim-policy-service` ↔ `deck-session-audit` | `governance` | Policy enforcement ↔ audit |
| `deck-fluent-ui-react-v9` ↔ `deck-mcp-apps` | `ui_layer` | Fluent components for MCP app UIs |
| `deck-best-practices-deck` → ALL other decks | `best_practice` | Practices apply to all |

### Phase D: Schema Enrichment

**New fields to add to the `cards` collection** based on pattern analysis:

| Field | Type | Purpose |
|-------|------|---------|
| `category` | string | High-level domain: `ai`, `platform`, `security`, `engineering`, `operations` |
| `difficulty` | string | `beginner`, `intermediate`, `advanced` — for progressive learning |
| `freshness` | string | `current`, `aging`, `stale` — based on source date vs now |
| `confidence` | float | 0.0-1.0 — how well-verified is this knowledge? |
| `access_count` | int | How many times this card has been recalled |
| `last_accessed` | datetime | Last time an agent recalled this card |
| `cross_deck_refs` | array | Explicit references to related decks |
| `semantic_embedding` | blob | Vector embedding for semantic similarity search |
| `source_hash` | string | SHA256 of original source document for change detection |
| `superseded_by` | string | Points to newer card if this one is outdated |

### Phase E: Pattern Discovery (O/O/S Triad)

**Goal:** Run the Observer/Orchestrator/Synthesizer engine on the card content to discover implicit patterns.

Target patterns to detect:
1. **Technology stack clusters** — cards that reference the same SDKs/APIs/protocols
2. **Agent capability gaps** — topics covered by cards but not routed to any agent
3. **Knowledge decay** — cards with source dates >90 days old
4. **Semantic duplicates** — cards with >85% content overlap across different decks
5. **Missing prerequisite chains** — cards that reference concepts defined in other decks without linking

### Phase F: txt2kg Re-Extraction

**Goal:** Feed all 408 card summaries through the txt2kg Ollama pipeline to extract deeper triples.

Pipeline:
1. Export all card `content_md` + `summary` fields as a single document
2. Upload to txt2kg via `/api/document-data`
3. Extract triples using `qwen3:32b` model
4. Store triples in graph → creates new entities/relationships
5. Bridge those back into DDB cards via `Txt2KGBridge.import_triples_as_cards()`

Expected yield: ~500-1,000 new relationship triples discovering cross-domain knowledge connections that aren't explicit in the deck structure.

### Phase G: ArangoSearch View Creation

**Goal:** Create ArangoSearch views for fast full-text + semantic search across cards.

```json
{
  "name": "cards_search_view",
  "type": "arangosearch",
  "links": {
    "cards": {
      "analyzers": ["text_en", "identity"],
      "fields": {
        "title": { "analyzers": ["text_en"] },
        "content_md": { "analyzers": ["text_en"] },
        "summary": { "analyzers": ["text_en"] },
        "tags": { "analyzers": ["identity"] },
        "zone": { "analyzers": ["identity"] },
        "kind": { "analyzers": ["identity"] },
        "agent_id": { "analyzers": ["identity"] }
      },
      "includeAllFields": false,
      "storeValues": "id"
    }
  }
}
```

---

## Execution Order

```
Phase A (zone rebalancing)     → 5 min   (AQL bulk update)
Phase B (agent routing)        → 10 min  (AQL upsert + edge creation)
Phase C (cross-deck linking)   → 10 min  (AQL edge creation)
Phase D (schema enrichment)    → 5 min   (AQL field addition)
Phase G (ArangoSearch views)   → 2 min   (API call)
Phase E (O/O/S pattern run)    → varies  (async, can run continuously)
Phase F (txt2kg re-extraction) → 30+ min (depends on Ollama throughput)
```

Phases A-D + G can run immediately. E and F are continuous/async processes.
