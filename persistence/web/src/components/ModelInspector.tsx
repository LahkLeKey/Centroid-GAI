import { useEffect, useState } from 'react';
import type { ReactNode } from 'react';
import { ApiError, getNativeSchema, type ModelMetadata } from '../api/client';
import { MetricBar } from './MetricBar';

interface ModelInspectorProps {
    model: ModelMetadata | null;
    cohort: ModelMetadata[];
}

export function ModelInspector({ model, cohort }: ModelInspectorProps) {
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

    const maxDimensions = Math.max(1, ...cohort.map((item) => item.dimensions));
    const maxCentroids = Math.max(1, ...cohort.map((item) => item.centroidCount));
    const maxContext = Math.max(1, ...cohort.map((item) => item.contextWindow));
    const maxVocab = Math.max(1, ...cohort.map((item) => Number(item.vocabularySize)));
    const maxExamples = Math.max(1, ...cohort.map((item) => Number(item.examplesSeen)));

    return (
        <div className="h-full space-y-4 overflow-y-auto rounded-lg border border-slate-800 bg-slate-900/60 p-5">
            <div>
                <h2 className="text-sm font-semibold text-slate-100">{model.name}</h2>
                <p className="mt-0.5 font-mono text-[11px] text-slate-500">
                    format v{model.formatVersion} · lib {model.libraryVersion}
                </p>
            </div>

            <div className="grid grid-cols-2 gap-3 sm:grid-cols-3">
                <StatCard label="Dimensions" value={model.dimensions.toLocaleString()} />
                <StatCard label="Centroids" value={model.centroidCount.toLocaleString()} />
                <StatCard label="Context window" value={model.contextWindow.toLocaleString()} />
                <StatCard label="Vocabulary" value={Number(model.vocabularySize).toLocaleString()} />
                <StatCard label="Examples seen" value={Number(model.examplesSeen).toLocaleString()} />
                <StatCard label="Updated" value={new Date(model.updatedAt).toLocaleString()} small />
            </div>

            <div className="rounded-md border border-slate-800 bg-slate-950/40 p-4">
                <h3 className="mb-3 text-xs font-semibold tracking-wide text-slate-400 uppercase">
                    Cohort comparison ({cohort.length} model{cohort.length === 1 ? '' : 's'})
                </h3>
                <div className="space-y-3">
                    <MetricBar label="Dimensions" value={model.dimensions} max={maxDimensions} color="bg-indigo-500" />
                    <MetricBar label="Centroids" value={model.centroidCount} max={maxCentroids} color="bg-violet-500" />
                    <MetricBar label="Context window" value={model.contextWindow} max={maxContext} color="bg-sky-500" />
                    <MetricBar
                        label="Vocabulary size"
                        value={Number(model.vocabularySize)}
                        max={maxVocab}
                        color="bg-emerald-500"
                    />
                    <MetricBar
                        label="Examples seen"
                        value={Number(model.examplesSeen)}
                        max={maxExamples}
                        color="bg-amber-500"
                    />
                </div>
            </div>

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
        </div>
    );
}

function StatCard({ label, value, small }: { label: string; value: string; small?: boolean }) {
    return (
        <div className="rounded-md border border-slate-800 bg-slate-950/40 p-3" data-testid="stat-card" data-label={label}>
            <p className="text-[11px] text-slate-500">{label}</p>
            <p className={`mt-1 font-mono text-slate-100 ${small ? 'text-xs' : 'text-sm'}`}>{value}</p>
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
