import { expect, test } from '@playwright/test';

const models = [
    { id: 'model-alpha', name: 'alpha', dimensions: 12, centroidCount: 4, contextWindow: 2, vocabularySize: '100', examplesSeen: '9007199254740993', formatVersion: 1, libraryVersion: '1.0', checksumSha256: 'a'.repeat(64), createdAt: '2026-09-01T12:00:00Z', updatedAt: '2026-09-02T12:00:00Z' },
    { id: 'model-beta', name: 'beta', dimensions: 24, centroidCount: 8, contextWindow: 4, vocabularySize: '200', examplesSeen: '0', formatVersion: 1, libraryVersion: '1.0', checksumSha256: 'b'.repeat(64), createdAt: '2026-09-01T12:00:00Z', updatedAt: '2026-09-03T12:00:00Z' },
];

test.beforeEach(async ({ page, context }) => {
    await context.route('**/api/v1/**', (route) => {
        const path = new URL(route.request().url()).pathname;
        if (path.endsWith('/contents')) {
            if (new URL(route.request().url()).searchParams.get('section') === 'highlights') return route.fulfill({ json: { data: { tokens: { total: 0, offset: 0, limit: 25, items: [] } } } });
            const name = decodeURIComponent(path.split('/')[4]!);
            const model = models.find((item) => item.name === name)!;
            return route.fulfill({ json: { name, checksumSha256: model.checksumSha256, artifactBytes: 500, section: 'summary', composition: null,
                data: { ...model, initializedCentroids: model.centroidCount, seed: '42', storage: { vectors: '192', tokenCounts: '100', clusterSizes: '32' } } } });
        }
        return route.fulfill({ json: path.endsWith('/models') ? models : { status: 'ok', native: true, database: 'postgresql' } });
    });
    await page.goto('/');
    await page.getByRole('button', { name: 'Inspector', exact: true }).click();
});

test('preserves separate drafts while switching models and app tabs', async ({ page }) => {
    await page.getByLabel('Add labels', { exact: true }).fill('alpha-draft');
    await page.getByRole('button', { name: 'beta: 200 tokens', exact: true }).click();
    await expect(page.getByLabel('Add labels', { exact: true })).toHaveValue('');
    await page.getByLabel('Add labels', { exact: true }).fill('beta-draft');
    await page.getByRole('button', { name: 'Text generation', exact: true }).click();
    await page.getByRole('button', { name: 'Resume alpha', exact: true }).click();
    await expect(page.getByLabel('Add labels', { exact: true })).toHaveValue('alpha-draft');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByRole('button', { name: 'Resume alpha', exact: true })).toHaveCount(0);
    await page.getByRole('button', { name: 'Resume beta', exact: true }).click();
    await expect(page.getByLabel('Add labels', { exact: true })).toHaveValue('beta-draft');
    await page.getByRole('button', { name: 'Discard changes' }).click();
    await expect(page.getByText('Unsaved label drafts:')).toHaveCount(0);
    await expect(page.getByRole('meter')).toHaveAttribute('aria-valuenow', '50');
});

test('syncs saved labels between tabs and preserves edits to different models', async ({ page, context }) => {
    const second = await context.newPage();
    await second.goto('/');
    await second.getByRole('button', { name: 'Inspector', exact: true }).click();
    await second.getByRole('button', { name: 'beta: 200 tokens', exact: true }).click();
    await second.getByLabel('Add labels', { exact: true }).fill('beta-label');
    await page.getByLabel('Add labels', { exact: true }).fill('alpha-label');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(second.getByRole('meter')).toHaveAttribute('aria-valuenow', '50');
    await expect(second.getByLabel('Add labels', { exact: true })).toHaveValue('beta-label');
    await second.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByRole('meter')).toHaveAttribute('aria-valuenow', '100');
    await page.reload();
    await page.getByLabel('Filter by label').selectOption('label:alpha-label');
    await expect(page.getByTestId('model-row')).toHaveAttribute('data-model-name', 'alpha');
    await page.getByLabel('Filter by label').selectOption('label:beta-label');
    await expect(page.getByTestId('model-row')).toHaveAttribute('data-model-name', 'beta');
});

test('detects conflicting edits to the same model without overwriting saved labels', async ({ page, context }) => {
    const second = await context.newPage();
    await second.goto('/');
    await second.getByRole('button', { name: 'Inspector', exact: true }).click();
    await page.getByLabel('Add labels', { exact: true }).fill('my-draft');
    await second.getByLabel('Add labels', { exact: true }).fill('saved-elsewhere');
    await second.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByRole('alert')).toContainText('Saved labels changed in another tab.');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByRole('alert')).toContainText('This model’s labels changed in another tab.');
    await expect(page.getByLabel('Add labels', { exact: true })).toHaveValue('my-draft');
    await page.getByRole('button', { name: 'Discard changes' }).click();
    await expect(page.getByRole('button', { name: 'Remove label saved-elsewhere' })).toBeVisible();
    await expect(page.getByRole('button', { name: 'Remove label my-draft' })).toHaveCount(0);
});

test('does not overwrite malformed saved data', async ({ page }) => {
    await page.evaluate(() => localStorage.setItem('centroid-gai:model-labels:v1', 'broken data'));
    await page.getByLabel('Add labels', { exact: true }).fill('baseline');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByText('Labels could not be saved. Your draft is still here.')).toBeVisible();
    expect(await page.evaluate(() => localStorage.getItem('centroid-gai:model-labels:v1'))).toBe('broken data');
});

test('filters comparisons from label distribution while retaining the all-model scale', async ({ page }) => {
    await page.getByLabel('Add labels', { exact: true }).fill('baseline, reviewed');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await page.getByText('Label distribution', { exact: true }).click();
    await page.getByRole('button', { name: 'Compare baseline: 1 of 2 models', exact: true }).click();
    const chart = page.getByRole('list', { name: 'Vocabulary size comparison' });
    await expect(chart.getByRole('button')).toHaveCount(1);
    await expect(chart.getByRole('button')).toHaveAccessibleName('alpha: 100 tokens');
    await expect(chart.locator('[aria-hidden] > span')).toHaveAttribute('style', 'width: 50%;');
    await page.getByLabel('Comparison group').selectOption('unlabeled');
    await expect(chart.getByRole('button')).toHaveAccessibleName('beta: 200 tokens');
    await expect(page.getByText('The selected model is outside this group.')).toBeVisible();
    await page.getByRole('button', { name: 'Show all models', exact: true }).click();
    await expect(chart.getByRole('button')).toHaveCount(2);
    await page.getByLabel('Comparison group').selectOption('label:baseline');
    await page.getByRole('button', { name: 'Remove label baseline' }).click();
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByText('No models in this group. Choose another group or update model labels.')).toBeVisible();
    await expect(chart.getByRole('button')).toHaveCount(0);
});

test('saves normalized labels, filters by them, and retains them after reload', async ({ page }) => {
    await page.getByLabel('Add labels', { exact: true }).fill(' Baseline, NEEDS-review, baseline ');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByRole('status')).toHaveText('Labels saved in this browser.');
    await expect(page.getByRole('button', { name: 'Remove label baseline' })).toHaveCount(1);
    await expect(page.getByRole('meter', { name: 'Label coverage' })).toHaveAttribute('aria-valuenow', '50');
    await page.getByLabel('Filter by label').selectOption('label:baseline');
    await expect(page.getByTestId('model-row')).toHaveCount(1);
    await expect(page.getByTestId('model-row')).toHaveAttribute('data-model-name', 'alpha');
    await page.reload();
    await page.getByTestId('model-search').fill('needs-review');
    await expect(page.getByTestId('model-row')).toHaveCount(1);
    await page.getByRole('button', { name: 'Edit labels', exact: true }).click();
    await expect(page.getByRole('button', { name: 'Remove label baseline' })).toBeVisible();
    await page.getByRole('button', { name: 'Remove label baseline' }).click();
    await page.getByRole('button', { name: 'Remove label needs-review' }).click();
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByTestId('model-row')).toHaveCount(0);
    await page.getByRole('button', { name: /Clear filters/ }).click();
    await page.getByLabel('Filter by label').selectOption('__unlabeled');
    await expect(page.getByTestId('model-row')).toHaveCount(2);
});

test('reuses labels on another model and discards a draft without changing saved labels', async ({ page }) => {
    await page.getByLabel('Add labels', { exact: true }).fill('baseline');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await page.getByRole('button', { name: 'beta: 200 tokens', exact: true }).click();
    await expect(page.getByRole('button', { name: 'Remove label baseline' })).toHaveCount(0);
    await page.getByRole('button', { name: 'baseline', exact: true }).click();
    await page.getByRole('button', { name: 'Discard changes' }).click();
    await expect(page.getByRole('button', { name: 'Remove label baseline' })).toHaveCount(0);
    await page.getByRole('button', { name: 'baseline', exact: true }).click();
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByRole('meter')).toHaveAttribute('aria-valuenow', '100');
});

test('validates label limits without losing the draft', async ({ page }) => {
    await page.getByLabel('Add labels', { exact: true }).fill('one,two,three,four,five,six,seven,eight,nine');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByRole('alert')).toHaveText('Use up to 8 labels per model.');
    await page.getByLabel('Add labels', { exact: true }).fill('x'.repeat(33));
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByRole('alert')).toHaveText('Keep each label to 32 characters or fewer.');
    await expect(page.getByLabel('Add labels', { exact: true })).toHaveValue('x'.repeat(33));
    await expect(page.getByRole('meter')).toHaveAttribute('aria-valuenow', '0');
});

test('saves and advances through the unlabeled queue without carrying over a draft', async ({ page }) => {
    await page.getByLabel('Filter by label').selectOption('__unlabeled');
    await page.getByLabel('Add labels', { exact: true }).fill('baseline');
    await page.getByRole('button', { name: 'Save & next unlabeled' }).click();
    await expect(page.getByRole('heading', { name: 'beta', exact: true })).toBeVisible();
    await expect(page.getByTestId('model-row')).toHaveCount(1);
    await expect(page.getByLabel('Add labels', { exact: true })).toHaveValue('');
    await page.getByLabel('Add labels', { exact: true }).fill('reviewed');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByTestId('model-row')).toHaveCount(0);
    await expect(page.getByRole('meter')).toHaveAttribute('aria-valuenow', '100');
});

test('keeps the draft and reports failure when browser storage is unavailable', async ({ page }) => {
    await page.evaluate(() => { Storage.prototype.setItem = () => { throw new DOMException('Storage full', 'QuotaExceededError'); }; });
    await page.getByLabel('Add labels', { exact: true }).fill('baseline');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await expect(page.getByText('Labels could not be saved. Your draft is still here.')).toBeVisible();
    await expect(page.getByLabel('Add labels', { exact: true })).toHaveValue('baseline');
    await expect(page.getByRole('status')).not.toContainText('Labels saved');
});

test('compares metrics on a shared scale, preserves large counters, and selects a model', async ({ page }) => {
    const chart = page.getByRole('list', { name: 'Vocabulary size comparison' });
    await expect(chart.getByRole('button').first()).toHaveAccessibleName('beta: 200 tokens');
    await expect(chart.getByRole('button').first().locator('[aria-hidden] > span')).toHaveAttribute('style', /width: 100%/);
    await expect(chart.getByRole('button').last().locator('[aria-hidden] > span')).toHaveAttribute('style', /width: 50%/);
    await chart.getByRole('button').first().click();
    await expect(page.getByRole('heading', { name: 'beta', exact: true })).toBeVisible();
    await page.getByLabel('Compare by').selectOption('examplesSeen');
    const examples = page.getByRole('list', { name: 'Examples seen comparison' });
    const exact = BigInt(models[0].examplesSeen).toLocaleString();
    await expect(examples.getByRole('button').first()).toHaveAccessibleName(`alpha: ${exact} examples`);
    await expect(examples.getByRole('button').last().locator('[aria-hidden] > span')).toHaveAttribute('style', /width: 0%/);
    await examples.getByRole('button').first().click();
    await expect(page.locator('[data-label="Examples seen"]')).toContainText(exact);
});

test('handles an all-zero metric without invalid bar widths', async ({ page }) => {
    await page.route('**/api/v1/models', (route) => route.fulfill({ json: models.map((model) => ({ ...model, examplesSeen: '0' })) }));
    await page.reload();
    await page.getByRole('button', { name: 'Inspector', exact: true }).click();
    await page.getByLabel('Compare by').selectOption('examplesSeen');
    for (const bar of await page.getByRole('list', { name: 'Examples seen comparison' }).locator('[aria-hidden] > span').all()) {
        await expect(bar).toHaveAttribute('style', /width: 0%/);
    }
});

test('labels and comparisons fit desktop and mobile viewports', async ({ page }, testInfo) => {
    await page.setViewportSize({ width: 1440, height: 1000 });
    await page.getByLabel('Add labels', { exact: true }).fill('baseline, needs-review');
    await page.getByRole('button', { name: 'Save labels', exact: true }).click();
    await page.screenshot({ path: testInfo.outputPath('labels-desktop.png'), fullPage: true });
    await page.setViewportSize({ width: 390, height: 844 });
    await expect(page.getByLabel('Add labels', { exact: true })).toBeVisible();
    await expect(page.getByRole('button', { name: 'Save labels', exact: true })).toBeVisible();
    expect(await page.evaluate('document.documentElement.scrollWidth <= window.innerWidth')).toBe(true);
    await page.screenshot({ path: testInfo.outputPath('labels-mobile.png'), fullPage: true });
    await page.getByLabel('Compare by').selectOption('dimensions');
    await page.getByRole('button', { name: 'beta: 24 dimensions', exact: true }).click();
    await page.screenshot({ path: testInfo.outputPath('comparison-mobile.png'), fullPage: true });
    await expect(page.getByRole('heading', { name: 'beta', exact: true })).toBeVisible();
});
