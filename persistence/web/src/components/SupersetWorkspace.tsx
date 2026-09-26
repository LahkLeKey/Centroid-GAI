import { useState } from 'react';
import type { ModelMetadata } from '../api/client';
import { ModelComposer } from './ModelComposer';
import { PatternDiscovery } from './PatternDiscovery';

interface Props {
    models: ModelMetadata[];
    model: ModelMetadata | null;
    loading: boolean;
    onSelect: (name: string) => void;
    onCreated: (name: string) => void;
    onRefresh: () => void;
    onNewModel: () => void;
    onMatch: () => void;
    onExplore: (text: string) => void;
    onInspect: () => void;
    onCompare: () => void;
    onGenerate: () => void;
}

export function SupersetWorkspace({ models, model, loading, onSelect, onCreated, onRefresh, onNewModel, onMatch, onExplore, onInspect, onCompare, onGenerate }: Props) {
    const [composing, setComposing] = useState(false);
    const supersets = models.filter((item) => item.composition);
    return <section aria-label="Superset workspace" className="h-full space-y-5 overflow-y-auto rounded-lg border border-slate-800 bg-slate-900/60 p-5">
        <div className="flex flex-wrap items-start justify-between gap-3">
            <div>
                <h2 className="text-lg font-semibold text-slate-100">Superset workspace</h2>
                <p className="mt-1 max-w-2xl text-sm leading-6 text-slate-400">Combine models into a shared body of learned patterns. Match inputs, explore observations, compare models, or generate text.</p>
            </div>
            {models.length > 0 && <button type="button" onClick={() => setComposing((value) => !value)} className="rounded-md bg-indigo-600 px-4 py-2 text-sm text-white">{composing ? 'Close composer' : 'Compose a superset'}</button>}
        </div>
        {loading && !models.length ? <p role="status" className="text-sm text-slate-400">Loading model workspace…</p> : !models.length ? <div className="rounded-lg border border-dashed border-slate-700 p-6">
            <h3 className="font-medium text-slate-200">Start with a source model</h3>
            <p className="mt-2 text-sm text-slate-400">Train a model from a corpus or upload an artifact. Then choose compatible sources to build your first superset.</p>
            <button type="button" onClick={onNewModel} className="mt-4 rounded-md bg-indigo-600 px-4 py-2 text-sm text-white">Add source model</button>
        </div> : <>
            {!composing && supersets.length > 0 && <details>
                <summary className="cursor-pointer text-xs text-slate-400">Saved supersets · {supersets.length}</summary>
                <div className="grid max-h-60 gap-2 overflow-y-auto sm:grid-cols-2 xl:grid-cols-3">
                    {supersets.map((item) => <button type="button" key={item.id} aria-pressed={model?.id === item.id} onClick={() => { onSelect(item.name); setComposing(false); }} className={`min-w-0 rounded-lg border p-3 text-left ${model?.id === item.id ? 'border-indigo-400 bg-indigo-500/10' : 'border-slate-800 hover:bg-slate-800'}`}>
                        <span className="block break-all text-sm font-medium text-slate-100">{item.name}</span>
                        <span className="mt-1 block text-xs text-slate-400">{item.composition?.autoRebuild ? 'Live' : 'Snapshot'} · {item.composition?.sourceCount} sources · {item.centroidCount.toLocaleString()} centroids</span>
                    </button>)}
                </div>
                <p className="mt-2 text-xs text-slate-500">Live supersets check their sources when you match, inspect, or generate.</p>
            </details>}
            {composing && model && <div className="rounded-lg border border-slate-800 p-4">
                <ModelComposer key={model.id} model={model} models={models} onRefresh={onRefresh} onCreated={(name) => { setComposing(false); onCreated(name); }} />
            </div>}
            {!composing && model && <div className="space-y-6">
                <div className="border-b border-slate-800 pb-4">
                <p className="break-all text-sm text-slate-200">Model: <span className="font-mono">{model.name}</span></p>
                <p className="mt-1 text-xs text-slate-400">{model.composition ? 'Superset' : 'Source model'} · {BigInt(model.examplesSeen).toLocaleString()} stored observations · {BigInt(model.vocabularySize).toLocaleString()} vocabulary entries</p>
                <div className="mt-4 flex flex-wrap gap-2">
                    <Operation title="Match learned patterns" description="Find the nearest learned contexts for an input and inspect their observed targets." onClick={onMatch} />
                    <Operation title="Inspect learned contents" description="Explore vocabulary, centroid vectors, observation counts, and source recipes." onClick={onInspect} />
                    <Operation title="Compare models" description="Compare capacity, vocabulary, and observations across the catalog." onClick={onCompare} />
                    <Operation title="Generate text" description="Sample a continuation from the selected model's learned transitions." onClick={onGenerate} />
                </div>
                </div>
                <PatternDiscovery key={`${model.id}:${model.checksumSha256}`} model={model} onRefresh={onRefresh} onExplore={onExplore} />
            </div>}
        </>}
    </section>;
}

function Operation({ title, description, onClick }: { title: string; description: string; onClick: () => void }) {
    return <button type="button" title={description} onClick={onClick} className="rounded-md border border-slate-700 px-3 py-2 text-xs text-indigo-200 hover:border-indigo-400/50 hover:bg-indigo-500/5">
        {title}
    </button>;
}
