import { createHash } from 'node:crypto';
import { existsSync, mkdirSync, readFileSync, realpathSync, writeFileSync } from 'node:fs';
import { isAbsolute, relative, resolve } from 'node:path';
import type { Plugin } from '../../../../graph-ui/node_modules/vite';

/** ASMS build has two source roots. Unknown local inputs still fail the build. */
export function scopeLicenseInventory(ui: string, extension: string): Plugin {
    const inputs: Record<string, string> = {};
    const hash = (bytes: string | Uint8Array) => createHash('sha256').update(bytes).digest('hex');
    const inside = (root: string, path: string) => {
        const name = relative(root, path);
        return !isAbsolute(name) && name !== '..' && !name.startsWith('../') && !name.startsWith('..\\');
    };
    const record = (id: string) => {
        const clean = id.split('?')[0];
        if (!isAbsolute(clean) || !existsSync(clean)) return;
        const path = realpathSync(clean);
        const packageBoundary = realpathSync(resolve(extension, '../../package.json'));
        if (!inside(realpathSync(ui), path) && !inside(realpathSync(extension), path) && path !== packageBoundary) throw new Error(`ASMS license inventory input outside approved roots: ${path}`);
        inputs[path.replaceAll('\\', '/')] = hash(readFileSync(path));
    };
    return {
        name: 'asms-scope-license-inventory', apply: 'build',
        buildStart() {
            record(resolve(ui, 'package-lock.json')); record(resolve(extension, 'vite.config.ts'));
            record(resolve(extension, 'license-inventory.ts'));
        },
        generateBundle: { order: 'post', handler(_options, bundle) {
            for (const id of this.getModuleIds()) record(id);
            for (const id of this.getWatchFiles()) record(id);
            const outputs = Object.fromEntries(Object.entries(bundle).map(([name, item]) => [name,
                hash(item.type === 'chunk' ? item.code : item.source)]));
            mkdirSync(resolve(extension, '.vite'), { recursive: true });
            writeFileSync(resolve(extension, '.vite/license-build.json'), JSON.stringify({ version: 1,
                roots: [ui, extension], lock_sha256: hash(readFileSync(resolve(ui, 'package-lock.json'))), inputs, outputs }, null, 2));
        } },
    };
}
