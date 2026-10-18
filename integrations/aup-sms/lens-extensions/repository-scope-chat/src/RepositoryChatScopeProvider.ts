import { describeScope } from './ScopeSelection';
import type { ChatScopeProvider, ChatScopeRequest, ChatScopeEvidence } from '../../../../../graph-ui/src/browser-ai/chat-scope';
import { readRepositorySnapshot, type RepositorySnapshot } from '../../../../../graph-ui/src/architecture/repository-source';
import { callToolJson } from '../../../../../graph-ui/src/provider/rpc-transport';
import { CONTRACT_VERSION, diagnostic } from './ScopeBudget';
import { normalizeFolder } from './FolderScopeEvidence';
import { repositoryScopeEvidence } from './RepositoryScopeEvidence';

export interface ScopeTransport {
    snapshot(project: string, signal: AbortSignal): Promise<RepositorySnapshot & { indexMetadata?: Record<string, unknown> }>;
    architecture(args: Record<string, unknown>, signal: AbortSignal): Promise<unknown>;
}
const transport: ScopeTransport = {
    async snapshot(project, signal) {
        const response = await fetch(`/api/repository-map?${new URLSearchParams({ project })}`, { signal });
        if (!response.ok) {
            const body = await response.text();
            throw Object.assign(new Error(`Repository map returned HTTP ${response.status}.`), { status: response.status, statusText: response.statusText, body, correlationId: response.headers.get('x-correlation-id') });
        }
        const raw: unknown = await response.json();
        if (raw && typeof raw === 'object' && ('error' in raw || ('status' in raw && raw.status === 'failed'))) throw raw;
        if (!raw || typeof raw !== 'object' || !('project' in raw) || raw.project !== project) mismatch({ project }, raw);
        const snapshot = readRepositorySnapshot(raw);
        const fields = raw as Record<string, unknown>;
        return { ...snapshot, indexMetadata: Object.fromEntries(Object.entries(fields).filter(([key]) => !['nodes', 'edges'].includes(key))) };
    },
    architecture: (args, signal) => callToolJson('get_architecture', args, { signal }),
};
function mismatch(request: ChatScopeRequest, actual: unknown): never {
    throw Object.assign(new Error('Project or index generation changed while resolving scope evidence.'), { code: 'SCOPE_IDENTITY_MISMATCH', details: { expected: request, actual } });
}
/** No persisted evidence and no shared cache between projects/generations. Failures are never cached. */
export class RepositoryChatScopeProvider implements ChatScopeProvider {
    private namespace = '';
    private revision = 0;
    private cache = new Map<string, ChatScopeEvidence>();
    readonly diagnostic = diagnostic;
    readonly scope = describeScope;
    /** Only indexed observations are renderable. Model prose can never introduce new claims. */
    finalize(evidence: ChatScopeEvidence, output: string): string {
        const parsed: unknown = JSON.parse(output);
        if (!parsed || typeof parsed !== 'object' || !('observations' in parsed) || !Array.isArray(parsed.observations)
            || Object.keys(parsed).some(key => key !== 'observations')) throw new TypeError('Scope answer has an invalid observation contract.');
        const facts = new Map(evidence.observations?.map(item => [item.id, item.text]));
        const selected = [...new Set(parsed.observations)];
        if (selected.some(id => typeof id !== 'string' || !facts.has(id))) throw new TypeError('Scope answer cites an unknown indexed observation.');
        const lines = selected.map(id => facts.get(id as string)!);
        return lines.length
            ? `${evidence.label}\n\n${lines.map(line => `- ${line.replace(/[\\`*_{}\[\]()<>#!|]/g, '\\$&')}`).join('\n')}\n\nIndexed observations only. These facts do not establish a framework, architectural decision or runtime pattern; more evidence is required for those conclusions.`
            : `${evidence.label}\n\nInsufficient indexed evidence to establish the requested pattern or relationship. No verified observations were selected.`;
    }
    constructor(private readonly io: ScopeTransport = transport) {}
    async resolve(request: ChatScopeRequest, signal: AbortSignal): Promise<ChatScopeEvidence> {
        if (!request.project || !request.generation) throw Object.assign(new Error('No verified indexed project generation is available.'), { code: 'SCOPE_GENERATION_UNAVAILABLE', details: request });
        const folder = request.selection && ['Folder', 'Directory'].includes(request.selection.kind);
        if (request.selection && !folder) throw new TypeError('Scope provider cannot replace a file or symbol selection.');
        const path = folder ? normalizeFolder(request.selection!.path ?? '') : undefined;
        const namespace = JSON.stringify([request.project, request.generation]);
        if (namespace !== this.namespace) { this.namespace = namespace; this.revision++; this.cache.clear(); }
        const revision = this.revision;
        const key = JSON.stringify([request.project, request.generation, folder ? 'folder' : 'repository', path ?? '', CONTRACT_VERSION]);
        const verify = (snapshot: RepositorySnapshot): void => {
            signal.throwIfAborted();
            if (revision !== this.revision || snapshot.generation !== request.generation) mismatch(request, { namespace: this.namespace, generation: snapshot.generation });
        };
        // The legacy scoped architecture surface has no generation field. Bracket it with index snapshots.
        const before = await this.io.snapshot(request.project, signal); verify(before);
        const cached = this.cache.get(key);
        if (cached) return structuredClone(cached);
        const raw = await this.io.architecture({ project: request.project, format: 'json',
            aspects: ['structure', 'dependencies', 'hotspots'], ...(path ? { path } : {}) }, signal);
        if (!raw || typeof raw !== 'object' || Array.isArray(raw)) throw new TypeError('Architecture evidence has an invalid shape.');
        const architecture = raw as Record<string, unknown>;
        if ('error' in architecture || architecture.status === 'failed') throw raw;
        if (architecture.project !== request.project || (path && architecture.path !== path)) mismatch(request, architecture);
        if (![architecture.total_nodes, architecture.total_edges].every(count => typeof count === 'number' && Number.isSafeInteger(count) && count >= 0)) throw new TypeError('Architecture evidence has invalid indexed counts.');
        const after = await this.io.snapshot(request.project, signal); verify(after);
        const evidence = repositoryScopeEvidence(key, request.project, after, architecture, path);
        if (this.cache.size >= 16) this.cache.delete(this.cache.keys().next().value!);
        this.cache.set(key, evidence);
        return structuredClone(evidence);
    }
}
