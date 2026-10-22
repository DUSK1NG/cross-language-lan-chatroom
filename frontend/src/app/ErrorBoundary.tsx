import { Component, type ErrorInfo, type ReactNode } from 'react';

import type { ChatBridgeClient } from '../bridge/types';
import { reportFrontendError } from './frontendDiagnostics';
import { Button } from '../components/ui/button';
import { StatusScreen, errorIcon } from './BridgeUnavailablePage';

type ErrorBoundaryProps = {
  bridge: Pick<ChatBridgeClient, 'dispatch'>;
  onBack(): void;
  children: ReactNode;
};

type ErrorBoundaryState = { hasError: boolean };

/**
 * 捕获渲染错误，显示可恢复的错误页（重试 / 返回），而不是白屏（R5.1）。
 * 与 BridgeUnavailablePage 共用 StatusScreen。捕获时通过 bridge 上报
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
      <StatusScreen icon={errorIcon} title="界面遇到问题"
        alert="页面渲染时出现异常。可以重试，或返回模式选择重新开始。"
        note="我们已记录这次错误（仅错误类型，不含聊天内容），不会伪造聊天状态。"
        actions={<>
          <Button variant="ghost" onClick={this.handleBack}>返回</Button>
          <Button variant="primary" onClick={this.handleRetry}>重试</Button>
        </>} />
    );
  }
}
