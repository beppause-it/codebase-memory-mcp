import type { ChatScopeRequest, ChatScopeSources } from '../../../../../graph-ui/src/browser-ai/chat-scope';
import type { BrowserChatAttachment, BrowserChatContext, BrowserChatReaderContext } from '../../../../../graph-ui/src/browser-ai/chat-model';
import { readGalaxyEvidence } from '../../../../../graph-ui/src/browser-ai/galaxy-evidence';
/** Folder navigation is a scope, not necessarily a selected graph node. */
export function effectiveChatScope(request: ChatScopeRequest | undefined, graph?: BrowserChatContext): ChatScopeRequest | undefined {
    if (!request || !graph) return request;
    const identity = readGalaxyEvidence(graph.text)?.identity;
    if (identity && typeof identity === 'object' && 'kind' in identity && identity.kind === 'folder'
        && 'path' in identity && typeof identity.path === 'string') {
        return { ...request, selection: { kind: 'Folder', path: identity.path } };
    }
    return request;
}
export function needsChatScope(request: ChatScopeRequest | undefined, sources: {
    attachment?: BrowserChatAttachment; reader?: BrowserChatReaderContext;
    graph?: BrowserChatContext; context: readonly BrowserChatContext[];
}): boolean {
    if (!request || sources.attachment || sources.reader?.path || sources.context.length) return false;
    if (request.selection) return ['Folder', 'Directory'].includes(request.selection.kind);
    return !sources.graph;
}
export function chatScopeLabel(request: ChatScopeRequest, sources: {
    attachment?: BrowserChatAttachment; reader?: BrowserChatReaderContext; context: readonly BrowserChatContext[];
}): string {
    if (sources.attachment || sources.reader?.source?.kind === 'selection') return 'Selezione';
    if (sources.reader?.path) return 'File';
    if (sources.context.length) return 'Selezione';
    if (request.selection) return ['Folder', 'Directory'].includes(request.selection.kind)
        ? `Cartella: ${request.selection.path ?? ''}` : request.selection.kind === 'File' ? 'File' : 'Simbolo';
    return 'Repository';
}

export function describeScope(request: ChatScopeRequest, sources: ChatScopeSources): { label: string; request?: ChatScopeRequest } {
    const effective = effectiveChatScope(request, sources.graph)!;
    return { label: chatScopeLabel(effective, sources), ...(needsChatScope(effective, sources) ? { request: effective } : {}) };
}
