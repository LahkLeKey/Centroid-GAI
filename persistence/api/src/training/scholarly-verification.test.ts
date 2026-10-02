import assert from 'node:assert/strict';
import test from 'node:test';
import { normalizeEvidence } from '../chat/evidence.ts';
import { scholarlyFixtureCollector, scholarlyFixtureCrossref, scholarlyFixturePaper, scholarlyFixtureSnapshot } from './scholarly-fixtures.ts';
import type { CrossrefRecord, ScholarlyCollector, ScholarlyPaper, VerifiedScholarlyPaper } from './scholarly-types.ts';
import { buildScholarlyCorpus, canonicalScholarlyDoi, scholarlySha256, verifyScholarlyPapers } from './scholarly-verification.ts';

const signal = () => new AbortController().signal;
const papers = (count = 6): ScholarlyPaper[] => Array.from({ length: count }, (_, index) => scholarlyFixturePaper(index));
async function admitted(source: readonly ScholarlyPaper[] = papers()): Promise<readonly VerifiedScholarlyPaper[]> {
    const result = await verifyScholarlyPapers(source, scholarlyFixtureCollector(source), signal());
    assert.deepEqual(result.excluded, []); return result.accepted;
}
function overridden(source: readonly ScholarlyPaper[], work: CrossrefRecord): Pick<ScholarlyCollector, 'crossref'> {
    const collector = scholarlyFixtureCollector(source);
    return { crossref(doi, abort) { return doi === work.doi ? Promise.resolve(work) : collector.crossref(doi, abort); } };
}

test('admission requires rederived source hashes, exact bibliography and two registry-linked citation snapshots', async () => {
    const [paper] = papers(1), result = await verifyScholarlyPapers([paper!], scholarlyFixtureCollector([paper!]), signal());
    assert.equal(result.accepted.length, 1);
    const verification = result.accepted[0]!.verification;
    assert.equal(verification.scientificTruth, 'not-assessed');
    assert.equal(verification.citedReferences.length, 2);
    for (const reference of verification.citedReferences) {
        assert.equal(reference.metadataSha256, scholarlySha256(reference.snapshot.text));
        assert.equal(reference.url, `https://doi.org/${reference.doi}`);
    }
    assert.equal(canonicalScholarlyDoi('https://doi.org/10.1234/EXAMPLE'), '10.1234/example');
    assert.throws(() => canonicalScholarlyDoi('javascript:example'));
    assert.throws(() => canonicalScholarlyDoi('10.1234/example?untrusted'));
});

test('wrong raw hash and injected derived text cannot receive automatic admission', async () => {
    const paper = scholarlyFixturePaper(0);
    for (const bad of [{ ...paper, fullText: { ...paper.fullText, sha256: 'a'.repeat(64) } },
        { ...paper, text: paper.text.replace('controlled', 'uncontrolled') },
        { ...paper, authors: ['Invented Author'] }, { ...paper, doi: '10.1234/forged' }]) {
        const result = await verifyScholarlyPapers([bad], scholarlyFixtureCollector([paper]), signal());
        assert.equal(result.accepted.length, 0); assert.equal(result.excluded.length, 1);
    }
});

test('standalone admission independently checks raw Europe PMC authors and permits bibliography initials', async () => {
    const paper = scholarlyFixturePaper(0, { author: 'Alice Beth Jones' }), metadata = JSON.parse(paper.metadata.text);
    const withAuthor = (authorString: string) => {
        metadata.resultList.result[0].authorString = authorString;
        return { ...paper, metadata: scholarlyFixtureSnapshot(paper.metadata.url, JSON.stringify(metadata)) };
    };
    for (const author of ['A B Jones', 'Jones AB', 'Alice Beth Jones'])
        assert.equal((await verifyScholarlyPapers([withAuthor(author)], scholarlyFixtureCollector([paper]), signal())).accepted.length, 1);
    for (const author of ['Alice Beth Smith', 'Andrew Beth Jones', 'Unrelated Researcher', 'Alice Beth Jones, Extra Author']) {
        const result = await verifyScholarlyPapers([withAuthor(author)], scholarlyFixtureCollector([paper]), signal());
        assert.equal(result.accepted.length, 0); assert.match(result.excluded[0]!.reasons.join(' '), /authors do not match/);
    }
});

test('conflicting title, year, article type or retraction notice fails closed', async () => {
    const paper = scholarlyFixturePaper(0);
    for (const update of [{ title: `${paper.title} unrelated` }, { year: 2023 }, { type: 'posted-content' }, { retracted: true }]) {
        const record = scholarlyFixtureCrossref(paper.doi, { title: paper.title, references: paper.references.map(ref => ref.doi), ...update });
        const result = await verifyScholarlyPapers([paper], overridden([paper], record), signal());
        assert.equal(result.accepted.length, 0); assert.equal(result.excluded.length, 1);
    }
    const forged = { ...scholarlyFixtureCrossref(paper.doi, { title: paper.title, retracted: true }), retractionSignal: false };
    assert.equal((await verifyScholarlyPapers([paper], overridden([paper], forged), signal())).accepted.length, 0);
});

test('bibliographic titles allow one optional terminal period while preserving words, numbers and qualifiers', async () => {
    // This title reproduces a punctuation discrepancy observed between the public Europe PMC and JATS projections.
    const paper = scholarlyFixturePaper(0, { title: 'Machine learning algorithm validation with a limited sample size' });
    const metadata = JSON.parse(paper.metadata.text);
    const withMetadataTitle = (title: string) => {
        metadata.resultList.result[0].title = title;
        return { ...paper, metadata: scholarlyFixtureSnapshot(paper.metadata.url, JSON.stringify(metadata)) };
    };
    const terminal = withMetadataTitle(`${paper.title}.`), registry = scholarlyFixtureCrossref(paper.doi,
        { title: `  ${paper.title.toUpperCase()}.  `, references: paper.references.map(reference => reference.doi) });
    assert.equal((await verifyScholarlyPapers([terminal], overridden([paper], registry), signal())).accepted.length, 1);
    for (const title of [paper.title.replace('limited', 'unlimited'), `${paper.title} 2.`, `${paper.title}..`,
        paper.title.replace('sample size', 'sample-size'), `${paper.title}!`]) {
        assert.equal((await verifyScholarlyPapers([withMetadataTitle(title)], scholarlyFixtureCollector([paper]), signal())).accepted.length, 0);
        const changed = scholarlyFixtureCrossref(paper.doi, { title, references: paper.references.map(reference => reference.doi) });
        assert.equal((await verifyScholarlyPapers([paper], overridden([paper], changed), signal())).accepted.length, 0);
    }
});

test('bibliographic display hyphens and apostrophes agree without removing punctuation or changing complete quotes', async () => {
    const title = "Model-guided validation in Alzheimer's disease with a limited sample size", paper = scholarlyFixturePaper(0, { title });
    const metadata = JSON.parse(paper.metadata.text);
    metadata.resultList.result[0].title = title.replace('-', '\u2010').replace("'", '\u2019') + '.';
    const display = { ...paper, metadata: scholarlyFixtureSnapshot(paper.metadata.url, JSON.stringify(metadata)) };
    const work = scholarlyFixtureCrossref(paper.doi, { title: title.replace('-', '\u2013'), references: paper.references.map(ref => ref.doi) });
    const admitted = await verifyScholarlyPapers([display], overridden([paper], work), signal());
    assert.equal(admitted.accepted.length, 1); assert.equal(admitted.accepted[0]!.text, paper.text);
    for (const altered of [title.replace('-', '\u2014'), title.replace('-', '\u2212'), title.replace('-', ' '), title.replace("'", ''),
        `${title} 1\u20132`]) {
        const changed = scholarlyFixtureCrossref(paper.doi, { title: altered, references: paper.references.map(ref => ref.doi) });
        assert.equal((await verifyScholarlyPapers([paper], overridden([paper], changed), signal())).accepted.length, 0);
    }
    const numeric = scholarlyFixturePaper(1, { title: 'Model validation using 1-2 samples' }), range = scholarlyFixtureCrossref(numeric.doi,
        { title: 'Model validation using 1\u20132 samples', references: numeric.references.map(ref => ref.doi) });
    assert.equal((await verifyScholarlyPapers([numeric], overridden([numeric], range), signal())).accepted.length, 0);
});

test('source licenses and publication kinds stop restrictive, ambiguous and preprint material', async () => {
    const paper = scholarlyFixturePaper(0);
    const ncRaw = paper.fullText.text.replace('/licenses/by/4.0/', '/licenses/by-nc/4.0/');
    const nc = { ...paper, fullText: scholarlyFixtureSnapshot(paper.fullText.url, ncRaw, 'application/xml') };
    const unknown = { ...paper, licenseUrl: 'https://publisher.example/license' };
    const preprint = scholarlyFixturePaper(1, { publicationTypes: ['JournalArticle', 'Preprint'] });
    const result = await verifyScholarlyPapers([nc, unknown, preprint], scholarlyFixtureCollector([paper, preprint]), signal());
    assert.equal(result.accepted.length, 0); assert.equal(result.excluded.length, 3);
    const review = scholarlyFixturePaper(2, { publicationTypes: ['JournalArticle', 'Review'] });
    assert.equal((await admitted([review])).length, 1);
});

test('reference adjacency, distinct DOI identity, source title and non-retraction are checked', async () => {
    const paper = scholarlyFixturePaper(0);
    for (const references of [[], [paper.references[0]!.doi], [paper.references[0]!.doi, paper.references[0]!.doi],
        [paper.doi, paper.references[0]!.doi]]) {
        const work = scholarlyFixtureCrossref(paper.doi, { title: paper.title, references });
        assert.equal((await verifyScholarlyPapers([paper], overridden([paper], work), signal())).accepted.length, 0);
    }
    const badReference = scholarlyFixtureCrossref(paper.references[0]!.doi, { retracted: true });
    assert.equal((await verifyScholarlyPapers([paper], overridden([paper], badReference), signal())).accepted.length, 0);
    const referenceTitle = paper.fullText.text.replace(`<pub-id pub-id-type="doi">${paper.references[0]!.doi}</pub-id>`,
        `<article-title>Fabricated citation title</article-title><pub-id pub-id-type="doi">${paper.references[0]!.doi}</pub-id>`);
    const titled = { ...paper, fullText: scholarlyFixtureSnapshot(paper.fullText.url, referenceTitle, 'application/xml'),
        references: [{ ...paper.references[0]!, title: 'Fabricated citation title' }, paper.references[1]!] };
    assert.equal((await verifyScholarlyPapers([titled], scholarlyFixtureCollector([paper]), signal())).accepted.length, 0);
});

test('source or reference API failure quarantines the paper and an abort interrupts verification', async () => {
    const paper = scholarlyFixturePaper(0), collector = scholarlyFixtureCollector([paper]);
    for (const failing of [paper.doi, paper.references[0]!.doi]) {
        const result = await verifyScholarlyPapers([paper], { async crossref(doi, abort) {
            if (doi === failing) throw new Error('API unavailable'); return collector.crossref(doi, abort);
        } }, signal());
        assert.equal(result.accepted.length, 0); assert.match(result.excluded[0]!.reasons.join(' '), /quarantined/);
    }
    const controller = new AbortController(); controller.abort(new Error('stop'));
    await assert.rejects(verifyScholarlyPapers([paper], collector, controller.signal), /stop/);
    await assert.rejects(verifyScholarlyPapers([paper], collector, signal(), { minimumReferences: 1 }), /two distinct/);
});

test('only a bounded deterministic subset of matched bibliography links is certified', async () => {
    const references = Array.from({ length: 30 }, (_, index) => `10.9999/reference-${index.toString().padStart(2, '0')}`),
        paper = scholarlyFixturePaper(0, { references: [...references].reverse() }), collector = scholarlyFixtureCollector([paper]), calls: string[] = [];
    const result = await verifyScholarlyPapers([paper], { crossref(doi, abort) { calls.push(doi); return collector.crossref(doi, abort); } }, signal());
    assert.deepEqual(calls, [paper.doi, references[0], references[1]]);
    assert.equal(result.accepted.length, 1);
    assert.deepEqual(result.accepted[0]!.verification.citedReferences.map(reference => reference.doi), references.slice(0, 2));
    assert.equal(result.accepted[0]!.references.length, 30);
    assert.equal(result.accepted[0]!.crossref.referenceDois.length, 30);
    const firstInvalid = await verifyScholarlyPapers([paper], { crossref(doi, abort) {
        return doi === references[0] ? Promise.resolve(scholarlyFixtureCrossref(doi, { retracted: true })) : collector.crossref(doi, abort);
    } }, signal());
    assert.equal(firstInvalid.accepted.length, 0);
});

test('unverified legacy and unsupported DOI forms remain raw without poisoning admitted corpus construction', async () => {
    const legacy = '10.1234/abcd(1997)12:3<123::aid-test>3.0.co;2-e', unsupported = '10.1234/quote"unsupported',
        standard = scholarlyFixturePaper(20), paper = scholarlyFixturePaper(20,
            { references: [...standard.references.map(reference => reference.doi), legacy] }), collector = scholarlyFixtureCollector([standard]);
    const work = scholarlyFixtureCrossref(paper.doi, { title: paper.title,
        references: [...paper.references.map(reference => reference.doi), legacy, unsupported] });
    const admission = await verifyScholarlyPapers([paper], { crossref(doi, abort) {
        return doi === paper.doi ? Promise.resolve(work) : collector.crossref(doi, abort);
    } }, signal());
    assert.deepEqual(admission.excluded, []); assert.equal(admission.accepted.length, 1);
    assert(admission.accepted[0]!.fullText.text.includes('10.1234/abcd(1997)12:3&lt;123::aid-test&gt;3.0.co;2-e'));
    assert(admission.accepted[0]!.crossref.snapshot.text.includes('aid-test'));
    assert(admission.accepted[0]!.crossref.referenceDois.includes(unsupported));
    assert(admission.accepted[0]!.verification.citedReferences.every(reference => reference.doi !== legacy && reference.doi !== unsupported));
    const initial = await admitted(), benchmark = buildScholarlyCorpus(initial).benchmark,
        corpus = buildScholarlyCorpus([...initial, ...admission.accepted], benchmark).corpus;
    assert(corpus.papers.some(source => source.doi === paper.doi));
});

test('fresh corpora have reproducible hashes, complete exact source spans and separated source groups', async () => {
    const accepted = await admitted(), first = buildScholarlyCorpus(accepted), second = buildScholarlyCorpus([...accepted].reverse());
    assert.equal(first.corpus.sha256, second.corpus.sha256); assert.equal(first.benchmark.sha256, second.benchmark.sha256);
    assert.deepEqual(Object.fromEntries(Object.entries(first.corpus.splits).map(([key, value]) => [key, value.length])), { train: 4, development: 1, test: 1 });
    const sources = new Map(first.corpus.papers.map(paper => [paper.doi, paper]));
    for (const passage of first.corpus.passages) {
        assert.equal(sources.get(passage.doi)!.text.slice(passage.start, passage.end), passage.quote);
        assert.equal(passage.sourceSha256, sources.get(passage.doi)!.fullText.sha256);
        assert.equal(passage.citationUrl, `https://doi.org/${passage.doi}`);
        assert.equal(passage.support, 'attributed-source-quote');
        assert.match(Object.values(first.corpus.texts).join('\n'), new RegExp(`According to paper ${passage.doi.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')},`));
    }
    const refresh = structuredClone(accepted);
    for (const paper of refresh) {
        (paper.metadata as { fetchedAt: string }).fetchedAt = '2026-02-01T00:00:00.000Z';
        (paper.fullText as { fetchedAt: string }).fetchedAt = '2026-02-01T00:00:00.000Z';
        (paper.crossref.snapshot as { fetchedAt: string }).fetchedAt = '2026-02-01T00:00:00.000Z';
        for (const ref of paper.verification.citedReferences) (ref.snapshot as { fetchedAt: string }).fetchedAt = '2026-02-01T00:00:00.000Z';
    }
    assert.equal(buildScholarlyCorpus(refresh).corpus.sha256, first.corpus.sha256);
});

test('DOI mirrors deduplicate and complete-text mirrors remain one source split', async () => {
    const original = papers(), sameWork = scholarlyFixturePaper(20, { doi: original[0]!.doi, title: original[0]!.title,
        author: original[0]!.authors[0]!, quote: original[0]!.text }),
        sameText = scholarlyFixturePaper(21, { quote: original[1]!.text });
    const corpus = buildScholarlyCorpus(await admitted([...original, sameWork, sameText])).corpus;
    assert.equal(corpus.papers.filter(paper => paper.doi === original[0]!.doi).length, 1);
    const splitFor = (doi: string) => Object.entries(corpus.splits).find(([, ids]) => ids.some(id => corpus.passages.find(passage => passage.id === id)!.doi === doi))?.[0];
    assert.equal(splitFor(sameText.doi), splitFor(original[1]!.doi));
    const mirrors = Array.from({ length: 6 }, (_, index) => scholarlyFixturePaper(index, { quote: original[0]!.text }));
    assert.throws(() => buildScholarlyCorpus([]), /independent/);
    const mirrored = await admitted(mirrors);
    assert.throws(() => buildScholarlyCorpus(mirrored), /independent source groups/);
});

test('later corpus growth pins exact benchmark bytes and blocks shared held-out complete units', async () => {
    const accepted = await admitted(), first = buildScholarlyCorpus(accepted), pinned = first.benchmark;
    const quote = pinned.passages[0]!.quote;
    const overlap = scholarlyFixturePaper(20, { quote, secondQuote: 'An additional synthetic experimental paragraph provides its own distinct source attribution for later training without changing the fixed benchmark.' });
    const mirror = scholarlyFixturePaper(21, { quote });
    const extra = await admitted([overlap, mirror, scholarlyFixturePaper(22)]);
    const next = buildScholarlyCorpus([...accepted, ...extra], pinned);
    assert.deepEqual(next.benchmark, pinned);
    assert.equal(next.corpus.texts.development, first.corpus.texts.development); assert.equal(next.corpus.texts.test, first.corpus.texts.test);
    assert(!next.corpus.texts.train.includes(quote));
    const train = new Set(next.corpus.splits.train), heldoutDois = new Set([...pinned.developmentDois, ...pinned.testDois]);
    assert(next.corpus.passages.filter(passage => train.has(passage.id)).every(passage => !heldoutDois.has(passage.doi)));
    const heldoutKeys = new Set(pinned.passages.map(passage => normalizeEvidence(passage.quote)));
    assert(next.corpus.passages.filter(passage => train.has(passage.id)).every(passage => !heldoutKeys.has(normalizeEvidence(passage.quote))));
    assert(next.corpus.passages.some(passage => passage.doi === overlap.doi));
    assert(!next.corpus.passages.some(passage => passage.doi === mirror.doi));
});

test('agreement names distinct works and distinct known lead authors without claiming scientific truth', async () => {
    const initial = await admitted(), pinned = buildScholarlyCorpus(initial).benchmark;
    const quote = 'The shared synthetic experimental evidence unit records an attributable observation with all its qualifiers preserved for reproducible software verification.';
    const shared = [scholarlyFixturePaper(20, { quote }), scholarlyFixturePaper(21, { quote }),
        scholarlyFixturePaper(22, { quote, author: 'Researcher 20' })];
    const corpus = buildScholarlyCorpus([...initial, ...await admitted(shared)], pinned).corpus;
    const passage = corpus.passages.find(passage => passage.doi === shared[0]!.doi)!;
    assert.deepEqual(passage.exactAgreementDois, [shared[1]!.doi]);
    assert.equal(passage.support, 'attributed-source-quote');
    const names = [scholarlyFixturePaper(30, { quote, author: 'Alice Beth Jones' }), scholarlyFixturePaper(31, { quote, author: 'A. B. Jones' }),
        scholarlyFixturePaper(32, { quote, author: 'Andrew Beth Jones' })];
    const aliases = buildScholarlyCorpus([...initial, ...await admitted(names)], pinned).corpus;
    assert.deepEqual(aliases.passages.find(unit => unit.doi === names[0]!.doi)!.exactAgreementDois, [names[2]!.doi]);
});

test('corpus excludes incomplete overlong units and rejects tampered pinned snapshots or citations', async () => {
    const accepted = await admitted(), built = buildScholarlyCorpus(accepted);
    const tampered = structuredClone(built.benchmark);
    (tampered as { developmentText: string }).developmentText += 'Injected scientific certainty.';
    assert.throws(() => buildScholarlyCorpus(accepted, tampered), /checksum/);
    const forged = structuredClone(accepted);
    (forged[0]!.verification.citedReferences[0]!.snapshot as { text: string }).text = '{}';
    assert.throws(() => buildScholarlyCorpus(forged), /Unverified/);
    const short = scholarlyFixturePaper(20, { quote: 'Too short.' });
    const long = scholarlyFixturePaper(21, { quote: Array.from({ length: 90 }, () => 'longword').join(' ') + '.' });
    const corpus = buildScholarlyCorpus([...accepted, ...await admitted([short, long])], built.benchmark).corpus;
    assert(!corpus.papers.some(paper => paper.doi === short.doi || paper.doi === long.doi));
});
