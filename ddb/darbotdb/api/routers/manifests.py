"""API routes for 3DKG manifests."""

from __future__ import annotations

from typing import Any

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field

from darbotdb.session.models import SceneRequest
from darbotdb.session.service import SessionService
from graph.manifest import Manifest

router = APIRouter()
_service = SessionService()


class SaveManifestRequest(BaseModel):
    db_id: str
    persist_to_graph: bool = True
    manifest: dict[str, Any]


@router.get("")
async def list_manifests(db_id: str | None = None, limit: int = 50):
    try:
        manifests = await _service.list_manifests(db_id=db_id, limit=limit)
        return {"manifests": manifests}
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))


@router.post("/project")
async def project_manifest(body: SceneRequest):
    try:
        manifest = await _service.build_manifest(body)
        return manifest.model_dump(mode="json")
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.post("")
async def save_manifest(body: SaveManifestRequest):
    try:
        manifest = Manifest.model_validate(body.manifest)
        await _service.composite.persist_manifest(manifest, db_id=body.db_id, persist_to_graph=body.persist_to_graph)
        return manifest.model_dump(mode="json")
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/{manifest_id}")
async def get_manifest(manifest_id: str, db_id: str | None = None):
    try:
        manifest = await _service.get_manifest(manifest_id, db_id=db_id)
        if manifest is None:
            raise HTTPException(status_code=404, detail=f"Manifest '{manifest_id}' not found")
        return manifest
    except HTTPException:
        raise
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))