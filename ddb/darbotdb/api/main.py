from fastapi import FastAPI
from uvicorn import run as uvicorn_run
from darbotdb.config import settings
from darbotdb.api.routers import health, aql, collections
from darbotdb.api.routers import micro, cards_router, memory, graph_router, triad_router
from darbotdb.api.routers import agui_router, txt2kg_router
from darbotdb.api.routers import graph3d_router

app = FastAPI(title="DarbotDB API", version="0.4.0")

# Core DarbotDB routers backed by the DDB graph engine
app.include_router(health.router, prefix="/health", tags=["health"])
app.include_router(aql.router, prefix="/v1/aql", tags=["aql"])
app.include_router(collections.router, prefix="/v1", tags=["collections"])

# DDB extension routers
app.include_router(micro.router, prefix="/v1/micro", tags=["micro"])
app.include_router(cards_router.router, prefix="/v1/cards", tags=["cards"])
app.include_router(memory.router, prefix="/v1/memory", tags=["memory"])
app.include_router(graph_router.router, prefix="/v1/graph", tags=["graph"])
app.include_router(triad_router.router, prefix="/v1/triad", tags=["triad"])

# 3D knowledge graph
app.include_router(graph3d_router.router, prefix="/v1/3dkg", tags=["3dkg"])

# AG-UI protocol (SSE streaming agent sessions)
app.include_router(agui_router.router, prefix="/v1/agui", tags=["agui"])

# txt2kg knowledge graph bridge (darbot@10.1.8.69)
app.include_router(txt2kg_router.router, prefix="/v1/txt2kg", tags=["txt2kg"])


def run():
    uvicorn_run("darbotdb.api.main:app", host="0.0.0.0", port=settings.API_PORT, reload=False)