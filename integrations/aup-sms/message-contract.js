export const CBM_LENS_EMBED_SOURCE = 'cbm-lens-embed';
export const CBM_LENS_EMBED_VERSION = '1.0';

const TYPES = new Set(['ready', 'error', 'fullscreen-request', 'fullscreen-exit']);

export function isCbmLensMessage(value, generation) {
    return typeof value === 'object'
        && value !== null
        && value.source === CBM_LENS_EMBED_SOURCE
        && value.version === CBM_LENS_EMBED_VERSION
        && value.generation === generation
        && TYPES.has(value.type);
}
