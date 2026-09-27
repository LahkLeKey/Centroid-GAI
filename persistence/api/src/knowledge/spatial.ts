/** Static exact nearest-neighbor index over vectors inspected from compatible native models. */
export interface SpatialPoint {
    id: string;
    category: string;
    centroid: number;
    vector: number[];
    observations: string;
}
export interface SpatialNode {
    minimum: number[];
    maximum: number[];
    categories: string[];
    points: number[];
    children: SpatialNode[];
}
export interface SpatialIndex { version: 1; dimensions: number; points: SpatialPoint[]; root: SpatialNode }
export interface Neighbor { id: string; squaredDistance: number }
export const compareIds = (a: string, b: string) => a < b ? -1 : a > b ? 1 : 0;

/** Use the inspected vector coordinates directly; neither normalize nor mix embedding spaces. */
export function squaredDistance(a: number[], b: number[]): number {
    return a.reduce((sum, value, dimension) => sum + (value - b[dimension]!) ** 2, 0);
}

/** Split the widest bounding-box axis at the median, with small contiguous leaf buckets. */
export function buildSpatialIndex(input: SpatialPoint[]): SpatialIndex {
    const dimensions = input[0]?.vector.length ?? 0;
    if (!dimensions || dimensions > 4096 || !input.length || input.length > 4096 ||
        new Set(input.map(point => point.id)).size !== input.length || input.some(point =>
            !point.id || !point.category || point.vector.length !== dimensions || !point.vector.every(Number.isFinite))) {
        throw new Error('Invalid spatial points: need unique IDs and finite vectors with equal dimensions (at most 4096 points)');
    }
    const points = input.map(point => ({ ...point, vector: [...point.vector] })).sort((a, b) => compareIds(a.id, b.id));
    function partition(indices: number[]): SpatialNode {
        const minimum = Array<number>(dimensions).fill(Infinity);
        const maximum = Array<number>(dimensions).fill(-Infinity);
        for (const index of indices) for (let d = 0; d < dimensions; d++) {
            minimum[d] = Math.min(minimum[d]!, points[index]!.vector[d]!);
            maximum[d] = Math.max(maximum[d]!, points[index]!.vector[d]!);
        }
        const node: SpatialNode = { minimum, maximum,
            categories: [...new Set(indices.map(index => points[index]!.category))].sort(compareIds), points: [], children: [] };
        if (indices.length <= 8) node.points = indices;
        else {
            let axis = 0;
            for (let d = 1; d < dimensions; d++) if (maximum[d]! - minimum[d]! > maximum[axis]! - minimum[axis]!) axis = d;
            indices.sort((a, b) => points[a]!.vector[axis]! - points[b]!.vector[axis]! || compareIds(points[a]!.id, points[b]!.id));
            const middle = Math.floor(indices.length / 2);
            node.children = [partition(indices.slice(0, middle)), partition(indices.slice(middle))];
        }
        return node;
    }
    return { version: 1, dimensions, points, root: partition(points.map((_, index) => index)) };
}

/** Exact best-first box traversal. Category filters prune whole branches, and self IDs can be excluded. */
export function nearest(index: SpatialIndex, vector: number[], limit = 5, category?: string, exclude?: string) {
    if (vector.length !== index.dimensions || !vector.every(Number.isFinite) ||
        !Number.isInteger(limit) || limit < 1 || limit > 100) throw new Error('Invalid vector or neighbor limit (1–100)');
    let comparisons = 0;
    let visitedNodes = 0;
    const neighbors: Neighbor[] = [];
    const lowerBound = (node: SpatialNode) => vector.reduce((sum, value, d) =>
        sum + Math.max(node.minimum[d]! - value, 0, value - node.maximum[d]!) ** 2, 0);
    function visit(node: SpatialNode) {
        if (category !== undefined && !node.categories.includes(category)) return;
        visitedNodes++;
        const worst = neighbors.length === limit ? neighbors[neighbors.length - 1]!.squaredDistance : Infinity;
        // A conservative tolerance prevents rounding at a box boundary from discarding a tied candidate.
        if (lowerBound(node) > worst + 1e-12 * Math.max(1, worst)) return;
        for (const position of node.points) {
            const point = index.points[position]!;
            if (point.id === exclude || (category !== undefined && point.category !== category)) continue;
            comparisons++;
            neighbors.push({ id: point.id, squaredDistance: squaredDistance(vector, point.vector) });
            neighbors.sort((a, b) => a.squaredDistance - b.squaredDistance || compareIds(a.id, b.id));
            if (neighbors.length > limit) neighbors.pop();
        }
        for (const child of [...node.children].sort((a, b) => lowerBound(a) - lowerBound(b))) visit(child);
    }
    visit(index.root);
    return { neighbors, comparisons, visitedNodes };
}

/** Independent exhaustive reference for audits; identical tie policy to the indexed query. */
export function exhaustive(points: SpatialPoint[], vector: number[], limit: number, category?: string, exclude?: string): Neighbor[] {
    return points.filter(point => point.id !== exclude && (category === undefined || point.category === category))
        .map(point => ({ id: point.id, squaredDistance: squaredDistance(vector, point.vector) }))
        .sort((a, b) => a.squaredDistance - b.squaredDistance || compareIds(a.id, b.id)).slice(0, limit);
}
