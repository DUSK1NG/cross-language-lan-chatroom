import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { EmojiPicker } from './EmojiPicker';

describe('EmojiPicker', () => {
  afterEach(cleanup);

  it('shows categorized Chinese labels and returns the selected emoji', () => {
    const selected: string[] = [];
    render(<EmojiPicker onSelect={(emoji) => selected.push(emoji)} onClose={() => undefined} />);

    expect(screen.getByRole('dialog', { name: '表情选择器' })).toBeInTheDocument();
    expect(screen.getByRole('tab', { name: '常用' })).toBeInTheDocument();
    fireEvent.click(screen.getByRole('tab', { name: '动物' }));
    fireEvent.click(screen.getByRole('button', { name: '🐶' }));

    expect(selected).toEqual(['🐶']);
  });
});
