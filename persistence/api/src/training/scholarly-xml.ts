import type { ScholarlyReference } from './scholarly-types.ts';

interface XmlNode { name: string; attributes: Record<string, string>; children: (XmlNode | string)[] }
export interface ParsedScholarlyXml {
    readonly id: string;
    readonly doi: string;
    readonly title: string;
    readonly authors: readonly string[];
    readonly year: number;
    readonly licenseUrl: string;
    readonly text: string;
    readonly references: readonly ScholarlyReference[];
}
const normalize = (value: string) => value.normalize('NFC').replace(/\s+/gu, ' ').trim();
const localName = (name: string) => name.split(':').at(-1)!;
const namePattern = /^[A-Za-z_][A-Za-z0-9_.:-]*/;

/** Only predefined XML and numeric references are decoded; no DTD or external entity loader exists. */
function decode(value: string): string {
    if (/&(?![^;\s<&]{1,80};)/.test(value)) throw new Error('malformed XML entity');
    return value.replace(/&([^;\s<&]{1,80});/g, (_whole, entity: string) => {
        const known: Record<string, string> = { amp: '&', lt: '<', gt: '>', quot: '"', apos: "'" };
        if (Object.hasOwn(known, entity)) return known[entity]!;
        const number = /^#x[0-9a-f]+$/i.test(entity) ? Number.parseInt(entity.slice(2), 16) :
            /^#\d+$/.test(entity) ? Number.parseInt(entity.slice(1), 10) : NaN;
        if (!Number.isInteger(number) || number < 1 || number > 0x10ffff || (number >= 0xd800 && number <= 0xdfff) ||
            (number < 0x20 && ![9, 10, 13].includes(number))) throw new Error('unsupported XML entity');
        return String.fromCodePoint(number);
    });
}

function parseXml(xml: string): XmlNode {
    if (Buffer.byteLength(xml) > 524288 || !xml.trim() || xml.includes('\0')) throw new Error('invalid bounded scholarly XML');
    if (/<!\s*ENTITY\b/i.test(xml)) throw new Error('XML entity declarations are forbidden');
    const root: XmlNode = { name: '', attributes: {}, children: [] }, stack = [root];
    let position = 0, nodes = 0;
    while (position < xml.length) {
        const current = stack.at(-1)!;
        if (xml[position] !== '<') {
            const end = xml.indexOf('<', position), stop = end < 0 ? xml.length : end;
            const value = decode(xml.slice(position, stop));
            if (stack.length === 1 && value.trim()) throw new Error('text outside XML document');
            current.children.push(value); position = stop; continue;
        }
        if (xml.startsWith('<!--', position)) {
            const end = xml.indexOf('-->', position + 4);
            if (end < 0 || xml.slice(position + 4, end).includes('--')) throw new Error('malformed XML comment');
            position = end + 3; continue;
        }
        if (xml.startsWith('<![CDATA[', position)) {
            const end = xml.indexOf(']]>', position + 9);
            if (end < 0 || stack.length === 1) throw new Error('malformed XML CDATA');
            current.children.push(xml.slice(position + 9, end)); position = end + 3; continue;
        }
        if (xml.startsWith('<?', position)) {
            const end = xml.indexOf('?>', position + 2);
            if (end < 0) throw new Error('malformed XML instruction');
            position = end + 2; continue;
        }
        if (/^<!DOCTYPE\b/.test(xml.slice(position))) {
            // JATS routinely declares an external DTD. Retain no declarations and never retrieve it.
            if (stack.length !== 1 || root.children.some(child => typeof child !== 'string')) throw new Error('misplaced XML doctype');
            let end = position + 9, quote = '';
            for (; end < xml.length; end++) {
                const character = xml[end]!;
                if (quote) { if (character === quote) quote = ''; }
                else if (character === '"' || character === "'") quote = character;
                else if (character === '[') throw new Error('XML internal subsets are forbidden');
                else if (character === '>') break;
            }
            if (end === xml.length || quote) throw new Error('malformed XML doctype');
            position = end + 1; continue;
        }
        if (xml.startsWith('</', position)) {
            const match = /^<\/([A-Za-z_][A-Za-z0-9_.:-]*)\s*>/.exec(xml.slice(position));
            if (!match || stack.length === 1 || current.name !== match[1]) throw new Error('mismatched XML closing tag');
            stack.pop(); position += match[0].length; continue;
        }
        if (xml.startsWith('<!', position)) throw new Error('unsupported XML declaration');
        const match = namePattern.exec(xml.slice(position + 1));
        if (!match || ++nodes > 30000 || stack.length > 128) throw new Error('invalid or excessive XML elements');
        const node: XmlNode = { name: match[0], attributes: {}, children: [] };
        position += match[0].length + 1;
        let closed = false, selfClosing = false;
        for (let attributes = 0; attributes <= 128; attributes++) {
            const beforeWhitespace = position;
            while (/\s/.test(xml[position] ?? '') && position < xml.length) position++;
            if (xml.startsWith('/>', position)) { position += 2; closed = true; selfClosing = true; break; }
            if (xml[position] === '>') { position++; closed = true; break; }
            if (position === beforeWhitespace) throw new Error('XML attributes require whitespace');
            const attribute = /^([A-Za-z_][A-Za-z0-9_.:-]*)\s*=\s*(["'])/.exec(xml.slice(position));
            if (!attribute || Object.hasOwn(node.attributes, attribute[1]!)) throw new Error('malformed XML attribute');
            position += attribute[0].length;
            const end = xml.indexOf(attribute[2]!, position);
            if (end < 0 || xml.slice(position, end).includes('<')) throw new Error('malformed XML attribute value');
            node.attributes[attribute[1]!] = decode(xml.slice(position, end)); position = end + 1;
        }
        if (!closed) throw new Error('excessive or incomplete XML attributes');
        current.children.push(node);
        if (!selfClosing) stack.push(node);
    }
    const documents = root.children.filter((child): child is XmlNode => typeof child !== 'string');
    if (stack.length !== 1 || documents.length !== 1 || localName(documents[0]!.name) !== 'article') throw new Error('scholarly XML must be one complete article');
    return documents[0]!;
}

function descendants(node: XmlNode, name: string): XmlNode[] {
    return node.children.flatMap(child => typeof child === 'string' ? [] :
        [...(localName(child.name) === name ? [child] : []), ...descendants(child, name)]);
}
function text(node: XmlNode): string {
    if (['script', 'style', 'ref-list'].includes(localName(node.name))) return '';
    return node.children.map(child => typeof child === 'string' ? child : text(child)).join('');
}
const firstText = (node: XmlNode, name: string) => normalize(descendants(node, name).map(text)[0] ?? '');
export const normalizeScholarlyDoi = (value: string) => value.trim().replace(/^https?:\/\/(?:dx\.)?doi\.org\//i, '').toLowerCase();
/** Bibliographic author identity; complete family names and compatible given-name initials are required. */
export function scholarlyAuthorsAgree(leftNames: readonly string[], rightNames: readonly string[]): boolean {
    if (!leftNames.length || leftNames.length > 1024 || leftNames.length !== rightNames.length) return false;
    const variants = (name: string): string[][] => {
        const words = name.normalize('NFKC').match(/[\p{L}\p{M}\p{N}]+/gu) ?? [];
        if (words.length > 32) return [[]];
        const direct = words.map(word => word.toLowerCase());
        const expanded = words.flatMap(word => {
            if (/^\p{Lu}{2,3}$/u.test(word)) return [...word].map(letter => letter.toLowerCase());
            // Some metadata omits the space between an initial and the family name.
            const joined = /^(\p{Lu}{1,3})(\p{Lu}\p{Ll}[\p{L}\p{M}]*)$/u.exec(word);
            return joined ? [...joined[1]!].map(letter => letter.toLowerCase()).concat(joined[2]!.toLowerCase()) : [word.toLowerCase()];
        });
        return JSON.stringify(direct) === JSON.stringify(expanded) ? [direct] : [direct, expanded];
    };
    const identity = (tokens: string[]) => tokens.join('\0');
    const givenMatch = (left: string[], right: string[]) => left.length === right.length && left.every((token, index) => {
        const other = right[index]!;
        return token === other || ([...token].length === 1 && other.startsWith(token)) || ([...other].length === 1 && token.startsWith(other));
    });
    const partitions = (tokens: string[]) => tokens.flatMap((_token, boundary) => boundary ? [
        { given: tokens.slice(0, boundary), family: tokens.slice(boundary) },
        { family: tokens.slice(0, boundary), given: tokens.slice(boundary) },
    ] : []);
    return leftNames.every((left, index) => {
        if (typeof left !== 'string' || typeof rightNames[index] !== 'string' || left.length > 512 || rightNames[index]!.length > 512) return false;
        const leftVariants = variants(left), rightVariants = variants(rightNames[index]!);
        return leftVariants.some(leftTokens => rightVariants.some(rightTokens => {
            if (!leftTokens.length || !rightTokens.length) return false;
            if (identity(leftTokens) === identity(rightTokens)) return true;
            return partitions(leftTokens).some(leftParts => partitions(rightTokens).some(rightParts =>
                identity(leftParts.family) === identity(rightParts.family) && givenMatch(leftParts.given, rightParts.given)));
        }));
    });
}
export function eligibleScholarlyLicense(value: string): string | null {
    try {
        const url = new URL(value);
        if (!['https:', 'http:'].includes(url.protocol) || url.hostname !== 'creativecommons.org' || url.port || url.username || url.password || url.search || url.hash) return null;
        if (!/^\/(?:licenses\/by\/(?:2\.0|2\.5|3\.0|4\.0)|publicdomain\/zero\/1\.0)\/?$/.test(url.pathname)) return null;
        return `https://creativecommons.org${url.pathname.replace(/\/?$/, '/')}`;
    } catch { return null; }
}

/** Complete abstract/body paragraphs only; citation references remain separate bibliographic records. */
export function parseScholarlyXml(xml: string): ParsedScholarlyXml {
    const article = parseXml(xml), metadata = descendants(article, 'article-meta')[0];
    if (!metadata) throw new Error('scholarly XML article metadata is missing');
    const identifiers = descendants(metadata, 'article-id');
    const doi = normalizeScholarlyDoi(text(identifiers.find(node => node.attributes['pub-id-type'] === 'doi') ?? { name: '', attributes: {}, children: [] }));
    if (!/^10\.\d{4,9}\/\S+$/.test(doi) || doi.length > 256) throw new Error('scholarly XML DOI is missing or invalid');
    const id = normalize(text(identifiers.find(node => ['pmc', 'pmcid'].includes(node.attributes['pub-id-type'] ?? '')) ?? { name: '', attributes: {}, children: [] }));
    const title = firstText(metadata, 'article-title');
    const contributors: XmlNode[] = [];
    const authorContributors = (node: XmlNode, authorGroup = false): void => {
        const group = localName(node.name) === 'contrib-group' ? node.attributes['content-type'] === 'author' || node.attributes['contrib-type'] === 'author' : authorGroup;
        if (localName(node.name) === 'contrib' && (node.attributes['contrib-type'] === 'author' ||
            (!node.attributes['contrib-type'] && group))) contributors.push(node);
        for (const child of node.children) if (typeof child !== 'string') authorContributors(child, group);
    };
    authorContributors(metadata);
    const authors = contributors.map(node => {
        const name = descendants(node, 'name')[0];
        return name ? normalize(`${firstText(name, 'given-names')} ${firstText(name, 'surname')}`) : firstText(node, 'collab');
    }).filter(Boolean);
    const dates = descendants(metadata, 'pub-date').filter(node => !['received', 'accepted'].includes(node.attributes['date-type'] ?? ''));
    const year = Number(firstText(dates.find(node => ['epub', 'ppub'].includes(node.attributes['pub-type'] ?? '') || node.attributes['date-type'] === 'pub') ?? dates[0] ?? metadata, 'year'));
    if (!title || !authors.length || !Number.isInteger(year) || year < 1600 || year > 2200) throw new Error('scholarly XML bibliography is incomplete');
    const licenses = descendants(metadata, 'license');
    const urls = new Set<string>();
    for (const license of licenses) {
        const licenseText = normalize(text(license));
        if (/non[ -]?commercial|no[ -]?derivatives|all rights reserved|\bCC[ -]BY[ -](?:NC|ND|SA)\b|share[ -]?alike/i.test(licenseText))
            throw new Error('scholarly XML license contains restrictions');
        const declared = license.attributes['xlink:href'] ?? license.attributes.href;
        const embedded = descendants(license, 'ext-link').map(node => node.attributes['xlink:href'] ?? node.attributes.href).filter((value): value is string => !!value);
        const candidates = [...(declared ? [declared] : []), ...embedded.filter(value => /creativecommons\.org/i.test(value))];
        if (!candidates.length || candidates.some(value => !eligibleScholarlyLicense(value))) throw new Error('unknown scholarly XML license');
        for (const value of candidates) urls.add(eligibleScholarlyLicense(value)!);
    }
    if (urls.size !== 1) throw new Error('scholarly XML requires one explicit eligible license');
    const paragraphs: string[] = [];
    const visit = (node: XmlNode, inside = false): void => {
        const name = localName(node.name);
        if (['ref-list', 'script', 'style', 'back', 'supplementary-material', 'sub-article', 'response'].includes(name)) return;
        const eligible = inside || name === 'abstract' || name === 'body';
        if (eligible && name === 'p') { const paragraph = normalize(text(node)); if (paragraph) paragraphs.push(paragraph); return; }
        for (const child of node.children) if (typeof child !== 'string') visit(child, eligible);
    };
    visit(article);
    if (!paragraphs.length || paragraphs.length > 5000) throw new Error('scholarly XML has no bounded complete paragraphs');
    const referenceDoi = (value: string): string | null => {
        let doi = value.trim();
        if (/^https?:\/\//i.test(doi)) {
            try {
                const url = new URL(doi);
                if (!['doi.org', 'dx.doi.org'].includes(url.hostname) || url.username || url.password || url.port || url.search || url.hash) return null;
                doi = decodeURIComponent(url.pathname.slice(1));
            } catch { return null; }
        }
        doi = normalizeScholarlyDoi(doi);
        return /^10\.\d{4,9}\/[^\s<>#?]{1,230}$/.test(doi) ? doi : null;
    };
    const references = descendants(article, 'ref').flatMap(node => {
        const values = descendants(node, 'pub-id').filter(id => id.attributes['pub-id-type'] === 'doi').map(text);
        for (const link of descendants(node, 'ext-link')) {
            const href = link.attributes['xlink:href'] ?? link.attributes.href;
            if (link.attributes['ext-link-type'] === 'doi') values.push(href ?? text(link));
            // A URI link is DOI evidence only when its actual destination is the DOI resolver.
            else if (href && /^https?:\/\/(?:dx\.)?doi\.org\//i.test(href)) values.push(href);
        }
        const title = firstText(node, 'article-title');
        return [...new Set(values.map(referenceDoi).filter((value): value is string => value !== null))]
            .map(doi => ({ doi, ...(title ? { title } : {}) }));
    });
    return { id, doi, title, authors, year, licenseUrl: [...urls][0]!, text: paragraphs.join('\n\n'), references };
}
