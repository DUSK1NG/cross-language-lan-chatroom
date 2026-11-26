import { useEffect } from 'react';
import { ease, scaled } from './motion';

type TextField = HTMLInputElement | HTMLTextAreaElement;
const inputTypes = new Set(['text', 'search', 'url', 'tel', 'email', 'password', 'number']);
const graphemes = new Intl.Segmenter();
const easing = 'cubic-bezier(' + ease.join(',') + ')';
const textProperties = [
  'font-family', 'font-size', 'font-weight', 'font-style', 'font-size-adjust', 'font-kerning',
  'font-feature-settings', 'font-variation-settings', 'letter-spacing', 'word-spacing',
  'text-align', 'text-indent', 'text-transform', 'direction', 'tab-size', 'padding'
];

function textRange(mirror: HTMLElement, start: number, end = start) {
  const range = document.createRange();
  const walker = document.createTreeWalker(mirror, NodeFilter.SHOW_TEXT);
  let offset = 0;
  let started = false;
  for (let node = walker.nextNode() as Text | null; node; node = walker.nextNode() as Text | null) {
    if (node.length === 0) continue;
    const next = offset + node.length;
    if (!started && start <= next) {
      range.setStart(node, start - offset);
      started = true;
    }
    if (end <= next) {
      range.setEnd(node, end - offset);
      return range;
    }
    offset = next;
  }
  throw new RangeError('Input motion range exceeds the mirrored text');
}

function animateEdit(mirror: HTMLElement, exits: HTMLElement, previous: string, value: string, caret: number, repaint: () => void) {
  let start = 0;
  const limit = Math.min(previous.length, value.length, caret);
  while (start < limit && previous[start] === value[start]) start++;
  let oldEnd = previous.length;
  let newEnd = value.length;
  while (oldEnd > start && newEnd > Math.max(start, caret) && previous[oldEnd - 1] === value[newEnd - 1]) {
    oldEnd--;
    newEnd--;
  }
  // Keep combining marks and surrogate pairs together when a composition replaces part of a glyph.
  const oldSegments = graphemes.segment(previous);
  const newSegments = graphemes.segment(value);
  start = Math.min(oldSegments.containing(start)?.index ?? start, newSegments.containing(start)?.index ?? start);
  const oldTail = oldSegments.containing(oldEnd);
  const newTail = newSegments.containing(newEnd);
  const extend = Math.max(
    oldTail && oldTail.index < oldEnd ? oldTail.index + oldTail.segment.length - oldEnd : 0,
    newTail && newTail.index < newEnd ? newTail.index + newTail.segment.length - newEnd : 0
  );
  oldEnd += extend;
  newEnd += extend;

  const options = { duration: scaled(0.12) * 1000, easing };
  const edit = textRange(mirror, start, oldEnd);
  if (oldEnd > start) {
    const origin = mirror.getBoundingClientRect();
    const path = [...edit.getClientRects()].filter((rect) => rect.width > 0).map((rect) => {
      const left = rect.left - origin.left;
      const right = rect.right - origin.left;
      const top = rect.top - origin.top;
      const bottom = rect.bottom - origin.top;
      return 'M' + left + ' ' + top + 'H' + right + 'V' + bottom + 'H' + left + 'Z';
    }).join('');
    if (path) {
      const ghost = mirror.cloneNode(true) as HTMLElement;
      const running = [...mirror.querySelectorAll('span')];
      ghost.querySelectorAll('span').forEach((span, index) => { span.style.opacity = getComputedStyle(running[index]).opacity; });
      ghost.style.clipPath = 'path("' + path + '")';
      exits.append(ghost);
      ghost.animate([{ opacity: 1 }, { opacity: 0 }], options).onfinish = () => {
        ghost.remove();
        repaint();
      };
    }
    edit.deleteContents();
  }
  if (newEnd > start) {
    const insert = document.createElement('span');
    insert.textContent = value.slice(start, newEnd);
    const at = textRange(mirror, start);
    const parent = at.startContainer.parentElement!;
    if (parent !== mirror && at.startOffset === at.startContainer.textContent!.length) at.setStartAfter(parent);
    at.insertNode(insert);
    insert.animate([{ opacity: 0 }, { opacity: 1 }], options).onfinish = () => {
      insert.replaceWith(...insert.childNodes);
      mirror.normalize();
      repaint();
    };
  }
}

function activeField(node: EventTarget | null): node is TextField {
  return (node instanceof HTMLTextAreaElement || (node instanceof HTMLInputElement && inputTypes.has(node.type)))
    && !node.readOnly && !node.disabled;
}

function motionForField(field: TextField, animateText: boolean, animateCaret: boolean) {
  const overlay = document.createElement('div');
  overlay.className = 'input-motion-overlay';
  overlay.setAttribute('aria-hidden', 'true');
  const exits = overlay.appendChild(document.createElement('div'));
  const mirror = overlay.appendChild(document.createElement('div'));
  mirror.className = 'input-motion-text';
  const caret = overlay.appendChild(document.createElement('span'));
  caret.className = 'input-motion-caret';
  document.body.append(overlay);

  const style = getComputedStyle(field);
  const color = style.color;
  const caretColor = style.caretColor;
  const inlineColor = field.style.getPropertyValue('color');
  const inlineColorPriority = field.style.getPropertyPriority('color');
  const inlineCaret = field.style.getPropertyValue('caret-color');
  const inlineCaretPriority = field.style.getPropertyPriority('caret-color');
  mirror.style.color = color;
  caret.style.backgroundColor = caretColor === 'auto' ? color : caretColor;

  let frame = 0;
  let inView = true;
  let canPaint = false;
  let hasCaret = false;
  let textPainted = false;
  let caretPainted = false;
  let previous = '';
  const repaint = () => {
    const text = canPaint && animateText && Boolean(mirror.firstElementChild || exits.firstChild);
    overlay.dataset.text = String(text);
    if (text !== textPainted) {
      textPainted = text;
      field.style.setProperty('color', text && field.value ? 'transparent' : inlineColor, text && field.value ? 'important' : inlineColorPriority);
    } else if (text) {
      field.style.setProperty('color', field.value ? 'transparent' : inlineColor, field.value ? 'important' : inlineColorPriority);
    }
    if (hasCaret !== caretPainted || textPainted) {
      caretPainted = hasCaret;
      field.style.setProperty('caret-color', hasCaret ? 'transparent' : textPainted ? caretColor : inlineCaret,
        hasCaret || textPainted ? 'important' : inlineCaretPriority);
    } else if (field.style.getPropertyValue('caret-color') !== inlineCaret && !hasCaret) {
      field.style.setProperty('caret-color', inlineCaret, inlineCaretPriority);
    }
    overlay.dataset.caret = String(hasCaret);
  };

  const update = (play: boolean) => {
    const multiline = field instanceof HTMLTextAreaElement;
    const { clientWidth, clientHeight, clientLeft, clientTop, scrollLeft, scrollTop, selectionStart, selectionEnd } = field;
    const bounds = field.getBoundingClientRect();
    canPaint = field.matches(':focus') && clientWidth > 0 && inView && selectionStart === selectionEnd;
    hasCaret = false;
    if (!canPaint) {
      repaint();
      mirror.textContent = '';
      exits.replaceChildren();
      previous = field.type === 'password' ? '•'.repeat(field.value.length) : field.value;
      return;
    }
    const value = field.type === 'password' ? '•'.repeat(field.value.length) : field.value;
    const index = selectionStart ?? value.length;
    const x = bounds.left + clientLeft;
    const y = bounds.top + clientTop;
    Object.assign(overlay.style, { left: x + 'px', top: y + 'px', width: clientWidth + 'px', height: clientHeight + 'px' });
    for (const property of textProperties) mirror.style.setProperty(property, style.getPropertyValue(property));
    mirror.style.width = clientWidth + 'px';
    mirror.style.whiteSpace = multiline && field.wrap !== 'off' ? 'pre-wrap' : 'pre';
    mirror.style.lineHeight = multiline ? style.lineHeight
      : clientHeight - parseFloat(style.paddingTop || '0') - parseFloat(style.paddingBottom || '0') + 'px';
    overlay.style.setProperty('--input-motion-offset', 'translate(' + -scrollLeft + 'px, ' + -scrollTop + 'px)');
    if (!mirror.firstChild) mirror.textContent = previous + '\u200b';
    if (value !== previous) {
      if (play && animateText && scaled(0.12) > 0) animateEdit(mirror, exits, previous, value, index, repaint);
      else mirror.textContent = value + '\u200b';
      previous = value;
    }
    if (animateCaret && selectionStart !== null) {
      const point = textRange(mirror, index).getBoundingClientRect();
      const origin = mirror.getBoundingClientRect();
      const caretX = Math.max(0, Math.min(clientWidth - 2, point.left - origin.left - scrollLeft));
      const caretY = point.top - origin.top - scrollTop;
      hasCaret = point.height > 0 && caretY < clientHeight && caretY + point.height > 0;
      caret.style.height = point.height + 'px';
      caret.style.transform = 'translate(' + caretX + 'px, ' + caretY + 'px)';
    }
    repaint();
  };
  const schedule = () => {
    if (frame) return;
    frame = requestAnimationFrame(() => { frame = 0; update(true); });
  };

  const events = new AbortController();
  const signal = events.signal;
  for (const event of ['input', 'selectionchange', 'select', 'scroll', 'compositionend']) field.addEventListener(event, schedule, { signal });
  document.addEventListener('scroll', schedule, { capture: true, signal });
  window.addEventListener('resize', schedule, { signal });
  document.addEventListener('animationend', schedule, { signal });
  const resize = new ResizeObserver(schedule);
  const visibility = new IntersectionObserver(([entry]) => {
    inView = entry.isIntersecting;
    const { boundingClientRect: box, intersectionRect: visible } = entry;
    overlay.style.clipPath = !inView || entry.intersectionRatio === 1 ? '' : 'inset('
      + (visible.top - box.top) + 'px ' + (box.right - visible.right) + 'px '
      + (box.bottom - visible.bottom) + 'px ' + (visible.left - box.left) + 'px)';
    schedule();
  }, { threshold: [0, 0.25, 0.5, 0.75, 1] });
  const mutations = new MutationObserver(schedule);
  resize.observe(field);
  visibility.observe(field);
  mutations.observe(field, { attributes: true, attributeFilter: ['value'], subtree: true, characterData: true, childList: true });
  previous = field.type === 'password' ? '•'.repeat(field.value.length) : field.value;
  update(false);

  return () => {
    events.abort();
    resize.disconnect();
    visibility.disconnect();
    mutations.disconnect();
    cancelAnimationFrame(frame);
    overlay.getAnimations({ subtree: true }).forEach((animation) => animation.cancel());
    field.style.setProperty('color', inlineColor, inlineColorPriority);
    field.style.setProperty('caret-color', inlineCaret, inlineCaretPriority);
    overlay.remove();
  };
}

/** One overlay follows the focused text field, including fields in dialogs and the emoji picker. */
export function useInputMotion(animateText: boolean, animateCaret: boolean) {
  useEffect(() => {
    if (!animateText && !animateCaret) return;
    const reducedMotion = matchMedia('(prefers-reduced-motion: reduce)');
    const forcedColors = matchMedia('(forced-colors: active)');
    let stop: (() => void) | undefined;
    const focus = (node: EventTarget | null) => {
      stop?.();
      const textMotion = animateText && !reducedMotion.matches;
      stop = (textMotion || animateCaret) && !forcedColors.matches && activeField(node) && node.clientWidth
        ? motionForField(node, textMotion, animateCaret)
        : undefined;
    };
    const refresh = () => {
      let node = document.activeElement;
      while (node?.shadowRoot?.activeElement) node = node.shadowRoot.activeElement;
      focus(node);
    };
    const events = new AbortController();
    const signal = events.signal;
    document.addEventListener('focusin', (event) => focus(event.composedPath()[0]), { signal });
    document.addEventListener('focusout', () => focus(null), { signal });
    window.addEventListener('focus', refresh, { signal });
    window.addEventListener('blur', () => focus(null), { signal });
    reducedMotion.addEventListener('change', refresh, { signal });
    forcedColors.addEventListener('change', refresh, { signal });
    refresh();
    return () => { events.abort(); stop?.(); };
  }, [animateText, animateCaret]);
}
