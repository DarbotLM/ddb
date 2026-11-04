# DDB Semantic Alignment

DDB semantic alignment is the repository-wide contract that keeps Dataverse tables, SQLite `.ddb` micro databases, DDB graph collections, vector indexes, MCP tools, and adaptive agent cards using the same names, metadata fields, relationship patterns, and deployment resources.

The goal is simple: a fact, card, workflow, connector, or index should be traceable across every DDB surface without renaming, lossy conversion, or hidden assumptions.

## Alignment surfaces

| Surface | Reference resource | Purpose |
|---|---|---|
| Dataverse knowledge graph | `ddb/schemas/kg_dv_schema.json` | Tables, columns, relationships, keys, indexes, semantic attributes, and metadata profiles. |
| Dataverse contract | `ddb/schemas/kg_dv_schema.schema.json` | JSON Schema contract for the Dataverse-aligned KG definition. |
| AC3 adaptive agent cards | `ddb/schemas/ac3_adaptive_agent_card.schema.json` | Adaptive Card 3.0-style YAML cards for agents, tools, workflows, and DDB metadata. |
| AC3 reference card | `ddb/schemas/ac3_adaptive_agent_card.example.yaml` | Professional example card with relatable entities, provenance, lineage, and workflow relations. |
| Schema registry | `ddb/schemas/schema_registry.json` | Index of packaged schemas and deployable reference DDB manifests. |
| Reference DDB manifests | `ddb/schemas/reference_ddbs/*.json` | Deployable manifests for KG, SQLite micro DB, and integration-index scenarios. |
| Installer | `scripts/install-ddb-reference-schemas.py` | Copies packaged schemas and can seed a reference SQLite `.ddb` file. |
| Validator | `scripts/validate-schemas.py` | Validates schema contracts, registry resources, and deployable reference DDB manifests. |

## Semantic model

DDB records align around these reusable metadata concepts:

- **Semantic attributes**: confidence, provenance, lineage, sensitivity, aliases, topics, capabilities, path patterns, embedding references, and routing hints.
- **Relationship patterns**: `adaptive-card->evidence->entity`, `entity->embedding->nearest-entity`, `workflow->connector->integration-index`, and `agent->session->adaptive-card->memory-zone`.
- **Metadata profiles**: provenance, semantic relatability, and lineage field groups that project consistently into Dataverse, `.ddb` files, graph collections, and cards.
- **Reference indexes**: Dataverse table indexes, AC3 card indexes, MCP tool indexes, SQLite micro DB indexes, graph edge indexes, and vector indexes.

## Deployable reference DDBs

| Manifest | Kind | Deployable target |
|---|---|---|
| `ddb_reference_kg.json` | Knowledge graph | Dataverse, DDB graph/document collections, and Qdrant vector stores. |
| `ddb_agent_memory.json` | SQLite micro DB | `.ddb` files with cards, triples, embeddings, metadata, and FTS5 search. |
| `ddb_integration_index.json` | Integration index | Dataverse, AC3, MCP, SQLite, graph, vector, and manifest routing. |

Install them locally:

```bash
python scripts/install-ddb-reference-schemas.py --target ~/.ddb/schemas --create-reference-microdb
```

Validate them:

```bash
python scripts/validate-schemas.py
```

## Completion status

| Backlog item | Status |
|---|---|
| Dataverse KG schema contract | Complete |
| AC3 adaptive agent YAML card schema | Complete |
| Semantic attributes and relationship path patterns | Complete |
| Schema registry | Complete |
| Deployable reference DDB manifests | Complete |
| Installer script for schemas and reference `.ddb` seed | Complete |
| Strict schema validation | Complete |
| Targeted schema tests | Complete |

## Maintenance guidance

When adding a new DDB surface:

1. Add or update the schema contract in `ddb/schemas`.
2. Register it in `ddb/schemas/schema_registry.json`.
3. Add a deployable reference manifest under `ddb/schemas/reference_ddbs` when it represents an installable DDB surface.
4. Update `scripts/validate-schemas.py` and `ddb/tests/test_schemas.py`.
5. Document the path pattern and metadata profile in this file or `ddb/schemas/README.md`.

