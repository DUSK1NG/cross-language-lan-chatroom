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
  codename: '正式版',
  date: '2026-11-26',
  headline: 'LAN Chat 1.4',
  summary: '局域网与在线连接合并到同一条代码线的首个正式版，桌面动效与聊天交互一起补齐。',
  sections: [
    {
      kind: 'new',
      title: '新增',
      items: [
        { title: '三档动效开关', detail: '设置 → 动效 可分别控制新文字淡入、消息气泡动画和聊天区滚动动画，关闭后立即停止播放。' },
        { title: '聊天操作更顺手', detail: '消息悬停与选择收进气泡，表情面板按分类切换，搜索展开时让出标题，输入框随宽度重排草稿行。' },
        { title: 'AstrBot 机器人设置', detail: 'Windows 在线版可在设置 → 机器人 接入 AstrBot，读取、新建和编辑模型源、模型与人格。' },
        { title: '群角色与群公告', detail: 'Windows 在线版与 Android 0.4.0 支持群角色、群公告、群图片与群文件。' }
      ]
    },
    {
      kind: 'improved',
      title: '改进',
      items: [
        { title: '断线可重连', detail: '局域网协商心跳，断线后可重新连接，重连仍由房主审批。' },
        { title: '搜索与频道恢复', detail: '会话搜索支持中文短词、符号和分页；房主重启后频道、密码、白名单、管理员和禁言恢复。' },
        { title: '检查更新', detail: '设置的「关于」可手动检查更新，安装版验签后下载安装，便携版给出新版 ZIP。' }
      ]
    },
    {
      kind: 'fixed',
      title: '修复',
      items: [
        { title: '组字时误发送', detail: '输入法拼音尚未上屏时点发送不会再发出原始拼音，Android 组字期间也不写入草稿。' },
        { title: '附件消息被限流', detail: '连续发送附件控制消息时按速率排队，上传完成后附件消息不再被吞掉。' }
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
