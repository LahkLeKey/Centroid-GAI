import assert from "node:assert/strict";
import test from "node:test";

const apiUrl = process.env.CGAI_API_URL;
const modelName = `test-${process.pid}-${Date.now()}`;

test('live supersets refresh nested dependencies, preserve snapshots, and recover after source failures', { skip: !apiUrl }, async () => {
    const names = ['live-a', 'live-b', 'live-super', 'live-nested', 'live-frozen'].map((suffix) => `${modelName}-${suffix}`);
    const [a, b, live, nested, frozen] = names as [string, string, string, string, string];
    const post = (name: string, action: string, body: unknown) => request(`/api/v1/models/${name}/${action}`, {
        method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body),
    });
    const train = (name: string, text: string, dimensions = 8, centroidCount = 2) => post(name, 'train', { text, config: { dimensions, centroidCount, contextWindow: 2 } });
    const contents = async (name: string) => {
        const response = await request(`/api/v1/models/${name}/contents`);
        assert.equal(response.status, 200, await response.clone().text());
        return response.json();
    };
    try {
        assert.equal((await train(a, 'alpha original shared')).status, 201);
        assert.equal((await train(b, 'beta original shared beta')).status, 201);
        const sources = [{ name: a }, { name: b }];
        assert.equal((await post(live, 'merge', { sources, autoRebuild: 'yes' })).status, 400);
        assert.equal((await post(live, 'merge', { sources })).status, 201);
        assert.equal((await post(frozen, 'merge', { sources, autoRebuild: false })).status, 201);
        assert.equal((await post(nested, 'merge', { sources: [{ name: live }], targetCentroids: 1 })).status, 201);
        const initial = await contents(live);
        const initialNested = await contents(nested);
        const initialFrozen = await contents(frozen);
        assert.equal(initial.composition.autoRebuild, true);
        const before = await (await request(`/api/v1/models/${live}/metadata`)).json();
        assert.equal((await contents(live)).checksumSha256, initial.checksumSha256);
        assert.equal((await (await request(`/api/v1/models/${live}/metadata`)).json()).updatedAt, before.updatedAt);

        assert.equal((await train(a, 'newword alpha updated training has more examples and different contexts', 8, 4)).status, 201);
        // Concurrent readers must publish a complete refresh and converge on the same bytes.
        const runs = await Promise.all(Array.from({ length: 4 }, () => post(nested, 'generate', { prompt: 'newword', maxTokens: 4, temperature: 0 })));
        for (const run of runs) assert.equal(run.status, 200, await run.clone().text());
        const refreshed = await contents(live);
        const refreshedNested = await contents(nested);
        assert.notEqual(refreshed.checksumSha256, initial.checksumSha256);
        assert.notEqual(refreshedNested.checksumSha256, initialNested.checksumSha256);
        assert.equal(refreshed.data.initializedCentroids, 6);
        assert.equal(refreshedNested.data.initializedCentroids, 1);
        assert.equal(refreshedNested.data.examplesSeen, refreshed.data.examplesSeen);
        assert.equal(refreshedNested.composition.sources[0].checksumSha256, refreshed.checksumSha256);
        assert.equal(refreshed.composition.sources[0].checksumSha256, (await contents(a)).checksumSha256);
        assert.equal((await contents(frozen)).checksumSha256, initialFrozen.checksumSha256);
        const vocab = await (await request(`/api/v1/models/${live}/contents?section=vocabulary&limit=100`)).json();
        assert.ok(vocab.data.items.some((item: { token: string }) => item.token === 'newword'));
        assert.equal((await request(`/api/v1/models/${live}/contents?checksum=${initial.checksumSha256}`)).status, 409);

        assert.equal((await train(a, 'incompatible representation', 16)).status, 201);
        const incompatible = await post(nested, 'generate', { prompt: 'alpha' });
        assert.equal(incompatible.status, 400);
        assert.match((await incompatible.json()).error, /Cannot rebuild.*dimensions/);
        assert.equal((await train(a, 'restored compatible training')).status, 201);
        assert.equal((await post(nested, 'generate', { prompt: 'restored' })).status, 200);
        await request(`/api/v1/models/${b}`, { method: 'DELETE' });
        const missing = await request(`/api/v1/models/${live}`);
        assert.equal(missing.status, 400);
        assert.match((await missing.json()).error, /source model .* is missing/);
        assert.equal((await request(`/api/v1/models/${frozen}`)).status, 200);
        assert.equal((await train(b, 'beta restored source')).status, 201);
        assert.equal((await request(`/api/v1/models/${nested}`)).status, 200);
        // Explicit replacement ends live tracking and cannot be overwritten by a later refresh.
        assert.equal((await train(live, 'independent replacement')).status, 201);
        assert.equal((await contents(live)).composition, null);
    } finally {
        for (const name of names.reverse()) await request(`/api/v1/models/${name}`, { method: 'DELETE' });
    }
});

test('API inspects contents, composes immutable supersets, and persists recipes', { skip: !apiUrl }, async () => {
    const names = ['source-a', 'source-b', 'incompatible', 'superset', 'compact', 'raced', 'copy'].map((suffix) => `${modelName}-${suffix}`);
    const [a, b, incompatible, superset, compact, raced, copy] = names as [string, string, string, string, string, string, string];
    const post = (path: string, body: unknown) => request(path, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });
    const merge = (name: string, sources: { name: string; checksumSha256?: string }[], targetCentroids = 0) => post(`/api/v1/models/${name}/merge`, { sources, targetCentroids });
    try {
        for (const [name, text, dimensions, centroidCount] of [[a, 'alpha shared alpha', 8, 2], [b, 'beta shared beta beta', 8, 3], [incompatible, 'other dimensions', 16, 2]] as const) {
            assert.equal((await post(`/api/v1/models/${name}/train`, { text, config: { dimensions, centroidCount, contextWindow: 2 } })).status, 201);
        }
        const original = await (await request(`/api/v1/models/${a}/metadata`)).json();
        const summary = await (await request(`/api/v1/models/${a}/contents`)).json();
        assert.equal(summary.data.initializedCentroids, 2);
        assert.equal(summary.composition, null);
        assert.equal(summary.checksumSha256, original.checksumSha256);
        assert.equal((await request(`/api/v1/models/${a}/contents?limit=0`)).status, 400);
        assert.equal((await request(`/api/v1/models/${a}/contents?section=nope`)).status, 400);
        assert.equal((await request(`/api/v1/models/${a}/contents?checksum=stale`)).status, 409);
        const detail = await (await request(`/api/v1/models/${a}/contents?section=centroid&centroid=0&limit=1`)).json();
        assert.equal(detail.data.vector.length, 8);
        assert.equal(detail.data.tokens.items.length, 1);
        assert.equal((await merge(superset, [{ name: a }, { name: incompatible }])).status, 400);
        assert.equal((await merge(superset, [{ name: a }, { name: a }])).status, 400);
        assert.equal((await merge(a, [{ name: a }, { name: b }])).status, 400);
        assert.equal((await merge(superset, [{ name: 'missing-model' }])).status, 404);
        assert.equal((await merge(superset, [{ name: a, checksumSha256: '0'.repeat(64) }])).status, 409);
        const created = await merge(superset, [{ name: a, checksumSha256: original.checksumSha256 }, { name: b }]);
        assert.equal(created.status, 201, await created.clone().text());
        assert.equal((await created.json()).metadata.centroidCount, 5);
        const composed = await (await request(`/api/v1/models/${superset}/contents`)).json();
        assert.equal(composed.composition.algorithm, 'preserve');
        assert.deepEqual(composed.composition.sources.map((source: { name: string }) => source.name), [a, b]);
        assert.equal((await merge(superset, [{ name: a }])).status, 409);
        assert.equal((await merge(compact, [{ name: superset }], 1)).status, 201);
        const compacted = await (await request(`/api/v1/models/${compact}/contents`)).json();
        assert.equal(compacted.data.initializedCentroids, 1);
        assert.equal(compacted.data.examplesSeen, composed.data.examplesSeen);
        assert.equal(compacted.composition.algorithm, 'weighted-streaming-v1');
        assert.equal((await post(`/api/v1/models/${compact}/generate`, { prompt: 'shared', maxTokens: 4, temperature: 0 })).status, 200);
        const originalAfter = await (await request(`/api/v1/models/${a}/metadata`)).json();
        assert.equal(originalAfter.checksumSha256, original.checksumSha256);
        const attempts = await Promise.all([merge(raced, [{ name: a }, { name: b }]), merge(raced, [{ name: a }, { name: b }])]);
        assert.deepEqual(attempts.map((result) => result.status).sort(), [201, 409]);
        const payload = await (await request(`/api/v1/models/${a}`)).arrayBuffer();
        assert.equal((await request(`/api/v1/models/${copy}`, { method: 'PUT', body: payload })).status, 201);
        assert.equal((await merge(`${modelName}-duplicate`, [{ name: a }, { name: copy }])).status, 400);
        assert.equal((await post(`/api/v1/models/${compact}/train`, { text: 'replaced with a new training corpus' })).status, 201);
        assert.equal((await (await request(`/api/v1/models/${compact}/contents`)).json()).composition, null);
    } finally {
        for (const name of [...names, `${modelName}-duplicate`]) await request(`/api/v1/models/${name}`, { method: 'DELETE' });
    }
});

/**
 * Sends one E2E request to the externally started API, failing clearly when no URL is configured.
 */
async function request(path: string, init?: RequestInit): Promise<Response> {
    if (!apiUrl) {
        throw new Error("CGAI_API_URL is required for API integration tests");
    }
    return fetch(`${apiUrl}${path}`, init);
}

// Every test is skipped locally until Compose supplies an API URL; native-only tests remain
// runnable without PostgreSQL, while CI/E2E explicitly opts into the full service boundary.
test("API integration environment is configured", {
    skip: !apiUrl,
}, async () => {
    const response = await request("/health");
    assert.equal(response.status, 200);
    assert.deepEqual(await response.json(), {
        status: "ok",
        native: true,
        database: "postgresql",
    });
});

test("API persists, generates, downloads, and deletes a model", {
    skip: !apiUrl,
}, async () => {
    const train = await request(`/api/v1/models/${modelName}/train`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({
            text: "centroid models generate useful text. centroid models learn from examples.",
        }),
    });
    assert.equal(train.status, 201);
    const trained = (await train.json()) as {
        name: string;
        metadata: { formatVersion: number; libraryVersion: string };
    };
    assert.equal(trained.name, modelName);
    assert.equal(trained.metadata.formatVersion, 1);
    assert.equal(trained.metadata.libraryVersion, "0.2.0");

    try {
        const metadata = await request(`/api/v1/models/${modelName}/metadata`);
        assert.equal(metadata.status, 200);
        assert.equal((await metadata.json()).name, modelName);

        const generate = await request(`/api/v1/models/${modelName}/generate`, {
            method: "POST",
            headers: { "content-type": "application/json" },
            body: JSON.stringify({
                prompt: "centroid models",
                maxTokens: 8,
                seed: "42",
            }),
        });
        assert.equal(generate.status, 200);
        const generated = (await generate.json()) as { continuation: string };
        assert.equal(typeof generated.continuation, "string");

        const download = await request(`/api/v1/models/${modelName}`);
        assert.equal(download.status, 200);
        assert.equal(download.headers.get("content-type"), "application/vnd.centroid-gai.model");
        assert.equal(
            Buffer.from(await download.arrayBuffer())
                .subarray(0, 4)
                .toString("ascii"),
            "CGAI",
        );
    } finally {
        const deleted = await request(`/api/v1/models/${modelName}`, {
            method: "DELETE",
        });
        assert.equal(deleted.status, 200);
        assert.deepEqual(await deleted.json(), { deleted: true });
    }

    const missing = await request(`/api/v1/models/${modelName}`);
    assert.equal(missing.status, 404);
});

test("API rejects malformed training requests", { skip: !apiUrl }, async () => {
    const response = await request(`/api/v1/models/${modelName}/train`, {
        method: "POST",
        headers: { "content-type": "application/json" },
        body: JSON.stringify({}),
    });
    assert.equal(response.status, 400);
    assert.deepEqual(await response.json(), {
        error: "training text must be a non-empty string",
    });
});

test("versioned system and catalog endpoints expose REST domains", {
    skip: !apiUrl,
}, async () => {
    const schema = await request("/api/v1/native/schema");
    assert.equal(schema.status, 200);
    assert.equal((await schema.json()).title, "Centroid-GAI ModelArtifact metadata");

    const models = await request("/api/v1/models");
    assert.equal(models.status, 200);
    assert.ok(Array.isArray(await models.json()));
});
