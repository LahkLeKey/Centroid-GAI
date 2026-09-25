import { useState } from 'react';
import type { FormEvent } from 'react';
import { MAX_LABELS, MAX_LABEL_LENGTH, hasLabelDraft, normalizeLabels, sameLabels, type LabelDraft } from '../modelLabels';

export function ModelLabelEditor({ labels, suggestions, onSave, onNextUnlabeled, draft, onDraftChange }: {
    labels: string[];
    suggestions: string[];
    onSave: (values: string[]) => string | null;
    onNextUnlabeled?: () => void;
    draft?: LabelDraft;
    onDraftChange: (draft: LabelDraft | undefined) => void;
}) {
    const [error, setError] = useState<string | null>(null);
    const [saved, setSaved] = useState(false);
    const current = draft?.labels ?? labels;
    const input = draft?.input ?? '';
    const pending = normalizeLabels([...current, ...input.split(',')]);
    const dirty = hasLabelDraft(draft);
    const conflicted = draft && !sameLabels(draft.baseline, labels);

    function edit(next: string[], text = input) {
        const nextDraft = { labels: next, input: text, baseline: draft?.baseline ?? labels };
        onDraftChange(hasLabelDraft(nextDraft) ? nextDraft : undefined);
        setSaved(false);
        setError(null);
    }

    function save(event?: FormEvent, next = false) {
        event?.preventDefault();
        const failure = onSave(pending);
        setError(failure);
        if (!failure) {
            onDraftChange(undefined);
            setSaved(true);
            if (next) onNextUnlabeled?.();
        }
    }

    return (
        <section aria-labelledby="model-labels-title" className="rounded-lg border border-slate-800 bg-slate-950/40 p-4">
            <div className="flex items-baseline justify-between gap-3">
                <h3 id="model-labels-title" className="text-sm font-semibold text-slate-100">Model labels</h3>
                <span className={`text-xs ${pending.length > MAX_LABELS ? 'text-rose-300' : 'text-slate-400'}`}>{pending.length} / {MAX_LABELS}</span>
            </div>
            <p id="label-help" className="mt-1 text-xs leading-5 text-slate-400">Organize models by dataset, purpose, or review status. Labels are saved only in this browser, not in the model artifact.</p>
            <form onSubmit={save} className="mt-3 space-y-3">
                <div className="flex flex-wrap gap-2">
                    {current.map((label) => (
                        <span key={label} className="inline-flex items-center gap-1 rounded-full border border-indigo-400/30 bg-indigo-500/10 py-1 pr-1 pl-2.5 text-xs text-indigo-200">
                            {label}
                            <button type="button" aria-label={`Remove label ${label}`} onClick={() => edit(current.filter((value) => value !== label))} className="rounded-full px-1.5 hover:bg-indigo-400/20">×</button>
                        </span>
                    ))}
                    {current.length === 0 && <p className="text-xs text-slate-500">No labels yet.</p>}
                </div>
                <label className="block text-xs text-slate-300">
                    Add labels
                    <input className="field mt-1" value={input} aria-describedby="label-help label-format" placeholder="e.g. baseline, needs-review"
                        onChange={(event) => edit(current, event.target.value)} />
                </label>
                <p id="label-format" className="text-xs text-slate-400">Separate labels with commas. Up to {MAX_LABELS} labels, {MAX_LABEL_LENGTH} characters each. Labels use lowercase.</p>
                {suggestions.some((label) => !pending.includes(label)) && (
                    <div className="flex flex-wrap items-center gap-2 text-xs">
                        <span className="text-slate-400">Reuse a label:</span>
                        {suggestions.filter((label) => !pending.includes(label)).slice(0, 10).map((label) => (
                            <button key={label} type="button" disabled={pending.length >= MAX_LABELS} onClick={() => edit(normalizeLabels([...current, label]))}
                                className="rounded-full border border-slate-700 px-2 py-1 text-slate-300 hover:border-indigo-400 disabled:opacity-40">{label}</button>
                        ))}
                    </div>
                )}
                {error && <p role="alert" className="text-xs text-rose-300">{error}</p>}
                {conflicted && !error && <p role="alert" className="text-xs text-amber-200">Saved labels changed in another tab. Your draft is preserved. Discard it to load the latest labels.</p>}
                <div className="flex flex-wrap items-center gap-3">
                    <button type="submit" disabled={!dirty} className="rounded-md bg-indigo-600 px-3 py-2 text-xs font-medium text-white hover:bg-indigo-500 disabled:opacity-40">Save labels</button>
                    {onNextUnlabeled && <button type="button" disabled={pending.length === 0} onClick={() => save(undefined, true)} className="rounded-md border border-slate-600 px-3 py-2 text-xs text-slate-200 hover:border-indigo-400 disabled:opacity-40">Save & next unlabeled</button>}
                    {dirty && <button type="button" onClick={() => { onDraftChange(undefined); setError(null); setSaved(false); }} className="text-xs text-slate-300">Discard changes</button>}
                    <span role="status" className="text-xs text-emerald-300">{saved ? 'Labels saved in this browser.' : dirty ? 'Unsaved changes' : ''}</span>
                </div>
            </form>
        </section>
    );
}
