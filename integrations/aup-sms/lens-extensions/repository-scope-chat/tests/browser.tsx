/** Deterministic browser contract fixture. Does not claim real WebGPU inference. */
import { useState } from 'react';
import { createRoot } from 'react-dom/client';
import BrowserChatDock from '../../../../../graph-ui/src/browser-ai/BrowserChatDock';
import type { BrowserChatMessage } from '../../../../../graph-ui/src/browser-ai/browser-ai-runtime';
import { RepositoryChatScopeProvider } from '../src/RepositoryChatScopeProvider';

const provider = new RepositoryChatScopeProvider();
const trace: { prepared: unknown[]; requests: readonly BrowserChatMessage[][] } = { prepared: [], requests: [] };
Object.assign(window, { scopeTestTrace: trace });
function Fixture() {
    const [project, setProject] = useState('A'), [generation, setGeneration] = useState('g1');
    const [folder, setFolder] = useState(false), [reference, setReference] = useState('');
    return <>
        <button onClick={() => setProject(project === 'A' ? 'B' : 'A')}>Switch project</button>
        <button onClick={() => setGeneration(generation === 'g1' ? 'g2' : 'g1')}>Reindex</button>
        <button onClick={() => setFolder(value => !value)}>Folder scope</button>
        <output aria-label="Opened reference">{reference}</output>
        <BrowserChatDock key={`${project}:${generation}`} open proactive={false} settingsRequest={1}
            onClose={() => {}} onAttachmentConsumed={() => {}} isCached={async () => true}
            historyKey={`cr24:${project}:${generation}`}
            chatScopeProvider={provider} chatScopeRequest={{ project, generation, selection: folder ? { kind: 'Folder', path: 'src' } : undefined }}
            onScopeReference={(path, line) => setReference(`${project}:${path}:${line}`)}
            createRuntime={() => ({
                async prepare(_progress, options) { trace.prepared.push(options); },
                async explain() { return ''; }, async countTokens() { return 100; },
                async chat(messages) { (trace.requests as (readonly BrowserChatMessage[])[]).push(messages); return '{"observations":["counts","node-1"]}'; },
                stop() {}, dispose() {},
            })} />
    </>;
}
createRoot(document.getElementById('root')!).render(<Fixture />);
