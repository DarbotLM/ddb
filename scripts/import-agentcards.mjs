#!/usr/bin/env node
/**
 * DarbotDB AgentCard Importer
 * Reads all AgentCards from OneDrive and imports them into the DDB graph.
 *
 * Usage: node scripts/import-agentcards.mjs [cards-dir]
 */

import { readFileSync, readdirSync, statSync, existsSync } from "fs";
import { join, basename } from "path";
import { createHash } from "crypto";

const DDB = process.env.DDB_URL || "http://10.1.8.69:8529";
const DB = process.env.DDB_NAME || "txt2kg";
const CARDS_DIR = process.argv[2] || "C:/Users/darbot/OneDrive/AgentCards";
const API = `${DDB}/_db/${DB}/_api/cursor`;

async function aql(query, bindVars = {}) {
  const resp = await fetch(API, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ query, bindVars }),
  });
  return resp.json();
}

function readJSON(path) {
  try {
    return JSON.parse(readFileSync(path, "utf-8"));
  } catch {
    return null;
  }
}

function sha256(str) {
  return createHash("sha256").update(str).digest("hex");
}

// ---------------------------------------------------------------
// Phase 2: Import root-level template cards (01-38)
// ---------------------------------------------------------------
async function importRootCards() {
  const index = readJSON(join(CARDS_DIR, "index.json"));
  const indexMap = new Map();
  if (index?.cards) {
    for (const entry of index.cards) indexMap.set(entry.id, entry);
  }

  const files = readdirSync(CARDS_DIR)
    .filter(f => /^\d+.*\.json$/.test(f) && f !== "index.json");

  let count = 0;
  for (const f of files) {
    const key = basename(f, ".json");
    const body = readJSON(join(CARDS_DIR, f));
    if (!body) continue;
    const meta = indexMap.get(key) || {};

    const result = await aql(
      `UPSERT { _key: @key }
       INSERT @doc
       UPDATE { schema_json: @schema, updated_at: DATE_ISO8601(DATE_NOW()), hash: @hash }
       IN cards RETURN NEW._key`,
      {
        key,
        hash: sha256(JSON.stringify(body)),
        schema: JSON.stringify(body),
        doc: {
          _key: key,
          card_type: "observation",
          kind: meta.kind || "template",
          title: meta.title || key,
          content_md: meta.use || meta.summary || "",
          summary: meta.summary || meta.use || meta.description || "",
          zone: "agentcards",
          agent_id: "agentcard-ingestor",
          tags: [...(meta.tags || []), "agentcard", "template"],
          layout: meta.layout || key,
          size: meta.size || "large",
          template_id: null,
          deck_id: null,
          deck_title: null,
          deck_position: null,
          elements: meta.elements || [],
          sources: meta.sources || [],
          route_to: meta.routeTo || [],
          related_cards: (meta.related || []).map(r => r.id || r),
          data_source: meta.dataSource || null,
          data_binding: meta.dataBinding || null,
          design_system: "Neon Pastel Blue / Fluent 2 Dark",
          ac_version: body.version || "1.5",
          schema_json: JSON.stringify(body),
          schema_version: "v1.0.0",
          hash: sha256(JSON.stringify(body)),
          created_at: new Date().toISOString(),
          updated_at: new Date().toISOString(),
        },
      }
    );
    count++;
  }
  return count;
}

// ---------------------------------------------------------------
// Phase 3: Import decks (index + member cards)
// ---------------------------------------------------------------
async function importDecks() {
  const dirs = readdirSync(CARDS_DIR).filter(d => {
    const p = join(CARDS_DIR, d);
    return statSync(p).isDirectory() && existsSync(join(p, "index.json"));
  });

  let deckCount = 0, memberCount = 0;

  for (const deckName of dirs) {
    const deckDir = join(CARDS_DIR, deckName);
    const deckIndex = readJSON(join(deckDir, "index.json"));
    if (!deckIndex) continue;

    const deckKey = `deck-${deckName}`;
    const deckIndexMap = new Map();
    if (deckIndex.cards) {
      for (const entry of deckIndex.cards) deckIndexMap.set(entry.id, entry);
    }

    // Import deck index card
    await aql(
      `UPSERT { _key: @key }
       INSERT @doc
       UPDATE { schema_json: @schema, updated_at: DATE_ISO8601(DATE_NOW()), hash: @hash }
       IN cards RETURN NEW._key`,
      {
        key: deckKey,
        hash: sha256(JSON.stringify(deckIndex)),
        schema: JSON.stringify(deckIndex),
        doc: {
          _key: deckKey,
          card_type: "index",
          kind: "deck",
          title: deckIndex.title || deckName,
          content_md: deckIndex.description || "",
          summary: deckIndex.description || "",
          zone: deckIndex.category || "agentcards",
          agent_id: deckIndex.author || "agentcard-ingestor",
          tags: [...(deckIndex.tags || []), "deck", deckName],
          layout: "deck-index",
          size: "large",
          template_id: null,
          deck_id: null,
          deck_title: null,
          deck_position: null,
          elements: [],
          sources: deckIndex.sourceLinks || [],
          route_to: deckIndex.targetAgents || [],
          related_cards: [],
          data_source: null,
          data_binding: null,
          design_system: deckIndex.designSystem || "Neon Pastel Blue / Fluent 2 Dark",
          ac_version: deckIndex.cardVersion || "1.5",
          schema_json: JSON.stringify(deckIndex),
          schema_version: "v1.0.0",
          hash: sha256(JSON.stringify(deckIndex)),
          created_at: deckIndex.date || new Date().toISOString(),
          updated_at: new Date().toISOString(),
        },
      }
    );
    deckCount++;

    // Import member cards
    const cardFiles = readdirSync(deckDir)
      .filter(f => /^\d+.*\.json$/.test(f))
      .sort();

    let pos = 0;
    for (const cf of cardFiles) {
      pos++;
      const cardFname = basename(cf, ".json");
      const cardKey = `${deckName}--${cardFname}`;
      const body = readJSON(join(deckDir, cf));
      if (!body) continue;
      const meta = deckIndexMap.get(cardFname) || {};

      await aql(
        `UPSERT { _key: @key }
         INSERT @doc
         UPDATE { schema_json: @schema, updated_at: DATE_ISO8601(DATE_NOW()), hash: @hash }
         IN cards RETURN NEW._key`,
        {
          key: cardKey,
          hash: sha256(JSON.stringify(body)),
          schema: JSON.stringify(body),
          doc: {
            _key: cardKey,
            card_type: "memory",
            kind: meta.kind || "knowledge",
            title: meta.title || cardFname,
            content_md: meta.description || "",
            summary: meta.description || "",
            zone: deckIndex.category || "agentcards",
            agent_id: deckIndex.author || "agentcard-ingestor",
            tags: [...(meta.tags || []), deckName],
            layout: meta.template || meta.layout || "",
            size: meta.size || "large",
            template_id: meta.template || null,
            deck_id: deckKey,
            deck_title: deckIndex.title || deckName,
            deck_position: pos,
            elements: meta.elements || [],
            sources: meta.sources || [],
            route_to: meta.routeTo || [],
            related_cards: (meta.related || []).map(r => r.id || r),
            data_source: meta.dataSource || null,
            data_binding: meta.dataBinding || null,
            design_system: deckIndex.designSystem || "Neon Pastel Blue / Fluent 2 Dark",
            ac_version: deckIndex.cardVersion || body.version || "1.5",
            schema_json: JSON.stringify(body),
            schema_version: "v1.0.0",
            hash: sha256(JSON.stringify(body)),
            created_at: new Date().toISOString(),
            updated_at: new Date().toISOString(),
          },
        }
      );
      memberCount++;
    }
  }
  return { deckCount, memberCount };
}

// ---------------------------------------------------------------
// Phase 4-6: Wire all edges
// ---------------------------------------------------------------
async function wireEdges() {
  // Deck → member edges
  const deckEdges = await aql(`
    FOR c IN cards FILTER c.deck_id != null AND DOCUMENT(CONCAT('cards/', c.deck_id)) != null
    LET dup = LENGTH(FOR e IN card_to_card FILTER e._from == CONCAT('cards/', c.deck_id) AND e._to == c._id RETURN 1)
    FILTER dup == 0
    INSERT { _from: CONCAT('cards/', c.deck_id), _to: c._id, edge_type: 'deck_member', weight: 0.9, position: c.deck_position, created_at: DATE_ISO8601(DATE_NOW()) }
    INTO card_to_card RETURN 1
  `);

  // Template → instance edges
  const tmplEdges = await aql(`
    FOR c IN cards FILTER c.template_id != null
    LET tmpl_key = SUBSTITUTE(c.template_id, '-', '-')
    LET tmpl = DOCUMENT(CONCAT('cards/', tmpl_key))
    FILTER tmpl != null
    LET dup = LENGTH(FOR e IN card_to_card FILTER e._from == tmpl._id AND e._to == c._id AND e.edge_type == 'template_instance' RETURN 1)
    FILTER dup == 0
    INSERT { _from: tmpl._id, _to: c._id, edge_type: 'template_instance', weight: 0.8, created_at: DATE_ISO8601(DATE_NOW()) }
    INTO card_to_card RETURN 1
  `);

  // related_cards edges
  const relatedEdges = await aql(`
    FOR c IN cards FILTER LENGTH(c.related_cards) > 0
    FOR rel_id IN c.related_cards
    LET target = DOCUMENT(CONCAT('cards/', rel_id))
    FILTER target != null
    LET dup = LENGTH(FOR e IN card_to_card FILTER e._from == c._id AND e._to == target._id AND e.edge_type == 'related' RETURN 1)
    FILTER dup == 0
    INSERT { _from: c._id, _to: target._id, edge_type: 'related', weight: 0.7, created_at: DATE_ISO8601(DATE_NOW()) }
    INTO card_to_card RETURN 1
  `);

  // route_to → agent_to_card edges (ensure agents exist)
  const routeEdges = await aql(`
    FOR c IN cards FILTER LENGTH(c.route_to) > 0
    FOR agent_name IN c.route_to
    UPSERT { _key: agent_name }
    INSERT { _key: agent_name, name: agent_name, status: 'active', fleet: 'dayourbot', created_at: DATE_ISO8601(DATE_NOW()) }
    UPDATE {} IN agents
    LET dup = LENGTH(FOR e IN agent_to_card FILTER e._from == CONCAT('agents/', agent_name) AND e._to == c._id RETURN 1)
    FILTER dup == 0
    INSERT { _from: CONCAT('agents/', agent_name), _to: c._id, relationship: 'routed_to', created_at: DATE_ISO8601(DATE_NOW()) }
    INTO agent_to_card RETURN 1
  `);

  // card_to_zone edges (ensure zones exist)
  const zoneEdges = await aql(`
    FOR c IN cards FILTER c.zone != null
    UPSERT { _key: c.zone }
    INSERT { _key: c.zone, name: c.zone, description: CONCAT('Auto-created zone: ', c.zone), created_at: DATE_ISO8601(DATE_NOW()) }
    UPDATE {} IN memory_zones
    LET dup = LENGTH(FOR e IN card_to_zone FILTER e._from == c._id AND e._to == CONCAT('memory_zones/', c.zone) RETURN 1)
    FILTER dup == 0
    INSERT { _from: c._id, _to: CONCAT('memory_zones/', c.zone), relationship: 'member_of', created_at: DATE_ISO8601(DATE_NOW()) }
    INTO card_to_zone RETURN 1
  `);

  return {
    deck_edges: deckEdges.result?.length || 0,
    template_edges: tmplEdges.result?.length || 0,
    related_edges: relatedEdges.result?.length || 0,
    route_edges: routeEdges.result?.length || 0,
    zone_edges: zoneEdges.result?.length || 0,
  };
}

// ---------------------------------------------------------------
// Main
// ---------------------------------------------------------------
async function main() {
  console.log("=== DarbotDB AgentCard Import ===");
  console.log(`Source: ${CARDS_DIR}`);
  console.log(`Target: ${DDB}/_db/${DB}`);
  console.log();

  console.log("[2/6] Importing root template cards...");
  const rootCount = await importRootCards();
  console.log(`  ${rootCount} root cards`);

  console.log("[3/6] Importing deck cards...");
  const { deckCount, memberCount } = await importDecks();
  console.log(`  ${deckCount} decks + ${memberCount} member cards`);

  console.log("[4-6/6] Wiring edges...");
  const edges = await wireEdges();
  console.log(`  Deck edges: ${edges.deck_edges}`);
  console.log(`  Template edges: ${edges.template_edges}`);
  console.log(`  Related edges: ${edges.related_edges}`);
  console.log(`  Route-to edges: ${edges.route_edges}`);
  console.log(`  Zone edges: ${edges.zone_edges}`);

  console.log();
  const final = await aql(`RETURN {
    cards: LENGTH(cards), agents: LENGTH(agents), memory_zones: LENGTH(memory_zones),
    agent_to_card: LENGTH(agent_to_card), card_to_card: LENGTH(card_to_card),
    card_to_zone: LENGTH(card_to_zone)
  }`);
  console.log("=== Final Counts ===");
  console.log(JSON.stringify(final.result?.[0], null, 2));
}

main().catch(console.error);
