import { createHash } from 'node:crypto';
import type { ChatExample } from '../../../shared/chat.ts';

export interface DialogueRecord extends ChatExample {
    readonly id: string;
    readonly family: string;
    readonly category: 'direct' | 'follow-up' | 'correction' | 'clarification' | 'abstention' | 'citation';
    readonly sources: readonly string[];
}
export interface DialogueDataset { version: 1; train: DialogueRecord[]; development: DialogueRecord[]; test: DialogueRecord[] }
const digest = (value: unknown) => createHash('sha256').update(JSON.stringify(value)).digest('hex');

/** Freeze families and source partitions before paraphrasing; reject exact normalized leakage. */
export function validateDataset(value: unknown): { dataset: DialogueDataset; hashes: Record<string, string> } {
    if (!value || typeof value !== 'object' || !('version' in value) || value.version !== 1) throw new Error('dataset version must be 1');
    const dataset = value as DialogueDataset;
    const ids = new Set<string>(), content = new Set<string>();
    const families = new Map<string, string>(), sources = new Map<string, string>();
    for (const split of ['train', 'development', 'test'] as const) {
        const records = dataset[split];
        if (!Array.isArray(records) || !records.length || records.length > 10000) throw new Error(`invalid ${split} split`);
        for (const record of records) {
            if (!record || typeof record.id !== 'string' || !record.id || ids.has(record.id)) throw new Error('duplicate or missing example ID');
            ids.add(record.id);
            if (typeof record.family !== 'string' || !record.family || !['direct', 'follow-up', 'correction', 'clarification', 'abstention', 'citation'].includes(record.category))
                throw new Error('invalid question family or category');
            if (families.has(record.family) && families.get(record.family) !== split) throw new Error('question family leakage');
            families.set(record.family, split);
            if (!Array.isArray(record.sources) || record.sources.some((source) => typeof source !== 'string' || !source)) throw new Error('invalid source references');
            for (const source of record.sources) {
                if (sources.has(source) && sources.get(source) !== split) throw new Error('source leakage');
                sources.set(source, split);
            }
            if (typeof record.answer !== 'string' || !record.answer.trim() || record.answer.includes('\0')) throw new Error('invalid assistant target');
            if (!Array.isArray(record.messages) || !record.messages.length || record.messages.length > 1024) throw new Error('invalid messages');
            let expected = 'user';
            for (const message of record.messages) {
                if (typeof message.content !== 'string' || !message.content.trim() || message.content.includes('\0')) throw new Error('invalid message content');
                if (message.role === 'evidence' && expected === 'user') continue;
                if (message.role !== expected) throw new Error('invalid dialogue order');
                expected = expected === 'user' ? 'assistant' : 'user';
            }
            if (record.messages.at(-1)!.role !== 'user') throw new Error('current user question required');
            const identity = digest(record.messages.map((message) => [message.role, message.content.toLowerCase().replace(/\s+/g, ' ').trim()]));
            if (content.has(identity)) throw new Error('duplicate normalized dialogue');
            content.add(identity);
        }
    }
    if (Buffer.byteLength(JSON.stringify(dataset)) > 16 * 1024 * 1024) throw new Error('dataset exceeds byte limit');
    return { dataset, hashes: { corpus: digest(dataset), train: digest(dataset.train), development: digest(dataset.development), test: digest(dataset.test) } };
}
