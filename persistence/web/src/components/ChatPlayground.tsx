import { useEffect, useRef, useState } from 'react';
import type { FormEvent } from 'react';
import { ApiError, generateFromModel } from '../api/client';

interface ChatMessage {
    id: string;
    role: 'user' | 'assistant';
    content: string;
}

interface ChatPlaygroundProps {
    modelName: string | null;
}

/** Number of prior turns folded into the prompt so replies stay responsive to recent context. */
const CONTEXT_TURNS = 4;

export function ChatPlayground({ modelName }: ChatPlaygroundProps) {
    const [messages, setMessages] = useState<ChatMessage[]>([]);
    const [prompt, setPrompt] = useState('');
    const [maxTokens, setMaxTokens] = useState('');
    const [temperature, setTemperature] = useState('');
    const [seed, setSeed] = useState('');
    const [settingsOpen, setSettingsOpen] = useState(false);
    const [pending, setPending] = useState(false);
    const [error, setError] = useState<string | null>(null);
    const scrollRef = useRef<HTMLDivElement>(null);

    useEffect(() => {
        setMessages([]);
        setError(null);
    }, [modelName]);

    useEffect(() => {
        scrollRef.current?.scrollTo({ top: scrollRef.current.scrollHeight, behavior: 'smooth' });
    }, [messages, pending]);

    async function handleSubmit(event: FormEvent) {
        event.preventDefault();
        const trimmed = prompt.trim();
        if (!modelName || !trimmed || pending) return;

        setError(null);
        const userMessage: ChatMessage = { id: crypto.randomUUID(), role: 'user', content: trimmed };
        const history = [...messages, userMessage];
        setMessages(history);
        setPrompt('');
        setPending(true);

        const contextPrompt = history
            .slice(-CONTEXT_TURNS * 2)
            .map((message) => (message.role === 'user' ? message.content : message.content))
            .join('\n');

        try {
            const response = await generateFromModel(
                modelName,
                contextPrompt,
                maxTokens ? Number(maxTokens) : undefined,
                temperature ? Number(temperature) : undefined,
                seed || undefined,
            );
            setMessages((current) => [
                ...current,
                { id: crypto.randomUUID(), role: 'assistant', content: response.continuation || '…' },
            ]);
        } catch (cause) {
            setError(cause instanceof ApiError ? cause.message : 'Generation request failed.');
        } finally {
            setPending(false);
        }
    }

    return (
        <div className="flex h-full flex-col overflow-hidden rounded-lg border border-slate-800 bg-slate-900/60">
            <div className="flex items-center justify-between border-b border-slate-800 px-4 py-3">
                <div>
                    <h2 className="text-sm font-semibold text-slate-100">Playground</h2>
                    <p className="text-xs text-slate-500">
                        Model: <span className="font-mono text-slate-300">{modelName ?? 'none selected'}</span>
                    </p>
                </div>
                <div className="flex items-center gap-2">
                    {messages.length > 0 && (
                        <button
                            type="button"
                            onClick={() => setMessages([])}
                            className="rounded-md px-2 py-1 text-xs font-medium text-slate-500 hover:bg-slate-800 hover:text-slate-300"
                        >
                            Clear
                        </button>
                    )}
                    <button
                        type="button"
                        onClick={() => setSettingsOpen((open) => !open)}
                        className={`rounded-md px-2 py-1 text-xs font-medium ${settingsOpen ? 'bg-slate-800 text-slate-200' : 'text-slate-500 hover:bg-slate-800 hover:text-slate-300'
                            }`}
                    >
                        Settings
                    </button>
                </div>
            </div>

            {settingsOpen && (
                <div className="grid grid-cols-3 gap-2 border-b border-slate-800 bg-slate-950/40 px-4 py-3">
                    <label className="block">
                        <span className="mb-1 block text-[11px] font-medium text-slate-500">Max tokens</span>
                        <input
                            value={maxTokens}
                            onChange={(event) => setMaxTokens(event.target.value)}
                            className="field"
                            placeholder="default"
                        />
                    </label>
                    <label className="block">
                        <span className="mb-1 block text-[11px] font-medium text-slate-500">Temperature</span>
                        <input
                            value={temperature}
                            onChange={(event) => setTemperature(event.target.value)}
                            className="field"
                            placeholder="default"
                        />
                    </label>
                    <label className="block">
                        <span className="mb-1 block text-[11px] font-medium text-slate-500">Seed</span>
                        <input
                            value={seed}
                            onChange={(event) => setSeed(event.target.value)}
                            className="field"
                            placeholder="0"
                        />
                    </label>
                </div>
            )}

            <div ref={scrollRef} className="flex-1 space-y-3 overflow-y-auto px-4 py-4">
                {messages.length === 0 && (
                    <div className="flex h-full flex-col items-center justify-center gap-2 text-center text-slate-600">
                        <p className="text-sm">
                            {modelName ? 'Say something to start a conversation with this model.' : 'Select a model to begin.'}
                        </p>
                    </div>
                )}
                {messages.map((message) => (
                    <div key={message.id} className={`flex ${message.role === 'user' ? 'justify-end' : 'justify-start'}`}>
                        <div
                            data-testid="chat-message"
                            data-role={message.role}
                            className={`max-w-[80%] rounded-lg px-3 py-2 text-sm whitespace-pre-wrap ${message.role === 'user'
                                ? 'bg-indigo-600 text-white'
                                : 'border border-slate-800 bg-slate-950 text-slate-200'
                                }`}
                        >
                            {message.content}
                        </div>
                    </div>
                ))}
                {pending && (
                    <div className="flex justify-start" data-testid="chat-pending">
                        <div className="flex items-center gap-1 rounded-lg border border-slate-800 bg-slate-950 px-3 py-2">
                            <Dot delay="0ms" />
                            <Dot delay="150ms" />
                            <Dot delay="300ms" />
                        </div>
                    </div>
                )}
            </div>

            {error && <p className="border-t border-slate-800 px-4 py-2 text-xs text-rose-400">{error}</p>}

            <form onSubmit={handleSubmit} className="flex items-center gap-2 border-t border-slate-800 px-4 py-3">
                <input
                    value={prompt}
                    onChange={(event) => setPrompt(event.target.value)}
                    disabled={!modelName}
                    placeholder={modelName ? 'Message the model…' : 'Select a model first'}
                    data-testid="chat-input"
                    className="field flex-1 disabled:cursor-not-allowed disabled:opacity-50"
                />
                <button
                    type="submit"
                    disabled={!modelName || pending || !prompt.trim()}
                    className="rounded-md bg-emerald-600 px-4 py-1.5 text-sm font-medium text-white hover:bg-emerald-500 disabled:cursor-not-allowed disabled:opacity-50"
                >
                    Send
                </button>
            </form>
        </div>
    );
}

function Dot({ delay }: { delay: string }) {
    return (
        <span
            className="h-1.5 w-1.5 animate-bounce rounded-full bg-slate-500"
            style={{ animationDelay: delay }}
        />
    );
}
