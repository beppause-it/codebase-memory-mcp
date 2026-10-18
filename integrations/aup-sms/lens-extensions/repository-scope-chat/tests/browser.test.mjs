import { test } from 'node:test';
import assert from 'node:assert/strict';
import { chromium } from '../../../../../graph-ui/node_modules/playwright/index.mjs';
import { createServer } from '../../../../../graph-ui/node_modules/vite/dist/node/index.js';
import { fileURLToPath } from 'node:url';

test('CR-24 browser contract: cached runtime, recursive scopes, references, egress and two-project isolation', { timeout: 90000 }, async () => {
    const server = await createServer({ configFile: fileURLToPath(new URL('../vite.config.ts', import.meta.url)), server: { host: '127.0.0.1', port: 5173, strictPort: true } });
    let browser;
    try {
        await server.listen(); browser = await chromium.launch({ headless: true, channel: process.env.CBM_TEST_BROWSER_CHANNEL || undefined });
        const page = await browser.newPage();
        const outside = [], calls = [];
        page.on('request', request => { if (!request.url().startsWith('http://127.0.0.1:5173/') && !request.url().startsWith('ws://127.0.0.1:5173/')) outside.push(request.url()); });
        let generation = 'g1';
        await page.route('**/api/repository-map?*', route => {
            const project = new URL(route.request().url()).searchParams.get('project');
            return route.fulfill({ json: { project, generation, total_nodes: 2, nodes_truncated: false, edges_truncated: false,
                nodes: [{ id: 1, name: `${project}-entry`, label: 'Function', file_path: `src/deep/${project}.ts`, start_line: 3 },
                    { id: 2, name: `${project}-outside`, label: 'Function', file_path: `elsewhere/${project}.ts`, start_line: 7 }],
                edges: [{ source: 1, target: 2, type: 'CALLS' }] } });
        });
        await page.route('**/rpc', route => {
            const body = route.request().postDataJSON(); const args = body.params.arguments; calls.push(args);
            return route.fulfill({ json: { jsonrpc: '2.0', id: body.id, result: { content: [{ type: 'text', text: JSON.stringify({ project: args.project, path: args.path, total_nodes: args.path ? 1 : 2, total_edges: args.path ? 0 : 1 }) }] } } });
        });
        await page.goto('http://127.0.0.1:5173/tests/browser.html');
        const load = async () => {
            if (!await page.getByRole('button', { name: 'Load model (cached, no download)', exact: true }).isVisible()) await page.getByRole('button', { name: 'Enable agent', exact: true }).click();
            await page.getByRole('button', { name: 'Load model (cached, no download)', exact: true }).click();
            await page.locator('textarea').waitFor();
        };
        const ask = async question => {
            await page.locator('textarea').fill(question);
            await page.locator('textarea').press('Enter');
            await page.getByText('Indexed observations only.', { exact: false }).first().waitFor();
        };
        await load();
        await ask('che pattern usa questo repo?');
        assert.equal(calls[0].path, undefined);
        await page.getByRole('button', { name: 'src/deep/A.ts:3', exact: true }).first().click();
        assert.equal(await page.getByLabel('Opened reference').textContent(), 'A:src/deep/A.ts:3');
        await page.getByRole('button', { name: 'Folder scope', exact: true }).click();
        await ask('quali sono le aree principali di questa cartella?');
        await page.waitForFunction(() => window.scopeTestTrace.requests.length === 2);
        assert.equal(calls.at(-1).path, 'src');
        const folderRequest = await page.evaluate(() => window.scopeTestTrace.requests.at(-1));
        assert.match(JSON.stringify(folderRequest), /src\/deep\/A.ts/);
        assert.doesNotMatch(JSON.stringify(folderRequest), /elsewhere\/A.ts/);
        await page.getByRole('button', { name: 'Switch project', exact: true }).click();
        await load(); await ask('quali dipendenze usa questo progetto?');
        const projectRequest = await page.evaluate(() => window.scopeTestTrace.requests.at(-1));
        assert.doesNotMatch(JSON.stringify(projectRequest), /A-entry|A\.ts/);
        assert.match(JSON.stringify(projectRequest), /B-entry/);
        generation = 'g2';
        await page.getByRole('button', { name: 'Reindex', exact: true }).click();
        await load(); await ask('quali aree ha questo progetto dopo il nuovo indice?');
        const reindexed = await page.evaluate(() => window.scopeTestTrace.requests.at(-1));
        assert.doesNotMatch(JSON.stringify(reindexed), /quali dipendenze usa/);
        assert.equal(outside.length, 0, `External requests: ${outside.join(', ')}`);
        assert.ok((await page.evaluate(() => window.scopeTestTrace.prepared)).every(options => options?.cacheOnly === true));
    } finally {
        await browser?.close(); await server.close();
    }
});
