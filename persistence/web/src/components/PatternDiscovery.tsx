import { useEffect, useState } from 'react';
import { ApiError, getArtifactContents, type ArtifactContents, type ModelMetadata } from '../api/client';

export function PatternDiscovery({ model, onExplore, onRefresh }: { model: ModelMetadata; onExplore: (text: string) => void; onRefresh: () => void }) {
    const [data, setData] = useState<ArtifactContents<'highlights'> | null>(null);
    const [error, setError] = useState<string | null>(null);
    const [attempt, setAttempt] = useState(0);
    useEffect(() => {
        const controller = new AbortController();
        getArtifactContents(model.name, model.checksumSha256, 'highlights', 0, 0, controller.signal)
            .then((result) => { if (!controller.signal.aborted) { setData(result); setError(null); } })
            .catch((cause: unknown) => {
                if (controller.signal.aborted) return;
                setError(cause instanceof Error ? cause.message : 'Unable to discover patterns.');
                if (cause instanceof ApiError && cause.status === 409) onRefresh();
            });
        return () => controller.abort();
    }, [model.name, model.checksumSha256, onRefresh, attempt]);
    return <section aria-label="Pattern discovery" className="space-y-4">
        <div><h3 className="text-base font-semibold text-slate-100">Explore what this model learned</h3><p className="mt-1 text-sm text-slate-400">Start with an observed token to find related learned contexts. No prompt needed.</p></div>
        {error ? <p role="alert" className="text-sm text-rose-300">{error} <button type="button" onClick={() => setAttempt((value) => value + 1)} className="underline">Retry discovery</button></p>
            : !data ? <p role="status" className="text-sm text-slate-400">Finding frequent observations…</p>
            : <>
                <div className="flex flex-wrap gap-2">{data.data.tokens.items.filter((item) => BigInt(item.count) > 0n).map((item) => <button type="button" key={item.id} onClick={() => onExplore(item.token)} aria-label={`Explore ${item.token}`} className="max-w-full rounded-full border border-slate-700 bg-slate-950/50 px-4 py-2 text-sm text-indigo-200 hover:border-indigo-400 hover:bg-indigo-500/10"><span className="break-all">{item.token}</span><span className="ml-2 text-xs text-slate-500">{BigInt(item.count).toLocaleString()}</span></button>)}</div>
                {!data.data.tokens.items.some((item) => BigInt(item.count) > 0n) && <p className="text-sm text-slate-400">No ordinary target tokens have been observed yet.</p>}
                <p className="text-xs text-slate-500">Most frequent observed targets; control tokens excluded. Counts describe the training, not topic importance or semantic similarity.</p>
            </>}
    </section>;
}
