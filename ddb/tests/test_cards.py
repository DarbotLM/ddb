"""Tests for the DDB adaptive card module."""

import json

import pytest

from cards.builder import CardBuilder
from cards.schema import AdaptiveCard, CardType, DDBMeta, LinkType
from cards.templates import (
    index_card,
    memory_card,
    observation_card,
    pattern_card,
    task_card,
)
from cards.validator import compute_hash, validate_card, verify_hash


# -- builder -----------------------------------------------------------------

def test_card_builder():
    card = (
        CardBuilder()
        .card_type(CardType.MEMORY)
        .title("API Rate Limiting")
        .fact("Confidence", "0.87")
        .fact("Evidence", "12 observations")
        .text("Pattern detected during load test.")
        .tag("api", "pattern")
        .zone("engineering")
        .agent("agent-1")
        .action_execute("Recall", "ddb.recall", {"depth": 3})
        .build()
    )
    assert card.type == "AdaptiveCard"
    assert card.ddb.card_type == CardType.MEMORY
    assert card.ddb.zone == "engineering"
    assert len(card.body) == 3  # title + text + factset
    assert len(card.actions) == 1
    assert card.ddb.hash is not None


def test_card_builder_chain():
    card = CardBuilder().card_type(CardType.TASK).title("Do something").build()
    assert card.ddb.card_type == CardType.TASK
    assert len(card.body) == 1


# -- JSON roundtrip ----------------------------------------------------------

def test_card_json_roundtrip():
    card = memory_card("Test Memory", "Content here", confidence=0.95, evidence_count=5)
    agent_json = card.to_agent_json()
    assert agent_json["type"] == "AdaptiveCard"
    assert agent_json["$schema"] == "http://adaptivecards.io/schemas/adaptive-card.json"
    assert "_ddb" in agent_json
    assert agent_json["_ddb"]["card_type"] == "memory"

    # roundtrip via JSON string
    raw = json.dumps(agent_json, default=str)
    parsed = json.loads(raw)
    card2 = AdaptiveCard.model_validate(parsed)
    assert card2.ddb.card_type == CardType.MEMORY


# -- templates ---------------------------------------------------------------

def test_memory_template():
    card = memory_card("Pattern X", "Details about pattern X", confidence=0.9, evidence_count=7, zone="ml")
    assert card.ddb.card_type == CardType.MEMORY
    assert card.ddb.zone == "ml"
    assert len(card.actions) >= 3  # recall, link, compose


def test_task_template():
    card = task_card("Fix bug", "Fix the auth bug", status="in_progress", assignee="alice")
    assert card.ddb.card_type == CardType.TASK
    assert len(card.actions) >= 4  # standard + mark complete


def test_observation_template():
    card = observation_card("API is slow", "Response times > 500ms", perspective="observer")
    assert card.ddb.card_type == CardType.OBSERVATION


def test_pattern_template():
    card = pattern_card(
        "Rate Limit Pattern",
        "APIs throttle at 100 req/s",
        evidence_cards=["card-1", "card-2"],
        confidence=0.85,
    )
    assert card.ddb.card_type == CardType.PATTERN
    assert len(card.body) >= 3


def test_index_template():
    card = index_card(
        "Engineering Index",
        referenced_cards=["card-a", "card-b", "card-c"],
        cross_refs={"API": "card-a", "Auth": "card-b"},
    )
    assert card.ddb.card_type == CardType.INDEX


# -- validation --------------------------------------------------------------

def test_validation_valid():
    card = memory_card("Valid Card", "All good", confidence=1.0, evidence_count=1)
    errors = validate_card(card)
    assert errors == []


def test_validation_missing_body():
    meta = DDBMeta(card_type=CardType.MEMORY)
    card = AdaptiveCard(**{"_ddb": meta}, body=[], actions=[])
    errors = validate_card(card)
    assert any("body is empty" in e for e in errors)


def test_validation_size_limit():
    card = (
        CardBuilder()
        .card_type(CardType.MEMORY)
        .title("Huge card")
        .text("x" * 30_000)
        .build()
    )
    errors = validate_card(card)
    assert any("28KB" in e for e in errors)


def test_validation_bad_link_scheme():
    card = (
        CardBuilder()
        .card_type(CardType.MEMORY)
        .title("Bad Link")
        .link(LinkType.EXTERNAL, "ftp://invalid")
        .build()
    )
    errors = validate_card(card)
    assert any("Invalid link URI" in e for e in errors)


# -- hash integrity ----------------------------------------------------------

def test_hash_integrity():
    card = memory_card("Hash Test", "Content", confidence=0.5, evidence_count=1)
    assert card.ddb.hash is not None
    assert verify_hash(card)
    # recompute matches
    assert compute_hash(card) == card.ddb.hash


def test_hash_detects_tampering():
    card = memory_card("Tamper Test", "Original content", confidence=0.5, evidence_count=1)
    assert verify_hash(card)
    # tamper with body
    card.body[0].text = "Tampered!"  # type: ignore[union-attr]
    assert not verify_hash(card)


# -- human summary -----------------------------------------------------------

def test_human_summary():
    card = memory_card("Summary Test", "Important details", confidence=0.9, evidence_count=3, tags=["test"])
    md = card.to_human_summary()
    assert "Summary Test" in md
    assert "memory" in md.lower()
    assert "0.90" in md
    assert "test" in md


# -- agent json --------------------------------------------------------------

def test_agent_json():
    card = task_card("Agent JSON Test", "Validate schema output", status="pending")
    j = card.to_agent_json()
    assert j["type"] == "AdaptiveCard"
    assert j["version"] == "1.5"
    assert "$schema" in j
    assert "_ddb" in j
    assert j["_ddb"]["schema_version"] == "v1.2.0"
