# CR-24 review evidence

Date: 2026-10-18. Status: implemented for review; **not release-qualified**.
CR status remains OPEN. No commit or push was performed. The work is uncommitted.

Owner baseline: `ec6427c38176efcd37b28682e82f2491afda94be`.
Package: `@aup-sms/lens-repository-scope-chat@0.1.0-cr24`.
No new native binary, delivery commit SHA, native version or binary SHA-256 is
claimed. The consumer binary/version/hash quoted in CR-24 has not been replaced.

Qualified `dist/index.html` SHA-256:
`f4b9089fd1566e5f9b6d053a04822320ba1eaf9164bd51c34af193d7629ea195`.
The ASMS inventory's 94 emitted output hashes were compared with the built files:
zero mismatches. The canonical build inventory contains zero ASMS extension
imports. Machine-readable proof: `build/cr24-artifacts.json`. This HTML hash
identifies the local UI artifact, not a packaged native binary or gateway release.

## Changed-file inventory

Canonical Lens changes:

- `graph-ui/src/browser-ai/chat-scope.ts`: optional typed scope/evidence/provider
  contract. Scope precedence, labels and Galaxy identity interpretation remain ASMS-owned.
- `graph-ui/src/browser-ai/BrowserChatDock.tsx`: optional scope resolution,
  cancellation, original scoped diagnostics, scoped response finalization, scope,
  limits and navigable references. Existing chat component is edited in place.
- `graph-ui/src/App.tsx`: optional provider pass-through, project/generation
  lifecycle/history identity and existing source-drawer navigation.

ASMS implementation, all under this directory:

- `.gitignore`, `package.json`, `tsconfig.json`, `vite.config.ts`, `index.html`,
  `license-inventory.ts`: isolated package/build/qualified bootstrap and inventory.
- `src/RepositoryChatScopeProvider.ts`, `src/RepositoryScopeEvidence.ts`,
  `src/FolderScopeEvidence.ts`, `src/ScopeBudget.ts`, `src/ScopeSelection.ts`: read-only resolution,
  observations, recursion, cache, identity checks, deterministic bounds and errors.
- `src/main.tsx`, `src/env.d.ts`: opt-in entry and canonical dependency types.
- `tests/provider.test.ts`, `tests/dock.test.tsx`: Vitest acceptance contracts.
- `tests/browser.html`, `tests/browser.tsx`, `tests/browser.test.mjs`: browser
  contract harness and Playwright journey.
- `README.md`, `DELIVERY.md`: usage, boundary, risks and review evidence.

Pre-existing modifications were preserved: `internal/cbm/doc_pdf.c`,
`scripts/build.sh`, `src/git/git_context.c`, `src/git/git_context.h`, `src/main.c`,
`src/mcp/mcp.c`, plus the existing untracked `integrations/` contents.
Generated canonical style/promise reports were restored to their original contents;
test logs and build output live in ignored build directories.

## Commands and results

Commands are run from the repository root unless stated otherwise. Windows runs
used workspace `build/cr24-temp` for `TEMP`/`TMP`. Tests requiring Bash/Chrome were
also run outside the process/network sandbox after the sandbox blocked them.

| Command | Result | Local log |
|---|---|---|
| `npm --prefix integrations/aup-sms/lens-extensions/repository-scope-chat test` | 26 passed, 2 files | `build/cr24-extension-unit.log` |
| `npm --prefix integrations/aup-sms/lens-extensions/repository-scope-chat run build` | TypeScript and qualified Vite production build passed; existing chunk-size warnings | `build/cr24-asms-build.log` |
| `CBM_TEST_BROWSER_CHANNEL=chrome node --test integrations/aup-sms/lens-extensions/repository-scope-chat/tests/browser.test.mjs` (PowerShell uses `$env:`) | 1 browser journey passed | `build/cr24-browser.log` |
| In `graph-ui`: `npm run build` | Canonical production build passed, extension unregistered | `build/cr24-canonical-build.log` |
| In `graph-ui`: `npm run test:unit -- --maxWorkers=2 --testTimeout=20000 --reporter=dot` | 3,949 passed, 3 failed; 306 files passed, 4 failed including 2 suite-load errors | `build/cr24-unit-final.log` |
| In `graph-ui`: `npm exec -- vitest run src/browser-ai/BrowserChatDock.test.tsx src/browser-ai/chat-model.test.ts src/browser-ai/chat-context.test.ts --maxWorkers=2 --reporter=dot` | 145 passed, 3 files | `build/cr24-canonical-chat-final.log` |
| In `graph-ui`: `npm run test:acceptance` | 197 passed, 1 failed (style report gate) | `build/cr24-acceptance.log` |
| `node graph-ui/tools/promise-scan.mjs --out build/cr24-promises-final.json` (also ran `npm run check:promises`) | 0 promises / 0 inactive controls | `build/cr24-promises-final.log` |
| `node graph-ui/tools/style-gate.mjs --out build/cr24-style-final.json` (also ran `npm run check:style`) | 0 prohibited dashes, 0 hardcoded Chrome strings, 57 attribution matches in the prior report | `build/cr24-style-final.log` |
| MSYS2: `scripts/test.sh --suites mcp SANITIZE= BUILD_DIR=build/cr24-native CC=gcc` | Native test compilation blocked by `-Werror=stringop-overflow` in unchanged `tests/test_daemon_ipc.c`; no native test pass claimed | `build/cr24-native.log` |
| `git diff --check` | Passed | terminal output |

The full UI failures are in unchanged source/test surfaces:

- `tools/agent-bridge.test.mjs` and `tools/net-deny-gate.test.mjs` fail to load with
  `SyntaxError: Invalid or unexpected token` in Vitest on this Windows host.
- `security-download-origins.test.ts` unexpectedly accepts the lookalike fixture
  `https://huggingface.co.attacker.test/model` through the existing Bash guard.
- Two `compact-references.test.ts` cases fail: the recorded response has seven
  cells where the header declares six, leaving the real inspector fixture empty.
- Style's `relative()` exclusion uses Windows backslashes while Git reports
  forward-slash paths, so its own tracked previous report is scanned. All 57
  matches come from that report; no new implementation strings are implicated.
- Native test compilation diagnoses a 1,024-byte output contract being passed
  256-byte buffers in `ipc_test_copy_path`. The failed compilation process was
  stopped after recording those diagnostics; unrelated native tests were not fixed.

These failures are not declared green or repaired as part of CR-24. They prevent
acceptance criterion 10 and release qualification. The initial sandbox-only runs
also hit temporary-file EPERM, unavailable Bash and denied loopback navigation;
reruns used writable temporary storage and the existing installed tools.

## Acceptance coverage

| CR criterion | Evidence / status |
|---|---|
| 1 Repository question | Dock + browser fixture prove scoped answer, source reference and limits; real cached-model answer still requires gateway qualification. |
| 2 No invented facts | Unknown IDs/prose/extra fields/malformed JSON fail; only indexed observations render, with explicit insufficiency for patterns/frameworks/decisions. |
| 3 Recursive folder | Provider test includes `src/deep` and excludes `src-other`; canonical Galaxy folder evidence with zero rendered neighbours is recognized; browser folder query uses `path=src`. |
| 4 Existing behavior | Selection/manual-context precedence tests, default-provider absence and existing canonical dock/model/context tests pass; retry uses the original packet/request and follow-up uses the same scoped topic. |
| 5 Extension absent | Canonical build passes with default undefined provider; canonical source has no import from this directory. |
| 6 Original failures | Vitest covers HTTP body/status/correlation, parsing exception identity, transport errors/causes, structured failures, runtime generation errors and project/generation mismatch. |
| 7 Budgets | Deterministic samples, explicit omission counts, nested truncation/pagination disclosure, oldest-history removal preserving exact evidence, and rejection before inference when still oversized. |
| 8 Vitest | 26 passing tests cover scope, recursion, precedence, truncation, cache/mutations, stale async results, project changes, original failures, injection and absence. |
| 9 Playwright | Deterministic browser journey passes cached `prepare({cacheOnly:true})`, repository/folder queries, source navigation, zero external requests, two projects and reindex isolation. Actual downloaded weights/WebGPU and ASMS gateway/CSP remain unverified. |
| 10 Canonical/native gates | Builds and promise scan pass. Complete canonical tests, style/acceptance gate and native compilation are not green; blockers above. |

## Remaining risks and qualification

The 0.5B/other pinned models must reliably emit the observation JSON protocol;
this was tested with deterministic runtimes, not real weights. The conservative
protocol returns observations and precise limitations, not free-form architectural
claims. The 6,000-character evidence budget is independent of tokenization; the
existing real tokenizer may still reject it at a user's selected input limit.
Increasing that limit is explicit; evidence is never silently shortened.

The legacy architecture RPC cannot provide an atomic generation check. Two map
reads detect generation changes around it, but backend atomic qualification would
require a separately agreed RPC change. Repository-map server bounds remain in
effect, so recursive scope is over the indexed repository with bounded returned
evidence, never a promise that every descendant is included in the prompt.

Before marking DELIVERED/VERIFIED: resolve the canonical/native blockers, pin the
qualified package/build hash, then run real-model loading and question journeys
through the ASMS Lens gateway, CSP, tenant/project isolation and shared model cache.
No release, installation, native binary replacement, commit or push was performed.
