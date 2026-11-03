import { useEffect } from 'react';
import { fitTableToViewport, zoomByFactor } from './components/Table';
import { useTable } from './store';
import { useUI } from './ui';

export function useHotkeys() {
  useEffect(() => {
    const isTyping = (el: EventTarget | null) => {
      const t = el as HTMLElement | null;
      return !!t && (t.tagName === 'INPUT' || t.tagName === 'TEXTAREA' || t.isContentEditable);
    };

    const onKeyDown = (e: KeyboardEvent) => {
      if (isTyping(e.target)) return;
      const ui = useUI.getState();
      const tbl = useTable.getState();

      if (e.code === 'Space' && !ui.spaceDown) {
        ui.set({ spaceDown: true });
      }

      // Undo / Redo
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'z') {
        e.preventDefault();
        if (e.shiftKey) tbl.redo();
        else tbl.undo();
        return;
      }
      if ((e.ctrlKey || e.metaKey) && e.key.toLowerCase() === 'y') {
        e.preventDefault();
        tbl.redo();
        return;
      }

      if (e.ctrlKey || e.metaKey || e.altKey) return;

      const key = e.key.toLowerCase();

      // Global toggles
      if (key === 'b') {
        e.preventDefault();
        ui.set({ boxOpen: !ui.boxOpen });
        return;
      }
      if (key === '0') {
        e.preventDefault();
        fitTableToViewport();
        return;
      }
      if (e.key === '=' || e.key === '+') {
        e.preventDefault();
        zoomByFactor(1.2);
        return;
      }
      if (e.key === '-') {
        e.preventDefault();
        zoomByFactor(1 / 1.2);
        return;
      }
      if (e.key === '?' || (e.shiftKey && e.key === '/')) {
        e.preventDefault();
        ui.set({ helpOpen: !ui.helpOpen });
        return;
      }
      if (e.key === 'Escape') {
        if (ui.menu) ui.set({ menu: null });
        else if (ui.inspect !== null) ui.set({ inspect: null });
        else if (ui.searchPile) ui.set({ searchPile: null });
        else if (ui.helpOpen) ui.set({ helpOpen: false });
        else if (ui.selection.length) ui.set({ selection: [] });
        return;
      }

      // Gather selected piles
      if (key === 'g' && ui.selection.length > 1) {
        e.preventDefault();
        tbl.gather(ui.selection, e.shiftKey);
        ui.set({ selection: [] });
        return;
      }

      // Inspect hovered card (table, hand, or box)
      if (key === 'z') {
        e.preventDefault();
        if (ui.inspect !== null) {
          ui.set({ inspect: null });
          return;
        }
        const h = ui.hover;
        if (!h) return;
        if (h.kind === 'pile') {
          const p = tbl.present.piles.find((q) => q.id === h.id);
          const top = p?.cards[p.cards.length - 1];
          if (top?.faceUp) ui.set({ inspect: top.def });
        } else if (h.kind === 'hand') {
          const c = tbl.present.hands[tbl.present.activeHand]?.cards[h.index];
          if (c) ui.set({ inspect: c.def });
        } else if (h.kind === 'def' && h.faceUp) {
          ui.set({ inspect: h.def });
        }
        return;
      }

      // Pile-specific shortcuts (act on hovered pile, or selection)
      const hoveredPileId = ui.hover?.kind === 'pile' ? ui.hover.id : null;

      if (key === 'f') {
        e.preventDefault();
        if (ui.selection.length > 1) {
          tbl.flipMany(ui.selection);
        } else if (hoveredPileId) {
          if (e.shiftKey) {
            tbl.flipTop(hoveredPileId);
          } else {
            const p = tbl.present.piles.find((q) => q.id === hoveredPileId);
            tbl.flipPile(hoveredPileId);
            if (p && p.cards.length > 1) ui.play(hoveredPileId, 'flip');
          }
        }
        return;
      }

      if (key === 's' && hoveredPileId) {
        e.preventDefault();
        const p = tbl.present.piles.find((q) => q.id === hoveredPileId);
        if (p && p.cards.length > 1) {
          tbl.shuffle(hoveredPileId);
          ui.play(hoveredPileId, 'shuffle');
          ui.say(`Shuffled (${p.cards.length} cards)`);
        }
        return;
      }

      if (key === 'r' && hoveredPileId) {
        e.preventDefault();
        tbl.rotate(hoveredPileId);
        return;
      }

      if (key === 'd' && hoveredPileId) {
        e.preventDefault();
        tbl.draw(hoveredPileId, 1);
        return;
      }

      if (/^[1-9]$/.test(e.key) && hoveredPileId) {
        e.preventDefault();
        tbl.draw(hoveredPileId, Number(e.key));
        return;
      }

      if (e.key === 'Delete' || e.key === 'Backspace') {
        if (ui.selection.length > 0) {
          e.preventDefault();
          tbl.returnToBox(ui.selection);
          ui.set({ selection: [] });
        } else if (hoveredPileId) {
          e.preventDefault();
          tbl.returnToBox([hoveredPileId]);
        }
      }
    };

    const onKeyUp = (e: KeyboardEvent) => {
      if (e.code === 'Space') {
        useUI.getState().set({ spaceDown: false });
      }
    };

    const onBlur = () => useUI.getState().set({ spaceDown: false });

    window.addEventListener('keydown', onKeyDown);
    window.addEventListener('keyup', onKeyUp);
    window.addEventListener('blur', onBlur);
    return () => {
      window.removeEventListener('keydown', onKeyDown);
      window.removeEventListener('keyup', onKeyUp);
      window.removeEventListener('blur', onBlur);
    };
  }, []);
}
