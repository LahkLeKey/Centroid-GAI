/** A provenance check for verbatim extracts, not a semantic truth/confidence score. */
export interface ChatGrounding {
    readonly version: 1;
    readonly mode: 'extractive';
    readonly status: 'quoted' | 'fallback' | 'abstained';
    readonly reason: string;
    readonly evidenceSourceIds: readonly string[];
    readonly claims: readonly {
        readonly sourceId: string;
        readonly quote: string;
        /** UTF-16 offsets into the cited source's original excerpt. */
        readonly start: number;
        readonly end: number;
    }[];
}
