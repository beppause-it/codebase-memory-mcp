import test from 'node:test';
import assert from 'node:assert/strict';
import {
    CBM_LENS_EMBED_SOURCE,
    CBM_LENS_EMBED_VERSION,
    isCbmLensMessage,
} from './message-contract.js';

test('accepts only the pinned source, version, generation and event type', () => {
    const valid = {
        source: CBM_LENS_EMBED_SOURCE,
        version: CBM_LENS_EMBED_VERSION,
        generation: 'generation-1',
        type: 'ready',
        payload: {},
    };
    assert.equal(isCbmLensMessage(valid, 'generation-1'), true);
    assert.equal(isCbmLensMessage({ ...valid, generation: 'generation-2' }, 'generation-1'), false);
    assert.equal(isCbmLensMessage({ ...valid, version: '2.0' }, 'generation-1'), false);
    assert.equal(isCbmLensMessage({ ...valid, type: 'mutation' }, 'generation-1'), false);
});

test('declares ready only after a valid child handshake', async () => {
    const windowTarget = new EventTarget();
    windowTarget.location = { origin: 'https://asms.test' };
    windowTarget.setTimeout = setTimeout;
    windowTarget.clearTimeout = clearTimeout;
    const registry = new Map();
    class FakeElement extends EventTarget {
        constructor() {
            super();
            this.attributes = new Map();
            this.children = [];
            this.style = {};
            this.isConnected = true;
        }
        setAttribute(name, value) { this.attributes.set(name, String(value)); }
        getAttribute(name) { return this.attributes.get(name) ?? null; }
        hasAttribute(name) { return this.attributes.has(name); }
        replaceChildren(...children) { this.children = children; }
        append(child) { this.children.push(child); }
        dispatchEvent(event) { return super.dispatchEvent(event); }
    }
    const frameWindow = {};
    globalThis.HTMLElement = FakeElement;
    globalThis.window = windowTarget;
    globalThis.document = {
        fullscreenElement: null,
        createElement: () => Object.assign(new FakeElement(), { contentWindow: frameWindow }),
    };
    globalThis.customElements = {
        get: (name) => registry.get(name),
        define: (name, value) => registry.set(name, value),
    };
    globalThis.fetch = async () => new Response(null, { status: 204 });
    const { CbmLensEmbed } = await import(`./cbm-lens-embed.js?handshake=${Date.now()}`);
    const embed = new CbmLensEmbed();
    embed.setAttribute('activation-url', 'https://asms.test/code-analysis/lens/activate#code=one');
    embed.setAttribute('generation', 'generation-1');
    embed.setAttribute('contract-version', '1.0');
    let ready = 0;
    embed.addEventListener('cbm-lens-ready', () => { ready++; });
    embed.connectedCallback();
    const frame = embed.children[0];

    frame.dispatchEvent(new Event('load'));
    assert.equal(ready, 0);
    const message = (origin) => Object.defineProperties(new Event('message'), {
        origin: { value: origin },
        source: { value: frameWindow },
        data: { value: { source: CBM_LENS_EMBED_SOURCE, version: '1.0', generation: 'generation-1', type: 'ready', payload: {} } },
    });
    windowTarget.dispatchEvent(message('https://attacker.test'));
    assert.equal(ready, 0);
    windowTarget.dispatchEvent(message('https://asms.test'));
    assert.equal(ready, 1);
    embed.disconnectedCallback();
});
