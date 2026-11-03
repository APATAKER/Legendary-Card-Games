"""
Generate an offline HTML visual reviewer (review.html) for all 331 card assets.
Allows the user to:
- See every card image alongside its group, filename, rulebook status, and quantity.
- Filter by group / category (Characters, Mini-Decks, Strikes, etc.).
- See live card counts per group and total running count towards 700.
- Edit quantities directly and download the updated card_quantities.csv.
"""

from __future__ import annotations

import csv
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
QUANTITIES_CSV = ROOT / "data" / "card_quantities.csv"
GROUPS_CSV = ROOT / "data" / "group_summary.csv"
OUT_HTML = ROOT / "review.html"

HTML_TEMPLATE = """<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Legendary Encounters: Predator — Card Quantities Reviewer</title>
<style>
  :root {
    --bg: #121417;
    --card-bg: #1b1f24;
    --border: #2d333b;
    --accent: #2ea043;
    --accent-warn: #d29922;
    --text: #e6edf3;
    --text-muted: #8b949e;
  }
  * { box-sizing: border-box; margin: 0; padding: 0; }
  body {
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    background: var(--bg);
    color: var(--text);
    padding: 20px;
    line-height: 1.5;
  }
  header {
    max-width: 1400px;
    margin: 0 auto 24px;
    padding-bottom: 16px;
    border-bottom: 1px solid var(--border);
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 16px;
  }
  h1 { font-size: 1.5rem; color: #fff; }
  .stats-bar {
    display: flex;
    gap: 20px;
    align-items: center;
    background: #161b22;
    padding: 10px 16px;
    border-radius: 8px;
    border: 1px solid var(--border);
  }
  .stat-pill {
    display: flex;
    flex-direction: column;
    align-items: center;
  }
  .stat-val { font-size: 1.25rem; font-weight: bold; color: var(--accent); }
  .stat-lbl { font-size: 0.75rem; color: var(--text-muted); text-transform: uppercase; }

  .controls {
    max-width: 1400px;
    margin: 0 auto 20px;
    display: flex;
    gap: 12px;
    flex-wrap: wrap;
    align-items: center;
  }
  select, input, button {
    background: #1c2128;
    color: var(--text);
    border: 1px solid var(--border);
    padding: 8px 12px;
    border-radius: 6px;
    font-size: 0.9rem;
  }
  select:focus, input:focus { outline: none; border-color: var(--accent); }
  button {
    background: #238636;
    color: #fff;
    cursor: pointer;
    font-weight: 600;
  }
  button:hover { background: #2ea043; }
  .btn-outline {
    background: transparent;
    border-color: var(--border);
  }
  .btn-outline:hover { background: #30363d; }

  .grid {
    max-width: 1400px;
    margin: 0 auto;
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(220px, 1fr));
    gap: 16px;
  }
  .card-item {
    background: var(--card-bg);
    border: 1px solid var(--border);
    border-radius: 8px;
    overflow: hidden;
    display: flex;
    flex-direction: column;
    transition: transform 0.15s, border-color 0.15s;
  }
  .card-item:hover {
    transform: translateY(-2px);
    border-color: #444c56;
  }
  .img-wrap {
    background: #0d1117;
    position: relative;
    padding-top: 140%; /* 2.5 x 3.5 aspect ratio = 1.4 */
    overflow: hidden;
  }
  .img-wrap img {
    position: absolute;
    top: 0; left: 0;
    width: 100%; height: 100%;
    object-fit: cover;
  }
  .card-info {
    padding: 10px;
    display: flex;
    flex-direction: column;
    gap: 6px;
    flex: 1;
  }
  .card-name {
    font-size: 0.85rem;
    font-weight: 600;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    title: attr(title);
  }
  .card-group {
    font-size: 0.75rem;
    color: var(--text-muted);
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
  }
  .qty-row {
    margin-top: auto;
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding-top: 6px;
    border-top: 1px solid rgba(255,255,255,0.08);
  }
  .qty-input {
    width: 60px;
    text-align: center;
    font-size: 1rem;
    font-weight: bold;
    padding: 4px;
  }
  .status-badge {
    font-size: 0.7rem;
    padding: 2px 6px;
    border-radius: 4px;
    font-weight: bold;
  }
  .status-confirmed { background: rgba(46, 160, 67, 0.2); color: #3fb950; }
  .status-unconfirmed { background: rgba(210, 153, 34, 0.2); color: #d29922; }
</style>
</head>
<body>

<header>
  <div>
    <h1>Legendary Encounters: Predator — Card Quantities Reviewer</h1>
    <p style="color: var(--text-muted); font-size: 0.85rem;">Review, verify and edit the physical print quantity for each card</p>
  </div>
  <div class="stats-bar">
    <div class="stat-pill">
      <span class="stat-val" id="totalCardsDisplay">0 / 700</span>
      <span class="stat-lbl">Total Cards</span>
    </div>
    <div class="stat-pill">
      <span class="stat-val" style="color: #58a6ff;" id="uniqueCardsDisplay">331</span>
      <span class="stat-lbl">Unique Images</span>
    </div>
    <div class="stat-pill">
      <span class="stat-val" style="color: #3fb950;" id="confirmedCountDisplay">0</span>
      <span class="stat-lbl">Confirmed Qty</span>
    </div>
  </div>
</header>

<div class="controls">
  <label style="font-size: 0.85rem; color: var(--text-muted);">Filter Group:</label>
  <select id="groupSelect" onchange="renderCards()">
    <option value="ALL">All Groups (331 cards)</option>
  </select>

  <label style="font-size: 0.85rem; color: var(--text-muted);">Status:</label>
  <select id="statusSelect" onchange="renderCards()">
    <option value="ALL">All Statuses</option>
    <option value="CONFIRMED">Confirmed only</option>
    <option value="UNCONFIRMED">Unconfirmed only</option>
  </select>

  <input type="text" id="searchInput" placeholder="Search card name..." oninput="renderCards()" style="flex: 1; min-width: 200px;">

  <button onclick="downloadCSV()">Export Updated CSV</button>
  <button class="btn-outline" onclick="applyDefaultEstimates()">Auto-Fill Standard Rarity</button>
</div>

<div class="grid" id="cardGrid"></div>

<!-- Modal for viewing full card -->
<div id="modal" style="display:none; position:fixed; z-index:999; top:0; left:0; width:100%; height:100%; background:rgba(0,0,0,0.85); justify-content:center; align-items:center; cursor:pointer;" onclick="this.style.display='none'">
  <img id="modalImg" style="max-height:90vh; max-width:90vw; border-radius:12px; box-shadow:0 0 30px rgba(0,0,0,0.9);">
</div>

<script>
const CARDS = CARD_DATA_PLACEHOLDER;

function showModal(src) {
  const m = document.getElementById("modal");
  document.getElementById("modalImg").src = src;
  m.style.display = "flex";
}

// Populate group select
const groups = [...new Set(CARDS.map(c => c.group))].sort();
const select = document.getElementById("groupSelect");
groups.forEach(g => {
  const opt = document.createElement("option");
  opt.value = g;
  opt.textContent = g;
  select.appendChild(opt);
});

function updateStats() {
  let total = 0;
  let confirmed = 0;
  CARDS.forEach(c => {
    const q = parseInt(c.qty, 10);
    if (!isNaN(q) && q > 0) {
      total += q;
      if (c.status === "CONFIRMED") confirmed += q;
    }
  });
  const totalDisp = document.getElementById("totalCardsDisplay");
  totalDisp.textContent = `${total} / 700`;
  totalDisp.style.color = total === 700 ? "var(--accent)" : "var(--accent-warn)";
  document.getElementById("confirmedCountDisplay").textContent = confirmed;
}

function onQtyChange(idx, val) {
  const q = parseInt(val, 10);
  CARDS[idx].qty = isNaN(q) || q < 0 ? "" : q;
  updateStats();
}

function renderCards() {
  const groupFilter = document.getElementById("groupSelect").value;
  const statusFilter = document.getElementById("statusSelect").value;
  const searchFilter = document.getElementById("searchInput").value.toLowerCase();
  const grid = document.getElementById("cardGrid");
  grid.innerHTML = "";

  CARDS.forEach((c, idx) => {
    if (groupFilter !== "ALL" && c.group !== groupFilter) return;
    if (statusFilter !== "ALL" && c.status !== statusFilter) return;
    if (searchFilter && !c.filename.toLowerCase().includes(searchFilter) && !c.group.toLowerCase().includes(searchFilter)) return;

    const div = document.createElement("div");
    div.className = "card-item";
    div.innerHTML = `
      <div class="img-wrap" onclick="showModal('${c.local_path}')" style="cursor: pointer;" title="Click to view full size">
        <img src="${c.local_path}" loading="lazy" alt="${c.filename}">
      </div>
      <div class="card-info">
        <div class="card-name" title="${c.filename}">${c.filename.replace(".jpg","").replace(".png","")}</div>
        <div class="card-group" title="${c.group}">${c.group}</div>
        <div class="qty-row">
          <span class="status-badge status-${c.status.toLowerCase()}">${c.status}</span>
          <label style="font-size:0.8rem; display:flex; align-items:center; gap:6px;">
            Qty:
            <input type="number" min="0" max="60" class="qty-input" value="${c.qty || ''}" onchange="onQtyChange(${idx}, this.value)">
          </label>
        </div>
      </div>
    `;
    grid.appendChild(div);
  });
  updateStats();
}

function applyDefaultEstimates() {
  if (!confirm("This will auto-fill standard Legendary Encounters distributions for Characters (1 Rare x1, 1 Uncommon x3, 2 Commons x5 each), Commanders (x2), and Killer Instincts (x2). Proceed?")) return;
  
  CARDS.forEach(c => {
    if (c.group.startsWith("Character:")) {
      if (c.filename.includes("1Rare")) c.qty = 1;
      else if (c.filename.includes("4Uncommon")) c.qty = 3;
      else if (c.filename.includes("2Common") || c.filename.includes("3Common")) c.qty = 5;
      c.status = "ESTIMATED";
    } else if (c.group === "Commanders") {
      c.qty = 2;
      c.status = "ESTIMATED";
    } else if (c.group === "Killer Instincts") {
      c.qty = 2;
      c.status = "ESTIMATED";
    }
  });
  renderCards();
}

function downloadCSV() {
  let csv = "group,local_path,filename,qty,status,basis\\n";
  CARDS.forEach(c => {
    const cleanGroup = `"${c.group.replace(/"/g, '""')}"`;
    const cleanBasis = `"${(c.basis || '').replace(/"/g, '""')}"`;
    csv += `${cleanGroup},${c.local_path},${c.filename},${c.qty || ''},${c.status},${cleanBasis}\\n`;
  });
  const blob = new Blob([csv], { type: "text/csv;charset=utf-8;" });
  const url = URL.createObjectURL(blob);
  const a = document.createElement("a");
  a.href = url;
  a.download = "card_quantities.csv";
  a.click();
}

// Initial render
renderCards();
</script>

</body>
</html>
"""


def main():
    rows = list(csv.DictReader(QUANTITIES_CSV.open(encoding="utf-8")))
    json_data = json.dumps(rows, ensure_ascii=False)
    html = HTML_TEMPLATE.replace("CARD_DATA_PLACEHOLDER", json_data)
    OUT_HTML.write_text(html, encoding="utf-8")
    print(f"Generated visual reviewer: {OUT_HTML}")


if __name__ == "__main__":
    main()
