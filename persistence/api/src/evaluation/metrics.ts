/** Small, explicit reference models for the ASCII diagnostic suite, not a replacement tokenizer. */
export interface Probe {
    id: string;
    domain: string;
    slice: 'recall' | 'challenge' | 'unknown';
    prompt: string;
    expected?: string;
}

export function asciiTokens(text: string): string[] {
    if (/[^\x00-\x7f]|\0/.test(text)) throw new Error('Reference baseline requires ASCII without NUL');
    return text.toLowerCase().match(/[a-z0-9']+|[^\s]/g) ?? [];
}

/** Train only on supplied corpora. Separate documents get separate BOS/EOS boundaries. */
export function referenceModel(corpora: string[], contextWindow: number) {
    const counts = new Map<string, Map<string, number>>();
    for (const corpus of corpora) {
        const history = Array<string>(contextWindow).fill('<bos>');
        for (const target of [...asciiTokens(corpus), '<eos>']) {
            for (let size = 0; size <= contextWindow; size++) {
                const key = JSON.stringify(size === 0 ? [] : history.slice(-size));
                const row = counts.get(key) ?? new Map<string, number>();
                row.set(target, (row.get(target) ?? 0) + 1);
                counts.set(key, row);
            }
            history.push(target);
        }
    }
    return (prompt: string): string[] => {
        const context = [...Array<string>(contextWindow).fill('<bos>'), ...asciiTokens(prompt)];
        for (let size = contextWindow; size >= 0; size--) {
            const row = counts.get(JSON.stringify(size === 0 ? [] : context.slice(-size)));
            if (row) return [...row].sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0)).map(([token]) => token);
        }
        return [];
    };
}

export interface Prediction {
    probe: Probe;
    ranked: string[];
    unknownTokens?: number;
    contextTokens?: number;
    expectedInVocabulary?: boolean;
}

/** Unknown probes are diagnostic only; they never enter prediction accuracy denominators. */
export function summarize(rows: Prediction[]) {
    const scored = rows.filter((row) => row.probe.expected !== undefined);
    const rank = (row: Prediction) => row.ranked.slice(0, 5).indexOf(row.probe.expected!);
    const contextTokens = rows.reduce((sum, row) => sum + (row.contextTokens ?? 0), 0);
    const covered = scored.filter((row) => row.expectedInVocabulary !== undefined);
    return {
        probes: rows.length,
        scored: scored.length,
        top1: scored.length ? scored.filter((row) => rank(row) === 0).length / scored.length : null,
        top5: scored.length ? scored.filter((row) => rank(row) >= 0).length / scored.length : null,
        mrr5: scored.length ? scored.reduce((sum, row) => sum + (rank(row) < 0 ? 0 : 1 / (rank(row) + 1)), 0) / scored.length : null,
        targetCoverage: covered.length ? covered.filter((row) => row.expectedInVocabulary).length / covered.length : null,
        contextUnknownRate: contextTokens ? rows.reduce((sum, row) => sum + (row.unknownTokens ?? 0), 0) / contextTokens : null,
    };
}

export function groupedScores(rows: Prediction[]) {
    return Object.fromEntries([
        ['all', summarize(rows)],
        ...[...new Set(rows.map((row) => row.probe.slice))].map((slice) => [`slice:${slice}`, summarize(rows.filter((row) => row.probe.slice === slice))]),
        ...[...new Set(rows.map((row) => row.probe.domain))].map((domain) => [`domain:${domain}`, summarize(rows.filter((row) => row.probe.domain === domain))]),
    ]);
}
