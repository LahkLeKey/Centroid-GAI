# Build and integration checks

CI runs native C builds/tests on Linux, macOS, and Windows. Linux also runs the
Node addon tests, Clang analysis, formatting checks, and strict Doxygen generation.
Clang is invoked separately for each translation unit to avoid Clang 18 retaining
incorrect `va_list` analyzer state across files. Checks remain enabled.

The web/integration job installs the locked workspace dependencies, typechecks
the packages, builds the web app, builds the deployable API image, and starts an
isolated PostgreSQL database through the committed migrations. It then runs API
integration tests and all Playwright workflows against the real native API.
Uploads create their own `.cgai` fixtures through the API; no developer build
artifacts are required. Failed runs retain service logs and browser traces.

Successful jobs publish `web-dist` and `api-documentation` artifacts. Deployment
to a hosting provider is not configured; no production credentials are required.
Bun 1.3.9 is pinned in CI and Docker images for reproducible workspace installs.

For local service-boundary verification, run `bun run test:e2e` from `persistence`.
It creates a process-specific Compose project, defaults to API port 3100 and
PostgreSQL port 55432, and cleans up its own containers and volumes even after a
partial startup failure. Override ports with `CGAI_E2E_API_PORT` and
`CGAI_E2E_POSTGRES_PORT`. It never shuts down the normal `centroid-gai` stack.
