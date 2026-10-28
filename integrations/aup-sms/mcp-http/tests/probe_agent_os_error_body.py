"""Simple real SDK/codec probe, NOT the complete Agent OS adapter.

This intentionally uses httpx.AsyncClient without PinnedMCPTools response hooks.
It demonstrates the need for early body capture, not the behavior of the complete
production adapter. Agent OS CR24 owner evidence confirms its existing hook reads
the legacy body; the codec fallback then loses its native fields. Owner acceptance
must exercise PinnedMCPTools and McpPinnedHttpTransport, not this probe alone.
"""
import asyncio, importlib.util, json, os, sys, tempfile
from pathlib import Path
import httpx
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client
from aupaos_runtime.diagnostics.distributed_exception_codec import DistributedExceptionCodec

binary = sys.argv[1]
spec = importlib.util.spec_from_file_location('native_fixture', 'integrations/aup-sms/mcp-http/tests/test_transport.py')
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)
root = Path(tempfile.mkdtemp(prefix='cr37-hop-', dir=os.environ['CBM_HTTP_TEST_ROOT']))
server = fixture.Server(root)
server.start()

def find_http(error):
    if isinstance(error, httpx.HTTPStatusError): return error
    for child in (*getattr(error, 'exceptions', ()), error.__cause__, error.__context__):
        if child is not None:
            found = find_http(child)
            if found: return found
    return None

async def probe():
    client = httpx.AsyncClient(headers={'Authorization': 'Bearer ' + server.data})
    try:
        async with client:
            async with streamable_http_client(f'http://127.0.0.1:{server.port}{server.base}/mcp', http_client=client) as (read, write, _):
                async with ClientSession(read, write) as session:
                    await session.initialize()
                    await session.call_tool('get_graph_schema', {'source_id':'7'})
    except BaseException as error:
        original = find_http(error)
        assert original is not None, repr(error)
        try:
            original.response.json()
            available = True
        except httpx.ResponseNotRead:
            available = False
        diagnostic = DistributedExceptionCodec().capture(error, operation='cr37.native-sdk-probe')
        print(json.dumps({'status': original.response.status_code, 'response_body_available': available,
            'response_closed': original.response.is_closed,
            'primary_type': diagnostic['primary']['type'],
            'completeness': diagnostic['completeness'],
            'contains_native_code': 'SOURCE_NOT_ACTIVE' in json.dumps(diagnostic)}))
    else:
        raise AssertionError('SOURCE_NOT_ACTIVE unexpectedly succeeded')
try:
    asyncio.run(probe())
finally:
    server.stop()
