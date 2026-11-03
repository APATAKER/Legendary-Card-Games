"""Dump the extracted text of rulebook pages 13-15.

Usage: py tools/rules/inspect_rules.py   (run extract_rules_text.py first)
"""
import json
from pathlib import Path

RULES_JSON = Path(__file__).resolve().parents[2] / "predator" / "data" / "rules_extracted.json"

with open(RULES_JSON, encoding="utf-8") as f:
    rules = json.load(f)

for p in rules:
    if 13 <= p["page"] <= 15:
        print(f"\n==================== PAGE {p['page']} ====================")
        print(p["text"])
