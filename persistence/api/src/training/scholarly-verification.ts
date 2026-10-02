/** Reproducible bibliographic admission and source-separated attributed excerpt corpora. */
import { createHash } from 'node:crypto';
import { evidenceUnits, normalizeEvidence } from '../chat/evidence.ts';
import { parseScholarlyXml, scholarlyAuthorsAgree } from './scholarly-xml.ts';
import { officialScholarlyUrl, parseCrossrefRecord, parseEuropePmcRecord } from './scholarly-provider.ts';
import type { BibliographicReference, CitedPassage, CrossrefRecord, ScholarlyAdmission, ScholarlyBenchmark,
    ScholarlyCollector, ScholarlyCorpus, ScholarlyPaper, ScholarlySnapshot, VerifiedScholarlyPaper } from './scholarly-types.ts';

export const scholarlySha256 = (text: string): string => createHash('sha256').update(text, 'utf8').digest('hex');

/** Sorting object keys keeps identities independent of JSON serialization and discovery order. */
export function canonicalScholarlyJson(value: unknown): string {
    const sorted = (item: unknown): unknown => {
        if (Array.isArray(item)) return item.map(sorted);
        if (item !== null && typeof item === 'object') return Object.fromEntries(Object.entries(item)
            .filter(([, entry]) => entry !== undefined).sort(([a], [b]) => a.localeCompare(b, 'en')).map(([key, entry]) => [key, sorted(entry)]));
        return item;
    };
    return JSON.stringify(sorted(value));
}

/** One DOI identifies one work even when the source is a URL or an alternate mirror. */
export function canonicalScholarlyDoi(input: string): string {
    const doi = input.trim().replace(/^(?:https?:\/\/(?:dx\.)?doi\.org\/|doi:\s*)/i, '').toLowerCase();
    if (!/^10\.\d{4,9}\/[^\s<>"?#]+$/u.test(doi) || /[\u0000-\u001f]/u.test(doi)) throw new Error('Invalid canonical DOI');
    return doi;
}

const normalizedTitle = (value: string): string => value.normalize('NFKC').replace(/\s+/gu, ' ').trim().toLowerCase();
// Europe PMC adds a terminal sentence period to some titles that JATS and Crossref omit.
// Canonicalize display punctuation in words, preserving numbers, other punctuation and qualifiers.
const bibliographicTitle = (value: string): string => normalizedTitle(value).replace(/[\u2010\u2011]/gu, '-')
    .replace(/(?<=\p{L})\u2013(?=\p{L})/gu, '-').replace(/(?<=\p{L})[\u2018\u2019](?=\p{L})/gu, "'")
    .replace(/\.$/u, '').trimEnd();
const normalizedText = (value: string): string => normalizedTitle(value);
const doiUrl = (doi: string): string => `https://doi.org/${doi}`;
const supportedDois = (values: readonly string[]): Set<string> => {
    const supported = new Set<string>();
    for (const value of values) { try { supported.add(canonicalScholarlyDoi(value)); } catch { /* Unverified raw bibliography stays in the source only. */ } }
    return supported;
};
const validSnapshot = (snapshot: ScholarlySnapshot): boolean => !!snapshot.text.trim() &&
    /^[a-f0-9]{64}$/u.test(snapshot.sha256) && scholarlySha256(snapshot.text) === snapshot.sha256;

function eligibleLicense(value: string): boolean {
    try {
        const url = new URL(value);
        return (url.protocol === 'https:' || url.protocol === 'http:') &&
            /^(?:www\.)?creativecommons\.org$/u.test(url.hostname) && !url.username && !url.password && !url.search && !url.hash &&
            /^\/(?:licenses\/by\/(?:1\.0|2\.0|2\.5|3\.0|4\.0)|publicdomain\/zero\/1\.0)\/?$/u.test(url.pathname);
    } catch { return false; }
}

function sourceReasons(paper: ScholarlyPaper): string[] {
    const reasons: string[] = [];
    if (!validSnapshot(paper.metadata) || !validSnapshot(paper.fullText)) reasons.push('Source snapshot SHA256 mismatch or empty snapshot');
    if (!/^(?:application\/xml|text\/xml)(?:;|$)/iu.test(paper.fullText.contentType)) reasons.push('Full source snapshot is not XML');
    if (!paper.text.trim() || !paper.title.trim() || paper.text.includes('\0')) reasons.push('Missing bounded source text or title');
    if (!Number.isInteger(paper.year) || paper.year < 1500 || paper.year > 9999) reasons.push('Missing valid publication year');
    if (!paper.authors.length || paper.authors.some(author => !author.trim())) reasons.push('Missing named publication authors');
    const kinds = paper.publicationTypes.map(type => type.replace(/[\s_-]/gu, '').toLowerCase());
    if (!kinds.includes('journalarticle') || kinds.some(kind => /preprint|retract|withdraw/u.test(kind)))
        reasons.push('Publication is not an unretracted journal article');
    if (/\b(?:article-type|related-article-type)\s*=\s*["'][^"']*(?:retract|withdraw|concern|correction)/iu.test(paper.fullText.text))
        reasons.push('JATS source signals a retraction, withdrawal, or correction');
    if (!eligibleLicense(paper.licenseUrl)) reasons.push('An explicit CC BY or CC0 license is required');
    const rawLicenses = `${paper.metadata.text}\n${paper.fullText.text}`.match(/https?:\/\/(?:www\.)?creativecommons\.org\/(?:licenses|publicdomain)\/[^\s"'<>]+/giu) ?? [];
    if (rawLicenses.some(url => !eligibleLicense(url.replace(/[).,;]+$/u, '')))) reasons.push('Conflicting or ambiguous source license');
    try {
        const metadataUrl = officialScholarlyUrl(paper.metadata.url), fullTextUrl = officialScholarlyUrl(paper.fullText.url);
        if (metadataUrl.hostname !== 'www.ebi.ac.uk' || metadataUrl.pathname !== '/europepmc/webservices/rest/search' ||
            fullTextUrl.hostname !== 'www.ebi.ac.uk' || fullTextUrl.pathname !== `/europepmc/webservices/rest/${paper.id}/fullTextXML`)
            reasons.push('Source snapshot URL is not the official Europe PMC article endpoint');
        const parsed = parseScholarlyXml(paper.fullText.text), metadata = parseEuropePmcRecord(paper.metadata, paper.id);
        if (parsed.id && parsed.id.replace(/^PMC/i, '') !== paper.id.replace(/^PMC/i, '')) reasons.push('JATS article identifier mismatch');
        if (canonicalScholarlyDoi(paper.doi) !== canonicalScholarlyDoi(parsed.doi) ||
            bibliographicTitle(paper.title) !== bibliographicTitle(parsed.title) || paper.year !== parsed.year ||
            paper.text !== parsed.text || paper.licenseUrl !== parsed.licenseUrl ||
            canonicalScholarlyJson(paper.authors) !== canonicalScholarlyJson(parsed.authors) ||
            canonicalScholarlyJson(paper.references) !== canonicalScholarlyJson(parsed.references))
            reasons.push('Derived paper fields do not match the immutable JATS source');
        if (canonicalScholarlyDoi(metadata.doi) !== canonicalScholarlyDoi(paper.doi) ||
            bibliographicTitle(metadata.title) !== bibliographicTitle(paper.title) || metadata.year !== paper.year ||
            canonicalScholarlyJson(metadata.publicationTypes) !== canonicalScholarlyJson(paper.publicationTypes))
            reasons.push('Derived paper bibliography does not match immutable Europe PMC metadata');
        if (!scholarlyAuthorsAgree(parsed.authors, metadata.authors)) reasons.push('JATS authors do not match immutable Europe PMC authors');
    } catch (error) { reasons.push(`Raw scholarly source cannot be rederived: ${error instanceof Error ? error.message : String(error)}`); }
    return reasons;
}

function registryReasons(record: CrossrefRecord, doi: string): string[] {
    const reasons: string[] = [];
    try { if (canonicalScholarlyDoi(record.doi) !== doi) reasons.push('Crossref canonical DOI mismatch'); }
    catch { reasons.push('Crossref DOI is invalid'); }
    if (!validSnapshot(record.snapshot)) reasons.push('Crossref snapshot SHA256 mismatch');
    if (record.retractionSignal !== false) reasons.push('Crossref reports a retraction or withdrawal signal');
    if (!record.title.trim() || !Number.isInteger(record.year)) reasons.push('Crossref bibliography is incomplete');
    try {
        const url = officialScholarlyUrl(record.snapshot.url), parsed = parseCrossrefRecord(record.snapshot);
        if (url.hostname !== 'api.crossref.org' || !url.pathname.startsWith('/works/') ||
            canonicalScholarlyDoi(decodeURIComponent(url.pathname.slice('/works/'.length))) !== doi)
            reasons.push('Crossref snapshot URL does not identify this DOI');
        const projection = (value: CrossrefRecord) => ({ doi: canonicalScholarlyDoi(value.doi), title: value.title, year: value.year,
            authors: value.authors, type: value.type, retractionSignal: value.retractionSignal, referenceDois: value.referenceDois });
        if (canonicalScholarlyJson(projection(record)) !== canonicalScholarlyJson(projection(parsed)))
            reasons.push('Crossref record fields do not match the immutable registry response');
    } catch (error) { reasons.push(`Raw Crossref response cannot be rederived: ${error instanceof Error ? error.message : String(error)}`); }
    return reasons;
}

/** Registry agreement confirms identity and citation adjacency; scientific truth remains unassessed. */
export async function verifyScholarlyPapers(papers: readonly ScholarlyPaper[], collector: Pick<ScholarlyCollector, 'crossref'>,
    signal: AbortSignal, options: { minimumReferences?: number } = {}): Promise<ScholarlyAdmission> {
    const minimum = options.minimumReferences ?? 2;
    if (!Number.isInteger(minimum) || minimum < 2) throw new Error('At least two distinct bibliographic references are required');
    const accepted: VerifiedScholarlyPaper[] = [], excluded: { id: string; doi: string; reasons: string[] }[] = [];
    const cache = new Map<string, Promise<CrossrefRecord>>();
    const lookup = (doi: string): Promise<CrossrefRecord> => {
        if (!cache.has(doi)) cache.set(doi, collector.crossref(doi, signal));
        return cache.get(doi)!;
    };
    for (const paper of [...papers].sort((a, b) => a.doi.localeCompare(b.doi, 'en') || a.id.localeCompare(b.id, 'en'))) {
        if (signal.aborted) throw signal.reason ?? new Error('Scholarly admission aborted');
        const reasons = sourceReasons(paper);
        let doi: string;
        try { doi = canonicalScholarlyDoi(paper.doi); }
        catch { excluded.push({ id: paper.id, doi: paper.doi, reasons: [...reasons, 'Source DOI is invalid'] }); continue; }
        if (reasons.length) { excluded.push({ id: paper.id, doi, reasons }); continue; }
        let record: CrossrefRecord;
        try { record = await lookup(doi); }
        catch { if (signal.aborted) throw signal.reason ?? new Error('Scholarly admission aborted');
            excluded.push({ id: paper.id, doi, reasons: ['Crossref lookup failed; paper quarantined'] }); continue; }
        reasons.push(...registryReasons(record, doi));
        if (record.type !== 'journal-article') reasons.push('Crossref type is not journal-article');
        if (bibliographicTitle(record.title) !== bibliographicTitle(paper.title) || record.year !== paper.year)
            reasons.push('Source title or year does not exactly match Crossref');
        const adjacency = new Set<string>();
        for (const reference of record.referenceDois) { try { adjacency.add(canonicalScholarlyDoi(reference)); } catch { /* Unresolved registry references supply no support. */ } }
        const references = new Map<string, string | undefined>();
        for (const reference of paper.references) {
            try {
                const referenceDoi = canonicalScholarlyDoi(reference.doi);
                if (referenceDoi !== doi && adjacency.has(referenceDoi)) references.set(referenceDoi, reference.title);
            } catch { /* A malformed or unlinked reference supplies no support. */ }
        }
        if (references.size < minimum) reasons.push(`Fewer than ${minimum} distinct source references confirmed in Crossref adjacency`);
        const citedReferences: BibliographicReference[] = [];
        // Only these deterministic selected links are certified; the full bibliography stays in the raw source.
        if (!reasons.length) for (const [referenceDoi, title] of [...references].sort(([a], [b]) => a.localeCompare(b, 'en')).slice(0, minimum)) {
            try {
                const reference = await lookup(referenceDoi), invalid = registryReasons(reference, referenceDoi);
                if (title && bibliographicTitle(title) !== bibliographicTitle(reference.title)) invalid.push('Reference title mismatch');
                if (invalid.length) reasons.push(`Reference ${referenceDoi}: ${invalid.join('; ')}`);
                else citedReferences.push({ doi: referenceDoi, url: doiUrl(referenceDoi), title: reference.title,
                    metadataSha256: reference.snapshot.sha256, snapshot: reference.snapshot });
            } catch { if (signal.aborted) throw signal.reason ?? new Error('Scholarly admission aborted');
                reasons.push(`Reference ${referenceDoi}: Crossref lookup failed; paper quarantined`); }
        }
        if (citedReferences.length < minimum && !reasons.length) reasons.push(`Fewer than ${minimum} verified references`);
        if (reasons.length) excluded.push({ id: paper.id, doi, reasons: [...new Set(reasons)] });
        else accepted.push({ ...paper, doi, crossref: { ...record, doi }, verification: {
            policy: 'scholarly-attribution-v1', bibliographyMatched: true, licenseEligible: true, sourceHashVerified: true,
            noRetractionSignal: true, citedReferences, scientificTruth: 'not-assessed' } });
    }
    return { accepted, excluded };
}

function paperIdentity(paper: VerifiedScholarlyPaper): unknown {
    return { id: paper.id, doi: paper.doi, title: paper.title, authors: paper.authors, year: paper.year,
        licenseUrl: paper.licenseUrl, publicationTypes: [...paper.publicationTypes].sort(), extractedTextSha256: scholarlySha256(paper.text),
        metadataSha256: paper.metadata.sha256, fullTextSha256: paper.fullText.sha256,
        crossrefSha256: paper.crossref.snapshot.sha256, references: paper.references,
        verification: { ...paper.verification, citedReferences: paper.verification.citedReferences.map(reference => ({
            doi: reference.doi, url: reference.url, title: reference.title, metadataSha256: reference.metadataSha256 })) } };
}

function verifiedReasons(paper: VerifiedScholarlyPaper): string[] {
    const doi = canonicalScholarlyDoi(paper.doi), reasons = [...sourceReasons(paper), ...registryReasons(paper.crossref, doi)];
    const verification = paper.verification;
    if (paper.crossref.type !== 'journal-article' || bibliographicTitle(paper.crossref.title) !== bibliographicTitle(paper.title) ||
        paper.crossref.year !== paper.year) reasons.push('Admitted paper no longer matches its registry bibliography');
    if (verification.policy !== 'scholarly-attribution-v1' || verification.bibliographyMatched !== true || verification.licenseEligible !== true ||
        verification.sourceHashVerified !== true || verification.noRetractionSignal !== true || verification.scientificTruth !== 'not-assessed')
        reasons.push('Admitted verification policy is invalid');
    const seen = new Set<string>(), sourceReferences = supportedDois(paper.references.map(reference => reference.doi)),
        registryReferences = supportedDois(paper.crossref.referenceDois);
    for (const reference of verification.citedReferences) {
        const referenceDoi = canonicalScholarlyDoi(reference.doi);
        if (seen.has(referenceDoi) || referenceDoi === doi || !sourceReferences.has(referenceDoi) || !registryReferences.has(referenceDoi))
            reasons.push('Admitted bibliographic references are duplicated, self-citations, or absent from source adjacency');
        seen.add(referenceDoi);
        try {
            const record = parseCrossrefRecord(reference.snapshot);
            if (registryReasons(record, referenceDoi).length || reference.metadataSha256 !== reference.snapshot.sha256 ||
                reference.title !== record.title || reference.url !== doiUrl(referenceDoi)) reasons.push('Admitted citation does not match its registry source');
            const sourceTitle = paper.references.find(source => { try { return canonicalScholarlyDoi(source.doi) === referenceDoi; }
                catch { return false; } })?.title;
            if (sourceTitle && bibliographicTitle(sourceTitle) !== bibliographicTitle(record.title)) reasons.push('Admitted source reference title mismatch');
        } catch { reasons.push('Admitted citation registry source cannot be rederived'); }
    }
    if (seen.size < 2) reasons.push('Admitted paper requires two distinct verified bibliographic references');
    return reasons;
}

function passageIdentity(paper: VerifiedScholarlyPaper, quote: string, start: number, end: number): string {
    return scholarlySha256(canonicalScholarlyJson({ doi: paper.doi, quote, start, end,
        sourceSha256: paper.fullText.sha256, extractedTextSha256: scholarlySha256(paper.text) }));
}

function boundedUnits(paper: VerifiedScholarlyPaper): CitedPassage[] {
    const seen = new Set<string>();
    return evidenceUnits(paper.text).filter(unit => {
        const words = unit.text.split(/\s+/u).length;
        if (words < 12 || words > 80 || unit.text.length > 768 || seen.has(normalizeEvidence(unit.text))) return false;
        seen.add(normalizeEvidence(unit.text)); return true;
    }).map(unit => ({ id: passageIdentity(paper, unit.text, unit.start, unit.end), doi: paper.doi, quote: unit.text,
        start: unit.start, end: unit.end, citationUrl: doiUrl(paper.doi), sourceSha256: paper.fullText.sha256,
        exactAgreementDois: [], support: 'attributed-source-quote' }));
}

const render = (passages: readonly CitedPassage[]): string => passages.map(passage => `According to paper ${passage.doi}, ${passage.quote}`).join('\n') + '\n';
const benchmarkIdentity = (benchmark: Omit<ScholarlyBenchmark, 'sha256'>): unknown => ({ version: benchmark.version,
    papers: benchmark.papers.map(paperIdentity), passages: benchmark.passages, developmentDois: benchmark.developmentDois,
    testDois: benchmark.testDois, developmentText: benchmark.developmentText, testText: benchmark.testText });

function validateBenchmark(benchmark: ScholarlyBenchmark): void {
    if (benchmark.version !== 1 || scholarlySha256(canonicalScholarlyJson(benchmarkIdentity(benchmark))) !== benchmark.sha256)
        throw new Error('Pinned scholarly benchmark checksum mismatch');
    const papers = new Map(benchmark.papers.map(paper => [paper.doi, paper]));
    const development = new Set(benchmark.developmentDois), test = new Set(benchmark.testDois);
    if (!development.size || !test.size || [...development].some(doi => test.has(doi)) ||
        development.size !== benchmark.developmentDois.length || test.size !== benchmark.testDois.length || papers.size !== benchmark.papers.length)
        throw new Error('Pinned scholarly benchmark must contain distinct held-out works');
    if ([...development, ...test].some(doi => !papers.has(doi))) throw new Error('Pinned scholarly benchmark is missing a held-out source');
    for (const paper of papers.values()) if (canonicalScholarlyDoi(paper.doi) !== paper.doi || verifiedReasons(paper).length ||
        !development.has(paper.doi) && !test.has(paper.doi)) throw new Error('Invalid pinned scholarly source');
    const developmentHashes = new Set([...development].map(doi => scholarlySha256(normalizedText(papers.get(doi)!.text))));
    if ([...test].some(doi => developmentHashes.has(scholarlySha256(normalizedText(papers.get(doi)!.text)))))
        throw new Error('Pinned scholarly benchmark mirrors cross source splits');
    const splitKeys = { development: new Set<string>(), test: new Set<string>() };
    const passageIds = new Set<string>();
    for (const passage of benchmark.passages) {
        const paper = papers.get(passage.doi);
        if (!paper || passageIds.has(passage.id) || !boundedUnits(paper).some(candidate => candidate.id === passage.id) ||
            paper.text.slice(passage.start, passage.end) !== passage.quote || passage.id !== passageIdentity(paper, passage.quote, passage.start, passage.end) ||
            passage.sourceSha256 !== paper.fullText.sha256 || passage.citationUrl !== doiUrl(paper.doi) || passage.support !== 'attributed-source-quote')
            throw new Error('Pinned scholarly passage does not match its complete source span');
        passageIds.add(passage.id);
        splitKeys[development.has(passage.doi) ? 'development' : 'test'].add(normalizeEvidence(passage.quote));
    }
    if (!splitKeys.development.size || !splitKeys.test.size || [...splitKeys.development].some(key => splitKeys.test.has(key)) ||
        render(benchmark.passages.filter(passage => development.has(passage.doi))) !== benchmark.developmentText ||
        render(benchmark.passages.filter(passage => test.has(passage.doi))) !== benchmark.testText)
        throw new Error('Pinned scholarly benchmark texts drift or duplicate complete units across splits');
}

/** Groups mirrors by canonical work and normalized full text, never by a claimed confidence probability. */
export function buildScholarlyCorpus(input: readonly VerifiedScholarlyPaper[], prior?: ScholarlyBenchmark,
    options: { maximumPassagesPerPaper?: number; minimumPapers?: number } = {}): { corpus: ScholarlyCorpus; benchmark: ScholarlyBenchmark } {
    const maximum = options.maximumPassagesPerPaper ?? 8, minimum = options.minimumPapers ?? 6;
    if (!Number.isInteger(maximum) || maximum < 1 || maximum > 128 || !Number.isInteger(minimum) || minimum < 4)
        throw new Error('Scholarly corpus requires bounded passages and at least four independent works');
    if (prior) validateBenchmark(prior);
    const byDoi = new Map<string, VerifiedScholarlyPaper>();
    for (const paper of [...input].sort((a, b) => a.fullText.sha256.localeCompare(b.fullText.sha256, 'en') || a.id.localeCompare(b.id, 'en'))) {
        const doi = canonicalScholarlyDoi(paper.doi);
        if (verifiedReasons(paper).length) throw new Error(`Unverified scholarly source: ${doi}`);
        if (!byDoi.has(doi)) byDoi.set(doi, { ...paper, doi });
    }
    // The first usable batch defines immutable held-out source snapshots, even if later downloads change.
    if (prior) for (const paper of prior.papers) byDoi.set(paper.doi, paper);
    const papers = [...byDoi.values()].sort((a, b) => a.doi.localeCompare(b.doi, 'en'));
    const allUnits = new Map(papers.map(paper => [paper.doi, boundedUnits(paper)]));
    const eligible = papers.filter(paper => allUnits.get(paper.doi)!.length);
    const grouped = new Map<string, VerifiedScholarlyPaper[]>();
    for (const paper of eligible) {
        const key = scholarlySha256(normalizedText(paper.text));
        if (!grouped.has(key)) grouped.set(key, []);
        grouped.get(key)!.push(paper);
    }
    const groups = [...grouped.values()].sort((a, b) => scholarlySha256(a.map(paper => paper.doi).sort().join('\n'))
        .localeCompare(scholarlySha256(b.map(paper => paper.doi).sort().join('\n')), 'en'));
    if (groups.length < minimum) throw new Error(`Scholarly corpus requires ${minimum} independent source groups with complete bounded excerpts`);
    const development = new Set(prior?.developmentDois ?? groups.at(-2)!.map(paper => paper.doi));
    const test = new Set(prior?.testDois ?? groups.at(-1)!.map(paper => paper.doi));
    const heldoutTextHashes = new Set(eligible.filter(paper => development.has(paper.doi) || test.has(paper.doi))
        .map(paper => scholarlySha256(normalizedText(paper.text))));
    const allHeldoutKeys = new Set(eligible.filter(paper => development.has(paper.doi) || test.has(paper.doi))
        .flatMap(paper => allUnits.get(paper.doi)!.map(passage => normalizeEvidence(passage.quote))));
    const devKeys = new Set([...development].flatMap(doi => allUnits.get(doi)!.map(passage => normalizeEvidence(passage.quote))));
    const testKeys = new Set([...test].flatMap(doi => allUnits.get(doi)!.map(passage => normalizeEvidence(passage.quote))));
    const crossHeldout = new Set([...devKeys].filter(key => testKeys.has(key)));
    const agreement = new Map<string, VerifiedScholarlyPaper[]>();
    for (const paper of eligible) for (const passage of allUnits.get(paper.doi)!) {
        if (!agreement.has(passage.quote)) agreement.set(passage.quote, []);
        agreement.get(passage.quote)!.push(paper);
    }
    const passages: CitedPassage[] = [], splits = { train: [] as string[], development: [] as string[], test: [] as string[] };
    const pinned = new Map(prior?.passages.map(passage => [passage.doi, [] as CitedPassage[]]) ?? []);
    if (prior) for (const passage of prior.passages) pinned.get(passage.doi)!.push(passage);
    for (const paper of eligible) {
        const split = development.has(paper.doi) ? 'development' : test.has(paper.doi) ? 'test' : 'train';
        if (split === 'train' && heldoutTextHashes.has(scholarlySha256(normalizedText(paper.text)))) continue;
        const candidates = prior && split !== 'train' ? pinned.get(paper.doi)! : allUnits.get(paper.doi)!.filter(passage =>
            split === 'train' ? !allHeldoutKeys.has(normalizeEvidence(passage.quote)) : !crossHeldout.has(normalizeEvidence(passage.quote))).slice(0, maximum);
        for (const passage of candidates) {
            const supported = prior && split !== 'train' ? passage : { ...passage, exactAgreementDois: [...new Set((agreement.get(passage.quote) ?? [])
                .filter(other => other.doi !== paper.doi && !scholarlyAuthorsAgree([other.authors[0]!], [paper.authors[0]!]))
                .map(other => other.doi))].sort() };
            passages.push(supported); splits[split].push(supported.id);
        }
    }
    const selectedDois = new Set(passages.map(passage => passage.doi));
    const selectedPapers = papers.filter(paper => selectedDois.has(paper.doi));
    const selectedGroups = new Set(selectedPapers.map(paper => scholarlySha256(normalizedText(paper.text))));
    if (selectedGroups.size < minimum) throw new Error(`Scholarly corpus requires ${minimum} independent source groups after duplicate-unit exclusions`);
    const trainGroups = new Set(selectedPapers.filter(paper => !development.has(paper.doi) && !test.has(paper.doi))
        .map(paper => scholarlySha256(normalizedText(paper.text))));
    if (trainGroups.size < 2 || !splits.development.length || !splits.test.length)
        throw new Error('Scholarly corpus lacks two independent training groups and nonoverlapping held-out excerpts');
    const passageById = new Map(passages.map(passage => [passage.id, passage]));
    const texts = { train: render(splits.train.map(id => passageById.get(id)!)),
        development: prior?.developmentText ?? render(splits.development.map(id => passageById.get(id)!)),
        test: prior?.testText ?? render(splits.test.map(id => passageById.get(id)!)) };
    const base = { version: 1 as const, policy: 'scholarly-attribution-v1' as const, papers: selectedPapers, passages, splits, texts };
    const corpus: ScholarlyCorpus = { ...base, sha256: scholarlySha256(canonicalScholarlyJson({ ...base, papers: selectedPapers.map(paperIdentity) })) };
    const benchmarkBase: Omit<ScholarlyBenchmark, 'sha256'> = { version: 1, papers: selectedPapers.filter(paper => development.has(paper.doi) || test.has(paper.doi)),
        passages: passages.filter(passage => development.has(passage.doi) || test.has(passage.doi)),
        developmentDois: [...development].sort(), testDois: [...test].sort(), developmentText: texts.development, testText: texts.test };
    const benchmark: ScholarlyBenchmark = prior ?? { ...benchmarkBase, sha256: scholarlySha256(canonicalScholarlyJson(benchmarkIdentity(benchmarkBase))) };
    validateBenchmark(benchmark);
    return { corpus, benchmark };
}
