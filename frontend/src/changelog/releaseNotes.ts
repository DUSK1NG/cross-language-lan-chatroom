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
  summary: '局域网与远程连接的开发和兼容性测试版本。',
  sections: [
    {
      kind: 'new',
      title: '局域网',
      items: [
        { title: '频道权限可恢复', detail: '房主重启后恢复频道、密码、白名单、频道管理员及禁言。访客每次连接仍需房主批准。' },
        { title: '搜索和更新', detail: '消息支持查找更早的匹配结果。可从设置的「关于」主动检查更新。' }
      ]
    },
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
