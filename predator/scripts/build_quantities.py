"""
Build data/card_quantities.csv from data/manifest.csv.

Rule: NEVER guess. A quantity is filled in only when it is
  (a) stated explicitly in the rulebook, or
  (b) mathematically forced: the rulebook gives a group total and the number of
      distinct images in that group equals the total, so every image is x1, or
  (c) stated per-card in the All Card List PDF AND the per-card numbers for the
      whole group sum to the rulebook's group total (cross-checked below).
Everything else is left blank with status UNCONFIRMED.

Rulebook = Legendary_Encounters_Rules-Predator.pdf (Upper Deck, 2015).
  p.2 (printed page 2) "Sorting Guide"  -> group totals
  p.30 (printed page 30) "Game Contents" -> component totals incl. 35 Experience / 25 Brute Strength
Card list = Legendary_Encounters_Predator_All_Card_List.pdf -> per-card "N x Name" counts
"""

from __future__ import annotations

import csv
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MANIFEST = ROOT / "data" / "manifest.csv"
OUT = ROOT / "data" / "card_quantities.csv"
GROUPS_OUT = ROOT / "data" / "group_summary.csv"

SRC_SORT = "Rulebook p.2 Sorting Guide"
SRC_CONTENTS = "Rulebook p.30 Game Contents"
SRC_LIST = "Card List PDF"

# Hero/Character cards: the card list gives every character 1 + 3 + 5 + 5 = 14.
# Image filenames carry the rarity; spot-checked (Blain_1Rare = "Ain't Got Time to
# Bleed" x1, UrbanTech_4Uncommon = "Active Camouflage" x3).
HERO_RARITY_QTY = {"1Rare": 1, "4Uncommon": 3, "2Common": 5, "3Common": 5}

# Per-card counts from the card list, keyed by the image's parent folder, then by the
# filename stem after its prefix (e.g. "Act1_GuerrillaCamp.jpg" -> "GuerrillaCamp").
# Value = (qty, name as printed in the card list).
CARD_LIST: dict[str, dict[str, tuple[int, str]]] = {
    # ---- Human mini-decks (card list: "Enemy Decks") ----
    "01_act_1_expendable_assets": {
        "ValVerdeGuerrilla": (4, "Val Verde Guerrilla"), "SovietAdvisor": (1, "Soviet Advisor"),
        "GuerrillaCamp": (1, "Guerrilla Camp (Goal)"), "Event": (2, "Event"), "Hazard": (1, "Hazard"),
    },
    "02_act_2_flares_frags_and_claymores": {
        "ItChangedColors": (3, "It Changed Colors"), "LikeAHunter": (2, "Like a Hunter"),
        "OneAtATime": (1, "One at a Time"), "NoPowderBurnsNoShrapnel": (1, "No Powder Burns, No Shrapnel"),
        "DefensivePosition": (1, "Defensive Position (Goal)"), "Event": (2, "Event"), "Hazard": (1, "Hazard"),
    },
    "03_act_3_get_to_the_choppa": {
        "NotASingleTrack": (2, "Not a Single Track"), "NowItWantsUs": (2, "Now it Wants Us"),
        "NothingOnEarth": (1, "Nothing on Earth"), "SomethingInThoseTrees": (1, "Something in those Trees"),
        "AWorldOfHurt": (1, "A World of Hurt"), "OneUglyMother": (1, "One Ugly Mother... (Final Enemy)"),
        "ThreeHundredCityBlocks": (1, "Three Hundred City Blocks (Special)"),
        "TheChoppa": (1, "The Choppa! (Minor Goal)"), "Event": (2, "Event"), "Hazard": (1, "Hazard"),
    },
    "01_act_1_war_zone": {
        "ScorpionEnforcer": (4, "Scorpion Enforcer"), "ElScorpio": (1, "El Scorpio"),
        "AssaultTheStronghold": (1, "Assault the Stronghold (Goal)"), "Event": (2, "Event"), "Hazard": (1, "Hazard"),
    },
    "02_act_2_personal_little_war": {
        "AbleToBendLight": (2, "Able to Bend Light"), "CityOfFear": (1, "City of Fear"),
        "ThisIsDreadTrulyDread": (1, "This is Dread. Truly Dread"), "ShishKebab": (1, "Shish Kebab"),
        "PhysicalEvidence": (1, "Physical Evidence (Goal)"), "WitnessTestimony": (1, "Witness Testimony (Goal)"),
        "ForensicAnalysis": (1, "Forensic Analysis (Goal)"), "Event": (2, "Event"), "Hazard": (1, "Hazard"),
    },
    "03_act_3_other_world_life_form": {
        "NoKillingWhatCantBeKilled": (2, "No Killing What Can't be Killed"), "BonedLikeAFish": (2, "Boned Like a Fish"),
        "NoStoppingWhatCantBeStopped": (1, "No Stopping What Can't be Stopped"),
        "WayOverYourHead": (1, "Way Over Your Head"), "NowYouWillDie": (1, "Now You Will Die"),
        "ShitHappens": (1, "S#!% Happens"), "OneBigUglyMother": (1, "One Big Ugly Mother... (Final Enemy)"),
        "HuntTheHunter": (1, "Hunt the Hunter (Minor Goal)"), "Event": (2, "Event"), "Hazard": (1, "Hazard"),
    },
    # ---- Predator mini-decks (card list: "Prey Decks") ----
    "01_act_1_no_sport": {
        "ValVerdeGuerrilla": (4, "Val Verde Guerrilla (Prey)"), "Anna": (1, "Anna (Prey)"),
        "SovietAdvisor": (1, "Soviet Advisor (Worthy Prey)"), "GearUp": (2, "Gear Up"), "Danger": (1, "Danger"),
    },
    "02_act_2_payback_time": {
        "IntelligenceAgent": (3, "Intelligence Agent (Prey)"), "Hawkins": (1, "Hawkins (Worthy Prey)"),
        "Poncho": (1, "Poncho (Worthy Prey)"), "Billy": (1, "Billy (Worthy Prey)"), "Blain": (1, "Blain (Worthy Prey)"),
        "GearUp": (2, "Gear Up"), "Danger": (2, "Danger"),
    },
    "03_act_3_what_the_hell_are_you": {
        "SpecialForces": (5, "Special Forces (Prey)"), "Dillon": (1, "Dillon (Worthy Prey)"),
        "Mac": (1, "Mac (Worthy Prey)"), "Dutch": (1, "Dutch (Ultimate Prey)"),
        "GearUp": (2, "Gear Up"), "Danger": (3, "Danger"),
    },
    "01_act_1_drawn_by_heat_and_conflict": {
        "ScorpionEnforcer": (3, "Scorpion Enforcer (Prey)"), "PoliceOfficer": (2, "Police Officer (Prey)"),
        "ElScorpio": (1, "El Scorpio (Worthy Prey)"), "GearUp": (2, "Gear Up"), "Danger": (1, "Danger"),
    },
    "02_act_2_this_is_history": {
        "VoodooPosseThug": (4, "Voodoo Posse Thug (Prey)"), "GoldTooth": (1, "Gold Tooth (Worthy Prey)"),
        "KingWillie": (1, "King Willie (Worthy Prey)"), "Danny": (1, "Danny (Worthy Prey)"),
        "GearUp": (2, "Gear Up"), "Danger": (2, "Danger"),
    },
    "03_act_3_a_taste_for_beef": {
        "OWLFAgent": (4, "O.W.L.F. Agent (Prey)"), "AgentGarber": (1, "Agent Garber (Worthy Prey)"),
        "Keyes": (1, "Keyes (Worthy Prey)"), "Lambert": (1, "Lambert (Worthy Prey)"),
        "Harrigan": (1, "Harrigan (Ultimate Prey)"), "GearUp": (2, "Gear Up"), "Danger": (3, "Danger"),
    },
    # ---- Strike / side decks ----
    "08_enemy_strike_deck": {
        "CloseCall": (6, "Close Call"), "FleshWound": (10, "Flesh Wound"), "DeepGash": (8, "Deep Gash"),
        "BrutalPuncture": (3, "Brutal Puncture"), "OpenWound": (2, "Open Wounds"),
        "FesteringWound": (2, "Festering Wounds"), "StunningBlow": (3, "Stunning Blow"),
        "AWholeNewLevelOfPain": (1, "A Hole New Level of Pain"), "PermanentDamage": (2, "Permanent Damage"),
        "CoverMe": (2, "Cover Me!"), "Sacrifice": (1, "Sacrifice"),
    },
    "09_prey_strike_deck": {
        "ThroughAndThrough": (4, "Through and Through"), "Outsmarted": (1, "Outsmarted"),
        "HeartyBlow": (1, "Hearty Blow"), "WoundedPride": (8, "Wounded Pride"), "BruisedEgo": (8, "Bruised Ego"),
        "ShortCircuit": (3, "Short Circuit"), "DishonorAndDeath": (2, "Dishonor and Death"),
        "FleshWound": (11, "Flesh Wound"), "SeveredArtery": (3, "Severed Artery"),
        "JustAScratch": (9, "Just a Scratch"), "GottaHandItToEm": (7, "Gotta Hand it to 'Em"),
        "BecomingTheHunted": (3, "Becoming the Hunted"),
    },
    "10_young_blood_deck": {
        "FunAndGames": (3, "Fun and Games"), "DefensiveAdaptations": (3, "Defensive Adaptations"),
        "OverHere": (3, "Over Here"), "SomethingOutThereWaitingForUs": (3, "Something Out There Waiting For Us"),
        "AFittingDemise": (3, "A Fitting Demise"), "AlmostNoWeightCutsLikeSteel": (3, "Almost No Weight. Cuts Like Steel"),
        "TheEyesOfTheDemon": (3, "The Eyes of the Demon"), "Event": (3, "Event"),
    },
    "11_mercenary_deck": {
        "Boatman": (3, "Boatman"), "Drifter": (3, "Drifter"), "Survivalist": (3, "Survivalist"),
        "SleepingSentry": (3, "Sleeping Sentry"), "Marksman": (3, "Marksman"), "Loner": (3, "Loner"),
        "StreetRat": (3, "Street Rat"), "GearUp": (1, "Gear Up"), "Danger": (2, "Danger"),
    },
}


def card_list_qty(row: dict) -> tuple[int, str] | None:
    """Return (qty, basis-text) from the card list PDF for this image, or None."""
    parts = row["local_path"].split("/")
    section, folder, fn = parts[2], parts[-2], row["filename"]
    stem = fn.rsplit(".", 1)[0]

    if section in ("01_heroes_human", "02_heroes_predator"):
        rarity = stem.split("_", 1)[1]
        if rarity in HERO_RARITY_QTY:
            q = HERO_RARITY_QTY[rarity]
            return q, f"{SRC_LIST}: {rarity} = x{q} (each character 1 Rare + 3 Uncommon + 5 + 5 Common = 14)"
        return None

    if section == "12_starter_cards":
        if fn.startswith("Commander"):
            return 2, f"{SRC_CONTENTS}: 10 Commanders across 5 classes (Covert, Instinct, Ranged, Strength, Tech) => x2 each"
        if fn.startswith("KillerInstinct"):
            return 2, f"{SRC_CONTENTS}: 8 Killer Instincts across 4 classes (Covert, Instinct, Strength, Tech) => x2 each"
        return None

    entries = CARD_LIST.get(folder)
    if entries is None:
        return None
    key = stem.split("_", 1)[1] if "_" in stem else stem
    if key not in entries:
        return None
    q, name = entries[key]
    return q, f"{SRC_LIST}: '{q} x {name}'"


def classify(row: dict) -> tuple[str, int | None, str]:
    """Return (group_name, rulebook_group_total, source) for a manifest row."""
    parts = row["local_path"].split("/")  # assets/raw/<section>/[<sub>/]<file>
    section = parts[2]
    sub = parts[3] if len(parts) == 5 else ""
    fn = row["filename"]

    if section in ("01_heroes_human", "02_heroes_predator"):
        return f"Character: {row['subsection']}", 14, f"{SRC_SORT} ('each has 14 cards')"

    if section == "03_avatars_and_role_cards":
        return "Avatars + Role cards (10 Human + 5 Predator each)", 30, f"{SRC_CONTENTS} (15 Role Avatars + 15 Role Character Cards)"

    if section in ("04_predator_humans", "05_predator_2_humans") and not sub:
        return "Locations + Objectives", 8, f"{SRC_SORT} (2 Location + 6 Objective)"

    minideck_sizes = {
        "01_act_1": 9, "02_act_2": 11, "03_act_3": 13,
    }
    if section in ("04_predator_humans", "05_predator_2_humans", "06_predator_predators", "07_predator_2_predators") and sub:
        key = "_".join(sub.split("_")[:3])
        return f"Mini-deck: {row['section']} / {row['subsection']}", minideck_sizes[key], f"{SRC_SORT} (9 / 11 / 13 cards per Act)"

    simple = {
        "08_enemy_strike_deck": ("Enemy Strike", 40),
        "09_prey_strike_deck": ("Prey Strike", 60),
        "10_young_blood_deck": ("Young Blood", 24),
        "11_mercenary_deck": ("Mercenary", 24),
        "13_gear": ("Gear", 20),
        "14_traps": ("Traps", 20),
        "15_challenges": ("Challenges", 20),
        "16_tests": ("Tests", 20),
    }
    if section in simple:
        name, total = simple[section]
        return name, total, SRC_SORT

    if section == "12_starter_cards":
        if fn == "StartingExperience.jpg":
            return "Starting: Experience", 35, f"{SRC_CONTENTS} ('35 Experience')"
        if fn == "StartingBruteStrength.jpg":
            return "Starting: Brute Strength", 25, f"{SRC_CONTENTS} ('25 Brute Strength')"
        if fn.startswith("Commander"):
            return "Commanders", 10, f"{SRC_CONTENTS} ('10 Commanders')"
        if fn.startswith("KillerInstinct"):
            return "Killer Instincts", 8, f"{SRC_CONTENTS} ('8 Killer Instincts')"

    if section == "17_alien_conversion_kit_for_predator":
        return "Alien Conversion Kit (web extra, NOT part of the 700-card box)", None, "Not in rulebook"

    raise ValueError(f"Unclassified card: {row['local_path']}")


def main() -> int:
    rows = list(csv.DictReader(MANIFEST.open(encoding="utf-8")))
    groups: dict[str, list[dict]] = defaultdict(list)
    meta: dict[str, tuple[int | None, str]] = {}
    for r in rows:
        g, total, src = classify(r)
        r["group"] = g
        groups[g].append(r)
        meta[g] = (total, src)

    out_rows = []
    summary = []
    errors: list[str] = []
    for g, members in groups.items():
        total, src = meta[g]
        n = len(members)
        per_card: list[tuple[object, str, str]]  # (qty, status, basis) per member
        listed = [card_list_qty(m) for m in members]
        if total is not None and n == total:
            per_card = [(1, "CONFIRMED", f"{src}: {total} cards / {n} distinct images => x1 each")] * n
        elif total is not None and n == 1:
            per_card = [(total, "CONFIRMED", src)]
        elif total is not None and all(listed):
            list_sum = sum(q for q, _ in listed)
            if list_sum != total:
                errors.append(f"{g}: card list sums to {list_sum}, rulebook says {total}")
                per_card = [("", "UNCONFIRMED", f"{src}: card list sum {list_sum} != rulebook {total}")] * n
            else:
                per_card = [(q, "CONFIRMED", f"{b}; group sum {list_sum} = {src} total {total}") for q, b in listed]
        else:
            per_card = [("", "UNCONFIRMED", f"{src}: group total {total} across {n} distinct images; per-card split not stated")] * n
        status = "CONFIRMED" if all(s == "CONFIRMED" for _, s, _ in per_card) else "UNCONFIRMED"
        for m, (qty, st, basis) in zip(members, per_card):
            out_rows.append({
                "group": g,
                "local_path": m["local_path"],
                "filename": m["filename"],
                "qty": qty,
                "status": st,
                "basis": basis,
            })
        summary.append({
            "group": g,
            "rulebook_total": "" if total is None else total,
            "distinct_images": n,
            "status": status,
            "source": src,
        })

    with OUT.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(out_rows[0].keys()))
        w.writeheader()
        w.writerows(out_rows)
    with GROUPS_OUT.open("w", newline="", encoding="utf-8") as f:
        w = csv.DictWriter(f, fieldnames=list(summary[0].keys()))
        w.writeheader()
        w.writerows(summary)

    # ---- Validation against the rulebook's 700-card total ----
    box_groups = [s for s in summary if s["rulebook_total"] != ""]
    box_total = sum(int(s["rulebook_total"]) for s in box_groups)
    confirmed = sum(int(s["rulebook_total"]) for s in box_groups if s["status"] == "CONFIRMED")
    qty_sum = sum(int(r["qty"]) for r in out_rows if r["qty"] != "")
    print(f"Box total from groups: {box_total} (rulebook says 700) -> {'OK' if box_total == 700 else 'MISMATCH'}")
    print(f"Confirmed cards: {confirmed} / 700   Unconfirmed: {700 - confirmed}")
    print(f"Sum of filled-in qty column: {qty_sum}")
    for e in errors:
        print(f"  ERROR {e}")
    print()
    for s in summary:
        print(f"  {s['status']:11s} {str(s['rulebook_total']):>3s} cards / {s['distinct_images']:>2d} imgs  {s['group']}")
    return 0 if box_total == 700 and not errors else 1


if __name__ == "__main__":
    sys.exit(main())
