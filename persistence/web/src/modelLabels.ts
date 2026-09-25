import { useEffect, useState } from 'react';

const STORAGE_KEY = 'centroid-gai:model-labels:v1';
export const MAX_LABELS = 8;
export const MAX_LABEL_LENGTH = 32;
export type ModelLabels = Record<string, string[]>;
export interface LabelDraft {
    labels: string[];
    input: string;
    baseline: string[];
}

export function sameLabels(left: string[], right: string[]): boolean {
    return JSON.stringify([...left].sort()) === JSON.stringify([...right].sort());
}

export function hasLabelDraft(draft: LabelDraft | undefined): boolean {
    return !!draft && (draft.input.trim().length > 0 || !sameLabels(draft.labels, draft.baseline));
}

export function normalizeLabels(values: string[]): string[] {
    return [...new Set(values.map((value) => value.trim().toLowerCase()).filter(Boolean))];
}

function readLabels(): ModelLabels {
    const raw: unknown = JSON.parse(localStorage.getItem(STORAGE_KEY) ?? '{}');
    if (!raw || typeof raw !== 'object' || Array.isArray(raw)) throw new Error('Invalid labels');
    const entries = Object.entries(raw).map(([id, values]) => {
        if (!Array.isArray(values) || !values.every((value) => typeof value === 'string')) throw new Error('Invalid labels');
        const normalized = normalizeLabels(values);
        if (normalized.length > MAX_LABELS || normalized.some((value) => value.length > MAX_LABEL_LENGTH)) throw new Error('Invalid labels');
        return [id, normalized] as const;
    });
    return Object.fromEntries(entries);
}

export function useModelLabels() {
    const [initial] = useState(() => {
        try {
            return { labels: readLabels(), error: null as string | null };
        } catch {
            return { labels: {} as ModelLabels, error: 'Saved labels could not be loaded. Check browser storage before saving; existing data will not be overwritten.' };
        }
    });
    const [labels, setLabels] = useState(initial.labels);
    const [storageError, setStorageError] = useState(initial.error);

    useEffect(() => {
        function sync(event: StorageEvent) {
            if (event.storageArea !== localStorage || (event.key !== STORAGE_KEY && event.key !== null)) return;
            try {
                setLabels(readLabels());
                setStorageError(null);
            } catch {
                setStorageError('Saved labels could not be loaded. Your current labels and drafts are still available in this tab.');
            }
        }
        window.addEventListener('storage', sync);
        return () => window.removeEventListener('storage', sync);
    }, []);

    function saveLabels(id: string, values: string[], expected: string[]): string | null {
        const normalized = normalizeLabels(values);
        if (normalized.length > MAX_LABELS) return `Use up to ${MAX_LABELS} labels per model.`;
        if (normalized.some((value) => value.length > MAX_LABEL_LENGTH)) return `Keep each label to ${MAX_LABEL_LENGTH} characters or fewer.`;
        try {
            // Read immediately before writing so edits to other models in another tab survive.
            const latest = readLabels();
            if (!sameLabels(latest[id] ?? [], expected)) {
                setLabels(latest);
                return 'This model’s labels changed in another tab. Copy any text you want to keep, then discard this draft to load the latest labels.';
            }
            const next = { ...latest, [id]: normalized };
            localStorage.setItem(STORAGE_KEY, JSON.stringify(next));
            setLabels(next);
        } catch {
            setStorageError('Browser storage could not be read or updated. Your changes have not been saved; check storage access, available space, and saved label data.');
            return 'Labels could not be saved. Your draft is still here.';
        }
        setStorageError(null);
        return null;
    }

    return { labels, saveLabels, storageError };
}
