from fastapi import APIRouter, HTTPException
from pydantic import BaseModel, Field
from arango import ArangoClient
from darbotdb.config import settings

router = APIRouter()

class CreateCollection(BaseModel):
    name: str
    type: str = "document"  # "document" | "edge"
    key_options: dict | None = None
    schema_: dict | None = Field(None, alias="schema")

@router.post("/db/{db_name}/collections")
def create_collection(db_name: str, body: CreateCollection):
    client = ArangoClient(hosts=settings.DDB_HOSTS)
    db = client.db(db_name, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)
    try:
        edge = body.type == "edge"
        col = db.create_collection(body.name, edge=edge, key_options=body.key_options, schema=body.schema_)
        return {"name": col.name, "type": body.type}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
