import { memo, useMemo } from 'react';

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
 * Links are plain same-window anchors: the Qt host intercepts link navigation and
 * hands http(s) URLs to the system browser (web_ui_host.cpp), so the page never navigates.
 */
export const RichText = memo(function RichText({ text, self, onCopyPath }: RichTextProps) {
  const tokens = useMemo(() => tokenize(text), [text]);
  return (
    <>
      {tokens.map((token, index) => {
        switch (token.kind) {
          case 'link':
            return isSafeHref(token.href)
              ? <a key={index} href={token.href} rel="noopener noreferrer" className={cn('underline decoration-1 underline-offset-2 break-all', self ? 'text-read-tick' : 'text-accent-text')}>{token.text}</a>
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
            return <span key={index}>{token.text}</span>;
        }
      })}
    </>
  );
});
