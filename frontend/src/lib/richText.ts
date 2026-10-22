/**
 * Splits message text into plain runs, links, inline code and filesystem paths (spec §6).
 * Pure and order-preserving: joining every token's `text` reproduces the input exactly,
 * so copy/select and existing text assertions are unaffected.
 */
export type RichToken =
  | { kind: 'text'; text: string }
  | { kind: 'link'; text: string; href: string }
  | { kind: 'code'; text: string; code: string }
  | { kind: 'path'; text: string };

// `code` — a single backtick pair, no newline inside.
const codePattern = /`([^`\n]+)`/;
// http(s) URLs; trailing punctuation that usually ends a sentence is not part of the link.
const urlPattern = /\bhttps?:\/\/[^\s<>"'`]+[^\s<>"'`.,;:!?，。；：！？）)\]】」』]/i;
// Windows drive or UNC paths, and absolute or home-relative Unix paths with at least one segment.
const pathPattern = /(?:\b[A-Za-z]:\\(?:[^\s\\/:*?"<>|，。；]+\\?)+|\\\\[^\s\\]+\\[^\s，。；]+|(?:~|\.{1,2})?\/(?:[\w.-]+\/)*[\w.-]+(?:\.\w+)?(?=$|[\s，。；,;:）)]))/;

const combined = new RegExp(`${codePattern.source}|(${urlPattern.source})|(${pathPattern.source})`, 'gi');

export function tokenize(input: string): RichToken[] {
  const tokens: RichToken[] = [];
  let last = 0;
  combined.lastIndex = 0;
  for (let match = combined.exec(input); match; match = combined.exec(input)) {
    const [whole, code, url, path] = match;
    // A bare "/" or a single path segment like "/a" inside prose is too noisy to style.
    if (path !== undefined && !isLikelyPath(path)) continue;
    if (match.index > last) tokens.push({ kind: 'text', text: input.slice(last, match.index) });
    if (code !== undefined) tokens.push({ kind: 'code', text: whole, code });
    else if (url !== undefined) tokens.push({ kind: 'link', text: url, href: url });
    else tokens.push({ kind: 'path', text: path });
    last = match.index + whole.length;
  }
  if (last < input.length) tokens.push({ kind: 'text', text: input.slice(last) });
  return tokens.length ? tokens : [{ kind: 'text', text: input }];
}

function isLikelyPath(path: string): boolean {
  if (/^[A-Za-z]:\\/.test(path) || path.startsWith('\\\\')) return path.length > 3;
  // Unix: require two segments, or a home/relative prefix, so "and/or" or "1/2" stay plain text.
  if (path.startsWith('~/') || path.startsWith('./') || path.startsWith('../')) return true;
  return path.startsWith('/') && path.split('/').filter(Boolean).length >= 2;
}

/** Only http(s) links may be opened; everything else is rendered as text. */
export function isSafeHref(href: string): boolean {
  try {
    const url = new URL(href);
    return url.protocol === 'http:' || url.protocol === 'https:';
  } catch {
    return false;
  }
}
