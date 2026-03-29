<picture>
  <source media="(prefers-color-scheme: dark)" srcset="https://user-images.githubusercontent.com/7819991/218699214-264942f9-b020-4f50-b1a6-3363cdc0ddc9.svg" width="638" height="105">
  <source media="(prefers-color-scheme: light)" srcset="https://user-images.githubusercontent.com/7819991/218699215-b9b4a465-45f8-4db9-b5a4-ba912541e017.svg" width="638" height="105">
  <img alt="Two stylized avocado halves and the product name." src="https://user-images.githubusercontent.com/7819991/218697980-26ffd7af-cf29-4365-8a5d-504b850fc6b1.png" width="638" height="105">
</picture>

darbotdb
=========

**darbotdb** was inspired by ArangoDB - a scalable graph database system to drive value from connected data,
faster. Native graphs, an integrated search engine, and JSON support, via a
single query language.

DDB will combine sqlite3 portability "micro dbs" that can be agent compostable, verifiable, persistent, isolate or shared, indentified or anonymous, authenticated or unauthenticated. 

DDB is ideal for agent database and neural net with compostable adaptive flash cards and semantic first architectural patterns. 

Agent to agent, human to agent, agent to agent swarm, swarm to human in the loop, compostable "memory" files for relevant radial data zones. DDB is ideal for parallel observer/orchestrator/synthesizer (three in one in parallel behind the scenes) to identify early patterns and "remember forward" by accessing and sharing the adaptive memory card file. 

Adaptive cards that are Model Context Protocol (MCP) apps, but care also generative, and the adaptive card is UI friendly for humans while being and adaptive flash card presenting data in a schema-first method for agents (adaptive card json), perfect for subclase querry and semantic indexing- index of index, cross index, deep relationship patterns while still separating the personal details from the task/action/output since it schema driven. 

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

    docker run -e DDB_ROOT_PASSWORD=test123 -p 8529:8529 -d darbotdb

Then point your browser to `http://127.0.0.1:8529/`.

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
