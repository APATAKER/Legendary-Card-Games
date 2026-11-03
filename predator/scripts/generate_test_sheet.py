"""
Generate a test A4 sheet (PDF + preview PNG) with 9 cards in a 3x3 grid.
- Card size: 2.5 x 3.5 inches (exact poker size).
- Paper: Standard A4 (210 x 297 mm) at 300 DPI.
- Mode: Grayscale (B/W) with enhanced contrast for razor-sharp text.
- Cutting guides: Clean hairline cutting lines for easy straight cuts.
"""

from __future__ import annotations

from pathlib import Path
from PIL import Image, ImageDraw, ImageEnhance

ROOT = Path(__file__).resolve().parents[1]
ASSETS_DIR = ROOT / "assets" / "raw"
OUTPUT_DIR = ROOT / "output"
OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

# --------------------------------------------------------------------------- #
# Dimensions at 300 DPI
# --------------------------------------------------------------------------- #
DPI = 300
INCH_TO_MM = 25.4

# A4: 210 x 297 mm
PAGE_WIDTH = int(round(210 / INCH_TO_MM * DPI))   # 2480 px
PAGE_HEIGHT = int(round(297 / INCH_TO_MM * DPI))  # 3508 px

# Card: 2.5 x 3.5 inches
CARD_WIDTH = int(round(2.5 * DPI))   # 750 px
CARD_HEIGHT = int(round(3.5 * DPI))  # 1050 px

COLS = 3
ROWS = 3

GRID_WIDTH = COLS * CARD_WIDTH       # 2250 px
GRID_HEIGHT = ROWS * CARD_HEIGHT     # 3150 px

# Centered margins
OFFSET_X = (PAGE_WIDTH - GRID_WIDTH) // 2   # 115 px (~9.74 mm)
OFFSET_Y = (PAGE_HEIGHT - GRID_HEIGHT) // 2  # 179 px (~15.16 mm)

# --------------------------------------------------------------------------- #
# 9 Representative cards from various card types
# --------------------------------------------------------------------------- #
TEST_CARDS = [
    ASSETS_DIR / "01_heroes_human" / "03_dutch_predator" / "Dutch_2Common.jpg",
    ASSETS_DIR / "01_heroes_human" / "03_dutch_predator" / "Dutch_1Rare.jpg",
    ASSETS_DIR / "02_heroes_predator" / "01_woodland_intel" / "WoodlandIntel_1Rare.jpg",
    ASSETS_DIR / "03_avatars_and_role_cards" / "Lieutenant.jpg",
    ASSETS_DIR / "04_predator_humans" / "Objective1ExpendableAssets.jpg",
    ASSETS_DIR / "04_predator_humans" / "01_act_1_expendable_assets" / "Act1_SovietAdvisor.jpg",
    ASSETS_DIR / "08_enemy_strike_deck" / "Strike_DeepGash.jpg",
    ASSETS_DIR / "12_starter_cards" / "StartingBruteStrength.jpg",
    ASSETS_DIR / "13_gear" / "HeavyMachineGun.jpg",
]


def prepare_card_image(card_path: Path) -> Image.Image:
    """Load, convert to grayscale, boost text contrast, and resize to exact card dimensions."""
    img = Image.open(card_path)
    
    # Convert to grayscale
    gray = img.convert("L")
    
    # Slight contrast enhancement (1.18x) makes text pop against dark art without blowing highlights
    enhancer = ImageEnhance.Contrast(gray)
    enhanced = enhancer.enhance(1.18)
    
    # High-quality resampling to exact 2.5" x 3.5" (750x1050 px)
    resized = enhanced.resize((CARD_WIDTH, CARD_HEIGHT), Image.Resampling.LANCZOS)
    return resized


def generate_sheet():
    # Create white A4 canvas in grayscale ('L')
    canvas = Image.new("L", (PAGE_WIDTH, PAGE_HEIGHT), color=255)
    
    # Place each card
    for idx, card_path in enumerate(TEST_CARDS):
        if not card_path.exists():
            print(f"Warning: {card_path} not found!")
            continue
            
        r = idx // COLS
        c = idx % COLS
        x = OFFSET_X + c * CARD_WIDTH
        y = OFFSET_Y + r * CARD_HEIGHT
        
        card_img = prepare_card_image(card_path)
        canvas.paste(card_img, (x, y))
        print(f"Placed [{c+1},{r+1}]: {card_path.name}")

    # Draw cutting guides
    draw = ImageDraw.Draw(canvas)
    
    # Guide line color: faint dark gray (value 120) so it guides cutting without being glaring
    line_color = 120
    line_width = 1

    # Vertical cut lines (full page height across the grid columns)
    for c in range(COLS + 1):
        x = OFFSET_X + c * CARD_WIDTH
        # Full cut lines extending past the grid to page margins
        draw.line([(x, 0), (x, PAGE_HEIGHT)], fill=line_color, width=line_width)

    # Horizontal cut lines (full page width across the grid rows)
    for r in range(ROWS + 1):
        y = OFFSET_Y + r * CARD_HEIGHT
        draw.line([(0, y), (PAGE_WIDTH, y)], fill=line_color, width=line_width)

    # Add small footer text at the bottom margin (safe printable area)
    margin_text = "Legendary Encounters: Predator - Test Sheet (2.5\" x 3.5\" Poker Size, 300 DPI A4)"
    draw.text((OFFSET_X, PAGE_HEIGHT - OFFSET_Y // 2), margin_text, fill=100)

    # Save output PDF & preview PNG
    pdf_out = OUTPUT_DIR / "test_sheet_a4.pdf"
    png_out = OUTPUT_DIR / "test_sheet_a4.png"
    
    # Save as PDF with exact 300 DPI resolution metadata
    canvas.save(pdf_out, "PDF", resolution=float(DPI))
    canvas.save(png_out, "PNG")
    
    print("\nSuccessfully generated:")
    print(f"  PDF: {pdf_out}")
    print(f"  PNG: {png_out}")
    print(f"  Page Size: {PAGE_WIDTH}x{PAGE_HEIGHT} px ({PAGE_WIDTH/DPI:.2f}\" x {PAGE_HEIGHT/DPI:.2f}\")")
    print(f"  Card Size: {CARD_WIDTH}x{CARD_HEIGHT} px ({CARD_WIDTH/DPI:.1f}\" x {CARD_HEIGHT/DPI:.1f}\")")


if __name__ == "__main__":
    generate_sheet()
