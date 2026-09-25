import { useEffect, useState } from 'react';
import { ApiError, getArtifactContents, type ArtifactContents, type ModelMetadata, type TokenEntry, type ArtifactPage } from '../api/client';
import type { ContentsSection } from '../../../shared/artifacts';
import { formatBytes } from '../format';

function useContents<S extends ContentsSection>(model: ModelMetadata, section: S, offset = 0, centroid = 0, onRefresh?: () => void) {
    const [attempt, setAttempt] = useState(0);
    const key = `${model.name}:${model.checksumSha256}:${section}:${offset}:${centroid}:${attempt}`;
    const [state, setState] = useState<{ key: string; data?: ArtifactContents<S>; error?: string }>();
    useEffect(() => {
        const controller = new AbortController();
        getArtifactContents(model.name, model.checksumSha256, section, offset, centroid, controller.signal)
            .then((data) => { if (!controller.signal.aborted) setState({ key, data }); })
            .catch((cause: unknown) => { if (!controller.signal.aborted) {
                setState({ key, error: cause instanceof Error ? cause.message : 'Unable to load artifact contents.' });
                if (cause instanceof ApiError && cause.status === 409) onRefresh?.();
            } });
        return () => controller.abort();
    }, [model.name, model.checksumSha256, section, offset, centroid, key, onRefresh]);
    return { data: state?.key === key ? state.data : undefined, error: state?.key === key ? state.error : undefined, retry: () => setAttempt((value) => value + 1) };
}

export function ArtifactExplorer({ model, onRefresh }: { model: ModelMetadata; onRefresh: () => void }) {
    const summary = useContents(model, 'summary', 0, 0, onRefresh);
    const [section, setSection] = useState<'centroids' | 'vocabulary'>('centroids');
    const [offset, setOffset] = useState(0);
    const [centroid, setCentroid] = useState<number | null>(null);
    const result = useContents(model, section, offset, 0, onRefresh);
    const data = summary.data?.data;
    return <section aria-label="Artifact contents" className="space-y-4">
        <div>
            <h3 className="text-base font-semibold text-slate-100">Inside the artifact</h3>
            <p className="mt-1 text-xs leading-5 text-slate-400">Explore learned statistics directly from the saved model. Original corpus text and individual training examples are not stored in the artifact.</p>
        </div>
        {summary.error && <LoadError message={summary.error} retry={summary.retry} />}
        {!data && !summary.error && <p role="status" className="text-sm text-slate-400">Reading artifact…</p>}
        {data && summary.data && <>
            <div className="grid grid-cols-2 gap-3 lg:grid-cols-4">
                <DetailStat label="Active / reserved centroids" value={`${data.initializedCentroids} / ${data.centroidCount}`} />
                <DetailStat label="Artifact size" value={formatBytes(summary.data.artifactBytes)} />
                <DetailStat label="Embedding seed" value={data.seed} />
                <DetailStat label="Context representation" value={`${data.dimensions} dimensions · ${data.contextWindow} tokens`} />
            </div>
            <div className="rounded-lg border border-slate-800 p-4">
                <h4 className="text-xs font-semibold text-slate-200">Storage breakdown</h4>
                <StorageBreakdown bytes={summary.data.artifactBytes} storage={data.storage} />
            </div>
            {summary.data.composition && <div className="rounded-lg border border-indigo-500/30 bg-indigo-500/5 p-4">
                <h4 className="text-sm font-semibold text-indigo-200">Composition recipe</h4>
                <p className="mt-1 text-xs text-slate-400">{summary.data.composition.algorithm === 'preserve' ? 'Preserved source centroids' : 'Approximate weighted compaction'} → {summary.data.composition.targetCentroids} centroids. Source snapshots are listed in merge order.</p>
                <ol className="mt-3 space-y-2 text-xs">
                    {summary.data.composition.sources.map((source, index) => <li key={`${index}:${source.checksumSha256}`} className="rounded bg-slate-950 p-2">
                        <p className="break-all text-slate-200">{index + 1}. {source.name} · {source.initializedCentroids} centroids · {BigInt(source.examplesSeen).toLocaleString()} examples</p>
                        <p className="mt-1 font-mono break-all text-slate-500">{source.checksumSha256}</p>
                    </li>)}
                </ol>
                <p className="mt-2 text-xs text-slate-400">{summary.data.composition.autoRebuild ? 'Live super model: changed sources automatically rebuild this artifact when opened or run. The sources above describe its latest build.' : 'Fixed snapshot: subsequent changes to source models do not change this artifact.'} Shared training history may be counted more than once. This recipe is stored in the database, separately from the downloadable binary.</p>
            </div>}
        </>}
        <div className="flex gap-2">
            {(['centroids', 'vocabulary'] as const).map((value) => <button key={value} type="button" aria-pressed={section === value} onClick={() => { setSection(value); setOffset(0); setCentroid(null); }} className={`rounded px-3 py-2 text-xs ${section === value ? 'bg-indigo-600 text-white' : 'bg-slate-800 text-slate-300'}`}>{value === 'centroids' ? 'Centroid occupancy' : 'Vocabulary'}</button>)}
        </div>
        {result.error && <LoadError message={result.error} retry={result.retry} />}
        {!result.data && !result.error && <p role="status" className="text-xs text-slate-400">Loading {section}…</p>}
        {result.data && <>
            {section === 'centroids' ? <div className="max-h-80 space-y-2 overflow-y-auto">
                <p className="text-xs text-slate-400">Select a centroid to inspect its vector and target-token distribution. Bar widths show its share of all observed transitions.</p>
                {(result.data as ArtifactContents<'centroids'>).data.items.map((item) => <button key={item.id} type="button" aria-pressed={centroid === item.id} onClick={() => setCentroid(item.id)} aria-label={`Inspect centroid ${item.id}`} className={`block w-full rounded-md border p-3 text-left ${centroid === item.id ? 'border-indigo-400 bg-indigo-500/10' : 'border-slate-800 hover:bg-slate-800/50'}`}>
                    <div className="mb-2 flex flex-wrap justify-between gap-2 text-xs"><span>Centroid {item.id} · {item.distinctTargets} target tokens</span><span className="font-mono text-slate-300">{BigInt(item.observations).toLocaleString()} observations</span></div>
                    <div aria-hidden="true" className="h-2 rounded-full bg-slate-800"><div className="h-full rounded-full bg-indigo-400" style={{ width: `${fraction(item.observations, data?.examplesSeen ?? '0')}%` }} /></div>
                </button>)}
                {result.data.data.total === 0 && <p className="text-sm text-slate-400">No learned centroids yet.</p>}
            </div> : <TokenTable page={(result.data as ArtifactContents<'vocabulary'>).data} />}
            <Pager offset={offset} total={result.data.data.total} onChange={setOffset} />
        </>}
        {section === 'centroids' && centroid !== null && <CentroidPanel key={centroid} model={model} centroid={centroid} onRefresh={onRefresh} />}
    </section>;
}

function CentroidPanel({ model, centroid, onRefresh }: { model: ModelMetadata; centroid: number; onRefresh: () => void }) {
    const [offset, setOffset] = useState(0);
    const result = useContents(model, 'centroid', offset, centroid, onRefresh);
    if (result.error) return <LoadError message={result.error} retry={result.retry} />;
    if (!result.data) return <p role="status" className="text-xs text-slate-400">Loading centroid {centroid}…</p>;
    const detail = result.data.data;
    const max = Math.max(...detail.vector.map(Math.abs), 0.000001);
    return <section aria-label={`Centroid ${centroid} details`} className="space-y-3 rounded-lg border border-indigo-400/30 bg-slate-950/50 p-4">
        <h4 className="text-sm font-semibold text-indigo-200">Centroid {centroid} details</h4>
        <p className="text-xs text-slate-400">{BigInt(detail.observations).toLocaleString()} observations · {detail.vector.length} vector components. Each bar is one actual dimension; this is not a projection of training examples.</p>
        <svg role="img" aria-label={`Centroid ${centroid} vector components, positive above the center line and negative below`} viewBox={`0 0 ${detail.vector.length} 100`} preserveAspectRatio="none" className="h-32 w-full rounded bg-slate-900">
            <line x1="0" x2={detail.vector.length} y1="50" y2="50" stroke="#64748b" strokeWidth="0.5" />
            {detail.vector.map((value, index) => <rect key={index} x={index} width="0.8" y={value >= 0 ? 50 - value / max * 45 : 50} height={Math.abs(value) / max * 45} fill={value >= 0 ? '#818cf8' : '#38bdf8'}><title>Dimension {index}: {value}</title></rect>)}
        </svg>
        <details className="text-xs text-slate-400"><summary className="cursor-pointer">Exact vector values</summary><pre className="mt-2 max-h-40 overflow-auto rounded bg-slate-900 p-3">{detail.vector.map((value, index) => `${index}: ${value}`).join('\n')}</pre></details>
        <h5 className="text-xs font-semibold text-slate-200">Target tokens · highest count first</h5>
        <TokenTable page={detail.tokens} total={detail.observations} />
        <Pager offset={offset} total={detail.tokens.total} onChange={setOffset} />
    </section>;
}

function TokenTable({ page, total }: { page: ArtifactPage<TokenEntry>; total?: string }) {
    return <div className="overflow-x-auto rounded-md border border-slate-800"><table className="w-full text-left text-xs">
        <thead className="bg-slate-950 text-slate-400"><tr><th className="p-3">ID</th><th className="p-3">Token</th><th className="p-3 text-right">Observed targets</th>{total && <th className="p-3 text-right">Share</th>}</tr></thead>
        <tbody>{page.items.map((item) => <tr key={item.id} className="border-t border-slate-800"><td className="p-3 font-mono text-slate-500">{item.id}</td><td className="max-w-xs p-3 font-mono break-all text-slate-200">{item.token}<span className="ml-2 text-[10px] text-slate-500">{item.id < 3 ? 'control' : ''}</span></td><td className="p-3 text-right font-mono">{BigInt(item.count).toLocaleString()}</td>{total && <td className="p-3 text-right font-mono">{fraction(item.count, total).toFixed(2)}%</td>}</tr>)}</tbody>
    </table></div>;
}

function Pager({ offset, total, onChange }: { offset: number; total: number; onChange: (offset: number) => void }) {
    return <div className="flex flex-wrap items-center justify-between gap-2 text-xs text-slate-400">
        <span>{total ? `${offset + 1}–${Math.min(offset + 25, total)} of ${total.toLocaleString()}` : '0 entries'}</span>
        <div className="flex gap-2"><button type="button" disabled={offset === 0} onClick={() => onChange(Math.max(0, offset - 25))} className="rounded border border-slate-700 px-3 py-1 disabled:opacity-40">Previous page</button><button type="button" disabled={offset + 25 >= total} onClick={() => onChange(offset + 25)} className="rounded border border-slate-700 px-3 py-1 disabled:opacity-40">Next page</button></div>
    </div>;
}

function DetailStat({ label, value }: { label: string; value: string }) {
    return <div className="rounded-md border border-slate-800 p-3"><p className="text-[11px] text-slate-400">{label}</p><p className="mt-1 font-mono text-xs break-all text-slate-200">{value}</p></div>;
}

function StorageBreakdown({ bytes, storage }: { bytes: number; storage: { vectors: string; tokenCounts: string; clusterSizes: string } }) {
    const parts = [
        { label: 'Token counts', bytes: Number(storage.tokenCounts), color: 'bg-indigo-400' },
        { label: 'Centroid vectors', bytes: Number(storage.vectors), color: 'bg-sky-400' },
        { label: 'Observation counts', bytes: Number(storage.clusterSizes), color: 'bg-emerald-400' },
        { label: 'Header & vocabulary', bytes: Math.max(0, bytes - Number(storage.vectors) - Number(storage.tokenCounts) - Number(storage.clusterSizes)), color: 'bg-amber-400' },
    ];
    return <><div aria-hidden="true" className="my-3 flex h-4 overflow-hidden rounded-full">{parts.map((part) => <span key={part.label} className={part.color} style={{ width: `${bytes ? part.bytes / bytes * 100 : 0}%` }} />)}</div><div className="flex flex-wrap gap-3 text-xs">{parts.map((part) => <span key={part.label} className="flex items-center gap-2 text-slate-400"><span aria-hidden="true" className={`h-2 w-2 rounded ${part.color}`} />{part.label}: {formatBytes(part.bytes)}</span>)}</div></>;
}

function LoadError({ message, retry }: { message: string; retry: () => void }) {
    return <div role="alert" className="rounded border border-rose-500/30 p-3 text-xs text-rose-300">{message}<button type="button" onClick={retry} className="ml-3 underline">Retry</button></div>;
}

function fraction(value: string, total: string): number { return BigInt(total) > 0n ? Number(BigInt(value) * 10000n / BigInt(total)) / 100 : 0; }
