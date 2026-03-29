from fastapi import APIRouter, HTTPException, Depends
from pydantic import BaseModel
from arango import ArangoClient
from darbotdb.config import settings

router = APIRouter()

class AQLRequest(BaseModel):
    query: str
    bind_vars: dict | None = None
    batch_size: int | None = None

def get_db():
    client = ArangoClient(hosts=settings.DDB_HOSTS)
    return client.db(settings.DDB_DATABASE, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)

@router.post("")
def run_aql(payload: AQLRequest, db = Depends(get_db)):
    try:
        cursor = db.aql.execute(payload.query, bind_vars=payload.bind_vars or {}, batch_size=payload.batch_size)
        return {"results": list(cursor)}
    except Exception as e:
        raise HTTPException(status_code=400, detail=str(e))
