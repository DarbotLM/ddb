"""Data models for txt2kg knowledge graph triples and extraction."""

from __future__ import annotations

from datetime import datetime, timezone
from typing import Any

from pydantic import BaseModel, Field


class TripleMetadata(BaseModel):
    """Metadata attached to an extracted triple."""
    entity_types: list[str] = Field(default_factory=list, alias="entityTypes")
    source: str = ""
    context: str = ""
    extraction_method: str = Field(default="ollama", alias="extractionMethod")
    model: str = "qwen3:32b"

    model_config = {"populate_by_name": True}


class Triple(BaseModel):
    """A subject-predicate-object knowledge graph triple."""
    subject: str
    predicate: str
    object: str
    confidence: float = 0.0
    metadata: TripleMetadata = Field(default_factory=TripleMetadata)


class ExtractionRequest(BaseModel):
    """Request to extract triples from text."""
    text: str
    model: str = "qwen3:32b"
    chunk_size: int = 1000
    chunk_overlap: int = 200


class ExtractionResult(BaseModel):
    """Result of triple extraction from a text document."""
    triples: list[Triple] = Field(default_factory=list)
    source_text: str = ""
    model: str = ""
    duration_ms: int = 0
    chunk_count: int = 0


class RAGQuery(BaseModel):
    """Request for RAG (Retrieval-Augmented Generation) search."""
    query: str
    top_k: int = 5
    include_context: bool = True


class RAGResult(BaseModel):
    """Result of a RAG search."""
    answer: str = ""
    sources: list[dict[str, Any]] = Field(default_factory=list)
    triples_used: list[Triple] = Field(default_factory=list)


class GraphStats(BaseModel):
    """Statistics about the txt2kg knowledge graph."""
    triple_count: int = 0
    entity_count: int = 0
    relationship_count: int = 0
    collections: list[str] = Field(default_factory=list)
