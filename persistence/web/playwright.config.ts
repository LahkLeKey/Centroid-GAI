import {defineConfig, devices} from '@playwright/test';

const port = Number(process.env.PLAYWRIGHT_WEB_PORT ?? '4173');
const apiTarget = process.env.VITE_API_PROXY_TARGET ?? 'http://localhost:3000';

export default defineConfig({
    testDir : './e2e',
    fullyParallel : false,
    workers : 1,
    retries : 0,
    reporter : [ [ 'list' ] ],
    use : {
        baseURL : `http://localhost:${port}`,
        trace : 'retain-on-failure',
        screenshot : 'only-on-failure',
    },
    webServer : {
        command : `bun run dev -- --port ${port} --strictPort`,
        url : `http://localhost:${port}`,
        reuseExistingServer : !process.env.CI,
        env : {VITE_API_PROXY_TARGET : apiTarget},
        timeout : 30_000,
    },
    projects : [
        {
            name : 'chromium',
            use : {...devices['Desktop Chrome']},
        },
    ],
});
