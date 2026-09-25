/** Thin fetch wrapper over the Centroid-GAI `/api/v1` REST contract. */
import type { ArtifactContents, ContentsSection, MergeRequest } from '../../../shared/artifacts';
export type { ArtifactContents, ArtifactSummary, ArtifactPage, TokenEntry, CentroidEntry, CentroidDetail, CompositionRecipe, MergeRequest } from '../../../shared/artifacts';

export interface NativeModelConfig {
    dimensions?: number;
    centroidCount?: number;
    contextWindow?: number;
    seed?: string;
}

export interface ModelMetadata {
    id: string;
    name: string;
    formatVersion: number;
    libraryVersion: string;
    dimensions: number;
    centroidCount: number;
    contextWindow: number;
    vocabularySize: string;
    examplesSeen: string;
    checksumSha256: string;
    createdAt: string;
    updatedAt: string;
}

export interface SaveModelResult {
    id: string;
    name: string;
    metadata: Omit<ModelMetadata, 'id'|'name'|'checksumSha256'|'createdAt'|'updatedAt'>;
}

export interface GenerateResult {
    name: string;
    prompt: string;
    continuation: string;
}

export interface HealthResult {
    status: string;
    native: boolean;
    database: string;
}

const API_BASE = import.meta.env.VITE_API_BASE_URL ?? '/api/v1';

class ApiError extends Error {
    readonly status: number;

    constructor(message: string, status: number) {
        super(message);
        this.name = 'ApiError';
        this.status = status;
    }
}

async function request<T>(path: string, init?: RequestInit): Promise<T> {
    const response = await fetch(`${API_BASE}${path}`, init);
    if (!response.ok) {
        let message = response.statusText;
        try {
            const body = (await response.json()) as {error?: string};
            if (body.error)
                message = body.error;
        } catch {
            // response had no JSON body; fall back to status text
        }
        throw new ApiError(message, response.status);
    }
    return response.json() as Promise<T>;
}

export function getHealth(): Promise<HealthResult> { return request('/health'); }

export function getNativeSchema(): Promise<unknown> { return request('/native/schema'); }

export function listModels(): Promise<ModelMetadata[]> { return request('/models'); }

export function getModelMetadata(name: string): Promise<ModelMetadata> {
    return request(`/models/${encodeURIComponent(name)}/metadata`);
}

export function deleteModel(name: string): Promise<{deleted : boolean}> {
    return request(`/models/${encodeURIComponent(name)}`, {method : 'DELETE'});
}

export function trainModel(
    name: string,
    text: string,
    config?: NativeModelConfig,
    ): Promise<SaveModelResult> {
    return request(`/models/${encodeURIComponent(name)}/train`, {
        method : 'POST',
        headers : {'content-type' : 'application/json'},
        body : JSON.stringify({text, config}),
    });
}

export function generateFromModel(
    name: string,
    prompt: string,
    maxTokens?: number,
    temperature?: number,
    seed?: string,
    ): Promise<GenerateResult> {
    return request(`/models/${encodeURIComponent(name)}/generate`, {
        method : 'POST',
        headers : {'content-type' : 'application/json'},
        body : JSON.stringify({prompt, maxTokens, temperature, seed}),
    });
}

export function uploadModelArtifact(name: string, payload: ArrayBuffer): Promise<SaveModelResult> {
    return request(`/models/${encodeURIComponent(name)}`, {
        method : 'PUT',
        headers : {'content-type' : 'application/vnd.centroid-gai.model'},
        body : payload,
    });
}

export function downloadModelArtifactUrl(name: string): string {
    return `${API_BASE}/models/${encodeURIComponent(name)}`;
}

export function getArtifactContents<S extends ContentsSection>(name: string, checksum: string, section: S, offset = 0, centroid = 0, signal?: AbortSignal): Promise<ArtifactContents<S>> {
    const query = new URLSearchParams({ section, offset: String(offset), limit: '25', centroid: String(centroid), checksum });
    return request(`/models/${encodeURIComponent(name)}/contents?${query}`, { signal });
}

export function mergeModels(name: string, input: MergeRequest): Promise<SaveModelResult> {
    return request(`/models/${encodeURIComponent(name)}/merge`, {
        method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(input),
    });
}

export {ApiError};
