/** Exact, complete evidence units. Never strip negation, digits, or punctuation to match a claim. */
import { cLocaleTokens } from '../evaluation/codebase-data.ts';
export interface EvidenceUnit { text: string; start: number; end: number }

/** Split on sentence endings or blank paragraphs, preserving soft-wrapped qualifiers. */
export function evidenceUnits(text: string): EvidenceUnit[] {
    const units: EvidenceUnit[] = [];
    const boundary = /[.!?](?=\s|$)|\r?\n[\t ]*\r?\n+/gu;
    let start = 0;
    const add = (end: number) => {
        const raw = text.slice(start, end);
        const leading = raw.length - raw.trimStart().length;
        const quote = raw.trim();
        if (quote) units.push({ text: quote, start: start + leading, end: start + leading + quote.length });
        start = end;
    };
    for (const match of text.matchAll(boundary)) add(match.index + match[0].length);
    if (start < text.length) add(text.length);
    return units;
}

/** Accommodate the native word tokenizer's case folding and punctuation spacing only. */
export function normalizeEvidence(text: string): string {
    return cLocaleTokens(text).join(' ');
}
