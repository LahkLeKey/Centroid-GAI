import { expect, test } from '@playwright/test';

const source = { id: 'source', name: 'source', dimensions: 8, centroidCount: 2, contextWindow: 2, vocabularySize: '6', examplesSeen: '8', formatVersion: 1, libraryVersion: '0.2.0', checksumSha256: 'a'.repeat(64), createdAt: '2026-09-25T12:00:00Z', updatedAt: '2026-09-25T12:00:00Z', composition: null };
const superset = { ...source, id: 'combined', name: 'combined', checksumSha256: 'b'.repeat(64), composition: { autoRebuild: true, sourceCount: 2 } };
const matchData = { inputTokens: 3, unknownTokens: 1, context: [{ token: 'shared', known: true }, { token: 'unfamiliar', known: false }], matches: [
    { centroidId: 1, squaredDistance: 0.125, observations: '4', targets: { offset: 0, limit: 10, total: 2, items: [{ id: 3, token: 'alpha', count: '3' }, { id: 4, token: 'beta', count: '1' }] } },
] };

test.beforeEach(async ({ context }) => {
    await context.route('**/api/v1/**', (route) => {
        const url = new URL(route.request().url());
        if (url.pathname.endsWith('/health')) return route.fulfill({ json: { status: 'ok', native: true, database: 'postgresql' } });
        if (url.pathname.endsWith('/models')) return route.fulfill({ json: [source, superset] });
        const name = url.pathname.split('/')[4];
        const model = name === 'combined' ? superset : source;
        if (url.pathname.endsWith('/match')) return route.fulfill({ json: { name, checksumSha256: model.checksumSha256, data: matchData } });
        if (url.pathname.endsWith('/metadata')) return route.fulfill({ json: model });
        const section = url.searchParams.get('section');
        const data = section === 'highlights' ? { tokens: matchData.matches[0]!.targets }
            : section === 'summary' ? { ...model, initializedCentroids: 2, seed: '42', storage: { vectors: '64', tokenCounts: '96', clusterSizes: '16' } }
            : section === 'centroid' ? { id: 1, observations: '4', vector: [0.1, 0.2, 0.3, 0.4, 0.1, 0.2, 0.3, 0.4], tokens: matchData.matches[0]!.targets }
            : { offset: 0, limit: 25, total: 2, items: [0, 1].map((id) => ({ id, observations: '4', norm: 0.5, distinctTargets: 2 })) };
        return route.fulfill({ json: { name, checksumSha256: model.checksumSha256, artifactBytes: 800, composition: null, section, data } });
    });
});

test('lands on supersets, selects a superset before ordinary sources, and matches without generation', async ({ page }, testInfo) => {
    const generations: string[] = [];
    page.on('request', (request) => { if (request.url().endsWith('/generate')) generations.push(request.url()); });
    await page.goto('/');
    await expect(page.getByRole('textbox')).toHaveCount(1);
    await expect(page.getByRole('button', { name: 'Explore alpha', exact: true })).toBeVisible();
    await expect(page.getByRole('button', { name: 'Supersets', exact: true })).toHaveAttribute('aria-pressed', 'true');
    await expect(page.getByRole('heading', { name: 'Superset workspace' })).toBeVisible();
    await expect(page.getByText('Model: combined', { exact: true })).toBeVisible();
    await expect(page.getByTestId('chat-input')).toHaveCount(0);
    await page.screenshot({ path: testInfo.outputPath('superset-workspace.png'), fullPage: true });
    await page.getByRole('button', { name: /Match learned patterns/ }).click();
    await page.getByLabel('Input context').fill('earlier shared unfamiliar');
    const request = page.waitForRequest((item) => item.url().endsWith('/combined/match'));
    await page.getByRole('button', { name: 'Find matches', exact: true }).click();
    expect((await request).postDataJSON()).toEqual({ text: 'earlier shared unfamiliar', limit: 5 });
    await expect(page.getByRole('heading', { name: '1. Centroid 1' })).toBeVisible();
    await expect(page.getByText('unfamiliar → <unk>', { exact: true })).toBeVisible();
    await expect(page.getByRole('cell', { name: '75.00%' })).toBeVisible();
    await page.setViewportSize({ width: 390, height: 844 });
    expect(await page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')).toBe(true);
    await page.screenshot({ path: testInfo.outputPath('pattern-matches-mobile.png'), fullPage: true });
    await page.getByRole('button', { name: 'Inspect centroid 1', exact: true }).click();
    await expect(page.getByRole('region', { name: 'Centroid 1 details' })).toBeVisible();
    expect(generations).toEqual([]);
});

test('the first superset can be composed directly from the default view', async ({ page }) => {
    await page.route('**/api/v1/models', (route) => route.fulfill({ json: [source] }));
    await page.goto('/');
    await expect(page.getByRole('heading', { name: 'Compose a model superset' })).toHaveCount(0);
    await page.getByRole('button', { name: 'Compose a superset', exact: true }).click();
    await expect(page.getByRole('heading', { name: 'Compose a model superset' })).toBeVisible();
    await expect(page.getByRole('checkbox', { name: /^source / })).toBeChecked();
    await expect(page.getByRole('checkbox', { name: /Automatically rebuild/ })).toBeChecked();
    await page.getByLabel('New superset name').fill('first-super');
    await expect(page.getByRole('button', { name: 'Create superset', exact: true })).toBeEnabled();
});

test('discovers patterns through observed tokens and follows target tokens without typing', async ({ page }) => {
    await page.goto('/');
    const first = page.waitForRequest((request) => request.url().endsWith('/combined/match'));
    await page.getByRole('button', { name: 'Explore alpha', exact: true }).click();
    expect((await first).postDataJSON().text).toBe('alpha');
    await expect(page.getByText('Input: alpha', { exact: true })).toBeVisible();
    const next = page.waitForRequest((request) => request.url().endsWith('/combined/match'));
    await page.getByRole('button', { name: 'Explore target beta', exact: true }).click();
    expect((await next).postDataJSON().text).toBe('beta');
    await expect(page.getByText('Input: beta', { exact: true })).toBeVisible();
});

test('empty catalogs offer source creation and matching has an empty state', async ({ page }) => {
    await page.route('**/api/v1/models', (route) => route.fulfill({ json: [] }));
    await page.goto('/');
    await page.getByRole('button', { name: 'Add source model', exact: true }).click();
    await expect(page.getByRole('dialog', { name: 'New model' })).toBeVisible();
    await page.getByRole('dialog').getByRole('button', { name: '✕' }).click();
    await page.getByRole('button', { name: 'Match patterns', exact: true }).click();
    await expect(page.getByText('Select or create a model to match patterns.')).toBeVisible();
    await expect(page.getByRole('button', { name: 'Find matches', exact: true })).toHaveCount(0);
});

test('match errors remain actionable and retry succeeds', async ({ page }) => {
    let fail = true;
    await page.route('**/combined/match', (route) => fail ? route.fulfill({ status: 400, json: { error: 'Cannot rebuild combined: source model missing is missing.' } }) : route.fallback());
    await page.goto('/');
    await page.getByRole('button', { name: 'Match patterns', exact: true }).click();
    await page.getByLabel('Input context').fill('shared');
    await page.getByRole('button', { name: 'Find matches', exact: true }).click();
    await expect(page.getByRole('alert')).toContainText('Cannot rebuild combined');
    fail = false;
    await page.getByRole('button', { name: 'Find matches', exact: true }).click();
    await expect(page.getByRole('heading', { name: 'Matched context' })).toBeVisible();
    await expect(page.getByRole('alert')).toHaveCount(0);
});

test('changing models aborts an in-flight match and clears the old result', async ({ page }) => {
    let release!: () => void;
    const gate = new Promise<void>((resolve) => { release = resolve; });
    await page.route('**/combined/match', async (route) => {
        await gate;
        await route.fulfill({ json: { name: 'combined', checksumSha256: superset.checksumSha256, data: matchData } }).catch(() => {});
    });
    await page.goto('/');
    await page.getByRole('button', { name: 'Match patterns', exact: true }).click();
    await page.getByLabel('Input context').fill('old model');
    const started = page.waitForRequest((request) => request.url().endsWith('/combined/match'));
    await page.getByRole('button', { name: 'Find matches', exact: true }).click();
    await started;
    await page.locator('[data-model-name="source"]').getByRole('button').first().click();
    release();
    await expect(page.getByText('Model: source', { exact: true })).toBeVisible();
    await expect(page.getByLabel('Input context')).toHaveValue('');
    await expect(page.getByRole('heading', { name: 'Matched context' })).toHaveCount(0);
    await page.getByLabel('Input context').fill('fresh model');
    await page.getByRole('button', { name: 'Find matches', exact: true }).click();
    await expect(page.getByText('Input: fresh model', { exact: true })).toBeVisible();
});
