import assert from "node:assert/strict";
import test from "node:test";

const apiUrl = process.env["CGAI_API_URL"];
const modelName = `test-${process.pid}-${Date.now()}`;

async function request(path: string, init?: RequestInit): Promise<Response> {
    if (!apiUrl) {
        throw new Error("CGAI_API_URL is required for API integration tests");
    }
    return fetch(`${apiUrl}${path}`, init);
}

test("API integration environment is configured", {skip : !apiUrl}, async () => {
    const response = await request("/health");
    assert.equal(response.status, 200);
    assert.deepEqual(await response.json(), {
        status : "ok",
        native : true,
        database : "postgresql",
    });
});

test("API persists, generates, downloads, and deletes a model", {skip : !apiUrl}, async () => {
    const train = await request(`/api/v1/models/${modelName}/train`, {
        method : "POST",
        headers : {"content-type" : "application/json"},
        body : JSON.stringify({
            text : "centroid models generate useful text. centroid models learn from examples.",
        }),
    });
    assert.equal(train.status, 201);
    const trained = (await train.json()) as {
        name: string;
        metadata: {formatVersion: number; libraryVersion : string};
    };
    assert.equal(trained.name, modelName);
    assert.equal(trained.metadata.formatVersion, 1);
    assert.equal(trained.metadata.libraryVersion, "0.2.0");

    try {
        const metadata = await request(`/api/v1/models/${modelName}/metadata`);
        assert.equal(metadata.status, 200);
        assert.equal((await metadata.json()).name, modelName);

        const generate = await request(`/api/v1/models/${modelName}/generate`, {
            method : "POST",
            headers : {"content-type" : "application/json"},
            body : JSON.stringify({prompt : "centroid models", maxTokens : 8, seed : "42"}),
        });
        assert.equal(generate.status, 200);
        const generated = (await generate.json()) as {continuation : string};
        assert.equal(typeof generated.continuation, "string");

        const download = await request(`/api/v1/models/${modelName}`);
        assert.equal(download.status, 200);
        assert.equal(download.headers.get("content-type"), "application/vnd.centroid-gai.model");
        assert.equal(Buffer.from(await download.arrayBuffer()).subarray(0, 4).toString("ascii"),
                     "CGAI");
    } finally {
        const deleted = await request(`/api/v1/models/${modelName}`, {method : "DELETE"});
        assert.equal(deleted.status, 200);
        assert.deepEqual(await deleted.json(), {deleted : true});
    }

    const missing = await request(`/api/v1/models/${modelName}`);
    assert.equal(missing.status, 404);
});

test("API rejects malformed training requests", {skip : !apiUrl}, async () => {
    const response = await request(`/api/v1/models/${modelName}/train`, {
        method : "POST",
        headers : {"content-type" : "application/json"},
        body : JSON.stringify({}),
    });
    assert.equal(response.status, 400);
    assert.deepEqual(await response.json(), {
        error : "training text must be a non-empty string",
    });
});

test("versioned system and catalog endpoints expose REST domains", {skip : !apiUrl}, async () => {
    const schema = await request("/api/v1/native/schema");
    assert.equal(schema.status, 200);
    assert.equal((await schema.json()).title, "Centroid-GAI ModelArtifact metadata");

    const models = await request("/api/v1/models");
    assert.equal(models.status, 200);
    assert.ok(Array.isArray(await models.json()));
});