import { describe, expect, it } from 'vitest';

import { isSafeHref, tokenize } from './richText';

const kinds = (input: string) => tokenize(input).map((token) => [token.kind, token.text]);

describe('rich text tokenizer', () => {
  it('round-trips the exact input', () => {
    for (const input of ['plain', 'see https://example.com/a?b=1, ok', 'run `pnpm test` in C:\\work\\lan', '路径 /var/log/lan-chat.log 已满。']) {
      expect(tokenize(input).map((token) => token.text).join('')).toBe(input);
    }
  });

  it('finds links and drops trailing sentence punctuation', () => {
    expect(kinds('看 https://example.com/x。')).toEqual([['text', '看 '], ['link', 'https://example.com/x'], ['text', '。']]);
    expect(kinds('(https://a.io/b)')).toEqual([['text', '('], ['link', 'https://a.io/b'], ['text', ')']]);
  });

  it('finds inline code', () => {
    expect(kinds('运行 `pnpm lint` 再提交')).toEqual([['text', '运行 '], ['code', '`pnpm lint`'], ['text', ' 再提交']]);
  });

  it('finds Windows and Unix paths but not fractions or and/or', () => {
    expect(kinds('确认一下 .tools\\mlspp 在 C:\\Users\\lin\\AppData 里')).toContainEqual(['path', 'C:\\Users\\lin\\AppData']);
    expect(kinds('日志在 /var/log/lan-chat.log')).toContainEqual(['path', '/var/log/lan-chat.log']);
    expect(kinds('~/projects/chat')).toEqual([['path', '~/projects/chat']]);
    expect(kinds('3/4 和 and/or')).toEqual([['text', '3/4 和 and/or']]);
  });

  it('only allows http and https links to open', () => {
    expect(isSafeHref('https://example.com')).toBe(true);
    expect(isSafeHref('javascript:alert(1)')).toBe(false);
    expect(isSafeHref('file:///C:/x')).toBe(false);
  });
});
