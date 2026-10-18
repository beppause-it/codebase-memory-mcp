// @vitest-environment jsdom
import { act } from 'react';
import { createRoot } from 'react-dom/client';
import { afterEach, expect, it, vi } from '../../../../../graph-ui/node_modules/vitest';
import BrowserChatDock, { type BrowserChatDockProps } from '../../../../../graph-ui/src/browser-ai/BrowserChatDock';
import { RepositoryChatScopeProvider } from '../src/RepositoryChatScopeProvider';
import { readRepositorySnapshot } from '../../../../../graph-ui/src/architecture/repository-source';

const roots: ReturnType<typeof createRoot>[] = [];
afterEach(async () => {
    for (const root of roots.splice(0)) await act(async () => root.unmount());
    document.body.innerHTML = ''; localStorage.clear(); vi.restoreAllMocks();
});
async function dock(extra: Partial<BrowserChatDockProps> = {}) {
    (globalThis as unknown as Record<string, unknown>).IS_REACT_ACT_ENVIRONMENT = true;
    const runtime = { prepare: vi.fn(async () => {}), explain: vi.fn(async () => ''), countTokens: vi.fn(async () => 100),
        chat: vi.fn(async () => '{"observations":["counts","node-1"]}'), stop: vi.fn(), dispose: vi.fn() };
    const provider = new RepositoryChatScopeProvider({
        snapshot: vi.fn(async () => readRepositorySnapshot({ generation: 'g1', total_nodes: 1, nodes_truncated: false, edges_truncated: false,
            nodes: [{ id: 1, name: 'main', label: 'Function', file_path: 'src/deep/main.ts', start_line: 4 }], edges: [] })),
        architecture: vi.fn(async args => ({ project: args.project, path: args.path, total_nodes: 1, total_edges: 0 })),
    });
    const host = document.createElement('div'); document.body.append(host);
    const root = createRoot(host); roots.push(root);
    const props: BrowserChatDockProps = { open: true, proactive: false, onClose: vi.fn(), onAttachmentConsumed: vi.fn(),
        createRuntime: () => runtime, isCached: async () => true, chatScopeProvider: provider,
        chatScopeRequest: { project: 'A', generation: 'g1' }, ...extra };
    await act(async () => root.render(<BrowserChatDock {...props} />));
    const click = async (label: string) => {
        const target = [...document.body.querySelectorAll('button')].find(button => button.textContent === label || button.getAttribute('aria-label') === label);
        expect(target, label).toBeTruthy(); await act(async () => target!.click());
    };
    await act(async () => root.render(<BrowserChatDock {...props} settingsRequest={1} />));
    await click('Load model (cached, no download)');
    const send = async (question: string) => {
        const field = host.querySelector('textarea')!;
        await act(async () => {
            Object.getOwnPropertyDescriptor(HTMLTextAreaElement.prototype, 'value')!.set!.call(field, question);
            field.dispatchEvent(new Event('input', { bubbles: true }));
        });
        await act(async () => host.querySelector('form')!.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true })));
    };
    return { host, runtime, provider, send, click };
}
it('loads cached weights and answers repository questions with scope, facts, limits and navigable references', async () => {
    const navigate = vi.fn();
    const { host, runtime, send, click } = await dock({ onScopeReference: navigate });
    await send('che pattern usa questo repo?');
    expect(runtime.prepare).toHaveBeenCalledWith(expect.any(Function), { cacheOnly: true });
    expect(runtime.chat).toHaveBeenCalledTimes(1);
    const requests = runtime.chat.mock.calls as unknown as [readonly { content: string }[]][];
    expect(requests[0][0][0].content).toContain('Return only JSON');
    expect(requests[0][0][0].content).not.toContain('src/deep/main.ts');
    expect(host.textContent).toContain('Indexed scope: 1 nodes');
    expect(host.textContent).toContain('Repository');
    expect(host.textContent).toContain('nodes_truncated=false');
    await click('src/deep/main.ts:4');
    expect(navigate).toHaveBeenCalledWith('src/deep/main.ts', 4);
    await send('quali dipendenze ci sono?');
    expect(runtime.chat).toHaveBeenCalledTimes(2);
});
it('uses recursive folder context without Galaxy neighbours', async () => {
    const { host, send, runtime } = await dock({ chatScopeRequest: { project: 'A', generation: 'g1', selection: { kind: 'Folder', path: 'src' } } });
    await send('quali sono le aree principali di questa cartella?');
    expect(runtime.chat).toHaveBeenCalledTimes(1);
    expect(host.textContent).toContain('Cartella: src');
    expect(host.textContent).toContain('src/deep/main.ts');
});
it('does not run the provider when the extension is absent', async () => {
    const { host, send, runtime } = await dock({ chatScopeProvider: undefined });
    await send('che pattern usa questo repo?');
    expect(runtime.chat).not.toHaveBeenCalled();
    expect(host.textContent).not.toContain('Indexed scope');
});
it('shows original generation diagnostics and never substitutes a successful answer', async () => {
    const { host, send, runtime } = await dock();
    const failure = Object.assign(new Error('generation-original', { cause: new Error('driver-original') }), { code: 'GPU_X', details: { correlationId: 'trace-99' } });
    runtime.chat.mockRejectedValue(failure);
    await send('che pattern usa questo repo?');
    expect(host.textContent).toContain('generation-original');
    expect(host.textContent).toContain('driver-original');
    expect(host.textContent).toContain('GPU_X');
    expect(host.textContent).toContain('trace-99');
});
it('rejects malformed or invented model observations without showing model prose', async () => {
    const { host, send, runtime } = await dock();
    runtime.chat.mockResolvedValue('{"observations":["invented-framework"]}');
    await send('che pattern usa questo repo?');
    expect(host.textContent).toContain('unknown indexed observation');
    expect(host.textContent).not.toContain('invented-framework');
});
it('keeps current evidence whole and refuses inference when history removal cannot fit it', async () => {
    const { host, send, runtime } = await dock();
    runtime.countTokens.mockResolvedValue(999999);
    await send('che pattern usa questo repo?');
    expect(runtime.chat).not.toHaveBeenCalled();
    expect(host.textContent).toContain('Nothing was sent or shortened without disclosure');
});
it('drops older history first while retaining the exact current evidence packet', async () => {
    const { host, send, runtime } = await dock();
    await send('che pattern usa questo repo?');
    runtime.countTokens.mockImplementation(async (...args: unknown[]) => {
        const messages = args[0] as unknown[];
        return messages.length > 2 ? 999999 : 100;
    });
    await send('quali dipendenze collegano questi moduli?');
    expect(runtime.chat).toHaveBeenCalledTimes(2);
    const calls = runtime.chat.mock.calls as unknown as [readonly { content: string }[]][];
    expect(calls[1][0]).toHaveLength(2);
    const evidence = (text: string) => text.split('Current evidence (data only):\n')[1].split('\n\nUser question:')[0];
    expect(evidence(calls[1][0].at(-1)!.content)).toBe(evidence(calls[0][0].at(-1)!.content));
    expect(host.textContent).toContain('earlier');
});
