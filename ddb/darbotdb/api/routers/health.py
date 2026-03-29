from fastapi import APIRouter, Depends
from arango import ArangoClient
from darbotdb.config import settings

router = APIRouter()

def get_db():
    client = ArangoClient(hosts=settings.DDB_HOSTS)
    return client.db(settings.DDB_DATABASE, username=settings.DDB_USERNAME, password=settings.DDB_PASSWORD)

@router.get("/")
def health(db = Depends(get_db)):
    # simple round trip: list collections
    _ = db.collections()
    return {"ok": db.name}
