import { useSyncExternalStore } from 'react';
import { dismissNotice, getNotices, subscribeNotices } from './notices';

/** What the page has to say about what went wrong. Nothing at all while all is well. */
export function Notices() {
  const notices = useSyncExternalStore(subscribeNotices, getNotices);
  if (notices.length === 0) {
    return null;
  }
  return (
    <ul className="notices" data-testid="notices">
      {notices.map((notice) => (
        <li key={notice.id} className={`notice ${notice.severity}`}>
          <p role={notice.severity === 'error' ? 'alert' : 'status'}>
            {notice.message}
            {notice.count > 1 && ` (${notice.count} times)`}
          </p>
          {notice.needsReload && (
            <button type="button" className="button" onClick={() => location.reload()}>
              Reload
            </button>
          )}
          <button type="button" className="notice-dismiss" aria-label="Dismiss" onClick={() => dismissNotice(notice.id)}>
            ×
          </button>
        </li>
      ))}
    </ul>
  );
}
