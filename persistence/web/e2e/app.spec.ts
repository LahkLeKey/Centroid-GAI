import {expect, test} from '@playwright/test';
import path from 'node:path';
import {fileURLToPath} from 'node:url';

import {deleteViaApi, SAMPLE_TRAINING_TEXT, trainViaApi, uniqueName} from './helpers.ts';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const TINY_CGAI_PATH = path.resolve(__dirname, '../../../build/tiny.cgai');

test.describe('health and shell', () => {
    test('shows a healthy status indicator on load', async ({page}) => {
        await page.goto('/');
        await expect(page.getByText(/native ok/)).toBeVisible();
        await expect(page.getByRole('heading', {name : 'Centroid-GAI'})).toBeVisible();
    });
});

test.describe('model catalog', () => {
    const modelA = uniqueName('e2e-cat-a');
    const modelB = uniqueName('e2e-cat-b');

    test.beforeAll(async () => {
        await trainViaApi(modelA, SAMPLE_TRAINING_TEXT);
        await trainViaApi(modelB, SAMPLE_TRAINING_TEXT);
    });

    test.afterAll(async () => {
        await deleteViaApi(modelA);
        await deleteViaApi(modelB);
    });

    test('lists persisted models in the sidebar', async ({page}) => {
        await page.goto('/');
        const sidebar = page.getByRole('complementary');
        await expect(sidebar.locator('[data-testid="model-row"]', {
            hasText : modelA
        })).toBeVisible();
        await expect(sidebar.locator('[data-testid="model-row"]', {
            hasText : modelB
        })).toBeVisible();
    });

    test('search filters the model list', async ({page}) => {
        await page.goto('/');
        const sidebar = page.getByRole('complementary');
        await sidebar.getByTestId('model-search').fill(modelA);
        await expect(sidebar.locator('[data-testid="model-row"]', {
            hasText : modelA
        })).toBeVisible();
        await expect(sidebar.locator('[data-testid="model-row"]', {
            hasText : modelB
        })).toHaveCount(0);
    });

    test('search with no matches shows an empty state', async ({page}) => {
        await page.goto('/');
        const sidebar = page.getByRole('complementary');
        await sidebar.getByTestId('model-search').fill('no-such-model-xyz');
        await expect(sidebar.getByText('No matches.')).toBeVisible();
    });

    test('selecting a model updates the chat header and inspector', async ({page}) => {
        await page.goto('/');
        const sidebar = page.getByRole('complementary');
        await sidebar.locator('[data-testid="model-row"]', {hasText : modelA}).click();
        await expect(page.getByText(`Model: ${modelA}`)).toBeVisible();

        await page.getByRole('button', {name : 'Inspector'}).click();
        await expect(page.getByRole('heading', {name : modelA})).toBeVisible();
    });

    test('switching models clears the previous conversation', async ({page}) => {
        await page.goto('/');
        const sidebar = page.getByRole('complementary');
        await sidebar.locator('[data-testid="model-row"]', {hasText : modelA}).click();
        await page.getByTestId('chat-input').fill('hello from a');
        await page.getByRole('button', {name : 'Send'}).click();
        await expect(page.getByTestId('chat-message').first()).toBeVisible();

        await sidebar.locator('[data-testid="model-row"]', {hasText : modelB}).click();
        await expect(page.getByTestId('chat-message')).toHaveCount(0);
    });

    test('deleting a model removes it from the sidebar', async ({page}) => {
        const disposable = uniqueName('e2e-cat-del');
        await trainViaApi(disposable, SAMPLE_TRAINING_TEXT);
        await page.goto('/');
        const sidebar = page.getByRole('complementary');
        const row = sidebar.locator('[data-testid="model-row"]', {hasText : disposable});
        await expect(row).toBeVisible();
        await row.getByRole('button', {name : 'Delete model'}).click();
        await expect(row).toHaveCount(0);
    });
});

test.describe('train workflow', () => {
    test('training a model via the UI persists and selects it', async ({page}) => {
        const name = uniqueName('e2e-train');
        await page.goto('/');
        await page.getByRole('button', {name : 'New model'}).click();
        await page.getByLabel('Model name').fill(name);
        await page.getByLabel('Training text').fill(SAMPLE_TRAINING_TEXT);
        await page.getByRole('button', {name : 'Train & persist'}).click();

        await expect(page.getByText(`Model: ${name}`)).toBeVisible({timeout : 15_000});
        await expect(page.getByRole('dialog')).toHaveCount(0);

        await deleteViaApi(name);
    });

    test('shows a validation error when required fields are missing', async ({page}) => {
        await page.goto('/');
        await page.getByRole('button', {name : 'New model'}).click();
        await page.getByRole('button', {name : 'Train & persist'}).click();
        await expect(page.getByText('Model name and training text are required.')).toBeVisible();
    });

    test('surfaces the API error message for an empty corpus', async ({page}) => {
        await page.goto('/');
        await page.getByRole('button', {name : 'New model'}).click();
        await page.getByLabel('Model name').fill(uniqueName('e2e-empty-corpus'));
        await page.getByLabel('Training text').fill(' ');
        await page.getByRole('button', {name : 'Train & persist'}).click();
        await expect(page.getByText('Model name and training text are required.')).toBeVisible();
    });
});

test.describe('upload workflow', () => {
    test('uploading a .cgai artifact persists and selects it', async ({page}) => {
        const name = uniqueName('e2e-upload');
        await page.goto('/');
        await page.getByRole('button', {name : 'New model'}).click();
        await page.getByRole('button', {name : 'Upload artifact'}).click();
        await page.getByLabel('Model name').fill(name);
        await page.getByLabel('.cgai file').setInputFiles(TINY_CGAI_PATH);
        await page.getByRole('button', {name : 'Upload & persist'}).click();

        await expect(page.getByText(`Model: ${name}`)).toBeVisible({timeout : 15_000});

        await deleteViaApi(name);
    });

    test('shows a validation error when no file is chosen', async ({page}) => {
        await page.goto('/');
        await page.getByRole('button', {name : 'New model'}).click();
        await page.getByRole('button', {name : 'Upload artifact'}).click();
        await page.getByLabel('Model name').fill(uniqueName('e2e-no-file'));
        await page.getByRole('button', {name : 'Upload & persist'}).click();
        await expect(page.getByText('Model name and a .cgai file are required.')).toBeVisible();
    });
});

test.describe('chat playground', () => {
    const modelName = uniqueName('e2e-chat');

    test.beforeAll(async () => { await trainViaApi(modelName, SAMPLE_TRAINING_TEXT); });

    test.afterAll(async () => { await deleteViaApi(modelName); });

    test('sends a prompt and displays a continuation', async ({page}) => {
        await page.goto('/');
        await page.getByRole('complementary')
            .locator('[data-testid="model-row"]', {hasText : modelName})
            .click();

        await page.getByTestId('chat-input').fill('centroid models');
        await page.getByRole('button', {name : 'Send'}).click();

        const userBubble = page.locator('[data-testid="chat-message"][data-role="user"]');
        await expect(userBubble).toHaveText('centroid models');

        const assistantBubble = page.locator('[data-testid="chat-message"][data-role="assistant"]');
        await expect(assistantBubble).toBeVisible({timeout : 15_000});
        await expect(assistantBubble).not.toHaveText('');
    });

    test('re-enables the send button once a second reply lands', async ({page}) => {
        await page.goto('/');
        await page.getByRole('complementary')
            .locator('[data-testid="model-row"]', {hasText : modelName})
            .click();

        await page.getByTestId('chat-input').fill('another prompt');
        await page.getByRole('button', {name : 'Send'}).click();
        await expect(page.getByTestId('chat-message').last()).toBeVisible({timeout : 15_000});

        await page.getByTestId('chat-input').fill('yet another prompt');
        await expect(page.getByRole('button', {name : 'Send'})).toBeEnabled();
    });

    test('clear button resets the conversation', async ({page}) => {
        await page.goto('/');
        await page.getByRole('complementary')
            .locator('[data-testid="model-row"]', {hasText : modelName})
            .click();
        await page.getByTestId('chat-input').fill('one more message');
        await page.getByRole('button', {name : 'Send'}).click();
        await expect(page.getByTestId('chat-message').first()).toBeVisible({timeout : 15_000});

        await page.getByRole('button', {name : 'Clear'}).click();
        await expect(page.getByTestId('chat-message')).toHaveCount(0);
    });

    test('settings panel toggles generation parameter fields', async ({page}) => {
        await page.goto('/');
        await page.getByRole('complementary')
            .locator('[data-testid="model-row"]', {hasText : modelName})
            .click();
        await expect(page.getByLabel('Max tokens')).toHaveCount(0);

        await page.getByRole('button', {name : 'Settings'}).click();
        await expect(page.getByLabel('Max tokens')).toBeVisible();
        await expect(page.getByLabel('Temperature')).toBeVisible();
        await expect(page.getByLabel('Seed')).toBeVisible();

        await page.getByRole('button', {name : 'Settings'}).click();
        await expect(page.getByLabel('Max tokens')).toHaveCount(0);
    });
});

test.describe('model inspector', () => {
    const modelName = uniqueName('e2e-inspect');

    test.beforeAll(async () => { await trainViaApi(modelName, SAMPLE_TRAINING_TEXT); });

    test.afterAll(async () => { await deleteViaApi(modelName); });

    test('shows metadata cards matching the persisted model', async ({page}) => {
        await page.goto('/');
        await page.getByRole('complementary')
            .locator('[data-testid="model-row"]', {hasText : modelName})
            .click();
        await page.getByRole('button', {name : 'Inspector'}).click();

        await expect(page.getByRole('heading', {name : modelName})).toBeVisible();
        await expect(page.locator('[data-testid="stat-card"][data-label="Dimensions"]'))
            .toBeVisible();
        await expect(page.locator('[data-testid="stat-card"][data-label="Vocabulary"]'))
            .toBeVisible();
    });

    test('expands the native persistence schema viewer', async ({page}) => {
        await page.goto('/');
        await page.getByRole('complementary')
            .locator('[data-testid="model-row"]', {hasText : modelName})
            .click();
        await page.getByRole('button', {name : 'Inspector'}).click();

        await page.getByRole('button', {name : 'Native persistence schema'}).click();
        await expect(page.getByText('"$schema"')).toBeVisible({timeout : 10_000});
    });
});
