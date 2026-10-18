import assert from 'node:assert/strict';
import { pathToFileURL } from 'node:url';
import path from 'node:path';

const sdk = process.env.CBM_MCP_SDK_ROOT || path.resolve('build/cr27-sdk/node_modules/@modelcontextprotocol/sdk/dist/esm');
const { Client } = await import(pathToFileURL(path.join(sdk, 'client/index.js')));
const { StreamableHTTPClientTransport } = await import(pathToFileURL(path.join(sdk, 'client/streamableHttp.js')));
const client = new Client({ name: 'cr27-standard-client', version: '1' });
const transport = new StreamableHTTPClientTransport(new URL(process.env.CBM_TEST_MCP_URL), {
  requestInit: { headers: { Authorization: `Bearer ${process.env.CBM_TEST_DATA_TOKEN}` } },
});
try {
  await client.connect(transport);
  const catalog = await client.listTools();
  assert.equal(catalog.tools.length, 6);
  const result = await client.callTool({ name: 'query_graph', arguments: {
    source_id: 'source-a', query: 'MATCH (n:Function) RETURN n.name',
  } });
  assert.equal(result.isError, false);
  assert.match(JSON.stringify(result), /alpha-only/);
  const outline = await client.callTool({ name: 'get_file_outline', arguments: {
    source_id: 'source-a', file_path: 'src/example.c', format: 'json',
  } });
  assert.equal(outline.isError, false);
  assert.match(JSON.stringify(outline), /alpha-only/);
  console.log('Official MCP SDK initialize, tools/list, tools/call: PASS');
} finally {
  await client.close();
}
