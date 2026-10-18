# Repository scope chat (CR-24)

ASMS package `@aup-sms/lens-repository-scope-chat`, version `0.1.0-cr24`.
The provider is opt-in. Canonical Lens imports no ASMS source and uses no provider.

From the repository root, with the locked `graph-ui` dependencies installed:

```powershell
npm --prefix integrations/aup-sms/lens-extensions/repository-scope-chat test
npm --prefix integrations/aup-sms/lens-extensions/repository-scope-chat run build
$env:CBM_TEST_BROWSER_CHANNEL = 'chrome' # optional installed Chrome; otherwise Playwright Chromium
npm --prefix integrations/aup-sms/lens-extensions/repository-scope-chat run test:browser
```

On restricted Windows hosts, set `TEMP` and `TMP` to a writable workspace directory
before Vitest. The browser test starts Vite on `127.0.0.1:5173`, intercepts the two
read-only data surfaces and uses a deterministic inference fixture. It does not
download or prove real model weights. The pinned-model/gateway qualification is
still required. See [delivery evidence](DELIVERY.md).

## Boundary and activation

The qualified entry point is `src/main.tsx`; its `App` receives a provider instance.
`vite.config.ts` builds that entry into this directory's ignored `dist/`.
`license-inventory.ts` records source and output hashes, allows only the canonical
UI, this extension, and the exact parent package manifest that Vite reads, and
rejects unknown input roots. Existing worker inventory collection stays enabled.
The ASMS inventory lives at `.vite/license-build.json`; it is distinct from the
canonical release inventory and does not claim canonical release qualification.

Disable by building the canonical `graph-ui` package, or by rendering `App` without
`chatScopeProvider`. No environment flags, compiled bundle edits, DOM scripts,
second chat, repository writes or new dependencies are used.

The additive Lens seam consists of `chat-scope.ts` and optional props in `App` and
`BrowserChatDock`. The dock retains loading, token counting, inference, retries,
history trimming and existing selection/automatic-explanation behavior. It invokes
the provider's descriptor to decide whether additional evidence is needed.
ASMS `ScopeSelection.ts` owns precedence, labels and Galaxy folder identity;
text, file/symbol and explicit manual context take precedence. Folder identity is
read from existing typed selection evidence, including an empty/limited rendered
neighbourhood. Provider-authored response format rules occupy a separate trusted
instruction field; repository strings remain exclusively untrusted evidence data.

## Evidence, answers and isolation

Repository requests omit `path`; folders normalize a relative path and request
`get_architecture` recursively. Snapshot paths use a segment boundary, so `src`
includes `src/deep` and excludes `src-other`. The renderer's neighbour set is never
an input to the provider. The read-only legacy architecture query requests
`structure`, `dependencies` and `hotspots` in JSON.

The legacy RPC has no generation field. Repository-map reads before and after it
must match the requested generation. This detects changes but is not an atomic
cross-endpoint transaction. Both successful HTTP payload identities and RPC
project/path identities are checked. Original transport/parser errors are thrown
unchanged; diagnostics retain error properties, stack, cause, code, details and
correlation data. HTTP error bodies and correlation headers remain available.

The memory cache key contains project, generation, kind, normalized path and
contract version. A namespace change clears it, and a revision rejects stale
in-flight results. Cache hits still verify the generation. Cached packets are
cloned, failures are not cached and scopes are bounded to 16 cached packets.
Qualified chat/history keys include project and generation; generation changes
remount the dock, cancel old work and clear pending snapshots. Local history keeps
the canonical TTL and storage bounds, with no server-side message persistence.

Evidence budgets are fixed: 8 nodes, 8 relationships, 1,200 architecture-detail
characters, 6,000 total evidence characters. Ordering is deterministic. Entire
architecture detail can be omitted with its size disclosed; original nested
limits/truncations/pagination metadata remain visible. Oversized evidence fails
explicitly. Current evidence is never sliced to fit inference: older history is
dropped first, then the original tokenizer gate rejects an oversized request.

To avoid invented architectural claims, the model selects observation IDs with
`{"observations":["counts","node-1"]}`. Only the corresponding indexed text is
rendered; model prose, unknown IDs, additional fields or invalid JSON fail visibly.
Empty selections state insufficient evidence. Observations expose indexed counts,
source identities, returned relationships and a bounded architecture summary.
They explicitly do not prove a framework, architecture decision or runtime pattern.
References use the existing Lens source drawer, not model-generated URLs. Scope
and limitations are shown beside each answer, including failed/shortened output.

## Rebase notes

Keep all ASMS files here. Reapply only the optional seam if upstream moves chat
submission, project-window props or history ownership. Do not copy the dock.
Recheck the legacy architecture JSON and repository-map generation contracts,
Galaxy selection identity, source-drawer navigation and tokenizer gate after a
rebase. Run both canonical and qualified builds; removing this directory must not
affect the canonical build. The extension's conservative observation protocol
requires real-model compatibility qualification before delivery.
