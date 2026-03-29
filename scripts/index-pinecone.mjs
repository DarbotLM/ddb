#!/usr/bin/env node
/**
 * Index all DarbotDB entities into the Pinecone vector DB via sentence-embeddings API.
 * Each entity gets its own vector with metadata for RAG retrieval.
 */

const DDB = "http://10.1.8.69:8529";
const DB = "txt2kg";
const APP = "http://10.1.8.69:3001";

async function aql(query) {
  const resp = await fetch(`${DDB}/_db/${DB}/_api/cursor`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ query }),
  });
  return resp.json();
}

async function embedAndStore(text, docId) {
  const resp = await fetch(`${APP}/api/sentence-embeddings`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ text, documentId: docId }),
  });
  return resp.json();
}

async function main() {
  // Get all entities
  const entities = await aql(
    "FOR e IN entities RETURN { key: e._key, text: CONCAT(e.name, '. ', e.description), zone: e.zone, type: e.type }"
  );
  console.log(`Indexing ${entities.result.length} entities...`);

  let ok = 0, fail = 0;
  for (const e of entities.result) {
    try {
      const result = await embedAndStore(e.text, `entity-${e.key}`);
      if (result.success) {
        ok++;
        if (ok % 20 === 0) console.log(`  ${ok}/${entities.result.length}...`);
      } else {
        fail++;
        console.log(`  FAIL ${e.key}: ${JSON.stringify(result)}`);
      }
    } catch (err) {
      fail++;
      console.log(`  ERR ${e.key}: ${err.message}`);
    }
  }

  // Also index cards summaries
  const cards = await aql(
    "FOR c IN cards FILTER c.summary != null AND c.summary != '' RETURN { key: c._key, text: CONCAT(c.title, '. ', c.summary), zone: c.zone, kind: c.kind }"
  );
  console.log(`\nIndexing ${cards.result.length} card summaries...`);

  for (const c of cards.result) {
    try {
      const result = await embedAndStore(c.text, `card-${c.key}`);
      if (result.success) {
        ok++;
        if (ok % 50 === 0) console.log(`  ${ok} total...`);
      } else {
        fail++;
      }
    } catch (err) {
      fail++;
    }
  }

  // Check final stats
  const stats = await fetch(`${APP}/api/pinecone-diag/stats`).then(r => r.json());
  console.log(`\nDone. OK: ${ok}, Failed: ${fail}`);
  console.log(`Pinecone vectors: ${stats.totalVectorCount}`);
}

main().catch(console.error);
