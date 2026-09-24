const API_BASE = process.env.VITE_API_PROXY_TARGET ?? 'http://localhost:3000';

export function uniqueName(prefix: string): string {
    return `${prefix}-${Date.now()}-${Math.floor(Math.random() * 1e6)}`;
}

export async function trainViaApi(name: string, text: string): Promise<void> {
    const response = await fetch(`${API_BASE}/api/v1/models/${encodeURIComponent(name)}/train`, {
        method : 'POST',
        headers : {'content-type' : 'application/json'},
        body : JSON.stringify({text}),
    });
    if (!response.ok) {
        throw new Error(`fixture training failed: ${response.status} ${await response.text()}`);
    }
}

export async function deleteViaApi(name: string): Promise<void> {
    await fetch(`${API_BASE}/api/v1/models/${encodeURIComponent(name)}`, {method : 'DELETE'});
}

export const SAMPLE_TRAINING_TEXT = `
Centroid models learn from repeated examples. Every context window captures
nearby tokens and folds them into a small set of centroids. Training on more
diverse corpora improves generation quality. Centroid models learn quickly
when the vocabulary is small and repetitive.
`.trim();
