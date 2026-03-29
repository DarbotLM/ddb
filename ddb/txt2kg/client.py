"""txt2kg HTTP client — connects to the txt2kg service on darbot@10.1.8.69."""

from __future__ import annotations

import json
from typing import Any
from dataclasses import dataclass, field

import httpx

from txt2kg.models import (
    ExtractionRequest,
    ExtractionResult,
    GraphStats,
    RAGQuery,
    RAGResult,
    Triple,
)


@dataclass
class Txt2KGConfig:
    """Connection config for the txt2kg service."""
    base_url: str = "http://10.1.8.69:3001"
    ddb_url: str = "http://10.1.8.69:8529"
    ddb_name: str = "txt2kg"
    ollama_url: str = "http://10.1.8.69:11434"
    default_model: str = "qwen3:32b"
    timeout: float = 1800.0  # 30 min — txt2kg can be slow for large docs


class Txt2KGClient:
    """HTTP client for the txt2kg knowledge graph service.

    Connects to the txt2kg Next.js app running on darbot@10.1.8.69 (spark-804f).

    Usage::

        client = Txt2KGClient()
        # Extract triples from text
        result = await client.extract("The quick brown fox jumps over the lazy dog.")
        # Store in graph
        await client.store_triples(result.triples)
        # RAG search
        answer = await client.rag_search("What does the fox do?")
    """

    def __init__(self, config: Txt2KGConfig | None = None) -> None:
        self.config = config or Txt2KGConfig()
        self._http = httpx.AsyncClient(
            base_url=self.config.base_url,
            timeout=self.config.timeout,
            headers={"Content-Type": "application/json"},
        )

    async def close(self) -> None:
        await self._http.aclose()

    async def __aenter__(self) -> Txt2KGClient:
        return self

    async def __aexit__(self, *exc: object) -> None:
        await self.close()

    # -- Health checks -------------------------------------------------------

    async def test_connection(self) -> dict[str, Any]:
        """Test connectivity to txt2kg and Ollama."""
        resp = await self._http.get("/api/ollama", params={"action": "test-connection"})
        resp.raise_for_status()
        return resp.json()

    async def backend_status(self) -> dict[str, Any]:
        """Get txt2kg backend status."""
        resp = await self._http.get("/api/backend")
        resp.raise_for_status()
        return resp.json()

    # -- Triple extraction ---------------------------------------------------

    async def extract(
        self,
        text: str,
        model: str | None = None,
    ) -> ExtractionResult:
        """Extract knowledge graph triples from text using the Ollama LLM pipeline.

        :param text: Source text to extract triples from.
        :param model: LLM model to use (default: qwen3:32b).
        :returns: Extraction result with triples.
        """
        payload = {
            "text": text,
            "model": model or self.config.default_model,
        }
        resp = await self._http.post("/api/extract-triples/route", json=payload)
        resp.raise_for_status()
        data = resp.json()

        triples = [Triple(**t) if isinstance(t, dict) else t for t in data.get("triples", [])]
        return ExtractionResult(
            triples=triples,
            source_text=text[:200],
            model=model or self.config.default_model,
            duration_ms=data.get("duration_ms", 0),
            chunk_count=data.get("chunk_count", 0),
        )

    async def extract_batch(
        self,
        documents: list[dict[str, str]],
        model: str | None = None,
    ) -> list[ExtractionResult]:
        """Batch extract triples from multiple documents.

        :param documents: List of {"text": "...", "name": "..."} dicts.
        :param model: LLM model override.
        :returns: List of extraction results.
        """
        payload = {
            "documents": documents,
            "model": model or self.config.default_model,
        }
        resp = await self._http.post("/api/ollama/batch", json=payload)
        resp.raise_for_status()
        data = resp.json()

        results = []
        for item in data.get("results", [data]):
            triples = [Triple(**t) if isinstance(t, dict) else t for t in item.get("triples", [])]
            results.append(ExtractionResult(
                triples=triples,
                source_text=item.get("source", "")[:200],
                model=model or self.config.default_model,
            ))
        return results

    # -- Graph operations ----------------------------------------------------

    async def store_triples(self, triples: list[Triple]) -> dict[str, Any]:
        """Store extracted triples in the txt2kg DarbotDB graph.

        :param triples: Triples to store.
        :returns: Storage result.
        """
        payload = {
            "triples": [t.model_dump(by_alias=True) for t in triples],
        }
        resp = await self._http.post("/api/graph-db/triples", json=payload)
        resp.raise_for_status()
        return resp.json()

    async def query_graph(self, query: str | None = None) -> dict[str, Any]:
        """Query the txt2kg graph database.

        :param query: Optional AQL-like query.
        :returns: Query results.
        """
        if query:
            resp = await self._http.post("/api/graph-db", json={"query": query})
        else:
            resp = await self._http.get("/api/graph-db")
        resp.raise_for_status()
        return resp.json()

    async def graph_stats(self) -> GraphStats:
        """Get statistics about the knowledge graph."""
        data = await self.query_graph()
        return GraphStats(
            triple_count=data.get("tripleCount", data.get("triple_count", 0)),
            entity_count=data.get("entityCount", data.get("entity_count", 0)),
            relationship_count=data.get("relationshipCount", data.get("relationship_count", 0)),
            collections=data.get("collections", []),
        )

    async def clear_graph(self) -> dict[str, Any]:
        """Clear the entire txt2kg graph. Use with extreme caution."""
        resp = await self._http.delete("/api/graph-db/clear")
        resp.raise_for_status()
        return resp.json()

    # -- RAG search ----------------------------------------------------------

    async def rag_search(self, query: str, top_k: int = 5) -> RAGResult:
        """Semantic search over the knowledge graph using RAG.

        :param query: Natural language query.
        :param top_k: Number of top results.
        :returns: RAG result with answer and sources.
        """
        payload = {"query": query, "top_k": top_k}
        resp = await self._http.post("/api/rag", json=payload)
        resp.raise_for_status()
        data = resp.json()
        return RAGResult(
            answer=data.get("answer", ""),
            sources=data.get("sources", []),
        )

    async def rag_answer(self, query: str) -> str:
        """Get a direct answer from the knowledge graph via RAG.

        :param query: Natural language question.
        :returns: Answer string.
        """
        payload = {"query": query}
        resp = await self._http.post("/api/rag/answer", json=payload)
        resp.raise_for_status()
        data = resp.json()
        return data.get("answer", "")

    # -- Document upload -----------------------------------------------------

    async def upload_document(
        self,
        content: str,
        filename: str = "document.txt",
        model: str | None = None,
    ) -> ExtractionResult:
        """Upload a document for processing through the full txt2kg pipeline.

        Pipeline: chunk → extract triples → store → index embeddings.

        :param content: Document text content.
        :param filename: Document filename.
        :param model: LLM model override.
        :returns: Extraction result.
        """
        payload = {
            "content": content,
            "filename": filename,
            "model": model or self.config.default_model,
        }
        resp = await self._http.post("/api/document-data", json=payload)
        resp.raise_for_status()
        data = resp.json()
        triples = [Triple(**t) if isinstance(t, dict) else t for t in data.get("triples", [])]
        return ExtractionResult(
            triples=triples,
            source_text=content[:200],
            model=model or self.config.default_model,
        )

    # -- Settings ------------------------------------------------------------

    async def get_settings(self) -> dict[str, Any]:
        """Get current txt2kg service settings."""
        resp = await self._http.get("/api/settings")
        resp.raise_for_status()
        return resp.json()

    async def update_settings(self, settings: dict[str, Any]) -> dict[str, Any]:
        """Update txt2kg service settings."""
        resp = await self._http.post("/api/settings", json=settings)
        resp.raise_for_status()
        return resp.json()

    # -- Ollama model management ---------------------------------------------

    async def list_models(self) -> list[dict[str, Any]]:
        """List available Ollama models on the txt2kg host."""
        ollama = httpx.AsyncClient(base_url=self.config.ollama_url, timeout=30)
        try:
            resp = await ollama.get("/api/tags")
            resp.raise_for_status()
            return resp.json().get("models", [])
        finally:
            await ollama.aclose()
