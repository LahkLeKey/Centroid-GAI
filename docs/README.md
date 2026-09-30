# Centroid neural chatbot documentation

Build a chatbot around the project's C centroid neural network, with an HTTP API,
curl workflows, and Docker Compose deployment. The neural prototype works through
the CLI and conversation service today; measured conversational quality remains
below the release goal.

## Guides

| Guide | Use it for |
| --- | --- |
| [Architecture](architecture.md) | Understand the current components and intended conversation path |
| [Chat service](chat-service.md) | Train chat models, use persistent conversations, configure research and memory, and review measured limitations |
| [Repository chat](repository-chat.md) | Prepare committed codebase evidence, ask for next steps through curl, and run the HTTP scenario suite |
| [Repository chat plan](repository-chat-plan.md) | Deliver codebase answers, a repeated next-step conversation loop, and a 40-scenario API suite |
| [Neural network](neural-centroid.md) | Train and evaluate the implemented model; understand its mathematics and limits |
| [HTTP API](api-contract.md) | Use the existing baseline endpoints and Compose workflow |
| [Development](development.md) | Build, test, document, and work on the native and service code |
| [Chatbot implementation plan](chatbot-plan.md) | Deliver the neural chatbot in testable milestones |
| [Research and memory](research-memory.md) | Design evidence-based search and persistent learning from interactions |

## Status vocabulary

**Implemented** means the behavior has executable code in the relevant build or
request path. It does not by itself establish answer quality.

**Partial** means declarations or components exist but are not connected into a
working end-to-end feature. In particular, chat headers and shared TypeScript
types do not establish a usable chat API.

**Planned** means a proposed implementation or acceptance criterion. Proposed chat
routes, quality targets, and research policies are not current guarantees.

Read the [repository overview](../README.md) for the current status table. Keep
these guides aligned with source changes and measured test results; do not carry
forward a success claim solely because an earlier document reported it.
