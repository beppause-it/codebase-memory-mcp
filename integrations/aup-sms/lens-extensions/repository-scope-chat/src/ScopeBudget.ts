/** UTF-16 units, not a tokenizer estimate. Token counting still occurs before inference. */
export const CONTRACT_VERSION = 'repository-scope-chat/1';
export const SCOPE_BUDGET = Object.freeze({ nodes: 8, edges: 8, architectureCharacters: 1200, evidenceCharacters: 6000 });
export function boundedItems<T>(items: readonly T[], limit: number): { shown: T[]; omitted: number } {
    return { shown: items.slice(0, limit), omitted: Math.max(0, items.length - limit) };
}
/** Preserve original diagnostics including non-enumerable Error properties and causes. */
export function diagnostic(value: unknown): string {
    const seen = new WeakSet<object>();
    return JSON.stringify(value, (_key, item: unknown) => {
        if (!item || typeof item !== 'object') return typeof item === 'bigint' ? String(item) : item;
        if (seen.has(item)) return '[Circular]';
        seen.add(item);
        if (item instanceof Error) return Object.fromEntries(Object.getOwnPropertyNames(item).map(key => [key, (item as unknown as Record<string, unknown>)[key]]));
        return item;
    }, 2) ?? String(value);
}
