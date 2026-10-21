import { Component, type ErrorInfo, type ReactNode } from 'react';

import type { ChatBridgeClient } from '../bridge/types';
import { reportFrontendError } from './frontendDiagnostics';

type ErrorBoundaryProps = {
  bridge: Pick<ChatBridgeClient, 'dispatch'>;
  onBack(): void;
  children: ReactNode;
};

type ErrorBoundaryState = { hasError: boolean };

/**
 * 捕获渲染错误，显示可恢复的错误页（重试 / 返回），而不是白屏（R5.1）。
 * 风格复用 BridgeUnavailablePage 的令牌与类名。捕获时通过 bridge 上报
 * `diagnostics.frontendError`（仅错误名与组件栈，各 ≤ 2048 字节；D5）。
 */
export class ErrorBoundary extends Component<ErrorBoundaryProps, ErrorBoundaryState> {
  state: ErrorBoundaryState = { hasError: false };

  static getDerivedStateFromError(): ErrorBoundaryState {
    return { hasError: true };
  }

  componentDidCatch(error: Error, info: ErrorInfo): void {
    reportFrontendError(this.props.bridge, error.name || 'Error', info.componentStack ?? '');
  }

  private handleRetry = (): void => {
    window.location.reload();
  };

  private handleBack = (): void => {
    this.setState({ hasError: false });
    this.props.onBack();
  };

  render(): ReactNode {
    if (!this.state.hasError) return this.props.children;
    return (
      <main className="app-shell mode-shell bridge-error-shell">
        <section className="mode-panel bridge-error-panel">
          <p className="eyebrow">LAN CHAT / RUNTIME ERROR</p>
          <h1>界面遇到问题</h1>
          <p className="status-line status-line--error" role="alert">
            页面渲染时出现异常。可以重试，或返回模式选择重新开始。
          </p>
          <p className="lede">我们已记录这次错误（仅错误类型，不含聊天内容），不会伪造聊天状态。</p>
          <div className="form-actions">
            <button className="secondary-button" type="button" onClick={this.handleBack}>返回</button>
            <button className="primary-button" type="button" onClick={this.handleRetry}>重试</button>
          </div>
        </section>
      </main>
    );
  }
}
