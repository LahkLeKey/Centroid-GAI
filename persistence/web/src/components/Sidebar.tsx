import { useMemo, useState } from 'react';
import type { ModelMetadata } from '../api/client';
import type { ModelLabels } from '../modelLabels';

interface SidebarProps {
    models: ModelMetadata[];
    loading: boolean;
    selectedName: string | null;
    onSelect: (name: string) => void;
    onDelete: (name: string) => void;
    onRefresh: () => void;
    onNewModel: () => void;
    labels: ModelLabels;
    onEditLabels: (name: string) => void;
}

export function Sidebar({
    models,
    loading,
    selectedName,
    onSelect,
    onDelete,
    onRefresh,
    onNewModel,
    labels,
    onEditLabels,
}: SidebarProps) {
    const [query, setQuery] = useState('');
    const [labelFilter, setLabelFilter] = useState('');
    const labelCounts = new Map<string, number>();
    for (const model of models) {
        for (const label of labels[model.id] ?? []) labelCounts.set(label, (labelCounts.get(label) ?? 0) + 1);
    }
    const labeledCount = models.filter((model) => labels[model.id]?.length).length;
    const coverage = models.length ? Math.round(labeledCount / models.length * 100) : 0;

    const filtered = useMemo(() => {
        const q = query.trim().toLowerCase();
        return models.filter((model) => {
            const modelLabels = labels[model.id] ?? [];
            const matchesQuery = !q || model.name.toLowerCase().includes(q) || modelLabels.some((label) => label.includes(q));
            const matchesLabel = !labelFilter || (labelFilter === '__unlabeled' ? !modelLabels.length : modelLabels.includes(labelFilter.slice(6)));
            return matchesQuery && matchesLabel;
        });
    }, [models, query, labels, labelFilter]);

    return (
        <aside className="flex max-h-[42dvh] w-full shrink-0 flex-col border-b border-slate-800 bg-slate-950/60 md:h-full md:max-h-none md:w-72 md:border-r md:border-b-0">
            <div className="flex items-center justify-between px-4 pt-4">
                <h2 className="text-xs font-semibold tracking-wide text-slate-400 uppercase">Models</h2>
                <button
                    type="button"
                    onClick={onRefresh}
                    title="Refresh"
                    className="rounded-md p-1 text-slate-500 hover:bg-slate-800 hover:text-slate-300"
                >
                    <RefreshIcon />
                </button>
            </div>

            <div className="px-4 py-3">
                <input
                    value={query}
                    onChange={(event) => setQuery(event.target.value)}
                    placeholder="Search models or labels…"
                    aria-label="Search models"
                    data-testid="model-search"
                    className="w-full rounded-md border border-slate-800 bg-slate-900 px-3 py-1.5 text-sm text-slate-200 placeholder:text-slate-600 focus:border-indigo-500 focus:outline-none"
                />
                <label className="mt-3 block text-xs text-slate-400">
                    Filter by label
                    <select value={labelFilter} onChange={(event) => setLabelFilter(event.target.value)} className="field mt-1">
                        <option value="">All models ({models.length})</option>
                        <option value="__unlabeled">Unlabeled ({models.length - labeledCount})</option>
                        {[...labelCounts].sort(([a], [b]) => a.localeCompare(b)).map(([label, count]) => <option key={label} value={`label:${label}`}>{label} ({count})</option>)}
                        {labelFilter.startsWith('label:') && !labelCounts.has(labelFilter.slice(6)) && <option value={labelFilter}>{labelFilter.slice(6)} (0)</option>}
                    </select>
                </label>
                <div className="mt-3 hidden md:block">
                    <div className="mb-1 flex justify-between text-[11px] text-slate-400"><span>Label coverage</span><span>{labeledCount} / {models.length} models</span></div>
                    <div role="meter" aria-label="Label coverage" aria-valuemin={0} aria-valuemax={100} aria-valuenow={coverage} aria-valuetext={`${labeledCount} of ${models.length} models labeled`} className="h-1.5 overflow-hidden rounded-full bg-slate-800">
                        <div className="h-full rounded-full bg-indigo-400" style={{ width: `${coverage}%` }} />
                    </div>
                    <p className="mt-1 text-[11px] text-slate-500">Labels are local to this browser.</p>
                </div>
                {(query || labelFilter) && <button type="button" onClick={() => { setQuery(''); setLabelFilter(''); }} className="mt-2 text-xs text-indigo-300">Clear filters · {filtered.length} shown</button>}
            </div>

            <div className="min-h-12 flex-1 overflow-y-auto px-2 pb-2">
                {loading ? (
                    <p className="px-2 py-6 text-center text-xs text-slate-500">Loading…</p>
                ) : filtered.length === 0 ? (
                    <p className="px-2 py-6 text-center text-xs text-slate-500">
                        {models.length === 0 ? 'No models persisted yet.' : 'No matches.'}
                    </p>
                ) : (
                    <ul className="space-y-1">
                        {filtered.map((model) => {
                            const active = selectedName === model.name;
                            const vocab = BigInt(model.vocabularySize);
                            return (
                                <li key={model.id} data-testid="model-row" data-model-name={model.name}>
                                    <div
                                        className={`group relative overflow-hidden rounded-md border px-3 py-2 transition ${active
                                            ? 'border-indigo-500/50 bg-indigo-500/10'
                                            : 'border-transparent hover:border-slate-800 hover:bg-slate-900'
                                            }`}
                                    >
                                        <button type="button" aria-pressed={active} onClick={() => onSelect(model.name)} className="block w-full text-left">
                                            <p className="truncate pr-5 text-sm font-medium text-slate-100">{model.name}</p>
                                            <p className="mt-0.5 truncate font-mono text-[11px] text-slate-500">
                                                d{model.dimensions} · c{model.centroidCount} · vocab {vocab.toLocaleString()}
                                            </p>
                                            <div className="mt-2 flex flex-wrap gap-1">
                                                {(labels[model.id] ?? []).map((label) => <span key={label} className="max-w-full truncate rounded bg-indigo-500/10 px-1.5 py-0.5 text-[10px] text-indigo-200">{label}</span>)}
                                            </div>
                                        </button>
                                        {active && <button type="button" onClick={() => onEditLabels(model.name)} className="mt-1 text-[11px] text-indigo-300 hover:text-indigo-200">Edit labels</button>}
                                        <button
                                            type="button"
                                            onClick={() => onDelete(model.name)}
                                            title="Delete model"
                                            className="absolute top-2 right-2 rounded p-1 text-slate-500 hover:bg-rose-500/10 hover:text-rose-400 focus-visible:opacity-100 md:opacity-0 md:group-hover:opacity-100"
                                        >
                                            <TrashIcon />
                                        </button>
                                    </div>
                                </li>
                            );
                        })}
                    </ul>
                )}
            </div>

            <div className="border-t border-slate-800 p-3">
                <button
                    type="button"
                    onClick={onNewModel}
                    className="flex w-full items-center justify-center gap-1.5 rounded-md bg-indigo-600 px-3 py-2 text-sm font-medium text-white hover:bg-indigo-500"
                >
                    <PlusIcon />
                    New model
                </button>
            </div>
        </aside>
    );
}

function RefreshIcon() {
    return (
        <svg viewBox="0 0 20 20" fill="currentColor" className="h-4 w-4">
            <path d="M15.312 5.312a5.5 5.5 0 1 0 1.359 5.55.75.75 0 1 1 1.442.417 7 7 0 1 1-1.717-7.06l1.194-1.193a.5.5 0 0 1 .854.353V6.5a.5.5 0 0 1-.5.5h-3.121a.5.5 0 0 1-.354-.854z" />
        </svg>
    );
}

function TrashIcon() {
    return (
        <svg viewBox="0 0 20 20" fill="currentColor" className="h-3.5 w-3.5">
            <path
                fillRule="evenodd"
                d="M8.75 1a.75.75 0 0 0-.75.75V3H4.5a.75.75 0 0 0 0 1.5h.32l.82 11.106A2.25 2.25 0 0 0 7.884 17.5h4.232a2.25 2.25 0 0 0 2.244-1.894L15.18 4.5h.32a.75.75 0 0 0 0-1.5H12v-1.25a.75.75 0 0 0-.75-.75zM8.5 7a.75.75 0 0 1 1.5 0v7a.75.75 0 0 1-1.5 0zm3.5 0a.75.75 0 0 1 1.5 0v7a.75.75 0 0 1-1.5 0z"
                clipRule="evenodd"
            />
        </svg>
    );
}

function PlusIcon() {
    return (
        <svg viewBox="0 0 20 20" fill="currentColor" className="h-4 w-4">
            <path d="M10 3a.75.75 0 0 1 .75.75v5.5h5.5a.75.75 0 0 1 0 1.5h-5.5v5.5a.75.75 0 0 1-1.5 0v-5.5h-5.5a.75.75 0 0 1 0-1.5h5.5v-5.5A.75.75 0 0 1 10 3z" />
        </svg>
    );
}
