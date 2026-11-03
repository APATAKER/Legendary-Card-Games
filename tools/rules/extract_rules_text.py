"""Extract the plain text of every rulebook page into predator/data/rules_extracted.json.

The JSON is a list of {"page": <1-based PDF page>, "text": <extracted text>} and is the
input for the other helpers in this folder (find_spaces.py, inspect_rules.py).

Usage:    py tools/rules/extract_rules_text.py
Requires: pypdf
"""
import json
from pathlib import Path

import pypdf

ROOT = Path(__file__).resolve().parents[2]
RULES_PDF = ROOT / "predator" / "Legendary_Encounters_Rules-Predator.pdf"
OUT_JSON = ROOT / "predator" / "data" / "rules_extracted.json"


def extract_pdf_text(path):
    reader = pypdf.PdfReader(path)
    pages = []
    for i, p in enumerate(reader.pages):
        text = p.extract_text() or ""
        pages.append({"page": i + 1, "text": text})
    return pages

rules = extract_pdf_text(RULES_PDF)
with open(OUT_JSON, "w", encoding="utf-8") as f:
    json.dump(rules, f, indent=2)

print(f"Extracted {len(rules)} pages of rules.")
