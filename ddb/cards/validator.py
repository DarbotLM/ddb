"""Validation and integrity utilities for DDB Adaptive Cards."""

from __future__ import annotations

import hashlib
import json
import re

from cards.schema import AdaptiveCard


_SEMVER_RE = re.compile(r"^v?\d+\.\d+\.\d+$")
_VALID_URI_SCHEMES = ("ddb://", "darbotdb://", "https://", "http://")
_MAX_CARD_SIZE_BYTES = 28 * 1024  # 28 KB Microsoft limit


def compute_hash(card: AdaptiveCard) -> str:
    """Compute a SHA256 hash of the card body + actions (excluding the hash field itself)."""
    payload = card.model_dump(by_alias=True, exclude_none=True)
    # remove the hash from _ddb so it doesn't self-reference
    ddb = payload.get("_ddb", {})
    ddb.pop("hash", None)
    raw = json.dumps(payload, sort_keys=True, default=str)
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()


def verify_hash(card: AdaptiveCard) -> bool:
    """Verify that a card's stored hash matches the recomputed hash."""
    if card.ddb.hash is None:
        return False
    return card.ddb.hash == compute_hash(card)


def validate_card(card: AdaptiveCard) -> list[str]:
    """Validate a card. Returns a list of error strings (empty = valid)."""
    errors: list[str] = []

    # required identity
    if not card.ddb.id:
        errors.append("Card missing _ddb.id")
    if not card.ddb.card_type:
        errors.append("Card missing _ddb.card_type")

    # must have at least one body element
    if not card.body:
        errors.append("Card body is empty — at least one element required")

    # schema version must be semver
    if not _SEMVER_RE.match(card.ddb.schema_version):
        errors.append(f"Invalid schema_version: {card.ddb.schema_version!r} (expected semver)")

    # hash integrity
    if card.ddb.hash is not None:
        if len(card.ddb.hash) != 64:
            errors.append(f"Invalid hash length: {len(card.ddb.hash)} (expected 64 hex chars)")
        if not verify_hash(card):
            errors.append("Hash mismatch — card content has been modified")

    # size limit
    card_json = json.dumps(card.model_dump(by_alias=True, exclude_none=True), default=str)
    if len(card_json.encode("utf-8")) > _MAX_CARD_SIZE_BYTES:
        errors.append(f"Card exceeds 28KB limit ({len(card_json.encode('utf-8'))} bytes)")

    # link URI validation
    for link in card.ddb.links:
        if not any(link.uri.startswith(scheme) for scheme in _VALID_URI_SCHEMES):
            errors.append(f"Invalid link URI scheme: {link.uri!r} (expected ddb://, darbotdb://, https://)")

    return errors
