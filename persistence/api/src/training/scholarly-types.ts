/** Immutable scholarly evidence contracts. Verification describes checks, not truth probabilities. */
export interface ScholarlySnapshot {
    readonly url: string;
    readonly contentType: string;
    readonly text: string;
    readonly sha256: string;
    readonly fetchedAt: string;
}
export interface ScholarlyReference { readonly doi: string; readonly title?: string }
export interface ScholarlyPaper {
    readonly id: string;
    readonly doi: string;
    readonly title: string;
    readonly authors: readonly string[];
    readonly year: number;
    readonly licenseUrl: string;
    readonly publicationTypes: readonly string[];
    readonly text: string;
    readonly references: readonly ScholarlyReference[];
    readonly metadata: ScholarlySnapshot;
    readonly fullText: ScholarlySnapshot;
}
export interface CrossrefRecord {
    readonly doi: string;
    readonly title: string;
    readonly year: number;
    readonly authors: readonly string[];
    readonly type: string;
    readonly retractionSignal: boolean;
    readonly referenceDois: readonly string[];
    readonly snapshot: ScholarlySnapshot;
}
export interface BibliographicReference {
    readonly doi: string;
    readonly url: string;
    readonly title: string;
    readonly metadataSha256: string;
    readonly snapshot: ScholarlySnapshot;
}
export interface VerifiedScholarlyPaper extends ScholarlyPaper {
    readonly crossref: CrossrefRecord;
    readonly verification: {
        readonly policy: 'scholarly-attribution-v1';
        readonly bibliographyMatched: true;
        readonly licenseEligible: true;
        readonly sourceHashVerified: true;
        readonly noRetractionSignal: true;
        readonly citedReferences: readonly BibliographicReference[];
        readonly scientificTruth: 'not-assessed';
    };
}
export interface CitedPassage {
    readonly id: string;
    readonly doi: string;
    readonly quote: string;
    readonly start: number;
    readonly end: number;
    readonly citationUrl: string;
    readonly sourceSha256: string;
    /** Other canonical works containing the complete unit; copies of one DOI count once. */
    readonly exactAgreementDois: readonly string[];
    readonly support: 'attributed-source-quote';
}
export interface ScholarlyAdmission {
    readonly accepted: readonly VerifiedScholarlyPaper[];
    readonly excluded: readonly { id: string; doi: string; reasons: readonly string[] }[];
}
export interface ScholarlyCorpus {
    readonly version: 1;
    readonly policy: 'scholarly-attribution-v1';
    readonly papers: readonly VerifiedScholarlyPaper[];
    readonly passages: readonly CitedPassage[];
    readonly splits: { readonly train: readonly string[]; readonly development: readonly string[]; readonly test: readonly string[] };
    readonly texts: { readonly train: string; readonly development: string; readonly test: string };
    readonly sha256: string;
}
/** Pinned work identities and exact texts; later discoveries never change these hold-outs. */
export interface ScholarlyBenchmark {
    readonly version: 1;
    readonly papers: readonly VerifiedScholarlyPaper[];
    readonly passages: readonly CitedPassage[];
    readonly developmentDois: readonly string[];
    readonly testDois: readonly string[];
    readonly developmentText: string;
    readonly testText: string;
    readonly sha256: string;
}
export interface ScholarlyCollector {
    collect(queries: readonly string[], maximum: number, signal: AbortSignal): Promise<{
        papers: readonly ScholarlyPaper[];
        excluded: readonly { id: string; reason: string }[];
    }>;
    crossref(doi: string, signal: AbortSignal): Promise<CrossrefRecord>;
}
