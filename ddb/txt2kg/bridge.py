"""Bridge between DDB adaptive cards and txt2kg knowledge graph.

Converts DDB cards → triples for ingestion, and triples → cards for recall.
This is the glue that lets the DDB micro DB ecosystem feed the txt2kg
knowledge graph and vice versa.
"""

from __future__ import annotations

from typing import Any

from cards.builder import CardBuilder
from cards.schema import AdaptiveCard, CardType, LinkType
from micro.engine import MicroDB
from txt2kg.client import Txt2KGClient
from txt2kg.models import Triple, ExtractionResult


class Txt2KGBridge:
    """Bidirectional bridge: DDB cards ↔ txt2kg triples."""

    def __init__(self, client: Txt2KGClient) -> None:
        self.client = client

    # -- Cards → Triples (push to knowledge graph) --------------------------

    async def cards_to_triples(self, micro: MicroDB) -> ExtractionResult:
        """Extract knowledge from all cards in a micro DB and push to txt2kg.

        Concatenates card content into text, runs through the LLM extraction
        pipeline, and stores resulting triples in the knowledge graph.
        """
        cards = await micro.execute(
            "SELECT title, content_md, card_type, zone FROM cards ORDER BY created_at"
        )
        if not cards:
            return ExtractionResult()

        # Build a document from card content
        lines: list[str] = []
        for card in cards:
            title = card.get("title", "")
            content = card.get("content_md", "")
            card_type = card.get("card_type", "")
            zone = card.get("zone", "")
            lines.append(f"[{card_type}] {title}")
            if zone:
                lines.append(f"Zone: {zone}")
            if content:
                lines.append(content)
            lines.append("")

        text = "\n".join(lines)
        result = await self.client.extract(text)

        # Store in graph
        if result.triples:
            await self.client.store_triples(result.triples)

        return result

    async def card_to_triples(self, card: dict[str, Any]) -> list[Triple]:
        """Extract triples from a single card's content."""
        text = f"{card.get('title', '')}. {card.get('content_md', '')}"
        if not text.strip(". "):
            return []
        result = await self.client.extract(text)
        return result.triples

    # -- Triples → Cards (pull from knowledge graph) ------------------------

    def triple_to_card(self, triple: Triple, zone: str | None = None) -> AdaptiveCard:
        """Convert a single knowledge graph triple into a DDB adaptive card."""
        card = (
            CardBuilder()
            .card_type(CardType.OBSERVATION)
            .title(f"{triple.subject} → {triple.predicate} → {triple.object}")
            .fact("Subject", triple.subject)
            .fact("Predicate", triple.predicate)
            .fact("Object", triple.object)
            .fact("Confidence", f"{triple.confidence:.2f}")
            .fact("Model", triple.metadata.model)
            .tag("txt2kg", "triple", triple.predicate.lower().replace(" ", "-"))
            .link(LinkType.EXTERNAL, f"darbotdb://txt2kg/triples/{triple.subject}")
        )
        if zone:
            card = card.zone(zone)
        if triple.metadata.source:
            card = card.text(f"Source: {triple.metadata.source[:200]}")
        return card.action_execute("Explore in Graph", "ddb.graph.explore", {
            "start_key": triple.subject,
        }).build()

    async def import_triples_as_cards(
        self,
        micro: MicroDB,
        query: str | None = None,
        zone: str = "txt2kg",
    ) -> int:
        """Pull triples from txt2kg and store as DDB cards in a micro DB.

        :param micro: Target micro DB.
        :param query: Optional graph query filter.
        :param zone: Zone to assign cards to.
        :returns: Number of cards created.
        """
        data = await self.client.query_graph(query)
        triples_data = data.get("triples", data.get("results", []))

        count = 0
        for t_data in triples_data:
            triple = Triple(**t_data) if isinstance(t_data, dict) else t_data
            card = self.triple_to_card(triple, zone=zone)
            await micro.insert_card({
                "id": card.ddb.id,
                "card_type": card.ddb.card_type.value,
                "title": next(
                    (b.text for b in card.body if hasattr(b, "weight") and b.weight == "Bolder"),
                    f"{triple.subject} → {triple.object}",
                ),
                "schema_json": card.to_agent_json(),
                "content_md": card.to_human_summary(),
                "tags": card.ddb.tags,
                "zone": zone,
            })
            count += 1

        return count

    # -- RAG-powered memory recall ------------------------------------------

    async def recall_from_kg(self, query: str) -> list[AdaptiveCard]:
        """Use txt2kg RAG to recall relevant knowledge as adaptive cards."""
        result = await self.client.rag_search(query, top_k=10)
        cards: list[AdaptiveCard] = []

        if result.answer:
            card = (
                CardBuilder()
                .card_type(CardType.MEMORY)
                .title(f"KG Recall: {query[:60]}")
                .text(result.answer)
                .tag("txt2kg", "rag", "recall")
                .zone("txt2kg")
                .action_execute("Deep Dive", "ddb.memory.recall", {"query": query, "depth": 5})
                .build()
            )
            cards.append(card)

        for source in result.sources[:5]:
            card = (
                CardBuilder()
                .card_type(CardType.OBSERVATION)
                .title(source.get("title", source.get("subject", "Source")))
                .text(source.get("text", source.get("context", "")))
                .tag("txt2kg", "source")
                .zone("txt2kg")
                .build()
            )
            cards.append(card)

        return cards

    # -- Thoughts → Knowledge graph -----------------------------------------

    async def thoughts_to_kg(self, micro: MicroDB) -> ExtractionResult:
        """Extract knowledge graph triples from O/O/S thought logs.

        This powers "remember forward" — the synthesizer's patterns become
        searchable knowledge in the graph.
        """
        thoughts = await micro.get_thoughts()
        if not thoughts:
            return ExtractionResult()

        lines: list[str] = []
        for t in thoughts:
            perspective = t.get("perspective", "")
            thought_text = t.get("thought", "")
            lines.append(f"[{perspective}] {thought_text}")

        text = "\n".join(lines)
        result = await self.client.extract(text)

        if result.triples:
            await self.client.store_triples(result.triples)

        return result
