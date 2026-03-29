#!/usr/bin/env bash
# DarbotDB AgentCard Importer
# Imports all AgentCards from OneDrive into the DDB graph on 10.1.8.69
#
# Collections used:
#   cards           - every card JSON + index.json entries
#   card_to_card    - parent→child, template→instance, related, deck membership
#   agent_to_card   - routeTo agent assignments
#   card_to_zone    - zone membership
#   agents          - ensure routeTo agents exist
#   memory_zones    - ensure card zones exist
#
# Usage: bash scripts/import-agentcards.sh

set -euo pipefail

DDB="http://10.1.8.69:8529"
DB="txt2kg"
CARDS_DIR="C:/Users/darbot/OneDrive/AgentCards"
API="$DDB/_db/$DB/_api/cursor"

aql() {
  local body="$1"
  curl -s "$API" -d "$body"
}

echo "=== DarbotDB AgentCard Import ==="
echo "Source: $CARDS_DIR"
echo "Target: $DDB/_db/$DB"
echo ""

# ---------------------------------------------------------------
# Phase 1: Schema evolution — add new fields to cards collection
# ---------------------------------------------------------------
echo "[1/6] Ensuring schema fields on cards collection..."

cat > /tmp/ddb_schema_evolve.json << 'EOF'
{
  "query": "FOR c IN cards FILTER c.kind == null UPDATE c WITH { kind: 'template', size: 'large', layout: null, summary: c.description, template_id: null, deck_id: null, deck_title: null, deck_position: null, sources: [], route_to: [], data_source: null, data_binding: null, elements: [], related: [], design_system: null, ac_version: c.schema_version } IN cards COLLECT WITH COUNT INTO cnt RETURN cnt"
}
EOF
result=$(aql "$(cat /tmp/ddb_schema_evolve.json)")
echo "  Updated existing cards with new fields: $result"

# ---------------------------------------------------------------
# Phase 2: Import root-level template cards (01-38)
# ---------------------------------------------------------------
echo "[2/6] Importing root template cards..."

root_count=0
for f in "$CARDS_DIR"/[0-9]*.json; do
  [ -f "$f" ] || continue
  fname=$(basename "$f" .json)

  # Read the card JSON and the index entry
  card_json=$(cat "$f")

  # Build the entity doc via AQL
  cat > /tmp/ddb_card_import.json << ENDOFQUERY
{
  "query": "LET card_body = $card_json LET idx = (FOR entry IN (DOCUMENT('_frontend/agentcard-index') || {}).cards || [] FILTER entry.id == '$fname' RETURN entry)[0] UPSERT { _key: '$fname' } INSERT { _key: '$fname', card_type: 'observation', kind: 'template', title: idx.title || '$fname', content_md: idx.use || idx.summary || '', summary: idx.summary || idx.use || '', description: idx.use || '', zone: 'agentcards', agent_id: 'agentcard-ingestor', tags: idx.tags || ['agentcard', 'template'], layout: idx.layout || '$fname', size: idx.size || 'large', elements: idx.elements || [], sources: idx.sources || [], route_to: idx.routeTo || [], related: idx.related || [], data_source: idx.dataSource || null, data_binding: idx.dataBinding || null, design_system: 'Neon Pastel Blue / Fluent 2 Dark', ac_version: '1.5', schema_json: TO_STRING(card_body), schema_version: 'v1.0.0', hash: SHA256(TO_STRING(card_body)), created_at: DATE_ISO8601(DATE_NOW()), updated_at: DATE_ISO8601(DATE_NOW()) } UPDATE { schema_json: TO_STRING(card_body), updated_at: DATE_ISO8601(DATE_NOW()), hash: SHA256(TO_STRING(card_body)) } IN cards RETURN 1"
}
ENDOFQUERY
  aql "$(cat /tmp/ddb_card_import.json)" > /dev/null 2>&1
  root_count=$((root_count + 1))
done
echo "  Imported $root_count root template cards"

# ---------------------------------------------------------------
# Phase 3: Import deck index cards + deck member cards
# ---------------------------------------------------------------
echo "[3/6] Importing deck cards..."

deck_count=0
member_count=0

for deck_dir in "$CARDS_DIR"/*/; do
  [ -d "$deck_dir" ] || continue
  deck_name=$(basename "$deck_dir")
  deck_index="$deck_dir/index.json"

  if [ ! -f "$deck_index" ]; then
    continue
  fi

  # Import deck index as an "index" type card
  deck_json=$(cat "$deck_index")
  deck_key="deck-${deck_name}"

  cat > /tmp/ddb_deck.json << ENDOFQUERY
{
  "query": "LET deck = $deck_json UPSERT { _key: '${deck_key}' } INSERT { _key: '${deck_key}', card_type: 'index', kind: CONTAINS('${deck_name}', 'example') ? 'example' : 'deck', title: deck.title || '${deck_name}', content_md: deck.description || '', summary: deck.description || '', zone: deck.category || 'agentcards', agent_id: deck.author || 'agentcard-ingestor', tags: APPEND(deck.tags || [], ['deck', '${deck_name}']), layout: 'deck-index', size: 'large', elements: [], sources: deck.sourceLinks || [], route_to: deck.targetAgents || [], design_system: deck.designSystem || 'Neon Pastel Blue / Fluent 2 Dark', ac_version: deck.cardVersion || '1.5', schema_json: TO_STRING(deck), schema_version: 'v1.0.0', hash: SHA256(TO_STRING(deck)), created_at: deck.date || DATE_ISO8601(DATE_NOW()), updated_at: DATE_ISO8601(DATE_NOW()) } UPDATE { schema_json: TO_STRING(deck), updated_at: DATE_ISO8601(DATE_NOW()), hash: SHA256(TO_STRING(deck)) } IN cards RETURN 1"
}
ENDOFQUERY
  aql "$(cat /tmp/ddb_deck.json)" > /dev/null 2>&1
  deck_count=$((deck_count + 1))

  # Import each member card in the deck
  pos=0
  for card_file in "$deck_dir"/[0-9]*.json; do
    [ -f "$card_file" ] || continue
    card_fname=$(basename "$card_file" .json)
    card_key="${deck_name}--${card_fname}"
    card_body=$(cat "$card_file")
    pos=$((pos + 1))

    cat > /tmp/ddb_member.json << ENDOFQUERY
{
  "query": "LET body = $card_body LET idx = (FOR entry IN (DOCUMENT('cards/${deck_key}') || {}).cards || [] FILTER entry.id == '${card_fname}' RETURN entry) LET meta = LENGTH(idx) > 0 ? idx[0] : {} UPSERT { _key: '${card_key}' } INSERT { _key: '${card_key}', card_type: 'memory', kind: meta.kind || 'knowledge', title: meta.title || '${card_fname}', content_md: meta.description || '', summary: meta.description || '', zone: DOCUMENT('cards/${deck_key}').zone || 'agentcards', agent_id: DOCUMENT('cards/${deck_key}').agent_id || 'agentcard-ingestor', tags: APPEND(meta.tags || [], ['${deck_name}']), layout: meta.template || meta.layout || '', size: meta.size || 'large', template_id: meta.template || null, deck_id: '${deck_key}', deck_title: DOCUMENT('cards/${deck_key}').title || '${deck_name}', deck_position: ${pos}, elements: meta.elements || [], sources: meta.sources || [], route_to: meta.routeTo || [], related: meta.related || [], design_system: 'Neon Pastel Blue / Fluent 2 Dark', ac_version: '1.5', schema_json: TO_STRING(body), schema_version: 'v1.0.0', hash: SHA256(TO_STRING(body)), created_at: DATE_ISO8601(DATE_NOW()), updated_at: DATE_ISO8601(DATE_NOW()) } UPDATE { schema_json: TO_STRING(body), updated_at: DATE_ISO8601(DATE_NOW()), hash: SHA256(TO_STRING(body)) } IN cards RETURN 1"
}
ENDOFQUERY
    aql "$(cat /tmp/ddb_member.json)" > /dev/null 2>&1
    member_count=$((member_count + 1))
  done
done
echo "  Imported $deck_count deck indexes + $member_count deck member cards"

# ---------------------------------------------------------------
# Phase 4: Wire card_to_card edges (deck→member, template→instance)
# ---------------------------------------------------------------
echo "[4/6] Wiring card_to_card edges..."

cat > /tmp/ddb_edges_deck.json << 'EOF'
{
  "query": "FOR c IN cards FILTER c.deck_id != null AND DOCUMENT(CONCAT('cards/', c.deck_id)) != null LET existing = LENGTH(FOR e IN card_to_card FILTER e._from == CONCAT('cards/', c.deck_id) AND e._to == c._id RETURN 1) FILTER existing == 0 INSERT { _from: CONCAT('cards/', c.deck_id), _to: c._id, edge_type: 'deck_member', weight: 0.9, description: CONCAT('Card ', c.deck_position, ' in deck'), position: c.deck_position, created_at: DATE_ISO8601(DATE_NOW()) } INTO card_to_card RETURN 1"
}
EOF
result=$(aql "$(cat /tmp/ddb_edges_deck.json)")
echo "  Deck membership edges: $(echo $result | grep -o '"writesExecuted":[0-9]*')"

cat > /tmp/ddb_edges_tmpl.json << 'EOF'
{
  "query": "FOR c IN cards FILTER c.template_id != null LET tmpl = DOCUMENT(CONCAT('cards/', c.template_id)) FILTER tmpl != null LET existing = LENGTH(FOR e IN card_to_card FILTER e._from == tmpl._id AND e._to == c._id AND e.edge_type == 'template_instance' RETURN 1) FILTER existing == 0 INSERT { _from: tmpl._id, _to: c._id, edge_type: 'template_instance', weight: 0.8, description: CONCAT('Instance of template ', c.template_id), created_at: DATE_ISO8601(DATE_NOW()) } INTO card_to_card RETURN 1"
}
EOF
result=$(aql "$(cat /tmp/ddb_edges_tmpl.json)")
echo "  Template instance edges: $(echo $result | grep -o '"writesExecuted":[0-9]*')"

# ---------------------------------------------------------------
# Phase 5: Wire agent_to_card edges (routeTo agents)
# ---------------------------------------------------------------
echo "[5/6] Wiring agent_to_card edges..."

cat > /tmp/ddb_route_agents.json << 'EOF'
{
  "query": "FOR c IN cards FILTER LENGTH(c.route_to) > 0 FOR agent_name IN c.route_to UPSERT { _key: agent_name } INSERT { _key: agent_name, name: agent_name, status: 'active', fleet: 'dayourbot', created_at: DATE_ISO8601(DATE_NOW()) } UPDATE {} IN agents LET existing = LENGTH(FOR e IN agent_to_card FILTER e._from == CONCAT('agents/', agent_name) AND e._to == c._id RETURN 1) FILTER existing == 0 INSERT { _from: CONCAT('agents/', agent_name), _to: c._id, relationship: 'routed_to', created_at: DATE_ISO8601(DATE_NOW()) } INTO agent_to_card RETURN 1"
}
EOF
result=$(aql "$(cat /tmp/ddb_route_agents.json)")
echo "  Route-to edges: $(echo $result | grep -o '"writesExecuted":[0-9]*')"

# ---------------------------------------------------------------
# Phase 6: Wire card_to_zone edges
# ---------------------------------------------------------------
echo "[6/6] Wiring card_to_zone edges..."

cat > /tmp/ddb_card_zones.json << 'EOF'
{
  "query": "FOR c IN cards FILTER c.zone != null UPSERT { _key: c.zone } INSERT { _key: c.zone, name: c.zone, description: CONCAT('Auto-created zone: ', c.zone), created_at: DATE_ISO8601(DATE_NOW()) } UPDATE {} IN memory_zones LET existing = LENGTH(FOR e IN card_to_zone FILTER e._from == c._id AND e._to == CONCAT('memory_zones/', c.zone) RETURN 1) FILTER existing == 0 INSERT { _from: c._id, _to: CONCAT('memory_zones/', c.zone), relationship: 'member_of', created_at: DATE_ISO8601(DATE_NOW()) } INTO card_to_zone RETURN 1"
}
EOF
result=$(aql "$(cat /tmp/ddb_card_zones.json)")
echo "  Zone membership edges: $(echo $result | grep -o '"writesExecuted":[0-9]*')"

# ---------------------------------------------------------------
# Final count
# ---------------------------------------------------------------
echo ""
echo "=== Import Complete ==="
cat > /tmp/ddb_final_count.json << 'EOF'
{
  "query": "RETURN { cards: LENGTH(cards), agents: LENGTH(agents), memory_zones: LENGTH(memory_zones), patterns: LENGTH(patterns), agent_to_card: LENGTH(agent_to_card), card_to_card: LENGTH(card_to_card), card_to_zone: LENGTH(card_to_zone), agent_to_agent: LENGTH(agent_to_agent) }"
}
EOF
aql "$(cat /tmp/ddb_final_count.json)"
