export function normalizeFolder(path: string): string {
    const normalized = path.replace(/\\/g, '/').replace(/\/+$/g, '').replace(/^(\.\/)+/, '');
    if (!normalized || normalized.startsWith('/') || /^[a-z]:/i.test(normalized)
        || normalized.split('/').some(part => part === '..' || part === '' || part === '.') || /[\u0000-\u001f]/.test(normalized)) {
        throw new TypeError(`Invalid relative folder path: ${path}`);
    }
    return normalized;
}
/** Segment boundary avoids including src-old when src is selected. */
export function inFolder(file: string | undefined, path: string): boolean {
    return Boolean(file && (file === path || file.startsWith(`${path}/`)));
}
