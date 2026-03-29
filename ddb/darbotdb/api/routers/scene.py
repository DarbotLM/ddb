"""API routes for 3DKG scene materialization."""

from __future__ import annotations

from fastapi import APIRouter, HTTPException

from darbotdb.session.models import SceneRequest
from darbotdb.session.service import SessionService

router = APIRouter()
_service = SessionService()


@router.post("/materialize")
async def materialize_scene(body: SceneRequest):
    try:
        manifest = await _service.build_manifest(body)
        return manifest.to_scene_json()
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))


@router.get("/{manifest_id}")
async def get_scene(manifest_id: str, db_id: str | None = None):
    try:
        return await _service.materialize_scene(manifest_id, db_id=db_id)
    except FileNotFoundError as e:
        raise HTTPException(status_code=404, detail=str(e))
    except Exception as e:
        raise HTTPException(status_code=500, detail=str(e))