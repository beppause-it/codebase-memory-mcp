# CR-27 runs and review boundary

Run CR27-ANALYSIS-20261018, pending Architect Ready Gate.

Specification: AUP-SMS docs/43-cross-team-change-request.md, CR-27 and both
append-only corrections, read 2026-10-18. Baseline: ec6427c38176efcd37b28682e82f2491afda94be.
No AGENTS.md exists in this checkout or its workspace ancestors. CONTRIBUTING.md
requires C, existing tests, security review and no unrelated changes. The user
authorizes this local capability and explicitly requires independent gates;
the gate sequence follows AUP-SMS AGENTS.md lines 124-161. No publication.

Proposed implementation: opt-in native command `asms-mcp-http CONFIG`, one
loopback listener and tenant/project identity per process, never Lens port 9749.
Existing httpd supplies HTTP parsing/lifecycle; additive selected security
headers reject duplicates. Stateless MCP Streamable HTTP 2025-03-26, JSON
responses, POST notifications 202, GET 405 (no server-initiated stream).
Catalog derives from native registry, restricted to graph inspection tools.
Every tool requires source_id; project/path/database selectors are removed from
the advertised schema and rejected at dispatch. A typed fixed-index seam in
native MCP bypasses all cache selection/fallback, opening the attested artifact
query-only. No changes to default stdio.

Versioned control routes exactly follow CR-27. Dedicated control bearer plus
HMAC-SHA256 signature over method/path/Idempotency-Key/If-Match/raw body.
Trusted local configuration owns artifact locators and tenant/project identity;
ASMS attests READY, index ID, native identity, generation, digest, validity.
SQLite integration state (not ASMS DB) serializes revision CAS, idempotency,
revocation and append-only technical metadata audit. Every access revalidates
credential configuration, expiry and local artifact. Restart never selects an
alternative index. A failed replacement leaves the old revision unchanged.

Calls execute in the same native binary's isolated worker using CBM's existing
cross-platform subprocess supervisor: bounded deadline, process-tree teardown,
crash/exit diagnostics retained in a versioned cause. Worker input/output are
private ephemeral files, removed after execution; no raw diagnostic logs.
Audit required before execution and after result, metadata only, bounded record,
retention/quota, original tool error primary on a second audit failure.
No user audit/statistics APIs, UI, discovery or ASMS DB reads.

Validation: isolated module protocol/security/lifecycle integration harness on
Windows, native MCP/httpd regression suites and canonical lint/test attempts;
Ubuntu execution only if a real local Ubuntu environment is available, otherwise
document exact unverified steps. Publish local binary/version/hash and contract
hash, commands and complete failure logs; never claim DELIVERED/VERIFIED solely
from local work.
