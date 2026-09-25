import { expect, test } from '@playwright/test';
import { deleteViaApi, trainViaApi, uniqueName } from './helpers.ts';

test.describe('artifact inspector interactions', () => {
    test.beforeEach(async ({ context, page }) => {
        const models = ['alpha', 'beta', 'gamma'].map((name, index) => ({ id: name, name, dimensions: 4, centroidCount: index === 1 ? 3 : 2, contextWindow: 2, vocabularySize: '30', examplesSeen: '6', formatVersion: 1, libraryVersion: '0.2.0', checksumSha256: String(index + 1).repeat(64), createdAt: '2026-09-25T12:00:00Z', updatedAt: '2026-09-25T12:00:00Z' }));
        let composition: unknown = null;
        await context.route('**/api/v1/**', async (route) => {
            const url = new URL(route.request().url());
            if (url.pathname.endsWith('/health')) return route.fulfill({ json: { status: 'ok', native: true, database: 'postgresql' } });
            if (url.pathname.endsWith('/models')) return route.fulfill({ json: models });
            const name = decodeURIComponent(url.pathname.split('/')[4] ?? '');
            if (url.pathname.endsWith('/merge')) {
                const input = route.request().postDataJSON();
                const output = { ...models[0], id: name, name, centroidCount: input.targetCentroids || 5, checksumSha256: 'f'.repeat(64) };
                models.push(output);
                composition = { version: 1, autoRebuild: input.autoRebuild, algorithm: input.targetCentroids ? 'weighted-streaming-v1' : 'preserve', targetCentroids: output.centroidCount,
                    sources: input.sources.map((source: { name: string; checksumSha256: string }) => ({ ...source, initializedCentroids: 2, examplesSeen: '6' })) };
                return route.fulfill({ status: 201, json: { name, id: name, metadata: output, composition } });
            }
            const model = models.find((item) => item.name === name)!;
            const section = url.searchParams.get('section') ?? 'summary';
            const offset = Number(url.searchParams.get('offset') ?? 0);
            let data: unknown;
            if (section === 'summary') data = { dimensions: 4, centroidCount: model.centroidCount, initializedCentroids: model.centroidCount, contextWindow: 2, seed: name === 'gamma' ? '99' : '42', vocabularySize: '30', examplesSeen: '6', storage: { vectors: '32', tokenCounts: '480', clusterSizes: '16' } };
            if (section === 'centroids') data = { offset, limit: 25, total: model.centroidCount, items: Array.from({ length: model.centroidCount }, (_, id) => ({ id, observations: '3', norm: 0.5, distinctTargets: 2 })) };
            if (section === 'vocabulary') data = { offset, limit: 25, total: 30, items: Array.from({ length: Math.min(25, 30 - offset) }, (_, index) => ({ id: offset + index, token: `${name}-token-${offset + index}`, count: '2' })) };
            if (section === 'centroid') data = { id: Number(url.searchParams.get('centroid')), observations: '3', vector: [0.4, -0.2, 0.1, -0.5], tokens: { offset: 0, limit: 25, total: 2, items: [{ id: 4, token: 'shared', count: '2' }, { id: 5, token: 'other', count: '1' }] } };
            return route.fulfill({ json: { name, section, checksumSha256: model.checksumSha256, artifactBytes: 700, composition: name.startsWith('superset') ? composition : null, data } });
        });
        await page.goto('/');
        await page.getByRole('button', { name: 'Inspector', exact: true }).click();
    });

    test('drills into centroid vectors and paged vocabulary on desktop and mobile', async ({ page }, testInfo) => {
        await page.setViewportSize({ width: 1440, height: 1100 });
        await page.getByRole('button', { name: 'Artifact contents', exact: true }).click();
        await expect(page.getByText('Storage breakdown', { exact: true })).toBeVisible();
        await page.getByRole('button', { name: 'Inspect centroid 0', exact: true }).click();
        const detail = page.getByRole('region', { name: 'Centroid 0 details', exact: true });
        await expect(detail.getByRole('img')).toBeVisible();
        await expect(detail.getByRole('cell', { name: '66.66%' })).toBeVisible();
        await detail.getByText('Exact vector values').click();
        await expect(detail.locator('pre')).toContainText('1: -0.2');
        await page.screenshot({ path: testInfo.outputPath('artifact-desktop.png'), fullPage: true });
        await page.getByRole('button', { name: 'Vocabulary', exact: true }).click();
        await expect(page.getByRole('cell', { name: 'alpha-token-0', exact: false })).toBeVisible();
        await page.getByRole('button', { name: 'Next page', exact: true }).click();
        await expect(page.getByRole('cell', { name: 'alpha-token-25', exact: false })).toBeVisible();
        await expect(page.getByRole('cell', { name: 'alpha-token-0', exact: false })).toHaveCount(0);
        await page.setViewportSize({ width: 390, height: 844 });
        expect(await page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')).toBe(true);
        await page.screenshot({ path: testInfo.outputPath('artifact-mobile.png'), fullPage: true });
    });

    test('rejects incompatible sources, reorders compatible ones, and opens the composed result', async ({ page }, testInfo) => {
        await page.setViewportSize({ width: 1440, height: 1100 });
        await page.getByRole('button', { name: 'Compose superset', exact: true }).click();
        await expect(page.getByRole('checkbox', { name: /Automatically rebuild/ })).toBeChecked();
        await page.getByRole('checkbox', { name: /gamma/ }).check();
        await page.getByLabel('New superset name').fill('superset-one');
        await expect(page.getByText(/Sources must share dimensions/)).toBeVisible();
        await expect(page.getByRole('button', { name: 'Create superset', exact: true })).toBeDisabled();
        await page.getByRole('checkbox', { name: /gamma/ }).uncheck();
        await page.getByRole('checkbox', { name: /beta/ }).check();
        await page.getByRole('button', { name: 'Move beta earlier', exact: true }).click();
        await page.getByLabel('Merge strategy').selectOption('compact');
        await page.getByLabel('Target centroids').fill('1');
        await expect(page.getByText('5 source centroids → 1 output centroids')).toBeVisible();
        await page.screenshot({ path: testInfo.outputPath('composer-desktop.png'), fullPage: true });
        const request = page.waitForRequest((req) => req.url().endsWith('/superset-one/merge'));
        await page.getByRole('button', { name: 'Create superset', exact: true }).click();
        expect((await request).postDataJSON()).toEqual({ autoRebuild: true, sources: [{ name: 'beta', checksumSha256: '2'.repeat(64) }, { name: 'alpha', checksumSha256: '1'.repeat(64) }], targetCentroids: 1 });
        await expect(page.getByRole('heading', { name: 'superset-one', exact: true })).toBeVisible();
        await expect(page.getByRole('heading', { name: 'Composition recipe', exact: true })).toBeVisible();
        await expect(page.getByText(/Approximate weighted compaction/)).toBeVisible();
        await expect(page.getByText(/Live super model: changed sources/)).toBeVisible();
        await page.getByRole('button', { name: 'Run model', exact: true }).click();
        await expect(page.getByText('Model: superset-one', { exact: true })).toBeVisible();
    });

    test('surfaces stale artifact errors and returns to label drafts from artifact contents', async ({ page }) => {
        await page.getByLabel('Add labels', { exact: true }).fill('draft-label');
        await page.route('**/models/alpha/contents?*', (route) => route.fulfill({ status: 409, json: { error: 'Artifact changed. Refresh the model catalog before inspecting more pages.' } }));
        await page.getByRole('button', { name: 'Artifact contents', exact: true }).click();
        await expect(page.getByRole('alert').first()).toContainText('Artifact changed.');
        await page.getByRole('button', { name: 'Resume alpha', exact: true }).click();
        await expect(page.getByLabel('Add labels', { exact: true })).toHaveValue('draft-label');
    });

    test('an old model response cannot replace the selected model contents', async ({ page }) => {
        let release!: () => void;
        let started!: () => void;
        const gate = new Promise<void>((resolve) => { release = resolve; });
        const intercepted = new Promise<void>((resolve) => { started = resolve; });
        await page.route('**/models/alpha/contents?*', async (route) => {
            if (new URL(route.request().url()).searchParams.get('section') !== 'vocabulary') return route.fallback();
            started(); await gate;
            await route.fulfill({ json: { name: 'alpha', section: 'vocabulary', data: { total: 1, offset: 0, limit: 25, items: [{ id: 0, token: 'stale-alpha', count: '1' }] } } }).catch(() => {});
        });
        await page.getByRole('button', { name: 'Artifact contents', exact: true }).click();
        await page.getByRole('button', { name: 'Vocabulary', exact: true }).click();
        await intercepted;
        await page.locator('[data-model-name="beta"]').getByRole('button').first().click();
        release();
        await page.getByRole('button', { name: 'Vocabulary', exact: true }).click();
        await expect(page.getByRole('cell', { name: 'beta-token-0', exact: false })).toBeVisible();
        await expect(page.getByText('stale-alpha', { exact: true })).toHaveCount(0);
    });
});

test('creates a superset through the real C/API/database stack', async ({ page }) => {
    const a = uniqueName('e2e-merge-a'), b = uniqueName('e2e-merge-b'), destination = uniqueName('e2e-superset');
    try {
        await trainViaApi(a, 'alpha shared vocabulary alpha contexts are useful.');
        await trainViaApi(b, 'beta shared vocabulary beta contexts learn patterns.');
        await page.goto('/');
        await page.getByTestId('model-search').fill(a);
        await page.locator('[data-model-name]').getByRole('button').first().click();
        await page.getByRole('button', { name: 'Inspector', exact: true }).click();
        await page.getByRole('button', { name: 'Compose superset', exact: true }).click();
        await page.getByLabel('Find source models').fill(b);
        await page.getByRole('checkbox', { name: new RegExp(b) }).check();
        await page.getByLabel('Merge strategy').selectOption('compact');
        await page.getByLabel('Target centroids').fill('3');
        await page.getByLabel('New superset name').fill(destination);
        await page.getByRole('button', { name: 'Create superset', exact: true }).click();
        await expect(page.getByRole('heading', { name: destination, exact: true })).toBeVisible();
        await expect(page.getByRole('heading', { name: 'Composition recipe', exact: true })).toBeVisible();
        await page.getByRole('button', { name: 'Inspect centroid 0', exact: true }).click();
        await expect(page.getByRole('region', { name: 'Centroid 0 details', exact: true }).getByRole('img')).toBeVisible();
        await trainViaApi(a, 'newword alpha changed training corpus adds new learned patterns and vocabulary.');
        await page.getByRole('button', { name: 'Run model', exact: true }).click();
        await page.getByTestId('chat-input').fill('newword');
        await page.getByRole('button', { name: 'Send', exact: true }).click();
        await expect(page.locator('[data-testid="chat-message"][data-role="assistant"]')).toBeVisible();
        await page.getByRole('button', { name: 'Inspector', exact: true }).click();
        // The inspector refreshes its pinned checksum after the automatic rebuild.
        await expect(page.getByText(/Live super model: changed sources/)).toBeVisible();
        await page.getByRole('button', { name: 'Vocabulary', exact: true }).click();
        await expect(page.getByRole('cell', { name: 'newword', exact: true })).toBeVisible();
    } finally {
        await deleteViaApi(destination); await deleteViaApi(a); await deleteViaApi(b);
    }
});
