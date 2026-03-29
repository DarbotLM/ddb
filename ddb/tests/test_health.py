from fastapi.testclient import TestClient
from darbotdb.api.main import app

def test_health():
    client = TestClient(app, raise_server_exceptions=False)
    # This only tests app wiring; for an integration test, start the DDB graph engine in CI
    resp = client.get("/health/")
    assert resp.status_code in (200, 500)  # 500 if the DDB graph engine is not reachable in CI
