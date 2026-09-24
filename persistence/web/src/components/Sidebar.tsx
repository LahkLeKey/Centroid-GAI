import { useMemo, useState } from 'react';
import type { ModelMetadata } from '../api/client';

interface SidebarProps {
    models: ModelMetadata[];
    loading: boolean;
    selectedName: string | null;
    onSelect: (name: string) => void;
    onDelete: (name: string) => void;
    onRefresh: () => void;
    onNewModel: () => void;
}

export function Sidebar({
    models,
    loading,
    selectedName,
    onSelect,
    onDelete,
    onRefresh,
    onNewModel,
}: SidebarProps) {
    const [query, setQuery] = useState('');

    const filtered = useMemo(() => {
        const q = query.trim().toLowerCase();
        if (!q) return models;
        return models.filter((model) => model.name.toLowerCase().includes(q));
    }, [models, query]);

    const maxVocab = Math.max(1, ...models.map((model) => Number(model.vocabularySize)));

    return (
        <aside className="flex h-full w-72 shrink-0 flex-col border-r border-slate-800 bg-slate-950/60">
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
                    placeholder="Search models…"
                    aria-label="Search models"
                    data-testid="model-search"
                    className="w-full rounded-md border border-slate-800 bg-slate-900 px-3 py-1.5 text-sm text-slate-200 placeholder:text-slate-600 focus:border-indigo-500 focus:outline-none"
                />
            </div>

            <div className="flex-1 overflow-y-auto px-2 pb-2">
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
                            const vocab = Number(model.vocabularySize);
                            const pct = Math.min(100, Math.round((vocab / maxVocab) * 100));
                            return (
                                <li key={model.id} data-testid="model-row" data-model-name={model.name}>
                                    <div
                                        className={`group relative overflow-hidden rounded-md border px-3 py-2 transition ${active
                                            ? 'border-indigo-500/50 bg-indigo-500/10'
                                            : 'border-transparent hover:border-slate-800 hover:bg-slate-900'
                                            }`}
                                    >
                                        <button type="button" onClick={() => onSelect(model.name)} className="block w-full text-left">
                                            <p className="truncate text-sm font-medium text-slate-100">{model.name}</p>
                                            <p className="mt-0.5 truncate font-mono text-[11px] text-slate-500">
                                                d{model.dimensions} · c{model.centroidCount} · vocab {vocab.toLocaleString()}
                                            </p>
                                            <div className="mt-1.5 h-1 w-full overflow-hidden rounded-full bg-slate-800">
                                                <div
                                                    className={`h-full rounded-full ${active ? 'bg-indigo-400' : 'bg-slate-600'}`}
                                                    style={{ width: `${pct}%` }}
                                                />
                                            </div>
                                        </button>
                                        <button
                                            type="button"
                                            onClick={() => onDelete(model.name)}
                                            title="Delete model"
                                            className="absolute top-2 right-2 rounded p-1 text-slate-600 opacity-0 hover:bg-rose-500/10 hover:text-rose-400 group-hover:opacity-100"
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
