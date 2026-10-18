# CR-24 — Repository and folder context for CBM Lens chat

| Field | Value |
|---|---|
| Date | 2026-10-18 |
| Requester | AUP-SMS |
| Owner | CodebaseMemory fork maintainers |
| Classification | `new-capability` |
| Status | `OPEN` |
| Implementation update | 2026-10-18: implemented for review; qualification blocked |
| ASMS phase | `0643 — Assistente locale di Codebase Memory` |
| Blocked scope | Repository-wide and folder-wide questions in CBM Lens chat |
| Verified owner baseline | `ec6427c38176efcd37b28682e82f2491afda94be` |
| Current consumer binary | `0.12.0-asms.1` |
| Current binary SHA-256 | `80cb59ad6074e67727e06bb894113c93a304cfe0f4f42a4c2c277814d7b581b1` |

## Request

Extend the browser-local CBM Lens assistant so it can answer grounded questions
about the whole indexed repository when no file or graph node is selected, and
about the recursive contents of a selected folder.

Examples: `che pattern usa questo repo?`, `quali sono le aree principali di
questa cartella?`, `quali dipendenze collegano questi moduli?`.

The change must remain isolated from upstream-owned Lens code so future
CodebaseMemory releases can be integrated without repeatedly forking existing
React components.

## Verified current behaviour

The current Lens chat is selection-driven:

- `README.md:203` states that Explore uses selected file/text and Galaxy uses
  the selected node and loaded neighbours;
- `README.md:216` states that chat explains supplied context and does not run tools;
- `graph-ui/README.md:43-50` limits automatic context to selected file/text;
- `graph-ui/src/browser-ai/BrowserChatDock.tsx:640-656` builds `currentGraph`
  from `proactiveSelection` and returns locally when no `chatTopic` exists;
- `graph-ui/src/browser-ai/chat-model.ts:189` prevents the model from claiming
  it executed tools;
- `graph-ui/src/App.tsx:4815-4822` passes only current selection, reader and
  inspector context;
- `graph-ui/src/architecture/repository-source.ts:63-100` exposes the repository
  snapshot and truncation indicators;
- `src/mcp/mcp.c:8036-8184` implements `get_architecture` with optional `path`
  scope and scoped node/edge counts.

Reproduction:

```powershell
rg -n "const currentGraph|Without code or graph facts|if \(!retry && !topic\)" graph-ui/src/browser-ai/BrowserChatDock.tsx
rg -n "scope_path|get_architecture|count_nodes_scoped|count_edges_scoped" src/mcp/mcp.c
```

The upstream roadmap was checked on 2026-10-18. Roadmap issue `#595`, UX epic
`#396`, Atlas proposal `#1964` and Lens PR `#2068` have no explicit
repository-wide or recursive-folder chat item. This is a new capability, not a
regression report.

## Required extension boundary

Place ASMS-owned implementation under:

```text
integrations/aup-sms/lens-extensions/repository-scope-chat/
  README.md
  src/
    RepositoryChatScopeProvider.ts
    RepositoryScopeEvidence.ts
    FolderScopeEvidence.ts
    ScopeBudget.ts
  tests/
```

Existing Lens code may receive only a small additive typed seam, such as an
optional `ChatScopeProvider`. Its default must be `undefined`, preserving the
current upstream behaviour and build when the extension is absent. The
qualified ASMS build registers the external provider.

Do not copy or replace `BrowserChatDock`, patch compiled bundles, inject prompts
through DOM scripts, create a second chat, turn the local model into a
tool-running agent, or add write access to repository/index/ADRs.

## Scope contract

Resolve bounded read-only evidence before inference. Precedence:

```text
selected text > selected file/symbol > selected folder > repository
```

- no selection uses the complete indexed repository;
- `Folder`/`Directory` uses its normalized path and recursive descendants, not
  only neighbours currently drawn in Galaxy;
- existing text/file/symbol/manual-context/retry/follow-up behaviour is unchanged;
- call `get_architecture` without `path` for repository scope and with normalized
  relative path for folder scope;
- `/api/repository-map` may supply areas, entry points, relations and counts;
  every truncation and limit remains explicit;
- cache keys include project, index generation, scope kind, normalized path and
  contract version; project/generation changes invalidate evidence;
- history and evidence never leak between projects or generations.

Composer and responses display effective scope: `Repository`,
`Cartella: <path>`, `File`, `Simbolo` or `Selezione`.

## Evidence and failure contract

Use only indexed bounded facts: counts, languages, node types, areas, entry
points, dependencies, clusters, hotspots, routes and returned relationships.
Patterns are grounded observations with navigable references, never invented
frameworks or architectural decisions.

Omissions, pagination, budgets, unindexed data and failures remain visible. A
failure never becomes empty context, a generic answer or success. Preserve
available type, code, message, details, correlation identifiers and cause.

Prompts, evidence and source remain browser-local. Do not add a network origin,
API key or server-side message persistence.

## Acceptance criteria

1. With a loaded model and no selection, `che pattern usa questo repo?` returns
   a repository-scoped answer with a verifiable reference and evidence limits.
2. Insufficient facts are stated precisely; no invented patterns, languages,
   frameworks, files or relationships.
3. A selected folder uses recursive descendants within budget; a test proves it
   is not limited to loaded Galaxy neighbours.
4. Selected text/file/symbol, `Add context`, retry, follow-up and automatic
   explanation preserve current behaviour when the provider is unnecessary.
5. The canonical package builds without the extension and preserves its previous default.
6. Original failures from `/api/repository-map`, `get_architecture`, parsing,
   generation and project/generation mismatch remain visible without renaming.
7. Budgets are deterministic; older history may be dropped first, current
   evidence is never silently truncated, and omissions are reported.
8. Vitest covers repository scope, recursive folder, precedence, truncation,
   cache invalidation, project change, original failures, prompt injection and
   extension absence.
9. Playwright verifies cached-model loading, repository and folder questions,
   navigable citations, no external request after model loading, and isolation
   between two projects.
10. Canonical UI tests, promise scan, style checks, production build and native
    tests for affected RPC surfaces are green.

## Owner delivery evidence

Provide commit SHA, binary/package version, changed-file inventory, extension
boundary rationale, exact tests and results, disabled-extension compatibility,
failure/truncation observability, isolation proof and upstream rebase notes.

ASMS marks this request `DELIVERED` only after owner evidence is available, and
`VERIFIED` only after testing the pinned build/hash through the ASMS Lens
gateway, CSP, tenant/project isolation, shared model cache and browser journey.

## Implementation handoff to ASMS / receiving team (2026-10-18)

The implementation is available in the current working tree, **uncommitted**.
Status remains `OPEN`: it is not `DELIVERED` or `VERIFIED`. No commit, push,
installation or native binary replacement has been performed. The consumer
binary/version/hash recorded above still refers to the previous consumer build.

Read the [owner review evidence](../lens-extensions/repository-scope-chat/DELIVERY.md)
for the changed-file inventory, exact commands, local logs, acceptance coverage,
failure diagnostics and remaining risks. The [extension README](../lens-extensions/repository-scope-chat/README.md)
documents activation, budgets, isolation and rebase requirements.

### Available implementation

- Package: `@aup-sms/lens-repository-scope-chat@0.1.0-cr24`.
- ASMS implementation: `integrations/aup-sms/lens-extensions/repository-scope-chat/`.
  Scope precedence, labels, folder interpretation, evidence, budgets and cache
  remain in that directory.
- Lens changes are the optional typed contract in
  `graph-ui/src/browser-ai/chat-scope.ts` and additive integration in `App.tsx`
  and `BrowserChatDock.tsx`. The canonical build registers no provider.
- Repository scope omits `path`; folder scope requests normalized recursive
  architecture evidence, independently of loaded Galaxy neighbours.
- Original diagnostics and nested truncation/pagination metadata remain visible.
  Cache and qualified history identities include project and index generation.
- The model selects indexed observation IDs. Only their grounded text renders;
  invented IDs, prose outside the JSON contract and malformed output fail visibly.
  This deliberately does not claim inferred frameworks or architectural decisions.

### How the receiving team can try the UI

The new UI has been built locally but is not embedded in the existing native
binary. With the CBM backend running at `http://127.0.0.1:9749` and the locked
`graph-ui` dependencies installed, run from the repository root:

```powershell
node graph-ui/node_modules/vite/bin/vite.js --config integrations/aup-sms/lens-extensions/repository-scope-chat/vite.config.ts --host 127.0.0.1
```

Open `http://127.0.0.1:5173`, enable the local agent and load the model. Model cache
availability must be checked on that browser origin; it is not proof of the ASMS
gateway's shared-cache configuration.

1. With no text/file/symbol/manual selection, ask `che pattern usa questo repo?`.
   Confirm scope `Repository`, evidence limits and a navigable source reference.
2. Select a folder in Galaxy and ask about its areas or dependencies. Confirm
   `Cartella: <path>` and recursive evidence beyond rendered neighbours.
3. Check text/file/symbol precedence, retry, follow-up and Add context.
4. Change project and reindex; verify that prior evidence/history never enters
   the new request. Inspect original error and truncation disclosures as well.

To build the qualified UI:

```powershell
npm --prefix integrations/aup-sms/lens-extensions/repository-scope-chat run build
```

Output is the extension's ignored `dist/`. Building `graph-ui` normally disables
the extension. Neither command installs or replaces the consumer native binary.

### Verification already performed

| Check | Recorded result |
|---|---|
| Extension Vitest | 26 passed, 2 files |
| Focused canonical chat/model/context tests | 145 passed, 3 files |
| Playwright contract journey | Passed with deterministic runtime and intercepted local data; cached-load call, repository/folder questions, citations, no external requests, two projects and reindex isolation |
| Canonical and qualified production builds | Passed; canonical inventory has zero ASMS imports |
| Qualified output inventory | 94 emitted hashes checked, zero mismatches |
| Promise scan and diff check | Passed |
| Full canonical Vitest | 3,949 passed, 3 failed; 2 additional suite-load errors |
| Frozen acceptance checks | 197 passed, 1 failed on the style report gate |
| Style gate | Failed: 57 matches in the prior tracked report due to Windows path-separator exclusion; no new implementation matches |
| Native MCP tests | Compilation blocked by `-Werror=stringop-overflow` in unchanged `tests/test_daemon_ipc.c`; no native pass claimed |

The local qualified `dist/index.html` SHA-256 is
`f4b9089fd1566e5f9b6d053a04822320ba1eaf9164bd51c34af193d7629ea195`.
This identifies the recorded local UI artifact, not a native package or deployed
gateway release. Any rebuild or packaging must have its own pinned evidence.

### Work remaining before acceptance

1. Review the uncommitted changes and preserve unrelated existing C/build changes
   listed in the owner evidence. Do not treat this tree as a clean release commit.
2. Resolve the canonical blockers: two tool-suite load errors, the existing
   security-origin guard failure, two compact-reference fixture failures and the
   Windows style-report exclusion. Resolve the native compilation blocker and
   rerun all required gates. Details and logs are linked in the owner evidence.
3. Verify the observation JSON protocol with real pinned model weights/WebGPU.
   The passing browser journey uses a deterministic runtime; it does **not** prove
   real-model loading, answer quality or compatibility with the small local model.
4. Validate the pinned build through the ASMS Lens gateway and CSP, tenant/project
   isolation, shared model cache and the real browser journey. A Vite preview is
   not gateway qualification.
5. Record the reviewed commit/package/native version and applicable hashes before
   marking `DELIVERED`; mark `VERIFIED` only after the consumer checks above.

The legacy scoped `get_architecture` response has no generation token. The
provider brackets it with repository-map reads and rejects changes; this is not
an atomic cross-endpoint transaction. Evidence is bounded to 8 nodes, 8 returned
relationships, 1,200 architecture-detail characters and 6,000 total evidence
characters. Original server bounds remain explicit. The real tokenizer may still
reject an oversized packet at the selected input limit rather than shortening it.
