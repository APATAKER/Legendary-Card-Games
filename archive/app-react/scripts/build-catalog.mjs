// Builds the tabletop card catalog + table thumbnails.
//
//   node scripts/build-catalog.mjs          (skips thumbnails that already exist)
//   node scripts/build-catalog.mjs --force  (regenerates every thumbnail)
//
// Input : ../predator/data/card_quantities.csv  (group, local_path, filename, qty, ...)
// Output: src/table/catalog.generated.ts        (typed card + box-group data)
//         public/thumbs/**/*.webp                (≈480px wide images for the table)
//
// Full-resolution art is still served from /assets/raw/... (public/assets is a junction
// to predator/assets) and is only loaded when a card is inspected / previewed.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import sharp from 'sharp';

const here = path.dirname(fileURLToPath(import.meta.url));
const appDir = path.resolve(here, '..');
const predatorDir = path.resolve(appDir, '..', '..', 'predator'); // app lives in archive/app-react
const csvPath = path.join(predatorDir, 'data', 'card_quantities.csv');
const outTs = path.join(appDir, 'src', 'table', 'catalog.generated.ts');
const thumbRoot = path.join(appDir, 'public', 'thumbs');
const FORCE = process.argv.includes('--force');
const THUMB_WIDTH = 480;

// ---------- CSV ----------
function parseCsv(text) {
  const rows = [];
  let row = [], field = '', inQuotes = false;
  for (let i = 0; i < text.length; i++) {
    const c = text[i];
    if (inQuotes) {
      if (c === '"' && text[i + 1] === '"') { field += '"'; i++; }
      else if (c === '"') inQuotes = false;
      else field += c;
    } else if (c === '"') inQuotes = true;
    else if (c === ',') { row.push(field); field = ''; }
    else if (c === '\n' || c === '\r') {
      if (c === '\r' && text[i + 1] === '\n') i++;
      row.push(field); field = '';
      if (row.some((f) => f !== '')) rows.push(row);
      row = [];
    } else field += c;
  }
  if (field !== '' || row.length) { row.push(field); rows.push(row); }
  const [header, ...body] = rows;
  return body.map((r) => Object.fromEntries(header.map((h, i) => [h, r[i] ?? ''])));
}

// ---------- naming helpers ----------
const splitCamel = (s) =>
  s
    .replace(/\+/g, ' ')
    .replace(/[_-]+/g, ' ')
    .replace(/([a-z])([A-Z0-9])/g, '$1 $2')
    .replace(/([0-9])([A-Za-z])/g, '$1 $2')
    .replace(/([A-Z]+)([A-Z][a-z])/g, '$1 $2')
    .replace(/\s+/g, ' ')
    .trim();

function cardName(filename) {
  let base = filename.replace(/\.[^.]+$/, '');
  base = base.replace(/^Act\d_/, '').replace(/^(Strike|PreyStrike|YoungBlood|Mercenary)_/, '');
  base = base.replace(/^Starting/, '');
  return splitCamel(base)
    .replace(/\bO W L F\b/g, 'OWLF')
    .replace(/\bC I A\b/g, 'CIA');
}

// Box sections, in drawer order.
const SECTIONS = [
  { key: 'setup', label: 'Locations & Objectives' },
  { key: 'roles', label: 'Avatars & Roles' },
  { key: 'starter', label: 'Starting Decks & Commanders' },
  { key: 'humans', label: 'Barracks: Human Characters' },
  { key: 'predators', label: 'Barracks: Predator Characters' },
  { key: 'enemy-h', label: 'Enemy Mini-decks (Play as Humans)' },
  { key: 'enemy-p', label: 'Prey Mini-decks (Play as Predators)' },
  { key: 'strikes', label: 'Strikes, Young Blood & Mercenaries' },
  { key: 'hunt', label: 'Predator Mode: Gear, Traps, Challenges, Tests' },
  { key: 'extras', label: 'Extras: Alien Conversion Kit' },
];

// Map one CSV row -> { groupKey, groupLabel, section, faceUp }
function classify(row) {
  const g = row.group;
  const file = row.filename;
  if (g.startsWith('Avatars')) {
    const isPred = /Hunter/.test(file);
    const isRole = /RoleCard/.test(file);
    const side = isPred ? 'Predator' : 'Human';
    const kind = isRole ? 'Role Cards' : 'Avatars';
    return { groupKey: `roles-${side}-${kind}`.toLowerCase().replace(/\s+/g, '-'), groupLabel: `${side} ${kind}`, section: 'roles', faceUp: true };
  }
  if (g.startsWith('Locations')) {
    const p2 = row.local_path.includes('05_predator_2');
    return { groupKey: p2 ? 'setup-p2' : 'setup-p1', groupLabel: p2 ? 'Predator 2 (Los Angeles)' : 'Predator (Val Verde)', section: 'setup', faceUp: true };
  }
  if (g.startsWith('Character:')) {
    const name = g.replace('Character: ', '');
    const pred = /^(Woodland|Urban)/.test(name);
    return { groupKey: 'char-' + name.toLowerCase().replace(/[^a-z0-9]+/g, '-'), groupLabel: name, section: pred ? 'predators' : 'humans', faceUp: false };
  }
  if (g.startsWith('Mini-deck:')) {
    const m = g.match(/Mini-deck: (.+?) \((Humans|Predators)\) \/ (.+)$/);
    const film = m[1];
    const side = m[2];
    const act = m[3];
    return {
      groupKey: 'mini-' + g.toLowerCase().replace(/[^a-z0-9]+/g, '-'),
      groupLabel: `${film}: ${act}`,
      section: side === 'Humans' ? 'enemy-h' : 'enemy-p',
      faceUp: false,
    };
  }
  if (g.startsWith('Alien Conversion')) return { groupKey: 'extras-alien', groupLabel: 'Alien Conversion Kit', section: 'extras', faceUp: true };
  const simple = {
    'Enemy Strike': ['strikes', false],
    'Prey Strike': ['strikes', false],
    'Young Blood': ['strikes', false],
    Mercenary: ['strikes', false],
    'Starting: Brute Strength': ['starter', true],
    'Starting: Experience': ['starter', true],
    Commanders: ['starter', false],
    'Killer Instincts': ['starter', false],
    Gear: ['hunt', false],
    Traps: ['hunt', false],
    Challenges: ['hunt', false],
    Tests: ['hunt', false],
  };
  const hit = simple[g];
  if (!hit) throw new Error('Unclassified group: ' + g);
  return { groupKey: 'g-' + g.toLowerCase().replace(/[^a-z0-9]+/g, '-'), groupLabel: g.replace('Starting: ', 'Starter: '), section: hit[0], faceUp: hit[1] };
}

// ---------- main ----------
const rows = parseCsv(fs.readFileSync(csvPath, 'utf8'));
const cards = [];
const groups = new Map();

for (const row of rows) {
  const qtyRaw = parseInt(row.qty, 10);
  const { groupKey, groupLabel, section, faceUp } = classify(row);
  const qty = Number.isFinite(qtyRaw) && qtyRaw > 0 ? qtyRaw : 1; // extras have no qty -> 1 each
  const rel = row.local_path.replace(/\\/g, '/'); // assets/raw/...
  const thumbRel = rel.replace(/^assets\/raw\//, '').replace(/\.[^.]+$/, '.webp');
  const def = {
    id: cards.length,
    name: cardName(row.filename),
    group: groupKey,
    img: '/' + rel,
    thumb: '/thumbs/' + thumbRel,
    land: false,
    qty,
  };
  cards.push(def);
  if (!groups.has(groupKey)) groups.set(groupKey, { key: groupKey, label: groupLabel, section, faceUp, cards: [] });
  groups.get(groupKey).cards.push(def.id);
}

// Thumbnails (+ orientation)
let made = 0, skipped = 0;
for (const def of cards) {
  const src = path.join(predatorDir, def.img.slice(1));
  const dst = path.join(appDir, 'public', def.thumb.slice(1));
  const meta = await sharp(src).metadata();
  def.land = (meta.width ?? 0) > (meta.height ?? 0);
  if (!FORCE && fs.existsSync(dst)) { skipped++; continue; }
  fs.mkdirSync(path.dirname(dst), { recursive: true });
  await sharp(src)
    .resize({ width: def.land ? Math.round(THUMB_WIDTH * 1.4) : THUMB_WIDTH })
    .webp({ quality: 82 })
    .toFile(dst);
  made++;
}

const groupList = [...groups.values()];
const sectionList = SECTIONS.filter((s) => groupList.some((g) => g.section === s.key));
const total = cards.reduce((n, c) => n + c.qty, 0);

const ts = `// AUTO-GENERATED by scripts/build-catalog.mjs - do not edit by hand.
// ${cards.length} distinct cards, ${total} physical cards.
import type { CardDef, BoxGroup, BoxSection } from './types';

export const CARDS: CardDef[] = ${JSON.stringify(cards, null, 0).replace(/},{/g, '},\n  {')};

export const GROUPS: BoxGroup[] = ${JSON.stringify(groupList, null, 2)};

export const SECTIONS: BoxSection[] = ${JSON.stringify(sectionList, null, 2)};
`;
fs.mkdirSync(path.dirname(outTs), { recursive: true });
fs.writeFileSync(outTs, ts);
console.log(`Catalog: ${cards.length} distinct / ${total} physical cards, ${groupList.length} groups.`);
console.log(`Thumbnails: ${made} created, ${skipped} already existed -> ${thumbRoot}`);
