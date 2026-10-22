# AUP-SMS native project MCP HTTP (CR-27)

Opt-in C module; the default CBM stdio and Lens keep their existing behavior.
The integration reuses native tool metadata/dispatcher, CBM's HTTP parser,
SHA-256/HMAC, private-directory validation and process-tree supervisor. It never
uses Lens port 9749. Read [CONTRACT.md](CONTRACT.md) before integrating.

This working tree is for review. Qualification results, binary hashes and exact
limitations are in DELIVERY.md; independent decisions are in ARCHITECT.md.
No commit, push, ASMS release pin change or VERIFIED state is implied.

## Deployment

Build the normal CBM binary with `scripts/build.sh --version 0.12.0-asms.cr27.1`.
There are no new production dependencies. Use a dedicated service identity and
one private runtime directory, port and process per tenant/project. CBM validates
and hardens the runtime directory with the existing native private-directory
primitive. Keep configuration and immutable artifacts in service-owned locations.
On Ubuntu use a real private local filesystem (0700 directory, 0600 config).
On Windows use the service user's private ACL; reparse points and untrusted
ancestor mutation permissions are rejected. Do not use a shared writable cache.
Windows 10 / Server 2016 or newer is required for atomic Job assignment at
process creation; owner exit also contains workers that are still suspended.
See Microsoft's [process creation attribute reference](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute)
for `PROC_THREAD_ATTRIBUTE_JOB_LIST` and its supported Windows versions.

Example trusted configuration (replace every placeholder, never commit secrets):

```json
{
  "tenant_id": "tenant-a",
  "project_scope": "project-a",
  "port": 9751,
  "runtime_dir": "/var/lib/cbm-asms/project-a",
  "data_token_sha256": "SHA256_OF_COMPLETE_DATA_BEARER",
  "control_token_sha256": "SHA256_OF_COMPLETE_CONTROL_BEARER",
  "attestation_key": "INDEPENDENT_RANDOM_HMAC_KEY_AT_LEAST_32_CHARS",
  "indexes": [{
    "repository_index_id": "index-opaque-id",
    "cbm_project": "native-project-id",
    "generation": "u0123456789abcdefg1",
    "artifact_sha256": "SHA256_OF_SEALED_NATIVE_ARTIFACT",
    "path": "/srv/cbm-artifacts/immutable-index.db"
  }]
}
```

Bearer format: `asms1.tenant-a.project-a.data.RANDOM_SECRET` and
`asms1.tenant-a.project-a.control.DIFFERENT_RANDOM_SECRET`; each secret has at
least 32 random ASCII characters. Configured SHA-256 covers the complete bearer.
Control HMAC key is separate. Config reload on every request supports immediate
revocation/rotation; replace the complete file atomically, retaining project,
tenant, runtime and port identity. Changing identity requires another process.
Remove the old bearer hash to revoke it; malformed/missing config denies access.

Launch foreground:

```text
codebase-memory-mcp asms-mcp-http /private/runtime/config.json
```

Windows lifecycle helper:

```powershell
./integrations/aup-sms/mcp-http/service.ps1 -Action start -Binary ./build/cr27-native/codebase-memory-mcp.exe -Config C:/private/project-a/config.json
# Same arguments with -Action stop, restart or status.
```

The helper verifies executable and process start identity before stopping it.
For unattended Windows hosting, run the foreground command in your existing
service manager. No scheduled task, firewall rule or service is installed here.

Example Ubuntu systemd unit (operator-owned configuration):

```ini
[Service]
User=cbm-asms
UMask=0077
ExecStart=/opt/cbm/codebase-memory-mcp asms-mcp-http /var/lib/cbm-asms/project-a/config.json
Restart=on-failure
TimeoutStopSec=125
KillMode=control-group
NoNewPrivileges=yes
```

Start/stop/restart via systemd. SIGTERM/SIGINT stop admission; the native worker
supervisor enforces deadline and descendant teardown. A containment failure
stops admission and retains private captures until the service manager confirms
the tree is stopped; inspect the returned cause before removing those captures.
Normal worker input/output/stderr captures are removed after quiescence. Raw
queries/results never enter the technical audit. Stale capture files from an
uncatchable process crash belong to the private service directory and should be
removed only while the service and its entire process tree are stopped.

The listener is loopback only. For Agent OS on another host, use your private
TLS ingress to this listener, preserving Host as `127.0.0.1:PORT`, Content-Type,
Accept and MCP headers. Do not expose the control plane to Agent OS; retain the
mandatory CBM bearer checks on both planes. Origin-bearing browser requests are
rejected. Health requires the control bearer.

## AUP-SMS integration sequence

1. Export a sealed immutable CBM artifact without WAL/journal sidecars. Register
   its opaque index ID, exact native project/generation and SHA-256 in trusted
   operator config. A file may contain that exact native project and its derived
   `<cbm_project>::missed` coverage shadow. Other projects/shadows are refused.
   `generation` is the native `cbm_store_generation` identity, not ASMS
   `lens_generation`; retain the Lens identity separately. The same native value
   must be supplied in trusted config and the signed attestation.
2. ASMS owns READY and the active index. Send the complete signed PUT attestation
   from CONTRACT.md using the project control bearer, Idempotency-Key and If-Match.
   The signature covers exact UTF-8 request bytes, not reserialized JSON. No ASMS
   database access or implicit latest-index lookup occurs in CBM.
3. Verify machine health, then initialize the data endpoint. Configure Agent OS
   `route_mode=direct` with the project's data bearer. Pass source_id on every
   tool call; source discovery remains in the separate ASMS MCP.
4. Renew expiry using a new key and current revision. Concurrent stale CAS fails.
   DELETE creates a persistent revocation tombstone. A replacement failure leaves
   the old revision unchanged. Rollback explicitly DELETEs the new revision;
   CBM never reactivates a previous index automatically.
5. ASMS updates its pin only after owner delivery and consumer verification;
   running these local tests does not mark CR-27 DELIVERED or VERIFIED.

Initial tools are get_graph_schema, search_graph, query_graph, trace_path,
get_file_outline and get_architecture (synchronous aspects).
Native schemas/descriptions/annotations are derived at runtime. HTTP adds the
required source_id and limits and rejects project/path/DB selectors, unknown
arguments and native cache cursors. Use bounded offset paging on immutable
artifacts; native account-cache cursor tokens are not project/source attestations.
Native errors remain primary, with correlation and audit/subprocess causes.

## Audit and operations

`control.sqlite` stores binding revisions, idempotency receipts and internal
technical metadata audit. It is isolated from CBM graph artifacts and ASMS DBs.
The audit records only validated opaque IDs, allowlisted tool names, stage,
outcome, duration, response size and generated correlation ID. It persists no
queries/results/header values/tokens/source/error messages. Default retention
is seven days, record cap 1024 bytes and project quota 10 MiB. Quota is a hard
failure after retention cleanup; it does not silently evict unexpired audit.
There is no user audit, statistics, monitoring UI or trace API.

Health reports source revision/effective state, with bounded native verification.
If the total verification deadline is exhausted, remaining source states are
VERIFYING. Native failures return redacted technical diagnostics and causes;
audit failure before execution refuses dispatch; after execution it returns an
observable error. A failed tool remains primary if completion audit also fails.

## Reproducible checks

```text
python integrations/aup-sms/mcp-http/tests/test_transport.py PATH_TO_NATIVE_BINARY
scripts/test.sh --suites mcp,mcp_mutation_guard,httpd
scripts/lint.sh
make -f Makefile.cbm security
git diff --check
```

The HTTP suite uses actual processes and SQLite fixtures, never production
credentials. Its official client test needs Node and the isolated test-only SDK:
`npm install --prefix build/cr27-sdk --ignore-scripts --no-audit --no-fund @modelcontextprotocol/sdk@1.20.2`.
Set CBM_MCP_SDK_ROOT to the SDK's `dist/esm` directory when testing a private Linux
copy of the tree. No SDK code is a production dependency.

Set `CBM_HTTP_TEST_ROOT` to a private runtime parent when the checkout has shared
ancestor permissions (for example `$env:LOCALAPPDATA\CBM-CR27-tests` on Windows).
The native runtime validates ancestor permissions and deliberately refuses a
directory below a shared writable ancestor. The transport and lifecycle suites
both honor this setting; it does not weaken the production permission checks.
