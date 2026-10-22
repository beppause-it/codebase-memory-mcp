# asms-cbm-http/v1 (CR-27)

Native command: `codebase-memory-mcp asms-mcp-http CONFIG.json`.
One tenant/project per process. HTTP binds loopback only, port 1..65535 except
9749. Remote deployments terminate TLS at an authenticated private ingress;
the CBM bearer remains mandatory. CONFIG and runtime directory are service-owner
only (Windows ACL / Ubuntu directory mode 0700, file mode 0600); no untrusted
local writers. CONFIG must have an absolute path beneath a private directory
with trusted ancestry, be a regular single-link file, and have no symlink or
reparse components. Each startup/reload reads through an owner/ACL-validated
handle; unsafe config fails closed before parsing credentials.
Windows requires Windows 10 / Server 2016 or newer: worker creation assigns
the kill-on-close Job atomically through `PROC_THREAD_ATTRIBUTE_JOB_LIST`.
Unsupported attribute setup fails before child creation, without a later
assignment fallback that could leave a suspended child after owner death.

CONFIG contains `tenant_id`, `project_scope` (ASCII identifiers, 1..96 chars),
`port`, `data_token_sha256`, `control_token_sha256` (distinct SHA-256 lowercase
hex of independent >=32-byte random bearer secrets), `attestation_key` (>=32
characters, independent HMAC key), `runtime_dir`, `indexes` array.
Each index: `repository_index_id`, `cbm_project`, `generation`, `artifact_sha256`,
`path`. Paths are trusted operator configuration, never MCP inputs. Artifacts
are immutable sealed CBM SQLite files without WAL/journal sidecars. Each call
verifies hash, native project and native generation in its isolated worker.
The artifact must contain the configured native project exactly once and may
also contain its exact `<cbm_project>::missed` coverage shadow. A shadow alone,
an unrelated shadow or a second real project is rejected. Shadow data is not
included in project-scoped tool queries.
`generation` is the opaque native identity returned by `cbm_store_generation`
(`u<db_uid>g<mutation_gen>` for current artifacts); ASMS `lens_generation` is a
different identity and must not be substituted or overwritten. Both config and
attestation must carry the same native identity. There is no permissive fallback.
`request_max_bytes` default 65536 (max 1MiB), `response_max_bytes` default 1MiB,
`timeout_ms` default 30000 (max 120000), `max_depth` default 8 (max 32),
`max_results` default 100 (max 1000), `audit_retention_seconds` default 604800,
`audit_quota_bytes` default 10485760 and `audit_record_max_bytes` default 1024.
All limits must be positive; response_max_bytes is at least 4096 and
audit_record_max_bytes at least 512. Defaults retain native tool defaults when
smaller than the configured ceiling. Config reload per request permits immediate token
rotation/revocation. Missing/invalid config fails closed; project/runtime/port
identity cannot change during a process lifetime.

Bearer wire format is `asms1.{tenant_id}.{project_scope}.{plane}.{secret}`, where
plane is `data` or `control` and secret is at least 32 random ASCII characters.
The configured hash covers the entire bearer. Prefix validation binds even an
accidentally reused credential hash to exactly one tenant/project/plane.

Data endpoint: `/integrations/aup-sms/v1/projects/{project_scope}/mcp`.
Stateless Streamable HTTP protocol `2025-06-18`, JSON-RPC POST with Content-Type
application/json and Accept containing application/json AND text/event-stream.
No session header is issued. GET returns 405. Supported methods: initialize,
ping, notifications/initialized, tools/list, tools/call; arrays rejected.
Origin is rejected (machine clients only). Host must be exact loopback listener.
MCP-Protocol-Version when supplied must match. No Lens listener is started.
Native registry supplies schema, description, annotations and dispatcher for
`get_graph_schema`, `search_graph`, `query_graph`, `trace_path`, `get_file_outline`
and `get_architecture`. source_id is
required on every call; project/project aliases, repository/path/DB selectors
are rejected, including unknown top-level arguments. Trace depth/result limits
are enforced. No discovery or mutable tool is exposed. Native identities and
artifact locators are redacted at the data boundary; native tool failures stay
MCP isError results, augmented with transport correlation metadata.
The HTTP profile rejects native account-cache cursors; use bounded offset
paging on immutable artifacts. Native cursor fields are omitted from structured
results. Multi-project artifact files are rejected before native dispatch.
Outline file_path and architecture path are strictly repository-relative graph
selectors. get_file_outline is the existing native capability, not a new alias.
Architecture exposes synchronous native aspects; system_structure and its
background-job/generation/behavior options are outside this worker profile.
This satisfies the scoped architecture/outline capabilities in ASMS docs/98;
discovery remains exclusively ASMS-owned.

Control routes:
PUT/DELETE `/integrations/aup-sms/v1/projects/{project_scope}/sources/{source_id}`;
GET `/integrations/aup-sms/v1/projects/{project_scope}/health`.
Control bearer is distinct; scope comes exclusively from trusted config.
PUT body: binding_version=1, tenant_id, project_scope, state=READY,
repository_index_id, cbm_project, generation, artifact_sha256, activated_at,
expires_at. Times are integer Unix seconds; activated_at<=now<expires_at.
Unknown attestation fields are rejected before persistence; the registry stores
only this closed set of metadata, never an arbitrary caller payload.
Header X-ASMS-Signature: lowercase HMAC-SHA256 hex over UTF-8 concatenation:
`METHOD\nPATH\nIDEMPOTENCY_KEY\nIF_MATCH\nRAW_BODY` (no terminal newline).
PUT and DELETE require Idempotency-Key (ASCII identifier), If-Match (unquoted
decimal revision, 0 for first creation). DELETE body is empty. Same key and
identical request replay the prior response; different bytes return 409.
Successful mutation increments persistent revision; DELETE writes a tombstone
even for an absent source, repeated equivalent DELETE remains revoked. A later
PUT needs the current revision and a fresh key and full fresh attestation.
Transactions serialize concurrent CAS. No automatic rollback/reactivation.

Control success/failure envelope: contract=asms-cbm-http/v1, type, code, message,
details, request_id, correlation_id, causes array. Source mutation returns only
source_id, revision and state in details; ETag is the decimal revision.
Artifact identity/integrity failures retain their existing code, message, OS and
SQLite diagnostics in `causes`. An additive `verification_stage` identifies
`project_identity`, `native_generation` or `integrity`; it does not replace the
original diagnostic or request/correlation identifiers.
Health machine-readable details contain process state and source revision/state,
never token, locator or checksum. States INSTALLING, VERIFYING, ACTIVE, DEGRADED,
FAILED, REVOKED, EXPIRED; effective state revalidated, no implicit graph fallback.
Generated request/correlation IDs ignore caller-provided IDs to prevent audit
injection. HTTP/MCP errors carry the same envelope in JSON-RPC error.data.

Audit is internal SQLite metadata only: generated correlation ID, safe source
ID/tool name, stage, status, elapsed milliseconds, response bytes, timestamp.
No queries, results, tokens, header values, source or arbitrary error messages.
Preflight insertion and quota/retention transaction must succeed before execution.
Postflight failure converts success to an audit error; an original tool failure
stays primary with audit failure as secondary cause. No audit reading/statistics
API. Operational counts stay internal. State DB/temporary captures are private.
Worker crash/timeout envelope includes native exit classification, exit code,
signal, containment flags and original bounded worker diagnostic where available.
Worker stdout is bounded before delivery, captures removed after tree teardown.
Artifact verification also runs under the native supervisor, including control
activation and health; health has one total verification deadline and returns
VERIFYING for remaining sources when it expires. Unconfirmed containment stops
admission and retains private captures until process-tree cleanup is established.

Lifecycle: start foreground command under systemd/Windows service wrapper;
SIGINT/SIGTERM stop listener after at most the configured call timeout plus
supervisor grace. Restart repeats startup validation and persisted revisions;
it never selects another artifact. Health requires the control credential.
Release qualification needs Windows + actual Ubuntu evidence and independent
Architect Release Gate. No commit/push or ASMS VERIFIED state is implied.

