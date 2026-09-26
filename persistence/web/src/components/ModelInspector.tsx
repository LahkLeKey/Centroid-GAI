import { useEffect, useState } from 'react';
import type { ReactNode } from 'react';
import { ApiError, getNativeSchema, downloadModelArtifactUrl, type ModelMetadata } from '../api/client';
import { CohortComparison } from './CohortComparison';
import { ModelLabelEditor } from './ModelLabelEditor';
import type { LabelDraft, ModelLabels } from '../modelLabels';
import { ArtifactExplorer } from './ArtifactExplorer';
import { ModelComposer } from './ModelComposer';

export type InspectorView = 'overview' | 'contents' | 'compose';

interface ModelInspectorProps {
    initialCentroid?: number;
    model: ModelMetadata | null;
    cohort: ModelMetadata[];
    onSelect: (name: string) => void;
    labels: string[];
    labelSuggestions: string[];
    onSaveLabels: (values: string[]) => string | null;
    onNextUnlabeled?: () => void;
    allLabels: ModelLabels;
    labelDraft?: LabelDraft;
    onLabelDraftChange: (draft: LabelDraft | undefined) => void;
    onCreated: (name: string) => void;
    onRefresh: () => void;
    onRun: () => void;
    view: InspectorView;
    onViewChange: (view: InspectorView) => void;
}

export function ModelInspector({ model, cohort, onSelect, labels, labelSuggestions, onSaveLabels, onNextUnlabeled, allLabels, labelDraft, onLabelDraftChange, onCreated, onRefresh, onRun, initialCentroid, view, onViewChange: setView }: ModelInspectorProps) {
    const [schema, setSchema] = useState<unknown>(null);
    const [schemaOpen, setSchemaOpen] = useState(false);
    const [schemaError, setSchemaError] = useState<string | null>(null);

    useEffect(() => {
        if (!schemaOpen || schema !== null) return;
        getNativeSchema()
            .then(setSchema)
            .catch((cause) => setSchemaError(cause instanceof ApiError ? cause.message : 'Unable to load schema.'));
    }, [schemaOpen, schema]);

    if (!model) {
        return (
            <div className="flex h-full items-center justify-center rounded-lg border border-slate-800 bg-slate-900/60 text-sm text-slate-600">
                Select a model to inspect its metadata.
            </div>
        );
    }

    return (
        <div className="h-full space-y-4 overflow-y-auto rounded-lg border border-slate-800 bg-slate-900/60 p-5">
            <div>
                <h2 className="text-sm font-semibold break-all text-slate-100">{model.name}</h2>
                <p className="mt-0.5 font-mono text-[11px] text-slate-500">
                    format v{model.formatVersion} · lib {model.libraryVersion}
                </p>
            </div>

            <div className="flex flex-wrap items-center gap-2 border-b border-slate-800 pb-3">
                {([{ id: 'overview', label: 'Overview' }, { id: 'contents', label: 'Artifact contents' }, { id: 'compose', label: 'Compose superset' }] as const).map((item) => <button key={item.id} type="button" aria-pressed={view === item.id} onClick={() => setView(item.id)} className={`rounded px-3 py-2 text-xs font-medium ${view === item.id ? 'bg-slate-700 text-white' : 'text-slate-400 hover:bg-slate-800'}`}>{item.label}</button>)}
                <button type="button" onClick={onRun} className="ml-auto rounded bg-emerald-600 px-3 py-2 text-xs text-white">Run model</button>
                <a href={downloadModelArtifactUrl(model.name)} download={`${model.name}.cgai`} className="text-xs text-indigo-300">Download .cgai</a>
            </div>

            {view === 'contents' && <ArtifactExplorer key={`${model.id}:${model.checksumSha256}`} model={model} onRefresh={onRefresh} initialCentroid={initialCentroid} />}
            {view === 'compose' && <ModelComposer models={cohort} model={model} onRefresh={onRefresh} onCreated={(name) => { onCreated(name); setView('contents'); }} />}
            {view === 'overview' && <>

            <div className="grid gap-4 xl:grid-cols-2">
                <ModelLabelEditor key={model.id} labels={labels} suggestions={labelSuggestions} onSave={onSaveLabels} onNextUnlabeled={onNextUnlabeled}
                    draft={labelDraft} onDraftChange={onLabelDraftChange} />

                <div className="grid grid-cols-2 gap-3 sm:grid-cols-3 xl:grid-cols-2">
                    <StatCard label="Dimensions" value={model.dimensions.toLocaleString()} />
                    <StatCard label="Centroids" value={model.centroidCount.toLocaleString()} />
                    <StatCard label="Context window" value={model.contextWindow.toLocaleString()} />
                    <StatCard label="Vocabulary" value={BigInt(model.vocabularySize).toLocaleString()} />
                    <StatCard label="Examples seen" value={BigInt(model.examplesSeen).toLocaleString()} />
                    <StatCard label="Updated" value={new Date(model.updatedAt).toLocaleString()} small />
                </div>
            </div>

            <CohortComparison model={model} cohort={cohort} onSelect={onSelect} labels={allLabels} />

            <div className="rounded-md border border-slate-800 bg-slate-950/40 p-4">
                <h3 className="mb-2 text-xs font-semibold tracking-wide text-slate-400 uppercase">Artifact</h3>
                <dl className="space-y-1.5 text-xs">
                    <Row term="Checksum (SHA-256)">
                        <span className="font-mono break-all text-slate-300">{model.checksumSha256}</span>
                    </Row>
                    <Row term="Created">
                        <span className="text-slate-300">{new Date(model.createdAt).toLocaleString()}</span>
                    </Row>
                </dl>
            </div>

            <div className="rounded-md border border-slate-800 bg-slate-950/40 p-4">
                <button
                    type="button"
                    onClick={() => setSchemaOpen((open) => !open)}
                    aria-expanded={schemaOpen}
                    className="flex w-full items-center justify-between text-xs font-semibold tracking-wide text-slate-400 uppercase"
                >
                    Native persistence schema
                    <span className="text-slate-600">{schemaOpen ? '−' : '+'}</span>
                </button>
                {schemaOpen && (
                    <div className="mt-3">
                        {schemaError && <p className="text-xs text-rose-400">{schemaError}</p>}
                        {schema !== null && (
                            <pre className="max-h-64 overflow-auto rounded-md bg-black/40 p-3 font-mono text-[11px] text-slate-300">
                                {JSON.stringify(schema, null, 2)}
                            </pre>
                        )}
                        {schema === null && !schemaError && <p className="text-xs text-slate-500">Loading…</p>}
                    </div>
                )}
            </div>
            </>}
        </div>
    );
}

function StatCard({ label, value, small }: { label: string; value: string; small?: boolean }) {
    return (
        <div className="rounded-md border border-slate-800 bg-slate-950/40 p-3" data-testid="stat-card" data-label={label}>
            <p className="text-[11px] text-slate-500">{label}</p>
            <p className={`mt-1 font-mono break-all text-slate-100 ${small ? 'text-xs' : 'text-sm'}`}>{value}</p>
        </div>
    );
}

function Row({ term, children }: { term: string; children: ReactNode }) {
    return (
        <div className="flex gap-2">
            <dt className="w-32 shrink-0 text-slate-500">{term}</dt>
            <dd className="min-w-0 flex-1">{children}</dd>
        </div>
    );
}
