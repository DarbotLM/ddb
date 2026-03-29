"""SQLite schema definitions for DDB micro databases (.ddb files)."""

SCHEMA_VERSION = "v1.2.0"

CREATE_META = """
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
"""

CREATE_TURNS = """
CREATE TABLE IF NOT EXISTS turns (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    conversation_id TEXT    NOT NULL,
    turn_number     INTEGER NOT NULL,
    timestamp_utc   TEXT    NOT NULL,
    role            TEXT    NOT NULL,
    content         TEXT    NOT NULL,
    model           TEXT,
    tools_used      TEXT,
    hash            TEXT,
    schema_version  TEXT    DEFAULT 'v1.2.0',
    UNIQUE(conversation_id, turn_number)
);
"""
CREATE_TURNS_IDX_CONV = "CREATE INDEX IF NOT EXISTS idx_turns_conv ON turns(conversation_id);"
CREATE_TURNS_IDX_TS = "CREATE INDEX IF NOT EXISTS idx_turns_ts ON turns(timestamp_utc);"

CREATE_CARDS = """
CREATE TABLE IF NOT EXISTS cards (
    id             TEXT PRIMARY KEY,
    card_type      TEXT NOT NULL,
    title          TEXT NOT NULL,
    schema_json    TEXT NOT NULL,
    content_md     TEXT,
    embedding      BLOB,
    parent_card_id TEXT,
    tags           TEXT,
    zone           TEXT,
    created_at     TEXT NOT NULL,
    updated_at     TEXT NOT NULL,
    expires_at     TEXT,
    hash           TEXT,
    FOREIGN KEY (parent_card_id) REFERENCES cards(id)
);
"""
CREATE_CARDS_IDX_TYPE = "CREATE INDEX IF NOT EXISTS idx_cards_type ON cards(card_type);"
CREATE_CARDS_IDX_ZONE = "CREATE INDEX IF NOT EXISTS idx_cards_zone ON cards(zone);"
CREATE_CARDS_IDX_TAGS = "CREATE INDEX IF NOT EXISTS idx_cards_tags ON cards(tags);"
CREATE_CARDS_IDX_PARENT = "CREATE INDEX IF NOT EXISTS idx_cards_parent ON cards(parent_card_id);"

CREATE_CARDS_FTS = """
CREATE VIRTUAL TABLE IF NOT EXISTS cards_fts USING fts5(
    title,
    content_md,
    tags,
    content='cards',
    content_rowid='rowid'
);
"""
CREATE_CARDS_FTS_INSERT_TRIGGER = """
CREATE TRIGGER IF NOT EXISTS cards_ai AFTER INSERT ON cards BEGIN
    INSERT INTO cards_fts(rowid, title, content_md, tags)
    VALUES (new.rowid, new.title, new.content_md, new.tags);
END;
"""
CREATE_CARDS_FTS_DELETE_TRIGGER = """
CREATE TRIGGER IF NOT EXISTS cards_ad AFTER DELETE ON cards BEGIN
    INSERT INTO cards_fts(cards_fts, rowid, title, content_md, tags)
    VALUES ('delete', old.rowid, old.title, old.content_md, old.tags);
END;
"""
CREATE_CARDS_FTS_UPDATE_TRIGGER = """
CREATE TRIGGER IF NOT EXISTS cards_au AFTER UPDATE ON cards BEGIN
    INSERT INTO cards_fts(cards_fts, rowid, title, content_md, tags)
    VALUES ('delete', old.rowid, old.title, old.content_md, old.tags);
    INSERT INTO cards_fts(rowid, title, content_md, tags)
    VALUES (new.rowid, new.title, new.content_md, new.tags);
END;
"""

CREATE_THOUGHTS = """
CREATE TABLE IF NOT EXISTS thoughts (
    id                 INTEGER PRIMARY KEY AUTOINCREMENT,
    thought_number     INTEGER NOT NULL,
    total_thoughts     INTEGER NOT NULL,
    perspective        TEXT    NOT NULL,
    thought            TEXT    NOT NULL,
    assumptions        TEXT,
    observations       TEXT,
    verification_level TEXT    NOT NULL,
    is_revision        INTEGER DEFAULT 0,
    revises_thought    INTEGER,
    branch_id          TEXT,
    timestamp_utc      TEXT    NOT NULL
);
"""
CREATE_THOUGHTS_IDX_PERSPECTIVE = "CREATE INDEX IF NOT EXISTS idx_thoughts_perspective ON thoughts(perspective);"
CREATE_THOUGHTS_IDX_BRANCH = "CREATE INDEX IF NOT EXISTS idx_thoughts_branch ON thoughts(branch_id);"

CREATE_TOOL_CALLS = """
CREATE TABLE IF NOT EXISTS tool_calls (
    id            INTEGER PRIMARY KEY AUTOINCREMENT,
    turn_id       INTEGER,
    tool_name     TEXT NOT NULL,
    input_json    TEXT,
    output_json   TEXT,
    duration_ms   INTEGER,
    timestamp_utc TEXT NOT NULL,
    FOREIGN KEY (turn_id) REFERENCES turns(id)
);
"""
CREATE_TOOL_CALLS_IDX_NAME = "CREATE INDEX IF NOT EXISTS idx_tool_calls_name ON tool_calls(tool_name);"
CREATE_TOOL_CALLS_IDX_TURN = "CREATE INDEX IF NOT EXISTS idx_tool_calls_turn ON tool_calls(turn_id);"

CREATE_LINKS = """
CREATE TABLE IF NOT EXISTS links (
    id          INTEGER PRIMARY KEY AUTOINCREMENT,
    link_type   TEXT NOT NULL,
    source_id   TEXT NOT NULL,
    target_uri  TEXT NOT NULL,
    metadata    TEXT,
    created_at  TEXT NOT NULL
);
"""
CREATE_LINKS_IDX_TYPE = "CREATE INDEX IF NOT EXISTS idx_links_type ON links(link_type);"
CREATE_LINKS_IDX_SOURCE = "CREATE INDEX IF NOT EXISTS idx_links_source ON links(source_id);"

CREATE_EVENTS = """
CREATE TABLE IF NOT EXISTS events (
    id                       INTEGER PRIMARY KEY AUTOINCREMENT,
    event_type               TEXT    NOT NULL,
    source_agent             TEXT,
    payload_json             TEXT,
    triad_status             TEXT    NOT NULL DEFAULT 'pending',
    observer_thoughts        INTEGER NOT NULL DEFAULT 0,
    orchestrator_thoughts    INTEGER NOT NULL DEFAULT 0,
    synthesizer_thoughts     INTEGER NOT NULL DEFAULT 0,
    new_cards                INTEGER NOT NULL DEFAULT 0,
    error_message            TEXT,
    processed_at             TEXT,
    timestamp_utc            TEXT    NOT NULL
);
"""
CREATE_EVENTS_IDX_TYPE = "CREATE INDEX IF NOT EXISTS idx_events_type ON events(event_type);"
CREATE_EVENTS_IDX_AGENT = "CREATE INDEX IF NOT EXISTS idx_events_agent ON events(source_agent);"
CREATE_EVENTS_IDX_STATUS = "CREATE INDEX IF NOT EXISTS idx_events_status ON events(triad_status);"

CREATE_TRIPLES = """
CREATE TABLE IF NOT EXISTS triples (
    id              INTEGER PRIMARY KEY AUTOINCREMENT,
    subject         TEXT    NOT NULL,
    predicate       TEXT    NOT NULL,
    object          TEXT    NOT NULL,
    confidence      REAL    NOT NULL DEFAULT 1.0,
    source_card_id  TEXT,
    source_event_id INTEGER,
    model           TEXT,
    zone            TEXT,
    graph_key       TEXT,
    created_at      TEXT    NOT NULL,
    FOREIGN KEY (source_card_id) REFERENCES cards(id),
    FOREIGN KEY (source_event_id) REFERENCES events(id)
);
"""
CREATE_TRIPLES_IDX_SUBJECT = "CREATE INDEX IF NOT EXISTS idx_triples_subject ON triples(subject);"
CREATE_TRIPLES_IDX_PREDICATE = "CREATE INDEX IF NOT EXISTS idx_triples_predicate ON triples(predicate);"
CREATE_TRIPLES_IDX_OBJECT = "CREATE INDEX IF NOT EXISTS idx_triples_object ON triples(object);"
CREATE_TRIPLES_IDX_ZONE = "CREATE INDEX IF NOT EXISTS idx_triples_zone ON triples(zone);"
CREATE_TRIPLES_IDX_GRAPH_KEY = "CREATE INDEX IF NOT EXISTS idx_triples_graph_key ON triples(graph_key);"

CREATE_EMBEDDINGS = """
CREATE TABLE IF NOT EXISTS embeddings (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    source_id    TEXT    NOT NULL,
    source_type  TEXT    NOT NULL,
    model        TEXT    NOT NULL,
    dimensions   INTEGER NOT NULL,
    vector       BLOB    NOT NULL,
    created_at   TEXT    NOT NULL
);
"""
CREATE_EMBEDDINGS_IDX_SOURCE = "CREATE INDEX IF NOT EXISTS idx_embeddings_source ON embeddings(source_id, source_type);"
CREATE_EMBEDDINGS_IDX_MODEL = "CREATE INDEX IF NOT EXISTS idx_embeddings_model ON embeddings(model);"
CREATE_EMBEDDINGS_IDX_UNIQUE = "CREATE UNIQUE INDEX IF NOT EXISTS idx_embeddings_unique ON embeddings(source_id, source_type, model);"

CREATE_MANIFESTS = """
CREATE TABLE IF NOT EXISTS manifests (
    id             TEXT PRIMARY KEY,
    title          TEXT NOT NULL,
    description    TEXT,
    session_id     TEXT,
    source_db_id   TEXT,
    source_kind    TEXT,
    schema_version TEXT NOT NULL,
    layout         TEXT,
    manifest_json  TEXT NOT NULL,
    created_at     TEXT NOT NULL,
    updated_at     TEXT NOT NULL
);
"""
CREATE_MANIFESTS_IDX_SESSION = "CREATE INDEX IF NOT EXISTS idx_manifests_session ON manifests(session_id);"
CREATE_MANIFESTS_IDX_SOURCE_DB = "CREATE INDEX IF NOT EXISTS idx_manifests_source_db ON manifests(source_db_id);"

CREATE_MANIFEST_NODES = """
CREATE TABLE IF NOT EXISTS manifest_nodes (
    id                TEXT PRIMARY KEY,
    manifest_id       TEXT NOT NULL,
    entity_id         TEXT NOT NULL,
    entity_kind       TEXT NOT NULL,
    label             TEXT NOT NULL,
    layer             TEXT,
    node_type         TEXT,
    inspector_card_id TEXT,
    spatial_json      TEXT,
    metadata_json     TEXT,
    created_at        TEXT NOT NULL,
    FOREIGN KEY (manifest_id) REFERENCES manifests(id) ON DELETE CASCADE,
    FOREIGN KEY (inspector_card_id) REFERENCES cards(id)
);
"""
CREATE_MANIFEST_NODES_IDX_MANIFEST = "CREATE INDEX IF NOT EXISTS idx_manifest_nodes_manifest ON manifest_nodes(manifest_id);"
CREATE_MANIFEST_NODES_IDX_ENTITY = "CREATE INDEX IF NOT EXISTS idx_manifest_nodes_entity ON manifest_nodes(entity_id, entity_kind);"

CREATE_MANIFEST_EDGES = """
CREATE TABLE IF NOT EXISTS manifest_edges (
    id             TEXT PRIMARY KEY,
    manifest_id    TEXT NOT NULL,
    source_node_id TEXT NOT NULL,
    target_node_id TEXT NOT NULL,
    relation       TEXT NOT NULL,
    edge_type      TEXT,
    label          TEXT,
    metadata_json  TEXT,
    created_at     TEXT NOT NULL,
    FOREIGN KEY (manifest_id) REFERENCES manifests(id) ON DELETE CASCADE
);
"""
CREATE_MANIFEST_EDGES_IDX_MANIFEST = "CREATE INDEX IF NOT EXISTS idx_manifest_edges_manifest ON manifest_edges(manifest_id);"
CREATE_MANIFEST_EDGES_IDX_SOURCE_TARGET = "CREATE INDEX IF NOT EXISTS idx_manifest_edges_source_target ON manifest_edges(source_node_id, target_node_id);"

CREATE_SESSION_VIEWS = """
CREATE TABLE IF NOT EXISTS session_views (
    id             TEXT PRIMARY KEY,
    manifest_id    TEXT NOT NULL,
    title          TEXT NOT NULL,
    layout         TEXT NOT NULL,
    view_json      TEXT NOT NULL,
    created_at     TEXT NOT NULL,
    updated_at     TEXT NOT NULL,
    FOREIGN KEY (manifest_id) REFERENCES manifests(id) ON DELETE CASCADE
);
"""
CREATE_SESSION_VIEWS_IDX_MANIFEST = "CREATE INDEX IF NOT EXISTS idx_session_views_manifest ON session_views(manifest_id);"


# ---------------------------------------------------------------------------
# Table: node_positions  --  3D spatial coordinates (v1.2.0)
# ---------------------------------------------------------------------------
CREATE_NODE_POSITIONS = """
CREATE TABLE IF NOT EXISTS node_positions (
    node_id     TEXT NOT NULL,
    node_type   TEXT NOT NULL,
    x           REAL NOT NULL DEFAULT 0.0,
    y           REAL NOT NULL DEFAULT 0.0,
    z           REAL NOT NULL DEFAULT 0.0,
    updated_at  TEXT NOT NULL,
    PRIMARY KEY (node_id, node_type)
);
"""

CREATE_NODE_POSITIONS_IDX_TYPE = "CREATE INDEX IF NOT EXISTS idx_node_positions_type ON node_positions(node_type);"

CREATE_NODE_POSITIONS_IDX_XYZ = "CREATE INDEX IF NOT EXISTS idx_node_positions_xyz ON node_positions(x, y, z);"
ALL_DDL: list[str] = [
    CREATE_META,
    CREATE_TURNS,
    CREATE_TURNS_IDX_CONV,
    CREATE_TURNS_IDX_TS,
    CREATE_CARDS,
    CREATE_CARDS_IDX_TYPE,
    CREATE_CARDS_IDX_ZONE,
    CREATE_CARDS_IDX_TAGS,
    CREATE_CARDS_IDX_PARENT,
    CREATE_CARDS_FTS,
    CREATE_CARDS_FTS_INSERT_TRIGGER,
    CREATE_CARDS_FTS_DELETE_TRIGGER,
    CREATE_CARDS_FTS_UPDATE_TRIGGER,
    CREATE_THOUGHTS,
    CREATE_THOUGHTS_IDX_PERSPECTIVE,
    CREATE_THOUGHTS_IDX_BRANCH,
    CREATE_TOOL_CALLS,
    CREATE_TOOL_CALLS_IDX_NAME,
    CREATE_TOOL_CALLS_IDX_TURN,
    CREATE_LINKS,
    CREATE_LINKS_IDX_TYPE,
    CREATE_LINKS_IDX_SOURCE,
    CREATE_EVENTS,
    CREATE_EVENTS_IDX_TYPE,
    CREATE_EVENTS_IDX_AGENT,
    CREATE_EVENTS_IDX_STATUS,
    CREATE_TRIPLES,
    CREATE_TRIPLES_IDX_SUBJECT,
    CREATE_TRIPLES_IDX_PREDICATE,
    CREATE_TRIPLES_IDX_OBJECT,
    CREATE_TRIPLES_IDX_ZONE,
    CREATE_TRIPLES_IDX_GRAPH_KEY,
    CREATE_EMBEDDINGS,
    CREATE_EMBEDDINGS_IDX_SOURCE,
    CREATE_EMBEDDINGS_IDX_MODEL,
    CREATE_EMBEDDINGS_IDX_UNIQUE,
    CREATE_MANIFESTS,
    CREATE_MANIFESTS_IDX_SESSION,
    CREATE_MANIFESTS_IDX_SOURCE_DB,
    CREATE_MANIFEST_NODES,
    CREATE_MANIFEST_NODES_IDX_MANIFEST,
    CREATE_MANIFEST_NODES_IDX_ENTITY,
    CREATE_MANIFEST_EDGES,
    CREATE_MANIFEST_EDGES_IDX_MANIFEST,
    CREATE_MANIFEST_EDGES_IDX_SOURCE_TARGET,
    CREATE_SESSION_VIEWS,
    CREATE_SESSION_VIEWS_IDX_MANIFEST,
    # v1.2.0 additions
    CREATE_NODE_POSITIONS,
    CREATE_NODE_POSITIONS_IDX_TYPE,
    CREATE_NODE_POSITIONS_IDX_XYZ,
]

MIGRATIONS: dict[str, list[str]] = {
    "v1.1.0": [
        CREATE_EVENTS,
        CREATE_EVENTS_IDX_TYPE,
        CREATE_EVENTS_IDX_AGENT,
        CREATE_EVENTS_IDX_STATUS,
        CREATE_TRIPLES,
        CREATE_TRIPLES_IDX_SUBJECT,
        CREATE_TRIPLES_IDX_PREDICATE,
        CREATE_TRIPLES_IDX_OBJECT,
        CREATE_TRIPLES_IDX_ZONE,
        CREATE_TRIPLES_IDX_GRAPH_KEY,
        CREATE_EMBEDDINGS,
        CREATE_EMBEDDINGS_IDX_SOURCE,
        CREATE_EMBEDDINGS_IDX_MODEL,
        CREATE_EMBEDDINGS_IDX_UNIQUE,
    ],
    "v1.2.0": [
        CREATE_MANIFESTS,
        CREATE_MANIFESTS_IDX_SESSION,
        CREATE_MANIFESTS_IDX_SOURCE_DB,
        CREATE_MANIFEST_NODES,
        CREATE_MANIFEST_NODES_IDX_MANIFEST,
        CREATE_MANIFEST_NODES_IDX_ENTITY,
        CREATE_MANIFEST_EDGES,
        CREATE_MANIFEST_EDGES_IDX_MANIFEST,
        CREATE_MANIFEST_EDGES_IDX_SOURCE_TARGET,
        CREATE_SESSION_VIEWS,
        CREATE_SESSION_VIEWS_IDX_MANIFEST,
    ],
    "v1.2.0": [
        CREATE_NODE_POSITIONS,
        CREATE_NODE_POSITIONS_IDX_TYPE,
        CREATE_NODE_POSITIONS_IDX_XYZ,
    ],
}