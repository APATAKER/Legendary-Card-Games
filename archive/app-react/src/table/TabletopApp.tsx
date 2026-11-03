import { useEffect, useState } from 'react';
import { BoxDrawer } from './components/BoxDrawer';
import { ContextMenu } from './components/ContextMenu';
import { HandTray } from './components/HandTray';
import { InspectOverlay } from './components/InspectOverlay';
import { SearchWindow } from './components/SearchWindow';
import { Table } from './components/Table';
import { Toolbar } from './components/Toolbar';
import { useUI } from './ui';
import { useHotkeys } from './useHotkeys';
import './table.css';

export function TabletopApp() {
  useHotkeys();
  const toast = useUI((s) => s.toast);
  const [visibleToast, setVisibleToast] = useState<string | null>(null);

  useEffect(() => {
    if (!toast) return;
    setVisibleToast(toast.msg);
    const t = window.setTimeout(() => setVisibleToast(null), 1900);
    return () => window.clearTimeout(t);
  }, [toast]);

  return (
    <div className="lc-root">
      <Table />
      <Toolbar />
      <BoxDrawer />
      <HandTray />
      <ContextMenu />
      <SearchWindow />
      <InspectOverlay />
      {visibleToast && <div className="lc-toast">{visibleToast}</div>}
    </div>
  );
}

export default TabletopApp;
