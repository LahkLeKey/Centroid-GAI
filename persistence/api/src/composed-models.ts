import type { CompositionRecipe } from '../../shared/artifacts.ts';
import { loadModelArtifact, refreshComposedArtifact } from './model-repository.ts';
import { inspectNativeContents, inspectNativeModel, mergeNativeModels } from './native.ts';

const MAX_BYTES = 64 * 1024 * 1024;
type Artifact = NonNullable<Awaited<ReturnType<typeof loadModelArtifact>>>;
class ConcurrentRefresh extends Error {}

/** Resolve a live dependency graph once per request, refreshing children before parents. */
export async function loadCurrentModelArtifact(name: string): Promise<Artifact | null> {
    for (let attempt = 0; attempt < 3; attempt++) {
        const resolved = new Map<string, Artifact | null>();
        const visited = new Set<string>();
        const visit = async (current: string, path: Set<string>): Promise<Artifact | null> => {
            if (path.has(current)) throw new TypeError(`Composition cycle at ${current}`);
            if (resolved.has(current)) return resolved.get(current)!;
            visited.add(current);
            if (path.size >= 32 || visited.size > 128) throw new TypeError('Composition dependency limit exceeded (32 levels, 128 models)');
            const model = await loadModelArtifact(current);
            const recipe = model?.compositionJson ? JSON.parse(model.compositionJson) as CompositionRecipe : null;
            if (!model || !recipe?.autoRebuild) {
                resolved.set(current, model);
                return model;
            }
            const next = new Set(path).add(current);
            const sources: Artifact[] = [];
            let bytes = 0;
            for (const source of recipe.sources) {
                const artifact = await visit(source.name, next);
                if (!artifact) throw new TypeError(`Cannot rebuild ${current}: source model ${source.name} is missing. Restore it to use this live super model.`);
                bytes += artifact.payload.byteLength;
                if (bytes > MAX_BYTES) throw new RangeError(`Cannot rebuild ${current}: combined source artifacts exceed 64 MiB`);
                sources.push(artifact);
            }
            if (sources.every((source, index) => source.checksumSha256 === recipe.sources[index]!.checksumSha256)) {
                resolved.set(current, model);
                return model;
            }
            if (new Set(sources.map((source) => source.checksumSha256)).size !== sources.length)
                throw new TypeError(`Cannot rebuild ${current}: sources now contain byte-identical artifacts`);
            // Preserve mode follows the current active row count; compact keeps its target.
            const target = recipe.algorithm === 'preserve' ? 0 : recipe.targetCentroids;
            let payload: Buffer;
            try { payload = mergeNativeModels(sources.map((source) => source.payload), target); }
            catch (error) { throw new TypeError(`Cannot rebuild ${current}: ${error instanceof Error ? error.message : 'merge failed'}`); }
            if (payload.byteLength > MAX_BYTES) throw new RangeError(`Cannot rebuild ${current}: merged artifact exceeds 64 MiB`);
            const metadata = inspectNativeModel(payload);
            if (metadata.examplesSeen > 9223372036854775807n) throw new TypeError('Merged examples exceed the database counter range');
            const updated: CompositionRecipe = { ...recipe, targetCentroids: metadata.centroidCount,
                sources: sources.map((source) => {
                    const summary = inspectNativeContents(source.payload, 'summary');
                    return { name: source.name, checksumSha256: source.checksumSha256,
                        initializedCentroids: summary.initializedCentroids, examplesSeen: summary.examplesSeen };
                }),
            };
            const saved = await refreshComposedArtifact(model, { name: current, payload, metadata }, updated);
            if (!saved) throw new ConcurrentRefresh();
            resolved.set(current, saved);
            return saved;
        };
        try { return await visit(name, new Set()); }
        catch (error) { if (!(error instanceof ConcurrentRefresh)) throw error; }
    }
    throw new TypeError('Model changed repeatedly during automatic rebuild. Retry the request.');
}
