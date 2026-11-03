"""
Download every card image from the official Legendary Encounters: Predator page.

Source: https://www.legendarycardgame.com/predator

Images are saved into folders that mirror the page's own section headings
(h1 = section, h4 = sub-section), in the order they appear on the page.
A manifest CSV records the provenance of every file.

Re-runnable: files that already exist with the expected size are skipped.
Uses only the Python standard library.
"""

from __future__ import annotations

import csv
import hashlib
import re
import struct
import sys
import time
import urllib.request
from dataclasses import dataclass, field
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlparse

PAGE_URL = "https://www.legendarycardgame.com/predator"
USER_AGENT = (
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    "(KHTML, like Gecko) Chrome/130 Safari/537.36"
)

ROOT = Path(__file__).resolve().parents[1]  # .../predator
ASSETS_DIR = ROOT / "assets" / "raw"
DATA_DIR = ROOT / "data"
SOURCE_DIR = ROOT / "source"
MANIFEST = DATA_DIR / "manifest.csv"


# --------------------------------------------------------------------------- #
# HTML parsing
# --------------------------------------------------------------------------- #
@dataclass
class CardImage:
    section: str
    subsection: str
    url: str
    page_dimensions: str  # "WxH" as declared by the page (data-image-dimensions)
    order: int = 0  # position within (section, subsection)


@dataclass
class _State:
    section: str = ""
    subsection: str = ""
    in_heading: str | None = None
    heading_text: list[str] = field(default_factory=list)


class PredatorPageParser(HTMLParser):
    """Walk the page linearly: track the latest h1/h4 and collect gallery images."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.s = _State()
        self.images: list[CardImage] = []
        self._seen: set[tuple[str, str, str]] = set()

    def handle_starttag(self, tag, attrs):
        if tag in ("h1", "h4"):
            self.s.in_heading = tag
            self.s.heading_text = []
            return
        if tag == "img":
            a = dict(attrs)
            url = a.get("data-image")
            if not url or not self.s.section:
                return  # skip site logo etc. that appear before the first section
            key = (self.s.section, self.s.subsection, url)
            if key in self._seen:  # each card is rendered twice (grid + lightbox)
                return
            self._seen.add(key)
            self.images.append(
                CardImage(
                    section=self.s.section,
                    subsection=self.s.subsection,
                    url=url,
                    page_dimensions=a.get("data-image-dimensions", ""),
                )
            )

    def handle_data(self, data):
        if self.s.in_heading:
            self.s.heading_text.append(data)

    def handle_endtag(self, tag):
        if tag != self.s.in_heading:
            return
        text = re.sub(r"\s+", " ", "".join(self.s.heading_text)).strip()
        self.s.in_heading = None
        if not text:
            return
        if tag == "h1":
            self.s.section = text
            self.s.subsection = ""  # new h1 resets the sub-section
        else:
            self.s.subsection = text


# --------------------------------------------------------------------------- #
# Helpers
# --------------------------------------------------------------------------- #
def http_get(url: str, retries: int = 3) -> bytes:
    last_err: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
            with urllib.request.urlopen(req, timeout=60) as r:
                return r.read()
        except Exception as e:  # noqa: BLE001
            last_err = e
            time.sleep(2 * attempt)
    raise RuntimeError(f"GET failed after {retries} attempts: {url}: {last_err}")


def slug(text: str) -> str:
    text = text.lower().replace("&", "and")
    text = re.sub(r"[^a-z0-9]+", "_", text)
    return text.strip("_")


def jpeg_size(data: bytes) -> tuple[int, int] | None:
    """Return (width, height) from a JPEG's SOF marker, or None."""
    if data[:2] != b"\xff\xd8":
        return None
    i = 2
    while i < len(data) - 9:
        if data[i] != 0xFF:
            i += 1
            continue
        marker = data[i + 1]
        if marker in (0xC0, 0xC1, 0xC2, 0xC3, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCD, 0xCE, 0xCF):
            h, w = struct.unpack(">HH", data[i + 5 : i + 9])
            return w, h
        seg_len = struct.unpack(">H", data[i + 2 : i + 4])[0]
        i += 2 + seg_len
    return None


def png_size(data: bytes) -> tuple[int, int] | None:
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        return None
    w, h = struct.unpack(">II", data[16:24])
    return w, h


# --------------------------------------------------------------------------- #
# Main
# --------------------------------------------------------------------------- #
def main() -> int:
    for d in (ASSETS_DIR, DATA_DIR, SOURCE_DIR):
        d.mkdir(parents=True, exist_ok=True)

    print(f"Fetching {PAGE_URL} ...")
    html = http_get(PAGE_URL).decode("utf-8", errors="replace")
    (SOURCE_DIR / "predator_page.html").write_text(html, encoding="utf-8")

    parser = PredatorPageParser()
    parser.feed(html)
    images = parser.images
    print(f"Found {len(images)} card images on the page.")

    # Assign folder numbers in page order.
    section_idx: dict[str, int] = {}
    sub_idx: dict[tuple[str, str], int] = {}
    order_in_group: dict[tuple[str, str], int] = {}
    for img in images:
        section_idx.setdefault(img.section, len(section_idx) + 1)
        if img.subsection:
            subs_in_sec = [k for k in sub_idx if k[0] == img.section]
            sub_idx.setdefault((img.section, img.subsection), len(subs_in_sec) + 1)
        g = (img.section, img.subsection)
        order_in_group[g] = order_in_group.get(g, 0) + 1
        img.order = order_in_group[g]

    rows = []
    downloaded = skipped = 0
    for n, img in enumerate(images, 1):
        sec_dir = f"{section_idx[img.section]:02d}_{slug(img.section)}"
        folder = ASSETS_DIR / sec_dir
        if img.subsection:
            folder = folder / f"{sub_idx[(img.section, img.subsection)]:02d}_{slug(img.subsection)}"
        folder.mkdir(parents=True, exist_ok=True)

        filename = unquote(Path(urlparse(img.url).path).name)
        dest = folder / filename

        if dest.exists() and dest.stat().st_size > 0:
            data = dest.read_bytes()
            skipped += 1
            status = "cached"
        else:
            data = http_get(img.url)
            dest.write_bytes(data)
            downloaded += 1
            status = "downloaded"

        size = jpeg_size(data) or png_size(data)
        actual_dims = f"{size[0]}x{size[1]}" if size else "unknown"
        rows.append(
            {
                "section": img.section,
                "subsection": img.subsection,
                "order_on_page": img.order,
                "filename": filename,
                "local_path": dest.relative_to(ROOT).as_posix(),
                "page_dimensions": img.page_dimensions,
                "actual_dimensions": actual_dims,
                "bytes": len(data),
                "sha256": hashlib.sha256(data).hexdigest(),
                "url": img.url,
            }
        )
        print(f"[{n:3d}/{len(images)}] {status:10s} {dest.relative_to(ROOT).as_posix()} ({actual_dims})")

    with MANIFEST.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys()))
        w.writeheader()
        w.writerows(rows)

    mismatched = [r for r in rows if r["page_dimensions"] and r["page_dimensions"] != r["actual_dimensions"]]
    print()
    print(f"Done. downloaded={downloaded} cached={skipped} total={len(rows)}")
    print(f"Manifest: {MANIFEST.relative_to(ROOT).as_posix()}")
    if mismatched:
        print(f"WARNING: {len(mismatched)} files differ from the page's declared dimensions:")
        for r in mismatched:
            print(f"  {r['local_path']}: page={r['page_dimensions']} actual={r['actual_dimensions']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
