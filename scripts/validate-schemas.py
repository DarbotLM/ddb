#!/usr/bin/env python3
"""Validate DDB reference schemas and deployable manifests."""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCHEMA_DIR = ROOT / "ddb" / "schemas"


def load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def validate_json_schema() -> None:
    try:
        import jsonschema
    except Exception as exc:  # pragma: no cover - dependency may be optional in minimal installs
        print(f"jsonschema unavailable; skipping strict validation ({exc})")
        return

    kg_contract = load_json(SCHEMA_DIR / "kg_dv_schema.schema.json")
    kg_reference = load_json(SCHEMA_DIR / "kg_dv_schema.json")
    ac3_contract = load_json(SCHEMA_DIR / "ac3_adaptive_agent_card.schema.json")

    jsonschema.Draft202012Validator.check_schema(kg_contract)
    jsonschema.Draft202012Validator.check_schema(ac3_contract)
    jsonschema.validate(kg_reference, kg_contract)


def validate_registry() -> None:
    registry = load_json(SCHEMA_DIR / "schema_registry.json")
    for entry in registry["schemas"]:
        for key in ("schemaPath", "referencePath"):
            path = SCHEMA_DIR / entry[key]
            if not path.exists():
                raise FileNotFoundError(f"Missing schema registry resource: {path}")

    for entry in registry["referenceDdbs"]:
        path = SCHEMA_DIR / entry["manifestPath"]
        if not path.exists():
            raise FileNotFoundError(f"Missing reference DDB manifest: {path}")
        manifest = load_json(path)
        if not manifest.get("manifestName"):
            raise ValueError(f"Reference DDB manifest is missing manifestName: {path}")
        if not manifest.get("version"):
            raise ValueError(f"Reference DDB manifest is missing version: {path}")
        if not entry.get("deployable"):
            raise ValueError(f"Reference DDB manifest is not marked deployable: {path}")


def main() -> int:
    validate_json_schema()
    validate_registry()
    print("DDB schema registry and reference DDB manifests are valid")
    return 0


if __name__ == "__main__":
    sys.exit(main())

