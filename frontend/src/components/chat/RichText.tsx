import { memo, useMemo, type CSSProperties } from 'react';

import { isSafeHref, tokenize } from '../../lib/richText';
import { cn } from '../../lib/cn';

type RichTextProps = {
  text: string;
  self: boolean;
  /** Copies a path to the clipboard (through the bridge) and confirms with a toast. */
  onCopyPath: (path: string) => void;
};

/**
 * Message body with links, `inline code` and filesystem paths (spec §6).
 * Link navigation is delegated to the desktop shell.
 */
export const RichText = memo(function RichText({ text, self, onCopyPath }: RichTextProps) {
  const tokens = useMemo(() => tokenize(text), [text]);
  const segmenter = useMemo(() => !self && text.length >= 72 ? new Intl.Segmenter(undefined, { granularity: 'word' }) : null, [self, text.length]);
  let nextWord = 0;
  return (
    <>
      {tokens.map((token, index) => {
        switch (token.kind) {
          case 'link':
            return isSafeHref(token.href)
              ? <a key={index} href={token.href} target="_blank" rel="noopener noreferrer"
                onClick={(event) => {
                  const runtime = window.runtime;
                  if (!runtime?.BrowserOpenURL) return;
                  event.preventDefault();
                  runtime.BrowserOpenURL(token.href);
                }} className={cn('underline decoration-1 underline-offset-2 break-all', self ? 'text-read-tick' : 'text-accent-text')}>{token.text}</a>
              : <span key={index}>{token.text}</span>;
          case 'code':
            return <code key={index} className="rounded-sm bg-code px-1 font-mono">{token.code}</code>;
          case 'path':
            return (
              <button key={index} type="button" title="点击复制" aria-label={`复制路径 ${token.text}`} onClick={() => onCopyPath(token.text)}
                className="press inline cursor-copy rounded-sm bg-code px-1 text-left font-mono break-all hover:underline">
                {token.text}
              </button>
            );
          default:
            return <span key={index}>{segmenter
              ? Array.from(segmenter.segment(token.text), ({ segment, isWordLike, index: offset }) => isWordLike && nextWord < 28
                ? <span key={offset} className="cirrus-word" style={{ '--word': nextWord++ } as CSSProperties}>{segment}</span>
                : segment)
              : token.text}</span>;
        }
      })}
    </>
  );
});
