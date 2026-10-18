import { fileURLToPath } from 'node:url';
import { resolve } from 'node:path';
import { defineConfig, mergeConfig } from '../../../../graph-ui/node_modules/vite';
import canonical from '../../../../graph-ui/vite.config';
import { scopeLicenseInventory } from './license-inventory';

const root = fileURLToPath(new URL('.', import.meta.url));
const ui = resolve(root, '../../../../graph-ui');
const config = mergeConfig(canonical, defineConfig({
    root,
    resolve: { alias: {
        react: resolve(ui, 'node_modules/react'),
        'react-dom': resolve(ui, 'node_modules/react-dom'),
    } },
    publicDir: resolve(ui, 'public'),
    build: { outDir: resolve(root, 'dist'), emptyOutDir: true },
    test: { include: ['tests/**/*.test.ts', 'tests/**/*.test.tsx'] },
}));
// The canonical collector deliberately permits one source root. Keep its worker
// collection and replace only the app collector with the explicit two-root ASMS collector.
config.plugins = (config.plugins ?? []).filter(plugin => !(plugin && typeof plugin === 'object'
    && 'name' in plugin && plugin.name === 'cbm-license-inventory-app'));
config.plugins.push(scopeLicenseInventory(ui, root));
export default config;
