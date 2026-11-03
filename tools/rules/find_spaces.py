"""Print every rulebook line that mentions starting-deck / personal-deck keywords.

Usage: py tools/rules/find_spaces.py   (run extract_rules_text.py first)
"""
import json
from pathlib import Path

RULES_JSON = Path(__file__).resolve().parents[2] / "predator" / "data" / "rules_extracted.json"

with open(RULES_JSON, encoding="utf-8") as f:
    rules = json.load(f)

for p in rules:
    for line in p["text"].split("\n"):
        if any(w in line.lower() for w in ["starting deck", "13 cards", "personal deck", "brute strength", "experience"]):
            print(f"P{p['page']}: {line.strip()}")
