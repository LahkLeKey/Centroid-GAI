/** JSON-only types shared by the HTTP service and the web inspector. */
export interface ArtifactSummary {
    dimensions: number;
    centroidCount: number;
    initializedCentroids: number;
    contextWindow: number;
    seed: string;
    vocabularySize: string;
    examplesSeen: string;
    storage: { vectors: string; tokenCounts: string; clusterSizes: string };
}
export interface ArtifactPage<T> { total: number; offset: number; limit: number; items: T[] }
export interface TokenEntry { id: number; token: string; count: string }
export interface CentroidEntry { id: number; observations: string; norm: number; distinctTargets: number }
export interface CentroidDetail { id: number; observations: string; vector: number[]; tokens: ArtifactPage<TokenEntry> }
export interface ContentsBySection {
    summary: ArtifactSummary;
    vocabulary: ArtifactPage<TokenEntry>;
    centroids: ArtifactPage<CentroidEntry>;
    centroid: CentroidDetail;
}
export type ContentsSection = keyof ContentsBySection;
export interface CompositionRecipe {
    version: 1;
    autoRebuild?: boolean;
    algorithm: 'preserve' | 'weighted-streaming-v1';
    targetCentroids: number;
    sources: { name: string; checksumSha256: string; initializedCentroids: number; examplesSeen: string }[];
}
export interface ArtifactContents<S extends ContentsSection = ContentsSection> {
    name: string;
    checksumSha256: string;
    artifactBytes: number;
    composition: CompositionRecipe | null;
    section: S;
    data: ContentsBySection[S];
}
export interface MergeRequest {
    autoRebuild?: boolean;
    sources: { name: string; checksumSha256?: string }[];
    targetCentroids: number;
}
