import {
    CBM_LENS_EMBED_VERSION,
    isCbmLensMessage,
} from './message-contract.js';

const TAG_NAME = 'cbm-lens-embed';
const ElementBase = globalThis.HTMLElement ?? class {};

export class CbmLensEmbed extends ElementBase {
    static observedAttributes = [
        'activation-url',
        'generation',
        'contract-version',
        'allow-fullscreen',
    ];

    #frame = null;
    #messageHandler = null;
    #sessionUrl = null;
    #handshakeTimer = null;

    connectedCallback() {
        this.#render();
    }

    disconnectedCallback() {
        if (this.#sessionUrl !== null) {
            void fetch(this.#sessionUrl, { method: 'DELETE', credentials: 'include', keepalive: true });
        }
        this.#detach();
    }

    attributeChangedCallback() {
        if (this.isConnected) this.#render();
    }

    requestFullscreen() {
        if (!this.hasAttribute('allow-fullscreen') || this.#frame === null) return;
        void this.#frame.requestFullscreen?.();
    }

    #render() {
        this.#detach();
        this.replaceChildren();

        const activationUrl = this.getAttribute('activation-url');
        const generation = this.getAttribute('generation');
        const version = this.getAttribute('contract-version') ?? CBM_LENS_EMBED_VERSION;
        if (activationUrl === null || generation === null || version !== CBM_LENS_EMBED_VERSION) {
            this.#emit('cbm-lens-error', {
                type: 'CbmLensEmbedContractError',
                code: 'CBM_LENS_EMBED_INVALID_CONFIGURATION',
                message: 'Lens embed requires a supported contract, activation URL and generation.',
                details: { version, generation },
            });
            return;
        }

        const url = new URL(activationUrl, window.location.origin);
        if (url.origin !== window.location.origin) {
            this.#emit('cbm-lens-error', {
                type: 'CbmLensEmbedContractError',
                code: 'CBM_LENS_EMBED_ORIGIN_MISMATCH',
                message: 'Lens activation URL must use the ASMS origin.',
                details: { origin: url.origin },
            });
            return;
        }
        this.#sessionUrl = new URL('./session', url).toString();

        const frame = document.createElement('iframe');
        frame.src = url.toString();
        frame.title = 'Codebase Memory Lens';
        frame.loading = 'eager';
        frame.referrerPolicy = 'no-referrer';
        frame.allowFullscreen = this.hasAttribute('allow-fullscreen');
        frame.style.width = '100%';
        frame.style.height = 'min(78vh, 960px)';
        frame.style.border = '0';
        this.#frame = frame;

        this.#handshakeTimer = setTimeout(() => {
            this.#emit('cbm-lens-error', {
                type: 'CbmLensEmbedHandshakeError',
                code: 'CBM_LENS_EMBED_HANDSHAKE_TIMEOUT',
                message: 'Codebase Memory Lens did not complete the versioned handshake.',
                details: { generation, version },
            });
        }, 15000);

        this.#messageHandler = (event) => {
            if (event.origin !== url.origin || event.source !== frame.contentWindow) return;
            if (!isCbmLensMessage(event.data, generation)) return;
            if (event.data.type === 'error') {
                this.#clearHandshakeTimer();
                this.#emit('cbm-lens-error', event.data.payload);
            }
            if (event.data.type === 'ready') {
                this.#clearHandshakeTimer();
                this.#emit('cbm-lens-ready', event.data.payload);
            }
            if (event.data.type === 'fullscreen-request') this.requestFullscreen();
            if (event.data.type === 'fullscreen-exit' && document.fullscreenElement === frame) {
                void document.exitFullscreen?.();
            }
        };
        window.addEventListener('message', this.#messageHandler);
        this.append(frame);
    }

    #detach() {
        if (this.#messageHandler !== null) window.removeEventListener('message', this.#messageHandler);
        this.#clearHandshakeTimer();
        this.#messageHandler = null;
        this.#frame = null;
        this.#sessionUrl = null;
    }

    #clearHandshakeTimer() {
        if (this.#handshakeTimer !== null) clearTimeout(this.#handshakeTimer);
        this.#handshakeTimer = null;
    }

    #emit(name, detail) {
        this.dispatchEvent(new CustomEvent(name, { detail, bubbles: true, composed: true }));
    }
}

if (globalThis.customElements && !globalThis.customElements.get(TAG_NAME)) {
    globalThis.customElements.define(TAG_NAME, CbmLensEmbed);
}
