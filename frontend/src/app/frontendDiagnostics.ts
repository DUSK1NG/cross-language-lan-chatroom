// 前端异常上报（规格 01 D5 / R5.1 R5.2）。
// 仅上报错误名与堆栈/组件栈，各截断到 2048 字节，不含消息正文。
// C++ 侧（bridge_protocol.cpp 白名单）仅在连接日志开启时写日志。
import { createCommand } from '../bridge/chatBridge';
import type { ChatBridgeClient } from '../bridge/types';

export const FRONTEND_ERROR_COMMAND = 'diagnostics.frontendError';
export const FRONTEND_ERROR_FIELD_LIMIT = 2048;

/** 按 UTF-8 字节截断字符串到 limit 字节，避免切碎多字节字符。 */
export function truncateUtf8(value: string, limit = FRONTEND_ERROR_FIELD_LIMIT): string {
  const encoder = new TextEncoder();
  const decoder = new TextDecoder('utf-8', { fatal: false });
  const bytes = encoder.encode(value);
  if (bytes.length <= limit) return value;
  // 截断到 limit 字节后解码并去掉可能出现的替换字符（半个多字节字符）。
  return decoder.decode(bytes.subarray(0, limit)).replace(/\uFFFD+$/u, '');
}

/**
 * 通过 bridge 上报前端异常。name 与 stack 各截断到 2048 字节。
 * dispatch 自身出错时静默忽略，避免错误上报再次抛出。
 */
export function reportFrontendError(
  bridge: Pick<ChatBridgeClient, 'dispatch'>,
  name: string,
  stack: string
): void {
  try {
    bridge.dispatch(createCommand(FRONTEND_ERROR_COMMAND, {
      name: truncateUtf8(name),
      stack: truncateUtf8(stack)
    }));
  } catch {
    // 上报链路不可用时不应影响主流程。
  }
}

/** 不让未处理的 Promise 拒绝冒到控制台（R5.2），同时上报诊断（D5）。 */
export function registerUnhandledRejectionReporter(bridge: Pick<ChatBridgeClient, 'dispatch'>): void {
  window.addEventListener('unhandledrejection', (event) => {
    const reason = event.reason;
    const name = reason instanceof Error ? (reason.name || 'UnhandledRejection') : 'UnhandledRejection';
    const stack = reason instanceof Error ? (reason.stack ?? '') : String(reason);
    reportFrontendError(bridge, name, stack);
    event.preventDefault();
  });
}
