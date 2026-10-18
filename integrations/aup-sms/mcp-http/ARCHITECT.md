# Independent Architect decisions — CR-27

## Ready Gate — 2026-10-18

Outcome: **APPROVED**

Reviewed productive run: `CR27-ANALYSIS-20261018`.
Reviewed artifact: `PLAN.md`, SHA-256
`06cb5d0ecb09ef1ff329ff1c2457b7051ae41a95ffa96e6b1a5f887eb93b746e`.
Specification: `C:/laragon/www/aup-sms/docs/43-cross-team-change-request.md`,
CR-27 lines 2579-2765 including both append-only corrections, SHA-256
`d91153bde9d6fa035b75a444c8044699b9d3b1acf5443082506c33c2e35cc109`.
Repository baseline declared by plan: `ec6427c38176efcd37b28682e82f2491afda94be`.
Reviewer: independent Architect agent; no productive implementation authored.

Evidence examined: complete PLAN.md; CONTRIBUTING.md; complete AUP-SMS
AGENTS.md; complete CR-27 and append-only addenda. Root AGENTS.md read
returned file-not-found, consistent with the analyst inventory.

The plan preserves the required C implementation and isolates the adapter,
reuses the native catalog/dispatcher through additive seams, makes source_id
mandatory, binds all calls to the exact attested artifact and forbids fallback.
Separate project/control credentials, signed versioned control routes, revision
CAS, idempotency, expiry/revocation and required redacted internal audit cover
the explicit trust boundaries. Process supervision and original diagnostics
are planned, as are cross-platform checks and explicit unverified-platform
reporting. The append-only telemetry correction is applied: no user monitoring
API or UI. Port 9749 and default stdio behavior are excluded from the change.

Decision on CONTRIBUTING scope: its issue/maintainer process governs upstream
PR submission/merge. The user's explicit authorization concerns local fork
implementation with no commit/push. This gate is an internal project control
decision, not upstream maintainer approval and not authorization to publish.

No Ready Gate findings. This approval authorizes contract elaboration; it does
not qualify an implementation or replace later gates. Contract Gate must
review concrete schemas, canonical signing bytes, replay/rotation semantics,
error/cause redaction, resource limits, native read-only tool allowlist and
source selector rejection. Required test evidence remains pending for the
Implementation/Release Gates; none was claimed as run by this Ready Gate.

## Contract Gate — 2026-10-18

Outcome: **APPROVED**

Reviewed run: `CR27-CONTRACT-20261018` (contract artifact review).
Reviewed artifact: `CONTRACT.md`, SHA-256
`3dcd9612fe5036c101c85de0aed6c62fde18b4ebe8181fce0c7868ec8ba317a9`.
Specification checksum remains the CR-27 checksum recorded above.
Independent Architect review; no contract or implementation authored here.

Evidence: complete contract and CR-27 with both corrections; official adopted
transport specification independently opened on 2026-10-18:
https://modelcontextprotocol.io/specification/2025-06-18/basic/transports
(lines 69-95 and 128-151). JSON POST responses, GET 405, optional session IDs,
Accept negotiation and no batches match the adopted version. The version
change from the Ready plan's 2025-03-26 is explicit and remains within CR-27's
requirement to name the adopted protocol. Strict configured-version handling
is an intentional machine-client deployment policy; standard clients send the
negotiated protocol header on subsequent requests.

The contract specifies independent rotating project/control bearer hashes,
trusted immutable project/runtime identity, canonical raw-byte HMAC input,
CAS revisions, persistent idempotency/tombstones and fresh full reattestation.
Fixed sealed native artifacts, native read-only allowlist, rejected selectors,
and limits provide an implementation-testable isolation boundary. Required
internal metadata-only audit, effective binding states and correlated causal
failures meet the current append-only scope; no user statistics API is added.

No blocking Contract Gate findings. Implementation review must verify the
standard 202/no-body notification behavior, duplicate/header validation,
full original diagnostics within configured response limits (with explicit
size-limit failures rather than silent truncation), exact native schema/dispatch
reuse and safe native read-only query enforcement. These are implementation
verification obligations, not claims that code or tests already satisfy them.
No tests were executed by this contract-only gate.

## Intermediate Implementation Gate — 2026-10-18

Outcome: **CHANGES_REQUIRED**

Reviewed run: `CR27-IMPLEMENTATION-20261018` (initial working-tree review).
Primary artifact `src/mcp_http.c` SHA-256:
`e10d6c0d0949542062fef82fb2f6938625b949d228e8b56fc9684544d69bb044`.
Also inspected native MCP seam diff, shared HTTP parser diff, subprocess API,
HTTP deadlines, native query handler and transport harness. No implementation
was authored by this reviewer. GCC syntax success is producer-reported; build
and execution evidence were not yet available to this gate.

### Findings

1. **P1 CR27-I01 — Original worker diagnostics are discarded.**
   `worker_call` sets `stdout_file` but leaves `log_file=NULL`; subprocess.h
   explicitly specifies that stderr is discarded in this mode. Worker failures
   return a generic message and numeric process fields, with no original
   stderr/cause. `worker` also collapses config/attestation/open/dispatch failures
   into numeric exits or a generic text result. Capture diagnostic output in a
   private bounded channel, redact only permitted sensitive values, retain the
   original failure and append supervisor context. Preserve errno/SQLite
   diagnostics before cleanup. Test a worker failure with distinctive original
   diagnostic and verify its correlation and causal preservation.

2. **P1 CR27-I02 — Native seam lifetime is unsafe.**
   `cbm_mcp_server_set_session_context` frees fixed_index_path/project but does
   not null them. A later resolution dereferences freed memory; rebind frees
   those pointers again. `cbm_mcp_server_free` does not free either allocation.
   Reset both pointers when leaving fixed-index mode and release ownership in
   the destructor. Add native seam lifecycle regression covering bind, session
   context replacement, subsequent resolution/rebind and destruction.

3. **P1 CR27-I03 — Artifact verification bypasses execution deadline.**
   `mcp_request -> effective_state -> verify_index` hashes the whole artifact
   and runs SQLite integrity verification synchronously before worker_call's
   timeout starts. Control PUT and health do the same on the single listener
   thread. A large/slow artifact can block health, revocation and stop well
   beyond configured timeout plus supervisor grace. Move expensive verification
   inside supervised bounded execution (including activation/health), or add a
   cancellation-aware deadline that covers the entire operation. Demonstrate
   stop and health/revocation behavior with a deliberately stalled verification.

4. **P2 CR27-I04 — Audit quota failures have no diagnostic cause.**
   `audit` reaches its generic fail branch for record/quota exhaustion without
   recording a code/message; the rollback does not create one. Preflight and
   postflight then expose only 'Required audit unavailable/failed'. Double-fault
   handling retains native MCP content, but its secondary audit cause loses
   why quota/record failure happened. Record a typed audit failure with actual
   bound/usage and original SQLite diagnostic where relevant; keep tool primary.
   Extend quota/postflight double-fault tests to assert the concrete cause.

Isolation observation: fixed-index resolution rejects different native project
arguments and uses a query-only connection; source selection is attested and
unknown top-level selectors are rejected. Existing test fixtures contain only
one native project per artifact. Add adversarial multi-project artifact tests
before Release Gate, including query/project predicates and cursor reuse.
Current source review found no demonstrated isolation bypass.

Release qualification remains pending: no crash/timeout/lifecycle execution,
Ubuntu evidence, complete native regressions or lint results reviewed here.

## Contract delta Gate — 2026-10-18

Outcome: **APPROVED**

Reviewed updated CONTRACT.md SHA-256:
`70e251367c5db3551b4fa000ac17fac1c1b68257a1e8ff38ab9ee49b0ee16927`.
The explicit `asms1.tenant.project.plane.secret` bearer format binds scope even
under operator hash reuse. This is additive clarification before release; it
preserves the approved trust model. No implementation/testing claim is implied.

## Intermediate Implementation correction Gate — 2026-10-18

Outcome: **CHANGES_REQUIRED**

Reviewed correction run: `CR27-IMPLEMENTATION-20261018` follow-up working tree.
Reviewed src/mcp_http.c SHA-256:
`93bccba55f84f54019d8d13acf60fcbc0d8f44f32eb3f1cac88eefdd64e2d4f1`.
Native seam diff and subprocess lifetime contract also independently examined.

I01: stderr capture and typed worker/artifact diagnostics are now present;
code correction accepted pending execution evidence. I02: native fixed-index
allocations now live until rebind/destructor; dangling session-context frees
removed. Code correction accepted pending the lifecycle regression. I03:
expensive artifact checks now execute only in supervised workers; absolute
poll/cancel bounds include verification. Code correction accepted pending
crash/timeout/stop qualification. No new source-isolation bypass demonstrated;
verification now rejects artifacts containing more than one native project.

Remaining findings:

- **P2 CR27-I04 remains open for double failure.** `error_json` includes
  `rt->audit_code`, but `annotate` constructs the secondary audit envelope
  directly and copies only `rt->db_error`. A native tool failure plus postflight
  quota/record overflow therefore still loses AUDIT_QUOTA_EXCEEDED or
  AUDIT_RECORD_LIMIT. Include the typed audit cause in this branch and test
  native error plus completion quota overflow, asserting original native text,
  specific secondary code and identical correlation ID.
- **P1 CR27-I05 — Failed containment still deletes live worker captures.**
  `worker_call` unconditionally unlinks stdin/stdout/stderr even when terminal
  supervisor result has `tree_quiesced=false`. subprocess.h explicitly requires
  retaining stdin_file until the tree is quiescent. The supervisor returns a
  terminal failed-containment result precisely when descendants may remain
  alive. On this path fail the runtime closed, retain protected captures with
  an explicit teardown diagnostic, and do not accept further work while a live
  uncontained tree may still access artifacts. Exercise the failed-containment
  branch with a controlled supervisor fault; normal timeout coverage is not
  equivalent to this branch.

Build/test execution remains in progress and is not approved by this gate.

## Contract capability delta Gate — 2026-10-18

Outcome: **APPROVED**

Reviewed CONTRACT.md SHA-256:
`b4adedc51c09b367ebb14f10a071c2587a210a67f238a4bb2c84e9dbf1af2b2b`.
Reviewed ASMS docs/98 document including capability table (architecture, graph
search, traversal, file outline) and its appended corrections. Native outline
with repository-relative paths and synchronous native architecture satisfy that
explicit capability list. Async system_structure and job arguments remain
excluded. Cursor rejection avoids account-cache coupling; immutable-artifact
offset paging is explicit. No new dispatcher or ASMS tool replica is introduced.

## Intermediate Implementation correction review 3 — 2026-10-18

Outcome: **CHANGES_REQUIRED**

Reviewed src/mcp_http.c SHA-256:
`2488866f593344651e171e09cb48963c0cbe3cde3c3feea300df07da73ecc42d`.
Reviewed Makefile response-file changes, security audit coverage extension,
native architecture/outline handler paths, argument/schema restrictions and
worker capture teardown. No productive code changes by this reviewer.

I04 is resolved in code: envelope includes the specific audit cause when
creating AUDIT_* failures, including annotate's secondary audit envelope.
I05 is resolved in code: captures are retained on nonquiescent tree outcome,
captures_retained is explicit and stopping prevents subsequent requests.
The new outline path guard rejects absolute, drive/UNC and parent traversal;
native handler independently validates a repository-relative path. Architecture
blocks the asynchronous aspect before dispatch and retains native validation.
MinGW response files contain inputs only, preserving shell flag quoting; the
change is confined to the affected build environment.

No additional blocking code finding was demonstrated in this review. The
remaining gate requirement is executable qualification evidence: completed
Windows native binary/harness, seam lifecycle regression, fault paths and
preserved original logs; Ubuntu evidence as available. Builds/tests were still
reported in progress. The mandatory engineering contract forbids approving a
gate while required tests are not run, so this intermediate decision remains
CHANGES_REQUIRED for evidence only. This does not authorize a Release Gate or
claim consumer DELIVERED/VERIFIED state.

Nonblocking robustness observation: native_args injects configured max_results
as outline's limit, while native outline caps limit at 200. Configuration permits
max_results up to 1000. Clamp the default for that tool to its native maximum
(or document/reject that incompatible configuration); otherwise raising a
transport limit makes an omitted native limit fail. Include a regression at
max_results=1000 if correcting this before QA.

## Final code/delta Contract review — 2026-10-18 (evidence pending)

Contract outcome: **APPROVED**
Reviewed CONTRACT.md SHA-256:
`be28c4b9dbd0df7e12b15f6150195f082a3d09ef99cf65822473e7db220c3008`.
Closed attestation metadata, integer binding_version, bounded native schema
defaults, protected failed-containment captures and total health verification
budget remain consistent with CR-27/docs98. Actual checkout baseline is
`acdd9c3bf2d8ae3af6be45c8a6a96285c82d65f8`; CR-27's ec6427c... is the historical
owner baseline, not this implementation's actual HEAD.

Implementation outcome: **CHANGES_REQUIRED**
Reviewed src/mcp_http.c SHA-256:
`d885389e4ce37884797a087c6e8c89c51525ab007744f8569aee9087d90b7cba`.
The outline default now derives from native schema and caps correctly. SQLite
native-code diagnostics and an append-only in-request SQLite cause chain cover
the two recorded initial Linux failures in code; successful rebuilt execution
is still pending. Existing failure logs remain valid historical evidence.

**P1 CR27-I06 — Credential configuration has no permission validation.**
config_read reads the given config_path directly. Startup validates only
runtime_dir using the native private-directory policy. Reload likewise reads
CONFIG without requiring a regular non-symlink owner-protected file or secure
parent hierarchy. CONFIG can be outside runtime_dir. A different local account
with write access to that file/parent can substitute data/control token hashes,
attestation key and index locators while preserving the four pinned identity
fields, and reload accepts its credentials. This contradicts the stated
service-owner-only configuration trust boundary and fail-closed credentials.
Require a protected canonical config location (for example inside the validated
private runtime hierarchy), regular/non-symlink file and owner/ACL policy on
startup and every reload, or equivalent native secure-file validation. Test an
externally writable/symlinked config rejection alongside the existing insecure
runtime rejection on Windows and Ubuntu.

No Release decision: native builds, final transport suites and sanitizer
regressions remain in progress. Initial Linux 27/29 and single Windows protocol
success are partial evidence, not full qualification.

## Secure configuration correction review — 2026-10-18

Contract outcome: **APPROVED**
CONTRACT.md SHA-256:
`d6054d3d6e2e58164cecaed0f8031045be304edfc32368e744eb1f4ffe70cc97`.

Implementation outcome: **CHANGES_REQUIRED** (execution evidence pending only).
src/mcp_http.c SHA-256:
`0e857feb38b44bef1a6ee79bc0df91a8999ea92efaae34a9f3fadac3041b6559`.
src/daemon/ipc.c SHA-256:
`a645602ee92bbd972da465ba689c8bce0338733ecb6a214823f81cbe89538aad`.

CR27-I06 is resolved in code. Every config_read now reads the stream returned
from the native secure-open seam, without reopening the pathname. POSIX
validates directory ancestry and regular-file owner, exact 0600 mode, single
link and path/handle identity through openat/O_NOFOLLOW. Windows validates
ancestry and opens a no-reparse single-link regular file, requires exact current
owner and checks its DACL on that handle before CRT conversion.

The Windows FILE_ALL_ACCESS union is a deny-untrusted-rights mask, not a grant
or allowlist: win_file_acl_secure intersects every allow ACE with it and rejects
untrusted identities. Therefore including read, write, execute and standard
rights makes the configuration check more restrictive. Existing trusted system
principals retain the native policy. The helper retains the original validated
handle throughout reading; no new pathname reopen race was introduced.
Directory-tree rules reject drive-relative and UNC/device Windows namespaces.

No new concrete blocker found in the secure-open correction. Added insecure
mode/ACL and hardlink restoration tests need execution on the final binaries;
producer-reported syntax success alone does not complete Implementation Gate.
No Release approval is issued by this code-only review.

## Lint-refactor safety review — 2026-10-18

Outcome: **CHANGES_REQUIRED**
Reviewed src/mcp_http.c SHA-256:
`25d9506ecf756c7bbcf815feb0d2687d1fc582ead5fcd0499380e401a357c9b9`.

Borrowed SQLite text binding audit: all current callers keep source/tool/stage,
request/body, runtime config, fingerprint and serialized mutation response
alive through their synchronous sqlite3_step/finalize sequence. No dangling
binding was demonstrated. This remains a local helper contract; future callers
must preserve that lifetime.

**P2 CR27-I07 — Replay read failure loses original SQLite diagnostic.**
After sqlite3_step fails in replay_read (neither ROW nor DONE), code finalizes
the statement and invokes replay_storage_failure without remember_sqlite.
The latter rolls back and returns a generic CONTROL_STORAGE_FAILED. Capture
SQLite code/message before finalize/rollback. The audit size SELECT should also
capture a failing step before finalize, supplementing AUDIT_SIZE_SCAN_FAILED
with the original diagnostic. This finding is distinct from an expected DONE
(no replay) and does not change replay success semantics.

Frozen Linux 30/30 previously passed before the refactor. New scoped lint,
final Windows/Linux runtime suites and native sanitizer qualification remain
pending; no Release approval follows from the earlier snapshot.

## Implementation Gate — cleanup candidate — 2026-10-18

Outcome: **APPROVED**

Reviewed correction run: `CR27-IMPLEMENTATION-20261018` cleanup candidate.
src/mcp_http.c SHA-256:
`c790586e53e9715512ab53f91afed82c2297094c5a9ca3acd199ac8c2135c82b`.
Contract remains approved at SHA-256
`d6054d3d6e2e58164cecaed0f8031045be304edfc32368e744eb1f4ffe70cc97`.

I07 is resolved: replay read and audit SUM step failures call remember_sqlite
before finalize; error-chain preservation precedes rollback. Linux v2 executable
transport evidence independently read in build/cr27-linux-v2-http.log: 32 tests,
OK (11.133s), including the added genuine SQLite step failures. Scoped clang-tidy
log build/cr27-tidy-module-cleanup.log contains no reported source warning/error;
its generated warning count includes suppressed dependency diagnostics and is
not treated as a clean whole-repository lint claim. Producer reports cppcheck
exit 0; final QA must retain its exact command/log.

The refactor retains borrowed SQLite strings through synchronous finalize as
previously reviewed. Windows service record uses decimal UTC ticks, avoiding
PowerShell date coercion while preserving PID, exact executable path and start
time identity checks. No new concrete code blocker was found in this review.
All recorded implementation findings I01-I07 are resolved on this candidate.

This gate authorizes QA consumption of the named candidate. It is not a Release
Gate: final Windows 32-test suite/lifecycle, native sanitizer regressions and
security findings/baseline attribution still require review. Full canonical
lint/security failures remain visible and cannot be converted to a release
pass by this implementation-only decision.

## Final Architect Release Gate — 2026-10-18

Outcome: **CHANGES_REQUIRED**

Reviewed QA run: `CR27-QA-FINAL-20261018`.
Reviewed delivery report SHA-256:
`b041aefe563078d3356cabc7e001700058d2411aa63296727ad19e23bbab11ec`.
Approved implementation module SHA-256:
`c790586e53e9715512ab53f91afed82c2297094c5a9ca3acd199ac8c2135c82b`.
Contract SHA-256:
`d6054d3d6e2e58164cecaed0f8031045be304edfc32368e744eb1f4ffe70cc97`.
Source manifest SHA-256:
`5bf595ac189ca6f477cfab88c729e0055bca2903b2b3a672f327d8d5fd4a3eaa`.
Reviewer independently recomputed manifest and both delivered binary hashes:
Windows `253b4b24d1478c4b0e791026e498fcfd58bd5d9aa031c3bf106a10599e2b1e6a`;
Ubuntu `75c228c82a104f5657175a39242af9e0999543ef41254139baab4b3d0beef4c8`.

### Functional qualification accepted

Independently read final logs: Windows native HTTP 32/32 OK in 390.224s
(build/cr27-windows-http-final.log); Ubuntu native HTTP 32/32 OK in 42.465s
(build/cr27-linux-http-final.log); Windows start/status/restart/stop and machine
health PASS (build/cr27-windows-v2-service-direct.log); targeted native C
ASan/UBSan run 503 passed, 5 Windows-only platform skips, zero failures
(build/cr27-linux-native-final.log). Scoped new-module clang-tidy and cppcheck
results are clean; cppcheck's configuration-limit information is explicitly
retained. Official SDK transport, source/project isolation, native outline and
architecture, original SQLite/subprocess failure causes, audit double faults,
revocation/CAS and stdio behavior have executable evidence on this candidate.
The source manifest identifies the tested inputs; no new unresolved CR27-I01–I07
implementation finding remains. This accepts functional evidence, not release.

### Release blockers

- **CR27-R01 — Mandatory vendored integrity gate fails.** Baseline git-archive
  reproduction fails as does the candidate. Committed manifest encoding proof
  reports 1083 CR bytes; the verifier rejects checksum entries and does not
  establish vendored integrity. Original logs:
  build/cr27-baseline-vendored.log,
  build/cr27-baseline-encoding-proof.log,
  build/cr27-security-vendored-proper-final.log.
  A baseline defect explains attribution but cannot establish a passing
  security gate. Correct via an explicitly scoped follow-up and rerun the
  unchanged integrity checks; do not accept replacement checksums blindly.
- **CR27-R02 — Mandatory general contract gate fails.** The baseline reproduction
  rejects scripts/setup-windows.ps1 for non-ASCII PS 5.1 compatibility
  (build/cr27-baseline-contracts.log). The unchanged-file attribution is
  credible and visible. A separate narrow baseline correction and passing
  contract rerun are required for release qualification.
- **CR27-R03 — Full canonical lint/format is not green.** Preserved frozen
  logs report global failures; module-scoped checks and zero changed-line
  formatting findings do not equal a passing repository gate. Baseline fixes
  or a documented repository-owner decision changing the applicable gate are
  required. No blanket suppression or inferred waiver is granted here.

These blockers are pre-existing repository gate failures, not a demonstrated
runtime defect introduced by CR-27. They remain blockers because the mandatory
engineering workflow forbids release approval while required tests/checks fail.
Pending whole-repository cppcheck completion must be reported as pending and
cannot be represented as PASS.

DELIVERY.md accurately distinguishes the technically implemented/tested local
candidate from an approved release and retains original failures. No report
correction required at this checksum. It must retain the CHANGES_REQUIRED
Release outcome. ASMS DELIVERED/VERIFIED and consumer pin activation are not
authorized by this gate. No commit, push or publication was performed.

## Late containment review — 2026-10-18

Implementation outcome: **CHANGES_REQUIRED** (supersedes Implementation APPROVED
for release consumption until CR27-I08 is resolved).
Release outcome remains **CHANGES_REQUIRED**.

**P1 CR27-I08 — Windows process creation has an uncontained parent-death gap.**
Independent code inspection of foundation/subprocess.c confirms the supervisor
creates a suspended child and only afterwards calls AssignProcessToJobObject.
If the parent dies during that interval, the child has no membership in the
KILL_ON_JOB_CLOSE job and can remain suspended indefinitely. Suspension prevents
child instructions running but does not make parent death reap the child.
The existing comment claims more containment than this sequence guarantees.

Producer independently observed an old test-owned worker PID 46652, one thread,
zero CPU and suspended, whose parent PID 50204 had exited. It was stopped using
its exact owned-test process identity. That observation is consistent with this
race; no claim is made that its exact timing/cause was conclusively reproduced.
Passing ordinary timeout and final 32-test transport runs do not exercise this
parent-death interval, so they do not close this finding.

Minimum corrective design: create the child already assigned to the prepared
non-inheritable KILL_ON_JOB_CLOSE job via PROC_THREAD_ATTRIBUTE_JOB_LIST in the
existing STARTUPINFOEX attribute list (two entries: handle list and job list).
Preserve the job handle/value through attribute-list destruction, retain
CREATE_SUSPENDED if desired, then resume only after successful CreateProcess.
Fail closed with original Windows diagnostic if atomic job assignment is
unsupported or fails; do not fall back to the vulnerable post-create sequence.
This requires Windows 10 / Server 2016 or later, to be explicit in deployment
compatibility documentation.

Primary API evidence independently verified on 2026-10-18:
https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute
and Microsoft's direct explanation of this process/job race:
https://devblogs.microsoft.com/oldnewthing/20230209-00/?p=107812

Required regression: pause deterministically after atomic child creation and
before resume, terminate the owning supervisor process, assert the child exits
and no tree remains. Also prove membership before resume and successful normal
spawn/cancel on Windows. Rebuild and requalify affected Windows artifacts; update
manifest/binary hashes and DELIVERY.md. Keep prior results/logs historical.
No productive code was modified by this Architect review.

## Atomic Windows containment delta Gate — 2026-10-18

Contract outcome: **APPROVED**
CONTRACT.md SHA-256:
`2a221d063207fd4642ff73a90c2b67c430dffaeb13604980c26c39490303303b`.
Windows 10 / Server 2016 minimum explicitly reflects the documented JOB_LIST API
requirement. No vulnerable compatibility fallback is specified.

Implementation outcome: **APPROVED** (I08 correction).
Reviewed foundation/subprocess.c SHA-256:
`a3793303006a08badfe8f72451f05894847320ed5df43c11d517907e9db822bf`.
Module remains approved at c790586e53e9715512ab53f91afed82c2297094c5a9ca3acd199ac8c2135c82b.

Independent diff review confirms both startup attributes are populated before
CreateProcessW: inherited stdio handles and JOB_LIST pointing to the prepared
kill-on-close Job. Attribute setup failure exits before process creation, and
post-create AssignProcessToJobObject has been removed. The Job handle remains
valid through attribute-list destruction; it is not inherited into the child.
Suspended creation remains, with ResumeThread after successful atomic creation.
No POSIX spawn behavior changes. The hook is compiled only under the explicit
Windows test-seam guard and therefore absent from normal production builds.

Independently read deterministic fixture and PowerShell test plus
build/cr27-windows-atomic-test.log. The test queries membership in the exact
owned Job at the post-create/pre-resume hook, confirms the child exists, kills
its owning parent and waits for both to terminate within five seconds. Result:
Atomic Job membership before ResumeThread and child termination on owner death:
PASS. This directly covers the race interval missed by earlier timeout tests.
I08 is RESOLVED on the reviewed correction.

Release outcome remains **CHANGES_REQUIRED**: replacement production Windows
HTTP/lifecycle suites are in progress; hashes/manifest/report must identify the
new binary. Previously recorded baseline R01-R03 still block release. This
correction approval does not reuse the previous Windows binary's functional
qualification as if it had tested the new binary.

## Post-atomic QA status review — 2026-10-18

Outcome: **CHANGES_REQUIRED** (Release R01-R03 remain).
Source manifest independently recomputed:
`af697be0e4a1bd9ad0134a27d45ac4944c4a0754b2a8c8bfa9825a7e0100ea3e`.

Reviewed the Windows crash harness correction: 256 MiB Windows-only fixture
padding and ten-second observer budget increase the chance of observing actual
post-resume user CPU accounting; GetExitCodeProcess filters exited snapshot
entries. Native assertions are unchanged: WORKER_FAILED, exact CRASH outcome 2
and correlation ID. Isolated corrected case PASS in 13.367s is recorded in
build/cr27-windows-atomic-crash-final.log. The previous 31-pass/one-observation
failure remains historical failed qualification, not a native classification
failure and not a complete suite pass. Await the coherent full-suite rerun.

Ubuntu post-atomic rerun independently read: 32/32 OK in 42.402s
(build/cr27-linux-atomic-http.log). POSIX production binary is byte-identical
according to producer; Windows production replacement remains a distinct
artifact requiring its own completed suite.

Full global cppcheck is now explicitly STOPPED / NOT PASS, rather than pending,
per build/cr27-global-cppcheck-status.log. The untouched git-archive CLI
clang-tidy proof independently shows cert-err33-c errors, confirming baseline
lint attribution. None of these facts waives the mandatory repository gates.
No new implementation blocker was demonstrated in this test-only correction.

## Conclusive frozen-candidate review — 2026-10-18

Implementation outcome: **APPROVED**.
Release outcome: **CHANGES_REQUIRED**.

Final replacement Windows suite completed: independently read
build/cr27-windows-qualified-http.log, 32 tests OK in 410.106s.
Replacement Windows lifecycle independently read in
build/cr27-windows-atomic-service.log: start/status/restart/stop and machine
health PASS. Combined with post-atomic Ubuntu 32/32 OK, deterministic atomic
Job owner-death PASS, test-hook absence from production proof, and previous
unchanged POSIX sanitizer evidence (503 pass/zero fail/five platform skips),
the I08-corrected candidate has functional qualification on both platforms.
Historical failed observation and superseded binary results remain preserved.

Independently recomputed final hashes:
- Windows binary: 41bd0423adb512c77b1ad18c8aef2f1f6cab1e8d21974d7794f9461c4cd268b1.
- Ubuntu binary: 75c228c82a104f5657175a39242af9e0999543ef41254139baab4b3d0beef4c8.
- Contract: 2a221d063207fd4642ff73a90c2b67c430dffaeb13604980c26c39490303303b.
- Source manifest: af697be0e4a1bd9ad0134a27d45ac4944c4a0754b2a8c8bfa9825a7e0100ea3e.
- Native module: c790586e53e9715512ab53f91afed82c2297094c5a9ca3acd199ac8c2135c82b.
HEAD remains acdd9c3bf2d8ae3af6be45c8a6a96285c82d65f8; independent git diff --check
passes. No commit or push performed by this reviewer.

All implementation findings I01-I08 are resolved. No additional concrete
implementation blocker found. Release R01 vendored integrity, R02 baseline
PowerShell ASCII contract and R03 global lint/format remain open, with
reproduced baseline evidence. Global cppcheck was interrupted and is NOT PASS,
not pending and not qualifying. Baseline attribution does not waive required
gates. DELIVERY.md must carry the completed final Windows result above while
continuing to identify this as a local tested candidate with Release
CHANGES_REQUIRED. Consumer DELIVERED/VERIFIED/pin activation remain blocked.

Final DELIVERY.md update independently checked: SHA-256
`f4f1f6f33491b44623440d8f0addc41167720c5946d5a179ca6fa37c2655888a`.
It records final Windows 32/32 at 410.106s and global cppcheck interrupted
without PASS. Consistent with the conclusive gate decisions immediately above.
