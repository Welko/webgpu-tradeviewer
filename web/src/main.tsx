import { StrictMode } from 'react';
import { createRoot } from 'react-dom/client';
import { App } from './App';
import { getElement } from './dom';
import { ErrorBoundary } from './ErrorBoundary';
import { installErrorReporting } from './notices';
import './index.css';

installErrorReporting();

createRoot(getElement('root')).render(
  <StrictMode>
    <ErrorBoundary>
      <App />
    </ErrorBoundary>
  </StrictMode>,
);
