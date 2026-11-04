# DDB Schemas

This directory contains first-party semantic contracts for DarbotDB integrations.

| File | Purpose |
|---|---|
| `kg_dv_schema.schema.json` | JSON Schema for Dataverse-aligned knowledge graph solution metadata. |
| `kg_dv_schema.json` | Canonical Dataverse solution/table definition for DDB knowledge graph storage. |
| `ac3_adaptive_agent_card.schema.json` | JSON Schema for Adaptive Card 3.0-style adaptive agent YAML cards. |
| `ac3_adaptive_agent_card.example.yaml` | Human-readable example card that follows the AC3 schema. |
| `schema_registry.json` | Registry of packaged schemas and deployable reference DDB manifests. |
| `reference_ddbs/*.json` | Deployable reference DDB manifests for KG, SQLite micro DB, and integration-index scenarios. |

## Design principles

- **Dataverse-aligned naming**: tables use Dataverse-style logical names, schema names, display names, primary name attributes, ownership type, columns, keys, relationships, and indexes.
- **Agent-first cards**: AC3 cards are YAML-friendly, schema-first, and suitable for adaptive agent workflows while preserving Adaptive Card-compatible body/action structure.
- **Clean separation**: Dataverse schema files describe persistent knowledge graph storage; AC3 files describe portable agent/human card payloads.
- **Interop-ready**: both schema families expose stable IDs and extension points for MCP tools, micro DBs, graph projection, and 3DKG scene rendering.

## Semantic metadata model

The Dataverse schema includes a portable metadata layer that is shared with AC3 cards:

| Metadata surface | Purpose |
|---|---|
| `semanticAttributes` | Catalog of relatable fields such as confidence, provenance, lineage, sensitivity, and semantic attributes. |
| `relationshipPatterns` | Presentation-ready graph path patterns such as `adaptive-card->evidence->entity` and `workflow->connector->integration-index`. |
| `metadataProfiles` | Named bundles for provenance, semantic relatability, and lineage fields. |
| `ddb_semanticattributesjson` | Per-record JSON payload for aliases, topics, capabilities, path patterns, and routing hints. |
| `ddb_relatableentitiesjson` | Card-level entity references that explain how a card should connect to graph entities. |
| `ddb_provenancejson` | Source, model, observation timestamp, URI, and hash metadata for auditability. |
| `ddb_lineagejson` | Agent, tool, workflow, and transformation steps that produced a graph fact. |

These fields make DDB graph records easier to relate across Dataverse, SQLite `.ddb` files, Arango/DDB graph collections, Qdrant vectors, MCP tools, and adaptive cards.

## Installer integration

Use the repository installer helper to publish schemas and seed a reference `.ddb` micro database:

```bash
python scripts/validate-schemas.py
python scripts/install-ddb-reference-schemas.py --target ~/.ddb/schemas --create-reference-microdb
```

The installed bundle includes `schema_registry.json`, the Dataverse and AC3 contracts, and all `reference_ddbs/*.json` manifests.

