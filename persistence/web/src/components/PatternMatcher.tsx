import { useCallback, useEffect, useRef, useState } from 'react';
import type { FormEvent } from 'react';
import { matchPatterns, type PatternMatchResult } from '../api/client';

export function PatternMatcher({ modelName, initialText = '', onInspect }: { modelName: string | null; initialText?: string; onInspect: (centroid: number) => void }) {
    const [text, setText] = useState(initialText);
    const [limit, setLimit] = useState('5');
    const [pending, setPending] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [result, setResult] = useState<{ text: string; value: PatternMatchResult } | null>(null);
    const active = useRef<AbortController | null>(null);
    const execute = useCallback(async (input: string, count: number) => {
        if (!modelName || !input.trim()) return;
        if (new TextEncoder().encode(input).byteLength > 16384) {
            setError('Input exceeds 16 KiB. Use a shorter context.');
            return;
        }
        active.current?.abort();
        const controller = new AbortController();
        active.current = controller;
        setPending(true); setError(null); setResult(null);
        try {
            const value = await matchPatterns(modelName, input, count, controller.signal);
            if (!controller.signal.aborted) setResult({ text: input, value });
        } catch (cause) {
            if (!controller.signal.aborted) setError(cause instanceof Error ? cause.message : 'Pattern matching failed.');
        } finally {
            if (!controller.signal.aborted) setPending(false);
        }
    }, [modelName]);
    useEffect(() => {
        if (initialText) void execute(initialText, 5);
        return () => active.current?.abort();
    }, [execute, initialText]);

    function submit(event: FormEvent) {
        event.preventDefault();
        if (!pending) void execute(text, Number(limit));
    }

    return <section aria-label="Pattern matching" className="h-full space-y-4 overflow-y-auto rounded-lg border border-slate-800 bg-slate-900/60 p-5">
        <div>
            <h2 className="text-lg font-semibold text-slate-100">Match learned patterns</h2>
            <p className="mt-1 break-all text-xs text-slate-400">Model: {modelName ?? 'none selected'}</p>
            <p className="mt-2 max-w-3xl text-sm leading-6 text-slate-400">Find the centroids nearest to your input's recent context and see which tokens followed those learned contexts. Matching reads the model without generating text or adding training.</p>
        </div>
        {!modelName ? <p className="text-sm text-slate-400">Select or create a model to match patterns.</p> : <form onSubmit={submit} className="space-y-3">
            <label className="block text-xs text-slate-400">Input context<textarea required rows={3} value={text} onChange={(event) => setText(event.target.value)} className="field mt-1 resize-y" placeholder="Enter a context to explore…" /></label>
            <div className="flex flex-wrap items-end gap-3">
                <label className="text-xs text-slate-400">Nearest centroids<select value={limit} onChange={(event) => setLimit(event.target.value)} className="field mt-1">{[1, 3, 5, 10].map((value) => <option key={value} value={value}>{value}</option>)}</select></label>
                <button type="submit" disabled={pending || !text.trim()} className="rounded-md bg-indigo-600 px-4 py-2 text-sm text-white disabled:opacity-40">{pending ? 'Matching…' : 'Find matches'}</button>
            </div>
        </form>}
        {error && <p role="alert" className="rounded border border-rose-500/30 p-3 text-sm text-rose-300">{error}</p>}
        {pending && <p role="status" className="text-xs text-slate-400">Checking source updates and matching learned centroids…</p>}
        {result && <div className="space-y-4" aria-label="Pattern match results">
            <div className="rounded-lg border border-slate-800 p-4">
                <h3 className="text-sm font-semibold text-slate-200">Matched context</h3>
                <p className="mt-1 break-words text-xs text-slate-400">Input: {result.text}</p>
                <p className="mt-2 text-xs text-slate-400">Using the last {result.value.data.context.length} of {result.value.data.inputTokens} input tokens, weighted toward the most recent.</p>
                <div className="mt-2 flex flex-wrap gap-2">{result.value.data.context.map((item, index) => <span key={index} className={`max-w-full break-all rounded px-2 py-1 font-mono text-xs ${item.known ? 'bg-indigo-500/10 text-indigo-200' : 'bg-amber-500/10 text-amber-200'}`}>{item.token}{!item.known && ' → <unk>'}</span>)}</div>
                {result.value.data.unknownTokens > 0 && <p className="mt-2 text-xs text-amber-200">{result.value.data.unknownTokens} context tokens are outside this model's vocabulary and use its shared unknown-token embedding.</p>}
                <p className="mt-3 text-xs leading-5 text-slate-400">Lower squared distance means closer in this model's hashed embedding space. It is not semantic similarity or confidence. These are aggregate centroids, not retrieved training passages.</p>
                <details className="mt-2 text-xs text-slate-500"><summary className="cursor-pointer">Artifact used for this match</summary><p className="mt-1 break-all font-mono">{result.value.checksumSha256}</p></details>
            </div>
            {result.value.data.matches.map((match, index) => <article key={match.centroidId} className="rounded-lg border border-slate-800 p-4">
                <div className="flex flex-wrap items-start justify-between gap-2">
                    <div><h3 className="text-sm font-semibold text-slate-100">{index + 1}. Centroid {match.centroidId}</h3><p className="mt-1 text-xs text-slate-400">Squared distance: <span className="font-mono text-indigo-200">{match.squaredDistance.toPrecision(6)}</span> · {BigInt(match.observations).toLocaleString()} observations</p></div>
                    <button type="button" onClick={() => onInspect(match.centroidId)} className="text-xs text-indigo-300">Inspect centroid {match.centroidId}</button>
                </div>
                <p className="mt-3 text-xs text-slate-400">Observed target distribution · top {match.targets.items.length} of {match.targets.total}</p>
                <div className="mt-2 overflow-x-auto"><table className="w-full text-left text-xs"><thead className="text-slate-500"><tr><th className="py-2">Token</th><th className="px-2 text-right">Count</th><th className="text-right">Share</th></tr></thead><tbody>
                    {match.targets.items.map((target) => <tr key={target.id} className="border-t border-slate-800"><td className="max-w-xs break-all py-2 font-mono text-slate-200">{target.id < 3 ? target.token : <button type="button" aria-label={`Explore target ${target.token}`} onClick={() => { setText(target.token); void execute(target.token, Number(limit)); }} className="break-all text-indigo-200 underline decoration-indigo-400/30">{target.token}</button>}</td><td className="px-2 text-right font-mono">{BigInt(target.count).toLocaleString()}</td><td className="text-right font-mono">{(Number(BigInt(target.count) * 10000n / BigInt(match.observations)) / 100).toFixed(2)}%</td></tr>)}
                </tbody></table></div>
            </article>)}
        </div>}
    </section>;
}
