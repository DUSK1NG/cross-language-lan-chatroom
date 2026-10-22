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
  date: '2026-10-22',
  headline: '全新聊天界面',
  summary: '深色优先的三栏布局、分组气泡和更顺手的输入体验。',
  sections: [
    {
      kind: 'new',
      title: '新功能',
      items: [
        { title: '三栏布局', detail: '会话列表可拖动调整宽度并记住；会话信息面板按需展开，窄窗口自动切换为单栏。' },
        { title: '外观设置', detail: '在 设置 › 外观 中选择主题、5 种强调色、消息字号与气泡圆角。' },
        { title: '快捷键', detail: 'Ctrl+K 搜索会话，Ctrl+F 搜索消息，Ctrl+↑/↓ 切换会话，Esc 关闭面板。' }
      ]
    },
    {
      kind: 'improved',
      title: '改进',
      items: [
        { title: '消息气泡', detail: '5 分钟内连续消息自动分组，引用可跳转原消息，悬停或右键即可回复、复制。' },
        { title: '输入框', detail: '每个会话单独保存草稿，支持中文输入法回车、表情面板和回复预览。' },
        { title: '连接审批', detail: '每个请求显示 60 秒倒计时，超时后自动失效。' }
      ]
    },
    {
      kind: 'fixed',
      title: '修复',
      items: [
        { title: '文字对比度', detail: '次要文字、气泡时间和投递状态标记均达到 WCAG AA 对比度。' },
        { title: '版本更新提示', detail: '每个版本只自动提示一次，退出重启后仍会记住；设置中的版本说明可随时手动查看。' }
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
