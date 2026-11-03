// Core data model for the tabletop sandbox.
// Nothing here knows any game rules: it's just cards, piles, hands and a box.

/** One distinct card image (a "printing"). `qty` physical copies exist in the box. */
export interface CardDef {
  id: number;
  name: string;
  group: string; // BoxGroup.key: where the card lives in the box
  img: string; // full-resolution art (/assets/raw/...)
  thumb: string; // table thumbnail (/thumbs/...webp)
  land: boolean; // landscape art
  qty: number;
}

export interface BoxGroup {
  key: string;
  label: string;
  section: string;
  faceUp: boolean; // orientation when a whole group is pulled out of the box
  cards: number[]; // CardDef ids
}

export interface BoxSection {
  key: string;
  label: string;
}

/** One physical card. */
export interface CardInst {
  uid: string;
  def: number; // CardDef id
  faceUp: boolean;
}

/** Anything lying on the table: a single card is just a pile of 1. */
export interface Pile {
  id: string;
  x: number; // world coords of the top-left corner
  y: number;
  cards: CardInst[]; // bottom -> top (last element is the visible top card)
  rot: 0 | 90;
  label?: string;
}

export interface Hand {
  id: string;
  name: string;
  cards: CardInst[];
}

/** Everything that is saved / undoable. */
export interface TableState {
  version: 1;
  piles: Pile[]; // array order = z-order (last is on top)
  hands: Hand[];
  activeHand: number;
  box: Record<string, CardInst[]>; // BoxGroup.key -> cards still in the box
  playerMats: number;
}

export interface Rect {
  x: number;
  y: number;
  w: number;
  h: number;
}

export interface Zone extends Rect {
  id: string;
  label: string;
  kind: 'slot' | 'area';
  tone?: 'wilds' | 'danger' | 'hq' | 'player' | 'neutral';
}
