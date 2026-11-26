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
  version: '1.4.1',
  codename: '顺手版',
  date: '2026-11-26',
  headline: 'LAN Chat 1.4.1',
  summary: '一次专门收拾「打断人」的小版本：草稿、选中的房主、审批弹窗、保存失败和 Android 传文件都不再把你从正在做的事里拽出来。',
  sections: [
    {
      kind: 'new',
      title: '新增',
      items: [
        { title: '审批不再抢屏', detail: '只有新进来的申请才弹窗，弹窗可以随时收起继续聊天，待批准提示留在标题角标和成员面板里。' },
        { title: '在线登录开箱可用', detail: '首次启动预置公共服务器，地址与端口收进「更改服务器」，需要换服务器时才展开。' }
      ]
    },
    {
      kind: 'improved',
      title: '改进',
      items: [
        { title: '草稿跟着你走', detail: '打到一半去设置里改点东西再回来，输入框里的草稿还在，不必重打。' },
        { title: '房主选择不被刷新打断', detail: '局域网扫描保留仍在广播的主机；选中的房主短暂缺席时先给 10 秒宽限并提示「暂时没有收到房主广播」，核对指纹不会被打断。' },
        { title: '登录失败会说原因', detail: '密码不足 8 字节、用户代码格式不对、端口不合法时直接在输入框下方说明，而不是只把登录按钮变灰。' },
        { title: 'Android 传文件不挡聊天', detail: '上传和下载换成底部浮动进度卡，传输期间可以继续看消息、发文字。' },
        { title: '在线群成员能撤回自己的消息', detail: '成员可以撤回自己发出的私信和群消息，服务端只接受作者本人撤回；改群名、管理其他成员仍只有群主和管理员能做。' },
        { title: '群主可以解散群聊', detail: '群主在群资料里可以解散群聊，所有成员会被移出、聊天记录随之删除；群员仍可自行退出，群主要退出需先转让群主或解散群聊。' }
      ]
    },
    {
      kind: 'fixed',
      title: '修复',
      items: [
        { title: '有待批准申请时弹窗关不掉', detail: '现在可以按 Esc、点遮罩或 × 收起弹窗，申请仍需要你明确批准，不会被悄悄放行。' },
        { title: '保存地址失败会断线', detail: '服务端已经接受连接后，本地保存地址失败只提示「已连接 · 地址未保存」，不再中止本次会话。' }
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
