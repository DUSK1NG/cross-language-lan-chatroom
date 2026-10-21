// 发行版说明：每次发版只改这里。版本号需与 installer/LANChat.iss 的 AppVersion 保持一致。
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
  version: '1.3.0',
  codename: 'Aurora',
  date: '2026-10-21',
  headline: '全新 Aurora 界面',
  summary: '重新设计的聊天体验：更清晰的层级、深色模式，以及一整套顺滑的动画。',
  sections: [
    {
      kind: 'new',
      title: '新功能',
      items: [
        { title: '深色模式', detail: '在 设置 › 界面 中选择 跟随系统、浅色或深色，切换时颜色平滑过渡。' },
        { title: '动画体验', detail: '页面切换、消息入场、频道列表、成员抽屉与弹窗均加入了轻量动画。' },
        { title: '版本更新卡片', detail: '每个新版本首次启动时展示一次更新内容，也可随时在设置中重新查看。' }
      ]
    },
    {
      kind: 'improved',
      title: '改进',
      items: [
        { title: '重新设计的启动页', detail: '创建与加入聊天室成为主入口，附近聊天室会标注 首次确认 / 已信任。' },
        { title: '更易读的聊天区', detail: '按用户区分的头像颜色、实心的自己消息气泡、悬浮的消息操作条。' },
        { title: '统一的图标', detail: '用矢量图标替换字符图标，在不同字体环境下显示一致。' }
      ]
    },
    {
      kind: 'fixed',
      title: '修复',
      items: [
        { title: '身份卡不再遮挡频道列表', detail: '当前身份固定停靠在侧栏底部。' }
      ]
    }
  ]
};

const SEEN_KEY = 'lan-chat.releaseSeen';

export function hasSeenRelease(version = currentRelease.version): boolean {
  try {
    return window.localStorage.getItem(SEEN_KEY) === version;
  } catch {
    return true;
  }
}

export function markReleaseSeen(version = currentRelease.version) {
  try {
    window.localStorage.setItem(SEEN_KEY, version);
  } catch {
    // Storage unavailable: the card simply shows again next launch.
  }
}
