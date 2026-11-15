import { Component, type ErrorInfo, type ReactNode } from 'react';

/**
 * Says what happened if drawing the page itself fails, which would otherwise
 * leave it blank.
 */
export class ErrorBoundary extends Component<{ children: ReactNode }, { message: string | null }> {
  override state = { message: null };

  static getDerivedStateFromError(error: unknown) {
    return { message: error instanceof Error ? error.message : String(error) };
  }

  override componentDidCatch(error: unknown, info: ErrorInfo) {
    console.error('The page stopped:', error, info.componentStack);
  }

  override render() {
    if (this.state.message === null) {
      return this.props.children;
    }
    return (
      <ul className="notices">
        <li className="notice error">
          <p role="alert">The page stopped because something went wrong: {this.state.message}</p>
          <button type="button" className="button" onClick={() => location.reload()}>
            Reload
          </button>
        </li>
      </ul>
    );
  }
}
