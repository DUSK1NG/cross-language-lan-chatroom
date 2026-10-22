import { afterEach, describe, expect, it } from 'vitest';

import { effectsDisabled, motionAllowed, scaled, setMotionScale } from './motion';

describe('motion gate', () => {
  afterEach(() => {
    delete document.documentElement.dataset.effects;
    setMotionScale(undefined);
  });

  it('reports motion as allowed while the effects gate is unset', () => {
    expect(effectsDisabled()).toBe(false);
    expect(motionAllowed()).toBe(true);
  });

  it('blocks motion and zeroes durations when data-effects is off', () => {
    document.documentElement.dataset.effects = 'off';
    expect(motionAllowed()).toBe(false);
    expect(scaled(0.2)).toBe(0);
  });

  it('applies the native duration scale within safe bounds and mirrors it to CSS', () => {
    setMotionScale(0.75);
    expect(scaled(0.2)).toBeCloseTo(0.15);
    expect(document.documentElement.style.getPropertyValue('--motion-scale')).toBe('0.75');

    setMotionScale(10);
    expect(scaled(0.2)).toBeCloseTo(0.4);
    setMotionScale(Number.NaN);
    expect(scaled(0.2)).toBeCloseTo(0.2);
  });
});
