"""Asset pipeline for the native Legendary Encounters desktop game.

Produces everything the C++ game loads at runtime into game/assets/:

  board.png          - full game mat: rulebook printed pages 3 (left) + 4 (right), stitched
  board_layout.json  - board size, card size and every named slot/area in board pixels
  catalog.json       - card definitions + box groups (from game/data/catalog.json)
  cards/...jpg       - card art downscaled for GPU upload (originals are ~1470x2070)
  card_back.png      - card back, cropped from a deck drawn on the mat

Usage:  py tools/build_assets.py [--force] [--debug-overlay PATH]
Requires: pypdf, pillow
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

import pypdf
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
RULES_PDF = ROOT / "predator" / "Legendary_Encounters_Rules-Predator.pdf"
RAW_CARDS = ROOT / "predator" / "assets" / "raw"
CATALOG_SRC = ROOT / "game" / "data" / "catalog.json"
OUT = ROOT / "game" / "assets"

CARD_TEX_HEIGHT = 1024  # runtime texture height; enough for a crisp full-screen inspect

# --- Board stitching ---------------------------------------------------------------
# Printed page 3 == PDF page index 3, printed page 4 == PDF page index 4. In the PDF the
# mat is one spread split over the two pages; each page clips its half at the gutter.
# Placement (from the content streams, 2.5702 px/pt on both images):
#   left : image at x=98.63pt, page clip ends at 576pt  -> visible image px [0, 1227)
#   right: image at x=-1.078pt, page clip starts at 0pt -> visible image px [3, 1237)
LEFT_PAGE, RIGHT_PAGE = 3, 4
LEFT_KEEP = 1227
RIGHT_SKIP = 3
SEAM_X = LEFT_KEEP - RIGHT_SKIP  # stitched_x = right_x + SEAM_X

# --- Board slots, in *stitched, uncropped* pixel coordinates ------------------------
# kind: "slot" holds one pile (deck / single card), "area" holds several spread cards.
# The game converts these to the cropped board space.
def R(x, y, w, h):
    return [x, y, w, h]

def r_(x):  # right-image x -> stitched x
    return x + SEAM_X

WILDS_X0, WILDS_W = 497, 210
ZONES = [
    # left column
    dict(id="objectives", label="Gear / Objectives", kind="slot", tone="neutral", rect=R(280, 203, 180, 260)),
    dict(id="location", label="Traps / Location", kind="slot", tone="neutral", rect=R(280, 570, 247, 180), land=True),
    dict(id="commanders", label="Killer Instincts / Commanders", kind="slot", tone="neutral", rect=R(277, 858, 183, 250)),
    # the wilds (6 spaces, left -> right)
    *[
        dict(id=f"wilds_{i}", label=name, kind="slot", tone="wilds",
             rect=R(WILDS_X0 + i * WILDS_W, 218, WILDS_W, 252))
        for i, name in enumerate(["Hills", "Ruins", "River", "Graveyard", "Underground"])
    ],
    dict(id="combat", label="Combat Zone", kind="area", tone="danger", rect=R(565, 535, 540, 247)),
    dict(id="testing", label="Testing Ground / Operations", kind="area", tone="neutral",
         rect=R(1145, 535, r_(322) - 1145, 247)),
    # HQ: five face-up slots
    *[
        dict(id=f"hq_{i}", label=f"HQ {i + 1}", kind="slot", tone="hq", rect=R(x, 875, 185, 240))
        for i, x in enumerate([540, 728, 917, 1103, 1279])
    ],
    # deck column
    dict(id="prey", label="Prey / Enemies", kind="slot", tone="neutral", rect=R(r_(352), 203, 183, 265)),
    dict(id="strikes", label="Strikes", kind="slot", tone="danger", rect=R(r_(352), 533, 183, 250)),
    dict(id="barracks", label="Armory / Barracks", kind="slot", tone="hq", rect=R(r_(352), 858, 183, 250)),
    # discard / dead piles
    dict(id="dead_prey", label="Dead Prey / Enemies", kind="area", tone="neutral",
         rect=R(r_(568), 203, 579, 250)),
    dict(id="discard_strikes", label="Discarded Strikes", kind="slot", tone="danger",
         rect=R(r_(568), 532, 180, 250)),
    dict(id="discard_hazards", label="Discarded Hazards / Events", kind="area", tone="neutral",
         rect=R(r_(782), 532, 365, 250)),
    dict(id="dead_characters", label="Dead Characters", kind="slot", tone="neutral",
         rect=R(r_(568), 855, 180, 252)),
]

CARD_W, CARD_H = 176, 246  # world size of a portrait card (matches the mat's printed slots)


def log(msg: str) -> None:
    print(msg, flush=True)


def build_board(force: bool) -> tuple[int, int, int, int]:
    """Stitch the two halves, crop to the mat. Returns crop box (x0, y0, x1, y1)."""
    reader = pypdf.PdfReader(str(RULES_PDF))

    def mat_image(page_index: int) -> Image.Image:
        imgs = [im.image for im in reader.pages[page_index].images]
        # page 0 image on these pages is the full-page green background; the mat is the other one
        return max(imgs[1:], key=lambda im: im.size[0] * im.size[1]).convert("RGBA")

    left, right = mat_image(LEFT_PAGE), mat_image(RIGHT_PAGE)
    h = max(left.height, right.height)
    stitched = Image.new("RGBA", (LEFT_KEEP + right.width - RIGHT_SKIP, h), (0, 0, 0, 0))
    stitched.paste(left.crop((0, 0, LEFT_KEEP, left.height)), (0, 0))
    stitched.paste(right.crop((RIGHT_SKIP, 0, right.width, right.height)), (LEFT_KEEP, 0))

    alpha = stitched.getchannel("A").point(lambda v: 255 if v > 8 else 0)
    box = alpha.getbbox()
    assert box, "board has no opaque pixels"
    board = stitched.crop(box)

    dst = OUT / "board.png"
    if force or not dst.exists():
        board.save(dst, optimize=True)
        log(f"board.png  {board.size[0]}x{board.size[1]}  (crop {box})")
    return box


def build_layout(box: tuple[int, int, int, int]) -> list[dict]:
    x0, y0, x1, y1 = box
    zones = []
    for z in ZONES:
        x, y, w, h = z["rect"]
        zones.append({**z, "rect": [x - x0, y - y0, w, h]})
    layout = {
        "version": 1,
        "board": {"w": x1 - x0, "h": y1 - y0},
        "card": {"w": CARD_W, "h": CARD_H},
        "zones": zones,
    }
    (OUT / "board_layout.json").write_text(json.dumps(layout, indent=2), encoding="utf-8")
    log(f"board_layout.json  {len(zones)} zones")
    return zones


def build_card_back(box: tuple[int, int, int, int], force: bool) -> None:
    dst = OUT / "card_back.png"
    if dst.exists() and not force:
        return
    board = Image.open(OUT / "board.png").convert("RGBA")
    x0, y0, _, _ = box
    # top card of the Barracks deck printed on the mat (right image px 360..522 x 871..1089)
    crop = board.crop((r_(360) - x0, 871 - y0, r_(522) - x0, 1089 - y0))
    crop = crop.resize((CARD_W * 2, CARD_H * 2), Image.LANCZOS)
    crop.save(dst)
    log("card_back.png")


def build_catalog_and_cards(force: bool) -> None:
    cat = json.loads(CATALOG_SRC.read_text(encoding="utf-8"))
    out_cards = OUT / "cards"
    made = 0
    for c in cat["cards"]:
        src = RAW_CARDS / c["src"]
        rel = Path(c["src"]).with_suffix(".jpg")
        dst = out_cards / rel
        c["tex"] = "cards/" + rel.as_posix()
        if dst.exists() and not force and dst.stat().st_mtime >= src.stat().st_mtime:
            continue
        dst.parent.mkdir(parents=True, exist_ok=True)
        im = Image.open(src).convert("RGB")
        if c.get("land"):
            # landscape cards: keep the long side at CARD_TEX_HEIGHT
            scale = CARD_TEX_HEIGHT / max(im.size)
        else:
            scale = CARD_TEX_HEIGHT / im.height
        im = im.resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS)
        im.save(dst, quality=90, optimize=True)
        made += 1
    (OUT / "catalog.json").write_text(json.dumps(cat, indent=1), encoding="utf-8")
    log(f"catalog.json  {len(cat['cards'])} cards, {len(cat['groups'])} groups  ({made} textures rebuilt)")


def debug_overlay(path: Path, zones: list[dict]) -> None:
    board = Image.open(OUT / "board.png").convert("RGBA")
    draw = ImageDraw.Draw(board)
    colors = {"wilds": "#00ff66", "danger": "#ff3030", "hq": "#30a0ff", "neutral": "#ffd000"}
    for z in zones:
        x, y, w, h = z["rect"]
        draw.rectangle([x, y, x + w, y + h], outline=colors[z["tone"]], width=4)
        draw.text((x + 6, y + 6), z["id"], fill="white")
    board.save(path)
    log(f"debug overlay -> {path}")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--force", action="store_true", help="rebuild everything")
    ap.add_argument("--debug-overlay", type=Path, help="write board.png with zone rectangles drawn")
    args = ap.parse_args()

    OUT.mkdir(parents=True, exist_ok=True)
    box = build_board(args.force)
    zones = build_layout(box)
    build_card_back(box, args.force)
    build_catalog_and_cards(args.force)
    if args.debug_overlay:
        debug_overlay(args.debug_overlay, zones)
    return 0


if __name__ == "__main__":
    sys.exit(main())
