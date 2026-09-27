import { defineConfig } from '@playwright/test';
import path from 'node:path';
const root = process.env.WVD_CLOSURE_ROOT;
if (!root || !path.isAbsolute(root)) throw new Error('WVD_CLOSURE_ROOT_REQUIRED');
export default defineConfig({
  testDir: './tests', testMatch: ['closure.spec.ts', 'closure-native.spec.ts'],
  workers: 1, fullyParallel: false, retries: 0, timeout: 35000,
  outputDir: path.join(root, 'playwright'),
  reporter: [['list'], ['json', { outputFile: path.join(root, 'playwright-results.json') }]],
  use: { baseURL: 'http://127.0.0.1:18754', viewport: { width: 1440, height: 900 },
    screenshot: 'only-on-failure', trace: 'retain-on-failure' },
  webServer: { command: 'node tests/closure-server.mjs', url: 'http://127.0.0.1:18754',
    reuseExistingServer: false, timeout: 60000, stdout: 'pipe', stderr: 'pipe' },
});
