import { useLayoutEffect, useRef, useState } from 'react';
import { animatePopover } from '../animation/motion';

type EmojiPickerProps = { onSelect(emoji: string): void; onClose(): void };

const categories: Record<string, string[]> = {
  常用: ['😀', '😂', '🤣', '😊', '😍', '😘', '😎', '🤔', '😮', '😢', '😡', '👍', '👎', '👏', '🙏', '❤️', '🎉', '🔥', '✨', '✅'],
  表情: ['😀', '😃', '😄', '😁', '😆', '😅', '🤣', '😂', '🙂', '🙃', '😉', '😊', '😇', '🥰', '😍', '🤩', '😘', '😋', '😜', '🤪', '🤗', '🤫', '🤔', '😐', '😏', '😒', '🙄', '😬', '😮', '😴'],
  手势: ['👍', '👎', '👌', '✌️', '🤞', '🤟', '🤘', '🤙', '👈', '👉', '👆', '👇', '☝️', '✋', '🤚', '🖐️', '👏', '🙌', '👐', '🙏', '💪', '🫶', '👋', '🤝', '✍️', '💅', '👀', '🧠'],
  动物: ['🐶', '🐱', '🐭', '🐹', '🐰', '🦊', '🐻', '🐼', '🐨', '🐯', '🦁', '🐮', '🐷', '🐸', '🐵', '🙈', '🙉', '🙊', '🐔', '🐧', '🐦', '🦄', '🐝', '🦋', '🐢', '🐍', '🦖', '🐙', '🦀', '🐳'],
  食物: ['🍎', '🍐', '🍊', '🍋', '🍌', '🍉', '🍇', '🍓', '🫐', '🍒', '🍑', '🥭', '🍍', '🥥', '🥝', '🍅', '🥑', '🍔', '🍕', '🌭', '🍟', '🍿', '🍜', '🍣', '🍱', '🍰', '🍪', '🍩', '🍫', '☕'],
  活动: ['⚽', '🏀', '🏈', '⚾', '🎾', '🏐', '🏆', '🥇', '🎮', '🎲', '🎯', '🎨', '🎵', '🎶', '🎤', '🎧', '🎬', '📚', '✈️', '🚗', '🚀', '🌈', '☀️', '🌙', '⭐', '🌟', '🎁', '🎈', '🎂', '🥳']
};

export function EmojiPicker({ onSelect, onClose }: EmojiPickerProps) {
  const [category, setCategory] = useState('常用');
  const pickerRef = useRef<HTMLDivElement>(null);
  useLayoutEffect(() => animatePopover(pickerRef.current), []);
  return (
    <div ref={pickerRef} className="emoji-picker" role="dialog" aria-label="表情选择器">
      <div className="emoji-picker__header"><strong>选择表情</strong><button type="button" aria-label="关闭表情选择器" onClick={onClose}>×</button></div>
      <div className="emoji-picker__tabs" role="tablist" aria-label="表情分类">
        {Object.keys(categories).map((name) => <button key={name} type="button" role="tab" aria-selected={category === name} onClick={() => setCategory(name)}>{name}</button>)}
      </div>
      <div className="emoji-picker__grid">
        {categories[category].map((emoji, index) => <button key={`${emoji}-${index}`} type="button" aria-label={emoji} onClick={() => { onSelect(emoji); onClose(); }}>{emoji}</button>)}
      </div>
    </div>
  );
}
