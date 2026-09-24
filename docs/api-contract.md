# REST API Contract

The TypeScript service is the application boundary. Callers use HTTP/JSON or the model artifact media type; they do not call PostgreSQL, Prisma, or the C ABI directly. The service validates requests, invokes the native ABI, and persists complete artifacts through Prisma PostgreSQL.

## Domains

| Domain | Responsibility |
| --- | --- |
| System | Health and native schema discovery |
| Model catalog | List, inspect, and delete named models |
| Training | Train text through native C and persist the artifact |
| Inference | Generate from a persisted artifact through native C |
| Artifacts | Upload and download complete `.cgai` payloads |

## Versioned Endpoints

| Method | Endpoint | Response |
| --- | --- | --- |
| GET | `/api/v1/health` | Service/native/database status |
| GET | `/api/v1/native/schema` | C-exported persistence JSON Schema |
| GET | `/api/v1/models` | Persisted model metadata list |
| GET | `/api/v1/models/:name` | Binary `.cgai` artifact |
| PUT | `/api/v1/models/:name` | Validate and persist an artifact |
| DELETE | `/api/v1/models/:name` | Delete result |
| GET | `/api/v1/models/:name/metadata` | Persisted metadata |
| POST | `/api/v1/models/:name/train` | Train and persist |
| POST | `/api/v1/models/:name/generate` | Generate continuation |

The existing unversioned endpoints remain compatibility aliases. New callers
should use `/api/v1` exclusively. JSON `uint64_t` values are decimal strings;
artifact downloads use `application/vnd.centroid-gai.model` and an SHA-256 ETag.

## Compose Smoke Test

```sh
docker compose up -d
cd persistence/api
CGAI_API_URL=http://localhost:3000 bun run test:all
```
