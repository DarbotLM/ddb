darbotdb
=========

**darbotdb** was inspired by ArangoDB - a scalable graph database system to drive value from connected data,
faster. Native graphs, an integrated search engine, and JSON support, via a
single query language.

DDB combines SQLite3 portability through micro DBs with graph storage, adaptive cards, MCP tools, and agent-first indexing. Micro DBs can be composable, verifiable, persistent, isolated or shared, identified or anonymous, and authenticated or unauthenticated.

DDB is designed for agent databases, adaptive flash cards, and semantic-first architectural patterns.

DDB supports agent-to-agent, human-to-agent, swarm-to-agent, and swarm-to-human-in-the-loop workflows with composable memory files for relevant radial data zones. The observer/orchestrator/synthesizer triad can run in parallel to identify early patterns and "remember forward" through adaptive memory cards.

Adaptive cards can be MCP apps, generative agent payloads, and UI-friendly human views at the same time. They present data in a schema-first format for agents, enabling subclass queries, semantic indexes, index-of-index patterns, cross-index traversal, and deep relationship discovery while keeping personal details separate from task, action, and output metadata.

### Schema Contracts

DDB ships first-party schema resources in [`ddb/schemas`](ddb/schemas):

- **`kg_dv_schema`** — Dataverse-aligned knowledge graph metadata for entities, relationships, adaptive cards, agent configs, and integration indexes.
- **`ac3_adaptive_agent_card`** — Adaptive Card 3.0-style YAML contract for adaptive agent cards with DDB metadata, workflow state, memory zones, tools, and connectors.

These schemas keep Dataverse storage, SQLite `.ddb` micro DBs, MCP tools, and adaptive cards aligned around stable names and professional presentation-ready path patterns.

The schema contracts also include semantic relatability metadata: confidence, provenance, lineage, sensitivity, semantic attributes, relatable entities, and relationship path patterns. This allows DDB to preserve flexible agent context while still projecting cleanly into Dataverse tables, graph indexes, micro DBs, and adaptive cards.

Install and validate the reference schema bundle:

```bash
python scripts/validate-schemas.py
python scripts/install-ddb-reference-schemas.py --target ~/.ddb/schemas --create-reference-microdb
```

The installer copies the full schema registry and deployable reference DDB manifests, and can seed a SQLite `.ddb` micro database at `~/.ddb/reference/ddb_agent_memory.ddb`.

### What's New in darbotdb

darbotdb extends ArangoDB with powerful modern integrations and platform support:

- **Full Prisma Support**: Native integration with Prisma ORM for seamless database management
- **Arm64 Architecture**: Complete support for ARM-based systems including Apple Silicon
- **Enhanced Docker Support**: Optimized containerization with improved Docker images
- **Complete FastAPI Integration**: Full integration for all tool calls, queries, and functions
- **58-Tool MCP Server**: Complete Model Context Protocol coverage of the entire API — adaptive cards, memory recall, graph traversal, 3DKG spatial queries, sessions, manifests, AG-UI conversations, and Txt2KG knowledge extraction with full Zod 4 schemas
- **3D Knowledge Graph (3DKG)**: Spatial graph engine with nearest-neighbor, bounding-box, shortest-path, and layout algorithms
- **Txt2KG Pipeline**: LLM-powered triple extraction, RAG search, and bidirectional bridge between micro DBs and knowledge graphs
- **AG-UI Protocol**: Agent-to-UI conversation threading with replay and ingestion
- **Dataverse + AC3 Schemas**: Packaged `kg_dv_schema` and `ac3_adaptive_agent_card` contracts for Dataverse, adaptive card, agent, workflow, and integration-index interoperability

### MCP Server

The DarbotDB MCP server exposes **58 tools** for AI agents and MCP-compatible clients:

```bash
cd ddb/mcp
npm run serve        # HTTP on port 3001
npm run serve:stdio  # stdio for Claude Desktop, VS Code, etc.
```

Four tools include interactive HTML UIs (card viewer, memory dashboard, graph explorer, 3DKG scene viewer). All schemas are fully typed with Zod 4. See [`ddb/mcp/README.md`](ddb/mcp/README.md) for the complete tool reference.

Getting Started with darbotdb
------------------------------

### Quick Start

Start darbotdb in a Docker container (with Arm64 support):

    docker run -e DDB_ROOT_PASSWORD=test123 -p 8008:8529 -d darbotdb

Then point your browser to `http://127.0.0.1:8008/`.

### Upstream Resources

darbotdb is based on ArangoDB. For comprehensive documentation and learning resources on ArangoDB:

- [ArangoDB Documentation](https://docs.arangodb.com/)
- [ArangoDB University](https://university.arangodb.com/)
- [Free Udemy Course](https://www.udemy.com/course/getting-started-with-arangodb)
- [Training Center](https://www.arangodb.com/learn/)

### darbotdb Repository

- **GitHub**: [https://github.com/dayour/darbotdb](https://github.com/dayour/darbotdb)
- **Issues**: [https://github.com/dayour/darbotdb/issues](https://github.com/dayour/darbotdb/issues)

Key Features Inherited from ArangoDB
------------------------------------

**Native Graph** - Store both data and relationships, for faster queries even
with multiple levels of joins and deeper insights that simply aren't possible
with traditional relational and document database systems.

**Document Store** - Every node in your graph is a JSON document:
flexible, extensible, and easily imported from your existing document database.

**ArangoSearch** - Natively integrated cross-platform indexing, text-search and
ranking engine for information retrieval, optimized for speed and memory.

### Core Features

- **Horizontal scalability**: Seamlessly shard your data across multiple machines.
- **High availability** and **resilience**: Replicate data to multiple cluster
  nodes, with automatic failover.
- **Flexible data modeling**: Model your data as combination of key-value pairs,
  documents, and graphs as you see fit for your application.
- Work **schema-free** or use **schema validation** for data consistency.
  Store any type of data - date/time, geo-spatial, text, nested.
- **Powerful query language** (_AQL_) to retrieve and modify data - from simple
  CRUD operations, over complex filters and aggregations, all the way to joins,
  graphs, and ranked full-text search.
- **Transactions**: Run queries on multiple documents or collections with
  optional transactional consistency and isolation.
- **Data-centric microservices**: Unify your data storage logic, reduce network
  overhead, and secure sensitive data with the _ArangoDB Foxx_ JavaScript framework.
- **Fast access to your data**: Fine-tune your queries with a variety of index
  types for optimal performance. ArangoDB is written in C++ and can handle even
  very large datasets efficiently.
- Easy to use **web interface** and **command-line tools** for interaction
  with the server.

### Scalability Features

Focus on solving scale problems for mission critical workloads using
secure graph data. ArangoDB offers special features for performance, compliance,
and security, as well as advanced query capabilities.

- Smartly shard and replicate graphs and datasets with features like
  **EnterpriseGraphs**, **SmartGraphs**, and **SmartJoins** for lightning fast
  query execution.
- Combine the performance of a single server with the resilience of a cluster
  setup using **OneShard** deployments.
- Increase fault tolerance with **Datacenter-to-Datacenter Replication** and
  and create incremental **Hot Backups** without downtime.
- Enable highly secure work with **Encryption 360**, enhanced **Data Masking**, 
  and detailed **Auditing**.
- Perform **parallel graph traversals**.
- Use ArangoSearch **search highlighting** and **nested search** for advanced
  information retrieval.

Latest Release
--------------

darbotdb releases and Docker images are available on GitHub:
<https://github.com/dayour/darbotdb/releases>

For what's new in the upstream ArangoDB, see the Release Notes in the
[ArangoDB Documentation](https://docs.arangodb.com/).

Stay in Contact
---------------

### darbotdb Community

- Please use GitHub for darbotdb-specific feature requests and bug reports:
  [https://github.com/dayour/darbotdb/issues](https://github.com/dayour/darbotdb/issues)

### Upstream ArangoDB Resources

- Ask questions about AQL, usage scenarios, etc. on StackOverflow:
  [https://stackoverflow.com/questions/tagged/arangodb](https://stackoverflow.com/questions/tagged/arangodb)

- Chat with the community and the developers on Slack:
  [https://arangodb-community.slack.com/](https://arangodb-community.slack.com/)

- Learn more about ArangoDB with the YouTube channel: 
  [https://www.youtube.com/@ArangoDB](https://www.youtube.com/@ArangoDB)

- Follow on Twitter:
  [https://twitter.com/arangodb](https://twitter.com/arangodb)

- Find out more about the community: [https://www.arangodb.com/community](https://www.arangodb.com/community/)
