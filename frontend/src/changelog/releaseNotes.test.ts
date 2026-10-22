import { afterEach, describe, expect, it, vi } from 'vitest';

import { currentRelease, hasSeenRelease, markReleaseSeen } from './releaseNotes';

describe('release seen state', () => {
  afterEach(() => {
    window.localStorage.removeItem('lan-chat.releaseSeen');
    vi.restoreAllMocks();
  });

  it('remembers the current version without suppressing a newer release', () => {
    markReleaseSeen('previous-release');
    expect(hasSeenRelease()).toBe(false);
    markReleaseSeen();
    expect(window.localStorage.getItem('lan-chat.releaseSeen')).toBe(currentRelease.version);
    expect(hasSeenRelease()).toBe(true);
    expect(hasSeenRelease('next-release')).toBe(false);
  });

  it('reports a storage failure instead of silently losing the seen state', () => {
    vi.spyOn(Storage.prototype, 'setItem').mockImplementation(() => {
      throw new DOMException('Release state storage is unavailable', 'QuotaExceededError');
    });
    expect(() => markReleaseSeen()).toThrow('Release state storage is unavailable');
  });
});
