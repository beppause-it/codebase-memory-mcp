import type { BrowserChatAttachment, BrowserChatContext, BrowserChatReaderContext } from './chat-model';

/** Optional read-only extension. Canonical Lens registers no provider. */
export interface ChatScopeRequest {
    project: string;
    generation?: string;
    selection?: { kind: string; path?: string };
}
export interface ChatScopeEvidence {
    key: string;
    label: string;
    text: string;
    /** Trusted provider-authored format rules; never interpolate repository data here. */
    instruction?: string;
    limitations: readonly string[];
    references: readonly { path: string; line: number }[];
    observations?: readonly { id: string; text: string }[];
}
export interface ChatScopeSources {
    attachment?: BrowserChatAttachment;
    reader?: BrowserChatReaderContext;
    graph?: BrowserChatContext;
    context: readonly BrowserChatContext[];
}
export interface ChatScopeProvider {
    scope(request: ChatScopeRequest, sources: ChatScopeSources): { label: string; request?: ChatScopeRequest };
    resolve(request: ChatScopeRequest, signal: AbortSignal): Promise<ChatScopeEvidence>;
    diagnostic(error: unknown): string;
    finalize?(evidence: ChatScopeEvidence, output: string): string;
}
