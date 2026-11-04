from __future__ import annotations

import json
from pathlib import Path


SCHEMA_DIR = Path(__file__).resolve().parents[1] / "schemas"


def load_json(name: str) -> dict:
    return json.loads((SCHEMA_DIR / name).read_text(encoding="utf-8"))


def test_kg_dv_schema_contract_and_instance_are_well_formed() -> None:
    contract = load_json("kg_dv_schema.schema.json")
    instance = load_json("kg_dv_schema.json")

    assert contract["title"] == "DDB Dataverse Knowledge Graph Schema"
    assert instance["schemaName"] == "kg_dv_schema"
    assert instance["publisher"]["prefix"] == "ddb"
    assert {attr["name"] for attr in instance["semanticAttributes"]} >= {
        "ddb_confidence",
        "ddb_provenancejson",
        "ddb_lineagejson",
        "ddb_semanticattributesjson",
        "ddb_sensitivitylabel",
    }
    assert {pattern["name"] for pattern in instance["relationshipPatterns"]} >= {
        "document_to_entity_evidence",
        "entity_semantic_similarity",
        "workflow_connector_index",
    }

    logical_names = {table["logicalName"] for table in instance["tables"]}
    assert {
        "ddb_kgentity",
        "ddb_kgrelationship",
        "ddb_adaptivecard",
        "ddb_agentconfig",
        "ddb_connector",
        "ddb_workflow",
        "ddb_integrationindex",
    }.issubset(logical_names)

    for table in instance["tables"]:
        columns = {column["logicalName"] for column in table["columns"]}
        assert table["primaryIdAttribute"] in columns
        assert table["primaryNameAttribute"] in columns

    entity_columns = {
        column["logicalName"]
        for table in instance["tables"]
        if table["logicalName"] == "ddb_kgentity"
        for column in table["columns"]
    }
    assert {"ddb_semanticattributesjson", "ddb_provenancejson", "ddb_lineagejson"}.issubset(entity_columns)


def test_ac3_adaptive_agent_card_schema_and_example_are_well_formed() -> None:
    schema = load_json("ac3_adaptive_agent_card.schema.json")
    example = (SCHEMA_DIR / "ac3_adaptive_agent_card.example.yaml").read_text(encoding="utf-8")

    assert schema["title"] == "AC3 Adaptive Agent YAML Card"
    assert '"3.0"' in json.dumps(schema)
    assert "apiVersion: adaptivecards.io/v3" in example
    assert "kind: AdaptiveAgentCard" in example
    assert "version: \"3.0\"" in example
    assert "semanticAttributes:" in example
    assert "relatableEntities:" in example
    assert "provenance:" in example


def test_schema_registry_references_deployable_ddbs() -> None:
    registry = load_json("schema_registry.json")

    schema_ids = {entry["id"] for entry in registry["schemas"]}
    assert {"kg_dv_schema", "ac3_adaptive_agent_card"}.issubset(schema_ids)

    reference_ids = {entry["id"] for entry in registry["referenceDdbs"]}
    assert {"ddb_reference_kg", "ddb_agent_memory", "ddb_integration_index"}.issubset(reference_ids)

    for entry in registry["referenceDdbs"]:
        manifest_path = SCHEMA_DIR / entry["manifestPath"]
        assert manifest_path.exists()
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        assert manifest["manifestName"] == entry["id"]
        assert entry["deployable"] is True

