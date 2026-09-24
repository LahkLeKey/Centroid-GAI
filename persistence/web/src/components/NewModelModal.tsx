import { useRef, useState } from 'react';
import type { FormEvent, ReactNode } from 'react';
import { ApiError, trainModel, uploadModelArtifact } from '../api/client';

interface NewModelModalProps {
    open: boolean;
    onClose: () => void;
    onCreated: (name: string) => void;
}

type Mode = 'train' | 'upload';

export function NewModelModal({ open, onClose, onCreated }: NewModelModalProps) {
    const [mode, setMode] = useState<Mode>('train');

    // Train state
    const [trainName, setTrainName] = useState('');
    const [text, setText] = useState('');
    const [dimensions, setDimensions] = useState('');
    const [centroidCount, setCentroidCount] = useState('');
    const [contextWindow, setContextWindow] = useState('');

    // Upload state
    const [uploadName, setUploadName] = useState('');
    const [file, setFile] = useState<File | null>(null);
    const fileInputRef = useRef<HTMLInputElement>(null);

    const [submitting, setSubmitting] = useState(false);
    const [error, setError] = useState<string | null>(null);

    if (!open) return null;

    function reset() {
        setText('');
        setTrainName('');
        setUploadName('');
        setFile(null);
        setError(null);
        if (fileInputRef.current) fileInputRef.current.value = '';
    }

    async function handleTrain(event: FormEvent) {
        event.preventDefault();
        setError(null);
        if (!trainName.trim() || !text.trim()) {
            setError('Model name and training text are required.');
            return;
        }
        setSubmitting(true);
        try {
            await trainModel(trainName.trim(), text, {
                dimensions: dimensions ? Number(dimensions) : undefined,
                centroidCount: centroidCount ? Number(centroidCount) : undefined,
                contextWindow: contextWindow ? Number(contextWindow) : undefined,
            });
            onCreated(trainName.trim());
            reset();
            onClose();
        } catch (cause) {
            setError(cause instanceof ApiError ? cause.message : 'Training request failed.');
        } finally {
            setSubmitting(false);
        }
    }

    async function handleUpload(event: FormEvent) {
        event.preventDefault();
        setError(null);
        if (!uploadName.trim() || !file) {
            setError('Model name and a .cgai file are required.');
            return;
        }
        setSubmitting(true);
        try {
            const payload = await file.arrayBuffer();
            await uploadModelArtifact(uploadName.trim(), payload);
            onCreated(uploadName.trim());
            reset();
            onClose();
        } catch (cause) {
            setError(cause instanceof ApiError ? cause.message : 'Upload failed.');
        } finally {
            setSubmitting(false);
        }
    }

    return (
        <div
            className="fixed inset-0 z-50 flex items-center justify-center bg-slate-950/70 backdrop-blur-sm"
            onClick={(event) => {
                if (event.target === event.currentTarget) onClose();
            }}
        >
            <div
                role="dialog"
                aria-modal="true"
                aria-label="New model"
                className="w-full max-w-lg rounded-lg border border-slate-800 bg-slate-900 shadow-2xl"
            >
                <div className="flex items-center justify-between border-b border-slate-800 px-5 py-3">
                    <h2 className="text-sm font-semibold text-slate-100">New model</h2>
                    <button
                        type="button"
                        onClick={onClose}
                        className="rounded-md p-1 text-slate-500 hover:bg-slate-800 hover:text-slate-300"
                    >
                        ✕
                    </button>
                </div>

                <div className="flex gap-1 border-b border-slate-800 px-5 pt-3">
                    {(['train', 'upload'] as const).map((tab) => (
                        <button
                            key={tab}
                            type="button"
                            onClick={() => setMode(tab)}
                            className={`rounded-t-md px-3 py-1.5 text-xs font-medium ${mode === tab
                                ? 'border-b-2 border-indigo-500 text-indigo-400'
                                : 'text-slate-500 hover:text-slate-300'
                                }`}
                        >
                            {tab === 'train' ? 'Train from text' : 'Upload artifact'}
                        </button>
                    ))}
                </div>

                {mode === 'train' ? (
                    <form onSubmit={handleTrain} className="space-y-3 px-5 py-4">
                        <Field label="Model name">
                            <input
                                value={trainName}
                                onChange={(event) => setTrainName(event.target.value)}
                                className="field"
                                placeholder="tiny-contexts"
                            />
                        </Field>
                        <Field label="Training text">
                            <textarea
                                value={text}
                                onChange={(event) => setText(event.target.value)}
                                rows={5}
                                className="field resize-none"
                                placeholder="Paste corpus text…"
                            />
                        </Field>
                        <div className="grid grid-cols-3 gap-2">
                            <Field label="Dimensions">
                                <input
                                    value={dimensions}
                                    onChange={(event) => setDimensions(event.target.value)}
                                    className="field"
                                    placeholder="default"
                                />
                            </Field>
                            <Field label="Centroids">
                                <input
                                    value={centroidCount}
                                    onChange={(event) => setCentroidCount(event.target.value)}
                                    className="field"
                                    placeholder="default"
                                />
                            </Field>
                            <Field label="Context window">
                                <input
                                    value={contextWindow}
                                    onChange={(event) => setContextWindow(event.target.value)}
                                    className="field"
                                    placeholder="default"
                                />
                            </Field>
                        </div>
                        {error && <p className="text-xs text-rose-400">{error}</p>}
                        <button
                            type="submit"
                            disabled={submitting}
                            className="w-full rounded-md bg-indigo-600 px-3 py-2 text-sm font-medium text-white hover:bg-indigo-500 disabled:cursor-not-allowed disabled:opacity-50"
                        >
                            {submitting ? 'Training…' : 'Train & persist'}
                        </button>
                    </form>
                ) : (
                    <form onSubmit={handleUpload} className="space-y-3 px-5 py-4">
                        <Field label="Model name">
                            <input
                                value={uploadName}
                                onChange={(event) => setUploadName(event.target.value)}
                                className="field"
                                placeholder="tiny"
                            />
                        </Field>
                        <Field label=".cgai file">
                            <input
                                ref={fileInputRef}
                                type="file"
                                accept=".cgai"
                                onChange={(event) => setFile(event.target.files?.[0] ?? null)}
                                className="w-full text-sm text-slate-400"
                            />
                        </Field>
                        {error && <p className="text-xs text-rose-400">{error}</p>}
                        <button
                            type="submit"
                            disabled={submitting}
                            className="w-full rounded-md bg-indigo-600 px-3 py-2 text-sm font-medium text-white hover:bg-indigo-500 disabled:cursor-not-allowed disabled:opacity-50"
                        >
                            {submitting ? 'Uploading…' : 'Upload & persist'}
                        </button>
                    </form>
                )}
            </div>
        </div>
    );
}

function Field({ label, children }: { label: string; children: ReactNode }) {
    return (
        <label className="block">
            <span className="mb-1 block text-xs font-medium text-slate-500">{label}</span>
            {children}
        </label>
    );
}
