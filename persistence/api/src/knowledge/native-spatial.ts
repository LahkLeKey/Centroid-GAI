/** Map repository centroid/category identities onto the immutable native spatial API. */
import { createNativeSpatialIndex } from '../native.ts';
import { compareIds } from './spatial.ts';
import type { SpatialPoint } from './spatial.ts';

/** Preserve lexical ID tie order while C owns the copied coordinates and performs all traversal. */
export function createNativeCentroidSearch(input: SpatialPoint[]) {
    const points = [...input].sort((a, b) => compareIds(a.id, b.id));
    if (new Set(points.map(point => point.id)).size !== points.length) throw new Error('Duplicate centroid IDs');
    const categories = [...new Set(points.map(point => point.category))].sort(compareIds);
    const index = createNativeSpatialIndex(points.map(point => point.vector), points.map(point => categories.indexOf(point.category)));
    return {
        search(vector: number[], limit = 5, category?: string, exclude?: string) {
            const categoryIndex = category === undefined ? undefined : categories.indexOf(category);
            if (categoryIndex === -1) return { neighbors: [], comparisons: 0, visitedNodes: 0 };
            const row = exclude === undefined ? -1 : points.findIndex(point => point.id === exclude);
            const result = index.query(vector, limit, categoryIndex, row < 0 ? undefined : row);
            return { ...result, neighbors: result.neighbors.map(hit => ({ id: points[hit.index]!.id, squaredDistance: hit.squaredDistance })) };
        },
    };
}
