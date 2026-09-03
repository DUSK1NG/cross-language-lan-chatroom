import { cleanup, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it } from 'vitest';

import { CommandFeedback } from './CommandFeedback';

describe('CommandFeedback', () => {
  afterEach(cleanup);

  it('renders nothing while idle', () => {
    const { container } = render(<CommandFeedback status="idle" message="" />);
    expect(container).toBeEmptyDOMElement();
  });

  it('pairs pending feedback with a spinner under a status role', () => {
    render(<CommandFeedback status="pending" message="正在发送…" />);

    expect(screen.getByRole('status')).toHaveTextContent('正在发送…');
    expect(document.querySelector('.command-feedback__spinner')).not.toBeNull();
  });

  it('announces errors through an alert role without a spinner', () => {
    render(<CommandFeedback status="error" message="发送失败" />);

    expect(screen.getByRole('alert')).toHaveTextContent('发送失败');
    expect(document.querySelector('.command-feedback__spinner')).toBeNull();
  });
});
