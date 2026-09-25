import { useState } from 'react';
import type { ModelMetadata } from '../api/client';
import type { ModelLabels } from '../modelLabels';

const metrics = {
    vocabularySize: { label: 'Vocabulary size', unit: 'tokens', description: 'Distinct tokens in each model’s vocabulary.' },
    examplesSeen: { label: 'Examples seen', unit: 'examples', description: 'Training examples processed by each model.' },
    dimensions: { label: 'Dimensions', unit: 'dimensions', description: 'Width of each model’s context representation.' },
    centroidCount: { label: 'Centroids', unit: 'centroids', description: 'Number of centroids configured for each model.' },
    contextWindow: { label: 'Context window', unit: 'tokens', description: 'Number of preceding tokens used as context.' },
} as const;
type Metric = keyof typeof metrics;

export function CohortComparison({ model, cohort, onSelect, labels }: {
    model: ModelMetadata;
    cohort: ModelMetadata[];
    onSelect: (name: string) => void;
    labels: ModelLabels;
}) {
    const [metric, setMetric] = useState<Metric>('vocabularySize');
    const [group, setGroup] = useState('');
    const definition = metrics[metric];
    const counts = new Map<string, number>();
    let unlabeled = 0;
    for (const item of cohort) {
        const assigned = labels[item.id] ?? [];
        if (!assigned.length) unlabeled++;
        for (const label of assigned) counts.set(label, (counts.get(label) ?? 0) + 1);
    }
    const groups = [
        { key: 'unlabeled', label: 'Unlabeled', count: unlabeled },
        ...[...counts].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0])).map(([label, count]) => ({ key: `label:${label}`, label, count })),
    ];
    const ranked = cohort.filter((item) => !group || (group === 'unlabeled' ? !labels[item.id]?.length : labels[item.id]?.includes(group.slice(6)))).sort((a, b) => {
        const left = BigInt(a[metric]);
        const right = BigInt(b[metric]);
        return left === right ? a.name.localeCompare(b.name) : left > right ? -1 : 1;
    });
    // Keep the scale stable while filtering so a bar retains its meaning across groups.
    const maximum = cohort.reduce((max, item) => BigInt(item[metric]) > max ? BigInt(item[metric]) : max, 0n);

    return (
        <section aria-labelledby="cohort-title" className="rounded-lg border border-slate-800 bg-slate-950/40 p-4">
            <div className="flex flex-wrap items-start justify-between gap-3">
                <div>
                    <h3 id="cohort-title" className="text-sm font-semibold text-slate-100">Compare models</h3>
                    <p className="mt-1 text-xs text-slate-400">{ranked.length} of {cohort.length} models · largest first</p>
                </div>
                <div className="flex max-w-full flex-wrap gap-3">
                <label className="block min-w-0 text-xs text-slate-400">
                    Comparison group
                    <select value={group} onChange={(event) => setGroup(event.target.value)} className="field mt-1 max-w-64">
                        <option value="">All models ({cohort.length})</option>
                        {groups.map((item) => <option key={item.key} value={item.key}>{item.label} ({item.count})</option>)}
                        {group && !groups.some((item) => item.key === group) && <option value={group}>{group.slice(6)} (0)</option>}
                    </select>
                </label>
                <label className="block text-xs text-slate-400">
                    Compare by
                    <select value={metric} onChange={(event) => setMetric(event.target.value as Metric)} className="field mt-1">
                        {Object.entries(metrics).map(([key, item]) => <option key={key} value={key}>{item.label}</option>)}
                    </select>
                </label>
                </div>
            </div>
            <details className="mt-4 rounded-md border border-slate-800 p-3">
                <summary className="cursor-pointer text-xs font-medium text-slate-300">Label distribution</summary>
                <p className="mt-2 text-xs leading-5 text-slate-400">Share of all {cohort.length} models. Models can have multiple labels, so shares may overlap. Select a group to compare it below.</p>
                <div className="mt-3 max-h-52 space-y-2 overflow-y-auto">
                    {groups.map((item) => {
                        const percent = cohort.length ? Math.round(item.count / cohort.length * 100) : 0;
                        return <button key={item.key} type="button" aria-pressed={group === item.key} onClick={() => setGroup(item.key)}
                            aria-label={`Compare ${item.label}: ${item.count} of ${cohort.length} models`}
                            className={`block w-full rounded p-2 text-left hover:bg-slate-800/60 ${group === item.key ? 'bg-indigo-500/10' : ''}`}>
                            <span className="mb-1 flex justify-between gap-3 text-xs text-slate-300"><span className="min-w-0 break-all">{item.label}</span><span className="shrink-0 font-mono">{item.count} · {percent}%</span></span>
                            <span aria-hidden="true" className="block h-1.5 rounded-full bg-slate-800"><span className="block h-full rounded-full bg-indigo-400" style={{ width: `${percent}%` }} /></span>
                        </button>;
                    })}
                </div>
            </details>
            <p id="cohort-description" className="mt-4 text-xs leading-5 text-slate-400">
                {definition.description} Bars share a linear scale from 0 to {maximum.toLocaleString()} {definition.unit} across all models, including those outside this group.
                {' '}Select a bar to inspect that model. Larger values do not necessarily mean better quality.
            </p>
            {group && <div className="mt-2 flex flex-wrap items-center gap-2 text-xs text-slate-400">
                {!ranked.some((item) => item.id === model.id) && <span>The selected model is outside this group.</span>}
                <button type="button" onClick={() => setGroup('')} className="text-indigo-300">Show all models</button>
            </div>}
            {ranked.length === 0 && <p className="mt-4 text-sm text-slate-400">No models in this group. Choose another group or update model labels.</p>}
            <ol aria-describedby="cohort-description" aria-label={`${definition.label} comparison`} className="mt-4 max-h-80 space-y-2 overflow-y-auto">
                {ranked.map((item) => {
                    const value = BigInt(item[metric]);
                    const percent = maximum > 0n ? Number(value * 10000n / maximum) / 100 : 0;
                    const selected = item.name === model.name;
                    return (
                        <li key={item.id}>
                            <button type="button" aria-pressed={selected} onClick={() => onSelect(item.name)}
                                aria-label={`${item.name}: ${value.toLocaleString()} ${definition.unit}`}
                                className={`w-full rounded-md border p-3 text-left transition ${selected ? 'border-indigo-400/50 bg-indigo-500/10' : 'border-transparent hover:bg-slate-800/60'}`}>
                                <span className="mb-2 flex items-baseline justify-between gap-3 text-xs">
                                    <span className="min-w-0 break-all text-slate-200">{item.name}{selected && <span className="ml-2 text-indigo-300">Selected</span>}</span>
                                    <span className="shrink-0 font-mono text-slate-200">{value.toLocaleString()}</span>
                                </span>
                                <span aria-hidden="true" className="block h-2 overflow-hidden rounded-full bg-slate-800">
                                    <span className={`block h-full rounded-full ${selected ? 'bg-indigo-400' : 'bg-slate-500'}`}
                                        style={{ width: `${percent}%` }} />
                                </span>
                            </button>
                        </li>
                    );
                })}
            </ol>
            {cohort.length === 1 && <p className="mt-3 text-xs text-slate-400">Create another model to compare training runs.</p>}
        </section>
    );
}
