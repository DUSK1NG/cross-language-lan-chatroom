// 发行版说明与 Cargo.toml 的桌面版本同步。
export type ReleaseSectionKind = 'new' | 'improved' | 'fixed';

export type ReleaseNotes = {
  version: string;
  codename: string;
  date: string;
  headline: string;
  summary: string;
  sections: { kind: ReleaseSectionKind; title: string; items: { title: string; detail: string }[] }[];
};

export const currentRelease: ReleaseNotes = {
  version: '1.4.0',
  codename: '开发版',
  date: '2026-11-23',
  headline: 'LAN Chat 1.4 开发版',
  summary: '局域网与在线版的开发和兼容性测试版本。',
  sections: [
    {
      kind: 'fixed',
      title: '修复',
      items: [
        { title: '局域网附件传输', detail: '连续发送附件控制消息时按速率排队，避免上传完成后附件消息未能发出。' }
      ]
    }
  ]
};

const SEEN_KEY = 'lan-chat.releaseSeen';

export function hasSeenRelease(version = currentRelease.version): boolean {
  return window.localStorage.getItem(SEEN_KEY) === version;
}

export function markReleaseSeen(version = currentRelease.version) {
  window.localStorage.setItem(SEEN_KEY, version);
}
