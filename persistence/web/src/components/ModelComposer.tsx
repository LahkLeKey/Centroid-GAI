import { useEffect, useState } from 'react';
import type { FormEvent } from 'react';
import { ApiError, getArtifactContents, mergeModels, type ArtifactContents, type ModelMetadata } from '../api/client';
import { formatBytes } from '../format';

export function ModelComposer({ model, models, onCreated, onRefresh }: { model: ModelMetadata; models: ModelMetadata[]; onCreated: (name: string) => void; onRefresh: () => void }) {
    const [selected, setSelected] = useState<string[]>([model.name]);
    const [query, setQuery] = useState('');
    const [name, setName] = useState('');
    const [mode, setMode] = useState<'preserve' | 'compact'>('preserve');
    const [autoRebuild, setAutoRebuild] = useState(true);
    const [target, setTarget] = useState('');
    const [pending, setPending] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const [attempt, setAttempt] = useState(0);
    const sourceModels = selected.map((source) => models.find((item) => item.name === source));
    const sourceKey = JSON.stringify(sourceModels.map((item) => item ? [item.name, item.checksumSha256] : null));
    const [loaded, setLoaded] = useState<{ key: string; attempt: number; items?: ArtifactContents<'summary'>[]; error?: string }>();
    useEffect(() => {
        const controller = new AbortController();
        const sources = JSON.parse(sourceKey) as ([string, string] | null)[];
        if (sources.some((source) => !source)) return;
        Promise.all(sources.map((source) => getArtifactContents(source![0], source![1], 'summary', 0, 0, controller.signal)))
            .then((items) => { if (!controller.signal.aborted) setLoaded({ key: sourceKey, attempt, items }); })
            .catch((cause: unknown) => { if (!controller.signal.aborted) {
                setLoaded({ key: sourceKey, attempt, error: cause instanceof Error ? cause.message : 'Unable to inspect source models.' });
                if (cause instanceof ApiError && cause.status === 409) onRefresh();
            } });
        return () => controller.abort();
    }, [sourceKey, attempt, onRefresh]);
    const current = loaded?.key === sourceKey && loaded.attempt === attempt ? loaded : undefined;
    const sources = current?.items ?? [];
    const baseline = sources[0]?.data;
    const active = sources.reduce((sum, item) => sum + item.data.initializedCentroids, 0);
    const examples = sources.reduce((sum, item) => sum + BigInt(item.data.examplesSeen), 0n);
    const targetCount = mode === 'preserve' ? active : Number(target);
    const sourceBytes = sources.reduce((sum, item) => sum + item.artifactBytes, 0);
    const vocabularyUpper = sources.reduce((sum, item) => sum + Number(item.data.vocabularySize), 0);
    const numericUpper = Number.isInteger(targetCount) && targetCount > 0 && baseline ? targetCount * (baseline.dimensions * 4 + 8 + vocabularyUpper * 8) : 0;
    const issues: string[] = [];
    if (!selected.length) issues.push('Choose at least one source. A single source can be compacted into a new configuration.');
    if (sourceModels.some((source) => !source)) issues.push('A selected source is no longer in the catalog. Remove it and refresh.');
    if (baseline && sources.some(({ data }) => data.dimensions !== baseline.dimensions || data.contextWindow !== baseline.contextWindow || data.seed !== baseline.seed)) issues.push('Sources must share dimensions, context window, and embedding seed. Retrain incompatible models into the same representation before merging.');
    if (sources.some(({ data }) => !data.initializedCentroids)) issues.push('Every source must contain learned centroids.');
    if (new Set(sources.map((source) => source.checksumSha256)).size !== sources.length) issues.push('Two sources contain identical artifacts. Remove one to avoid duplicate contributions.');
    if (sourceBytes > 64 * 1024 * 1024) issues.push('Combined source artifacts exceed the 64 MiB limit. Choose a smaller group.');
    if (mode === 'compact' && (!/^\d+$/.test(target) || !Number.isInteger(targetCount) || targetCount < 1 || targetCount > active)) issues.push(`Choose a target centroid count from 1 to ${active || 'the active source count'}.`);
    if (targetCount > 65536) issues.push('The output exceeds 65,536 centroids. Use compact mode or fewer sources.');
    if (mode === 'compact' && baseline && (active - targetCount) * targetCount * baseline.dimensions > 100000000) issues.push('This configuration exceeds the merge compute budget. Use a smaller source group or target.');
    if (name.trim() && models.some((item) => item.name === name.trim())) issues.push('That destination already exists. Choose a new name.');
    const ready = sources.length === selected.length && sources.length > 0 && !current?.error;

    function move(index: number, delta: number) {
        setSelected((values) => {
            const next = [...values];
            [next[index], next[index + delta]] = [next[index + delta], next[index]];
            return next;
        });
        setError(null);
    }

    async function submit(event: FormEvent) {
        event.preventDefault();
        if (!ready || issues.length || !name.trim() || pending) return;
        setPending(true); setError(null);
        try {
            const result = await mergeModels(name.trim(), { autoRebuild, sources: sources.map((source) => ({ name: source.name, checksumSha256: source.checksumSha256 })), targetCentroids: mode === 'preserve' ? 0 : targetCount });
            onCreated(result.name);
        } catch (cause) { setError(cause instanceof Error ? cause.message : 'Unable to create the superset.'); }
        finally { setPending(false); }
    }

    return <section aria-label="Compose superset" className="space-y-4">
        <div><h3 className="text-base font-semibold text-slate-100">Compose a model superset</h3><p className="mt-1 text-xs leading-5 text-slate-400">Combine compatible learned models into a new artifact, or compact one model into a different centroid configuration. Source models stay unchanged.</p></div>
        <form onSubmit={submit} className="space-y-4">
            <fieldset disabled={pending} className="space-y-4 disabled:opacity-60">
                <legend className="sr-only">Superset configuration</legend>
                <div className="grid gap-4 lg:grid-cols-2">
                    <div className="rounded-lg border border-slate-800 p-4">
                        <label className="block text-xs text-slate-400">Find source models<input className="field mt-1" value={query} onChange={(event) => setQuery(event.target.value)} placeholder="Search model names" /></label>
                        <div className="mt-3 max-h-60 space-y-2 overflow-y-auto">
                            {models.filter((item) => item.name.toLowerCase().includes(query.toLowerCase())).map((item) => <label key={item.id} className="flex cursor-pointer items-start gap-3 rounded border border-slate-800 p-2 text-xs hover:bg-slate-800/50">
                                <input type="checkbox" checked={selected.includes(item.name)} disabled={!selected.includes(item.name) && selected.length >= 32} onChange={(event) => { setSelected((values) => event.target.checked ? [...values, item.name] : values.filter((value) => value !== item.name)); setError(null); }} className="mt-0.5 accent-indigo-500" />
                                <span className="min-w-0"><span className="block break-all text-slate-200">{item.name}</span><span className="mt-1 block text-slate-500">{item.dimensions} dimensions · {item.centroidCount} capacity · context {item.contextWindow}</span></span>
                            </label>)}
                            {!models.some((item) => item.name.toLowerCase().includes(query.toLowerCase())) && <p className="text-xs text-slate-400">No matching models.</p>}
                        </div>
                    </div>
                    <div className="rounded-lg border border-slate-800 p-4">
                        <h4 className="text-xs font-semibold text-slate-200">Source order · {selected.length} / 32</h4>
                        <p className="mt-1 text-xs text-slate-400">Compaction is deterministic for this order. Reordering sources can change the result.</p>
                        <ol className="mt-3 max-h-60 space-y-2 overflow-y-auto">{selected.map((source, index) => <li key={source} className="flex flex-wrap items-center gap-2 rounded bg-slate-950 p-2 text-xs">
                            <span className="min-w-0 flex-1 break-all">{index + 1}. {source}</span>
                            <button type="button" disabled={index === 0} aria-label={`Move ${source} earlier`} onClick={() => move(index, -1)} className="rounded border border-slate-700 px-2 py-1 disabled:opacity-30">↑</button>
                            <button type="button" disabled={index === selected.length - 1} aria-label={`Move ${source} later`} onClick={() => move(index, 1)} className="rounded border border-slate-700 px-2 py-1 disabled:opacity-30">↓</button>
                            <button type="button" aria-label={`Remove source ${source}`} onClick={() => setSelected((values) => values.filter((value) => value !== source))} className="px-1 text-slate-400">×</button>
                        </li>)}</ol>
                    </div>
                </div>
                {selected.length > 0 && !ready && !current?.error && !sourceModels.some((source) => !source) && <p role="status" className="text-xs text-slate-400">Checking source contents and compatibility…</p>}
                {current?.error && <p role="alert" className="text-xs text-rose-300">{current.error}<button type="button" onClick={() => setAttempt((value) => value + 1)} className="ml-2 underline">Retry source inspection</button></p>}
                <div className="grid gap-3 sm:grid-cols-2">
                    <label className="block text-xs text-slate-400">Merge strategy<select value={mode} onChange={(event) => { setMode(event.target.value as 'preserve' | 'compact'); setError(null); }} className="field mt-1"><option value="preserve">Preserve all active centroids</option><option value="compact">Compact to a target count</option></select></label>
                    {mode === 'compact' && <label className="block text-xs text-slate-400">Target centroids<input type="number" min="1" max={active || 65536} step="1" value={target} onChange={(event) => setTarget(event.target.value)} className="field mt-1" placeholder="e.g. 24" /></label>}
                </div>
                {ready && <div className="rounded-lg border border-indigo-400/30 bg-indigo-500/5 p-4">
                    <h4 className="text-xs font-semibold text-indigo-200">Configuration preview</h4>
                    <p className="mt-2 text-sm text-slate-200">{active.toLocaleString()} source centroids → {Number.isInteger(targetCount) && targetCount > 0 ? targetCount.toLocaleString() : '…'} output centroids</p>
                    <p className="mt-1 text-xs text-slate-400">{examples.toLocaleString()} total observations · {formatBytes(sourceBytes)} source artifacts</p>
                    {baseline && <p className="mt-1 text-xs text-slate-400">{baseline.dimensions} dimensions · context {baseline.contextWindow} · seed {baseline.seed}</p>}
                    {numericUpper > 0 && <p className="mt-1 text-xs text-slate-400">Numeric storage upper bound: {formatBytes(numericUpper)} before vocabulary deduplication. Actual output must fit 64 MiB of numeric arrays.</p>}
                </div>}
                <p className="rounded-md border border-amber-500/20 bg-amber-500/5 p-3 text-xs leading-5 text-amber-200">{mode === 'compact' ? 'Compaction approximates source centroids with observation-weighted means; it cannot retrain original examples. ' : 'Preserve mode keeps source statistics, but combining nearest-centroid candidates can change generation. '}Shared training history is counted again. This operation does not deduplicate examples or guarantee better generation quality.</p>
                <label className="block text-xs text-slate-400">New superset name<input required maxLength={200} value={name} onChange={(event) => setName(event.target.value)} className="field mt-1" placeholder="e.g. knowledge-superset-48" /></label>
                <label className="flex items-start gap-2 text-xs text-slate-300"><input type="checkbox" checked={autoRebuild} onChange={(event) => setAutoRebuild(event.target.checked)} className="mt-0.5 accent-indigo-500" /><span>Automatically rebuild from source models<span className="mt-1 block leading-5 text-slate-400">Checks sources whenever this super model is opened or run, including nested supersets. Changed sources rebuild it using this strategy. Missing or incompatible sources pause generation until fixed. Uncheck to save a fixed snapshot.</span></span></label>
                {issues.length > 0 && <ul className="list-disc space-y-1 pl-4 text-xs text-amber-200">{issues.map((issue) => <li key={issue}>{issue}</li>)}</ul>}
                {error && <p role="alert" className="text-xs text-rose-300">{error}</p>}
                <button type="submit" disabled={!ready || issues.length > 0 || !name.trim()} className="rounded-md bg-indigo-600 px-4 py-2 text-sm font-medium text-white hover:bg-indigo-500 disabled:opacity-40">{pending ? 'Creating superset…' : 'Create superset'}</button>
            </fieldset>
        </form>
    </section>;
}
