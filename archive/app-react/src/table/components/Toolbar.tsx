import { useRef } from 'react';
import {
  CircleHelp,
  Eye,
  EyeOff,
  FolderOpen,
  Maximize2,
  Minus,
  Package,
  Plus,
  Redo2,
  RotateCcw,
  Save,
  Undo2,
  Users,
  X,
  ZoomIn,
  ZoomOut,
} from 'lucide-react';
import { fitTableToViewport, zoomByFactor } from './Table';
import { useTable, validate } from '../store';
import { useUI } from '../ui';

export function Toolbar() {
  const canUndo = useTable((s) => s.past.length > 0);
  const canRedo = useTable((s) => s.future.length > 0);
  const playerMats = useTable((s) => s.present.playerMats);
  const pilesCount = useTable((s) => s.present.piles.length);

  const boxOpen = useUI((s) => s.boxOpen);
  const previewOn = useUI((s) => s.previewOn);
  const helpOpen = useUI((s) => s.helpOpen);
  const zoom = useUI((s) => s.view.zoom);
  const fileRef = useRef<HTMLInputElement>(null);

  const saveToFile = () => {
    const data = JSON.stringify(useTable.getState().present, null, 2);
    const blob = new Blob([data], { type: 'application/json' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = `legendary-table-${new Date().toISOString().slice(0, 10)}.json`;
    a.click();
    URL.revokeObjectURL(url);
    useUI.getState().say('Saved table to file');
  };

  const loadFromFile = (e: React.ChangeEvent<HTMLInputElement>) => {
    const f = e.target.files?.[0];
    e.target.value = '';
    if (!f) return;
    const reader = new FileReader();
    reader.onload = () => {
      try {
        const parsed = validate(JSON.parse(String(reader.result)));
        if (!parsed) throw new Error('Invalid save file');
        useTable.getState().load(parsed);
        useUI.getState().say('Loaded table state');
      } catch {
        window.alert('Could not read that table save file.');
      }
    };
    reader.readAsText(f);
  };

  const closeHelp = () => {
    try {
      localStorage.setItem('legendary-table-seen-help', '1');
    } catch {
      /* ignore */
    }
    useUI.getState().set({ helpOpen: false });
  };

  return (
    <>
      <header className="lc-toolbar" data-nodrop>
        <div className="lc-toolbar-group">
          <button
            className={boxOpen ? 'lc-btn is-accent' : 'lc-btn'}
            onClick={() => useUI.getState().set({ boxOpen: !boxOpen })}
            title="Toggle Game Box Drawer (B)"
          >
            <Package className="w-4 h-4" />
            <span>Box</span>
          </button>

          <div className="lc-sep" />

          <button
            className="lc-btn"
            disabled={!canUndo}
            onClick={() => useTable.getState().undo()}
            title="Undo (Ctrl+Z)"
          >
            <Undo2 className="w-4 h-4" />
          </button>
          <button
            className="lc-btn"
            disabled={!canRedo}
            onClick={() => useTable.getState().redo()}
            title="Redo (Ctrl+Y)"
          >
            <Redo2 className="w-4 h-4" />
          </button>
        </div>

        <div className="lc-toolbar-center">
          <span className="lc-brand">LEGENDARY ENCOUNTERS: PREDATOR</span>
          <span className="lc-subbrand">TABLETOP SANDBOX</span>
        </div>

        <div className="lc-toolbar-group">
          {/* Player mats */}
          <div className="lc-pill" title="Number of Player Mats on the table">
            <Users className="w-3.5 h-3.5 text-emerald-400" />
            <span>Mats: {playerMats}</span>
            <button
              onClick={() => useTable.getState().setPlayerMats(playerMats - 1)}
              disabled={playerMats <= 0}
            >
              <Minus className="w-3 h-3" />
            </button>
            <button
              onClick={() => useTable.getState().setPlayerMats(playerMats + 1)}
              disabled={playerMats >= 5}
            >
              <Plus className="w-3 h-3" />
            </button>
          </div>

          {/* Camera zoom */}
          <div className="lc-pill">
            <button onClick={() => zoomByFactor(1 / 1.2)} title="Zoom Out (-)">
              <ZoomOut className="w-3.5 h-3.5" />
            </button>
            <span className="w-11 text-center">{Math.round(zoom * 100)}%</span>
            <button onClick={() => zoomByFactor(1.2)} title="Zoom In (+)">
              <ZoomIn className="w-3.5 h-3.5" />
            </button>
            <button onClick={() => fitTableToViewport()} title="Fit Table to Screen (0)">
              <Maximize2 className="w-3.5 h-3.5" />
            </button>
          </div>

          <button
            className={previewOn ? 'lc-btn is-accent' : 'lc-btn'}
            onClick={() => useUI.getState().set({ previewOn: !previewOn })}
            title="Toggle Corner Magnifier when hovering face-up cards"
          >
            {previewOn ? <Eye className="w-4 h-4" /> : <EyeOff className="w-4 h-4" />}
            <span className="hidden md:inline">Magnifier</span>
          </button>

          <button className="lc-btn" onClick={saveToFile} title="Save Table to File">
            <Save className="w-4 h-4" />
          </button>
          <button className="lc-btn" onClick={() => fileRef.current?.click()} title="Load Table from File">
            <FolderOpen className="w-4 h-4" />
          </button>
          <input ref={fileRef} type="file" accept=".json" className="hidden" onChange={loadFromFile} />

          <button
            className="lc-btn is-danger"
            disabled={pilesCount === 0}
            onClick={() => {
              if (window.confirm('Return all cards on the table and in hands back to the box?')) {
                useTable.getState().reset();
                useUI.getState().say('Table cleared — all cards returned to box');
              }
            }}
            title="Pack up / Reset Table"
          >
            <RotateCcw className="w-4 h-4" />
            <span className="hidden lg:inline">Clear</span>
          </button>

          <button
            className="lc-btn"
            onClick={() => useUI.getState().set({ helpOpen: true })}
            title="Controls & Tabletop Guide (?)"
          >
            <CircleHelp className="w-4 h-4 text-amber-400" />
          </button>
        </div>
      </header>

      {helpOpen && (
        <div className="lc-modal-backdrop" data-nodrop onClick={closeHelp}>
          <div className="lc-help-dialog" onClick={(e) => e.stopPropagation()}>
            <div className="lc-inspect-bar">
              <div>
                <div className="lc-inspect-title">Tabletop Controls & Shortcuts</div>
                <div className="lc-inspect-sub">
                  Pure Sandbox — move, stack, flip, shuffle and draw any card yourself
                </div>
              </div>
              <button className="lc-btn" onClick={closeHelp}>
                <X className="w-4 h-4" />
                <span>Got It</span>
              </button>
            </div>

            <div className="lc-help-grid">
              <div className="lc-help-card">
                <h4>Handling Cards & Piles</h4>
                <ul>
                  <li><strong>Drag a pile:</strong> picks up the <em>top card</em></li>
                  <li><strong>Shift + Drag a pile:</strong> moves the <em>entire pile</em></li>
                  <li><strong>Drop onto another card/pile:</strong> stacks on top</li>
                  <li><strong>Double-click (or <kbd>F</kbd>):</strong> flips card / entire pile</li>
                  <li><strong><kbd>Shift</kbd> + <kbd>F</kbd>:</strong> flips only the top card of a deck</li>
                  <li><strong>Right-click any card/pile:</strong> full menu (Search, Spread, Shuffle, Label…)</li>
                </ul>
              </div>

              <div className="lc-help-card">
                <h4>Hover Shortcuts (Point at a Pile)</h4>
                <ul>
                  <li><kbd>S</kbd> — Shuffle the hovered deck/pile (with riffle animation)</li>
                  <li><kbd>D</kbd> or <kbd>1</kbd>–<kbd>9</kbd> — Draw 1 (or 1–9) cards to active hand</li>
                  <li><kbd>Z</kbd> — Inspect full-resolution card art</li>
                  <li><kbd>R</kbd> — Rotate card/pile 90°</li>
                  <li><kbd>Del</kbd> / <kbd>Backspace</kbd> — Return hovered or selected pile to Box</li>
                </ul>
              </div>

              <div className="lc-help-card">
                <h4>Camera & Multi-Select</h4>
                <ul>
                  <li><strong>Drag empty table</strong> (or hold <kbd>Space</kbd> / Middle Mouse) — Pan camera</li>
                  <li><strong>Mouse Wheel</strong> — Zoom in/out at cursor (<kbd>0</kbd> fits table)</li>
                  <li><strong><kbd>Shift</kbd> + Drag on empty table</strong> — Draw a selection box around multiple cards</li>
                  <li><strong><kbd>G</kbd></strong> — Gather all selected cards into a single pile</li>
                  <li><strong><kbd>Ctrl+Z</kbd> / <kbd>Ctrl+Y</kbd></strong> — Undo / Redo any move</li>
                </ul>
              </div>
            </div>
          </div>
        </div>
      )}
    </>
  );
}
