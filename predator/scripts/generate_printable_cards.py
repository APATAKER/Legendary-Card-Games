"""
Generate print-ready A4 PDF documents for Legendary Encounters: Predator.

Features:
- Exact 2.5" x 3.5" (Poker / TCG size) cards arranged in a 3x3 grid (9 cards per sheet).
- Standard A4 paper (210 x 297 mm) at 300 DPI (2480 x 3508 px canvas).
- Centered layout with generous margins (9.7 mm sides, 15.2 mm top/bottom) avoiding any printer clipping.
- Clean 1-px hairline cutting guide lines extending to the page borders for continuous straight cuts.
- Two versions:
  1. High-Contrast Black & White (Grayscale with 1.18x contrast boost for crisp text legibility).
  2. Full Color (24-bit TrueColor RGB preserving card artwork and class colors).
- Both versions include page footer labels with sheet numbers and card indices.
- Outputs:
  - output/predator_cards_bw_700.pdf (78 pages, 700 cards)
  - output/predator_cards_color_700.pdf (78 pages, 700 cards)
  - output/predator_alien_conversion_kit_bw.pdf (2 pages, 16 cards)
  - output/predator_alien_conversion_kit_color.pdf (2 pages, 16 cards)
  - Page 1 preview PNGs for quick visual inspection.
"""

from __future__ import annotations

import csv
import io
import sys
import time
from pathlib import Path
from PIL import Image, ImageDraw, ImageEnhance, ImageFilter, ImageFont
import pypdf

ROOT = Path(__file__).resolve().parents[1]
QUANTITIES_CSV = ROOT / "data" / "card_quantities.csv"
OUTPUT_DIR = ROOT / "output"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# --------------------------------------------------------------------------- #
# Dimensions at 300 DPI
# --------------------------------------------------------------------------- #
DPI = 300
INCH_TO_MM = 25.4

PAGE_WIDTH = int(round(210 / INCH_TO_MM * DPI))   # 2480 px
PAGE_HEIGHT = int(round(297 / INCH_TO_MM * DPI))  # 3508 px

CARD_WIDTH = int(round(2.5 * DPI))   # 750 px
CARD_HEIGHT = int(round(3.5 * DPI))  # 1050 px

COLS = 3
ROWS = 3
CARDS_PER_PAGE = COLS * ROWS         # 9 cards

GRID_WIDTH = COLS * CARD_WIDTH       # 2250 px
GRID_HEIGHT = ROWS * CARD_HEIGHT     # 3150 px

OFFSET_X = (PAGE_WIDTH - GRID_WIDTH) // 2   # 115 px (~9.74 mm)
OFFSET_Y = (PAGE_HEIGHT - GRID_HEIGHT) // 2  # 179 px (~15.16 mm)

# Load font for footer (fallback to default if necessary)
try:
    FOOTER_FONT = ImageFont.truetype("arial.ttf", 26)
except Exception:
    FOOTER_FONT = ImageFont.load_default()


def load_card_manifest() -> tuple[list[Path], list[Path]]:
    """
    Load and expand cards by quantity from card_quantities.csv.
    Returns:
      (box_cards_list, alien_kit_cards_list)
    """
    rows = list(csv.DictReader(QUANTITIES_CSV.open(encoding="utf-8")))
    box_cards: list[Path] = []
    alien_kit: list[Path] = []

    for r in rows:
        path = ROOT / r["local_path"]
        if not path.exists():
            print(f"Warning: File not found: {path}")
            continue

        is_alien_kit = "Alien Conversion" in r["group"]
        if is_alien_kit:
            # Alien kit promo cards: 1 copy each
            alien_kit.append(path)
        else:
            qty_str = r.get("qty", "").strip()
            qty = int(qty_str) if qty_str.isdigit() else 0
            for _ in range(qty):
                box_cards.append(path)

    return box_cards, alien_kit


def prepare_card(card_path: Path, mode: str) -> Image.Image:
    """
    Load, orient to portrait (if landscape), resize with Lanczos to exact 750x1050 px,
    enhance text contrast for B/W, and apply subtle sharpening.
    """
    with Image.open(card_path) as raw:
        img = raw.copy()

    # Rotate landscape cards to portrait if needed
    if img.width > img.height:
        img = img.rotate(270, expand=True)

    if mode == "BW":
        gray = img.convert("L")
        # 1.18x contrast enhancement makes text pop without blowing highlights
        gray_enhanced = ImageEnhance.Contrast(gray).enhance(1.18)
        resized = gray_enhanced.resize((CARD_WIDTH, CARD_HEIGHT), Image.Resampling.LANCZOS)
        # Subtle unsharp mask to crisp up small card text and icons
        final = resized.filter(ImageFilter.UnsharpMask(radius=1.0, percent=115, threshold=3))
        return final
    else:
        rgb = img.convert("RGB")
        resized = rgb.resize((CARD_WIDTH, CARD_HEIGHT), Image.Resampling.LANCZOS)
        final = resized.filter(ImageFilter.UnsharpMask(radius=1.0, percent=115, threshold=3))
        return final


def render_page(
    page_cards: list[Image.Image],
    page_num: int,
    total_pages: int,
    card_start_idx: int,
    card_end_idx: int,
    total_cards: int,
    mode: str,
    doc_title: str,
) -> Image.Image:
    """Render a single A4 sheet with up to 9 cards, cutting lines, and footer."""
    if mode == "BW":
        canvas = Image.new("L", (PAGE_WIDTH, PAGE_HEIGHT), color=255)
        line_color = 120
        footer_color = 100
    else:
        canvas = Image.new("RGB", (PAGE_WIDTH, PAGE_HEIGHT), color=(255, 255, 255))
        line_color = (130, 130, 130)
        footer_color = (110, 110, 110)

    # Paste cards in 3x3 grid
    for idx, card_img in enumerate(page_cards):
        r = idx // COLS
        c = idx % COLS
        x = OFFSET_X + c * CARD_WIDTH
        y = OFFSET_Y + r * CARD_HEIGHT
        canvas.paste(card_img, (x, y))

    draw = ImageDraw.Draw(canvas)
    line_width = 1

    # Vertical cutting lines (across columns)
    for c in range(COLS + 1):
        x = OFFSET_X + c * CARD_WIDTH
        draw.line([(x, 0), (x, PAGE_HEIGHT)], fill=line_color, width=line_width)

    # Horizontal cutting lines (across rows)
    for r in range(ROWS + 1):
        y = OFFSET_Y + r * CARD_HEIGHT
        draw.line([(0, y), (PAGE_WIDTH, y)], fill=line_color, width=line_width)

    # Footer note at bottom margin
    footer_text = (
        f"{doc_title} — Sheet {page_num} of {total_pages} "
        f"(Cards {card_start_idx}–{card_end_idx} of {total_cards}) [{mode}]"
    )
    draw.text((OFFSET_X, PAGE_HEIGHT - OFFSET_Y // 2 - 12), footer_text, fill=footer_color, font=FOOTER_FONT)

    return canvas


def build_pdf(cards: list[Path], mode: str, pdf_out: Path, preview_png: Path | None, doc_title: str):
    """
    Build multi-page PDF document from list of card paths.
    Caches processed card images in memory to minimize redundant computation.
    """
    total_cards = len(cards)
    total_pages = (total_cards + CARDS_PER_PAGE - 1) // CARDS_PER_PAGE
    print(f"\n==================================================================")
    print(f"Generating: {pdf_out.name} ({mode})")
    print(f"Total Cards: {total_cards} | Total Sheets: {total_pages} pages")
    print(f"==================================================================")

    # Pre-cache unique card images
    unique_paths = sorted(set(cards))
    print(f"Caching {len(unique_paths)} unique card images in memory...")
    cache: dict[Path, Image.Image] = {}
    t0 = time.time()
    for i, path in enumerate(unique_paths, start=1):
        cache[path] = prepare_card(path, mode)
        if i % 50 == 0 or i == len(unique_paths):
            print(f"  Processed {i}/{len(unique_paths)} unique images ({round(time.time() - t0, 1)}s)")

    writer = pypdf.PdfWriter()

    for page_idx in range(total_pages):
        start_idx = page_idx * CARDS_PER_PAGE
        end_idx = min(start_idx + CARDS_PER_PAGE, total_cards)
        page_card_paths = cards[start_idx:end_idx]
        page_card_images = [cache[p] for p in page_card_paths]

        page_image = render_page(
            page_cards=page_card_images,
            page_num=page_idx + 1,
            total_pages=total_pages,
            card_start_idx=start_idx + 1,
            card_end_idx=end_idx,
            total_cards=total_cards,
            mode=mode,
            doc_title=doc_title,
        )

        # Save first page preview PNG
        if page_idx == 0 and preview_png:
            page_image.save(preview_png, "PNG")
            print(f"  Saved preview: {preview_png.name}")

        # Stream page image to in-memory PDF buffer
        buf = io.BytesIO()
        page_image.save(buf, "PDF", resolution=float(DPI))
        buf.seek(0)
        writer.append(buf)

        if (page_idx + 1) % 15 == 0 or (page_idx + 1) == total_pages:
            print(f"  Assembled sheet {page_idx + 1}/{total_pages}...")

    print(f"Writing final PDF to disk: {pdf_out}...")
    with pdf_out.open("wb") as f:
        writer.write(f)

    file_size_mb = pdf_out.stat().st_size / (1024 * 1024)
    print(f"Done! {pdf_out.name} ({file_size_mb:.1f} MB) in {round(time.time() - t0, 1)} seconds.")


def main():
    box_cards, alien_kit = load_card_manifest()
    print(f"Loaded {len(box_cards)} box cards (Target: 700 cards).")
    print(f"Loaded {len(alien_kit)} Alien Conversion Kit promo cards.")

    if len(box_cards) != 700:
        print(f"Error: Expected 700 box cards, found {len(box_cards)}!")
        sys.exit(1)

    # 1. Base Game 700 Cards - Black & White
    build_pdf(
        cards=box_cards,
        mode="BW",
        pdf_out=OUTPUT_DIR / "predator_cards_bw_700.pdf",
        preview_png=OUTPUT_DIR / "preview_bw_page1.png",
        doc_title="Legendary Encounters: Predator",
    )

    # 2. Base Game 700 Cards - Full Color
    build_pdf(
        cards=box_cards,
        mode="COLOR",
        pdf_out=OUTPUT_DIR / "predator_cards_color_700.pdf",
        preview_png=OUTPUT_DIR / "preview_color_page1.png",
        doc_title="Legendary Encounters: Predator",
    )

    # 3. Alien Conversion Kit Web Extra (16 cards, 2 pages)
    if alien_kit:
        build_pdf(
            cards=alien_kit,
            mode="BW",
            pdf_out=OUTPUT_DIR / "predator_alien_conversion_kit_bw.pdf",
            preview_png=None,
            doc_title="Legendary Encounters: Alien Conversion Kit",
        )
        build_pdf(
            cards=alien_kit,
            mode="COLOR",
            pdf_out=OUTPUT_DIR / "predator_alien_conversion_kit_color.pdf",
            preview_png=None,
            doc_title="Legendary Encounters: Alien Conversion Kit",
        )

    print("\nAll printable documents have been successfully generated!")


if __name__ == "__main__":
    main()
