#!/usr/bin/env python3
"""Copy shipped Android translations into the Qt Linguist catalogues.

dish-android and dish-windows are the same product with the same vocabulary,
and Android's catalogues are the mature ones: every locale there is ~96%%
translated and human-reviewed. Any English string the two apps share therefore
already HAS a reviewed translation in all five languages, and re-translating it
would only invent a second wording for a phrase the user has already seen on
their phone.

This script moves those across. It is deliberately conservative:

  * A message is seeded only when its English source matches an Android string
    EXACTLY after normalisation (whitespace collapsed, escapes and entities
    resolved, placeholders reduced to positional markers). No fuzzy matching --
    a near-match is a different sentence.
  * A message that already carries a translation is never touched, so local
    edits always win over the importer.
  * A message whose placeholders do not correspond one-for-one is skipped and
    reported rather than guessed at. Dropping a %%1 silently would ship a string
    that renders a bare number with no noun.

Numerus (plural) messages need an explicit mapping because Android selects the
quantity argument by name and Qt selects it by position, so PLURAL_MAP below
pairs them by hand. That table is short on purpose: if it starts growing,
the two apps' plural strings have drifted and that is worth knowing.

Re-run after Android ships new translations:

    python scripts/seed-from-android.py --android ../dish-android

Add --dry-run to see what it would write. Run lupdate afterwards to normalise
the file formatting back to Qt's canonical serialisation.
"""

from __future__ import annotations

import argparse
import html
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

# Qt orders <numerusform> by the target language's plural rules; Bosnian needs
# a third slot for the Slavic paucal (2-4). tests/test_translations.cpp asserts
# this ordering against the compiled .qm rather than trusting the table.
NUMERUS_ORDER = {
    "bs": ["one", "few", "other"],
    "de": ["one", "other"],
    "en": ["one", "other"],
    "es": ["one", "other"],
    "fr": ["one", "other"],
    "pt_BR": ["one", "other"],
}

# Qt locale suffix -> Android resource qualifier.
LOCALES = {
    "bs": "values-bs",
    "de": "values-de",
    "es": "values-es",
    "fr": "values-fr",
    "pt_BR": "values-pt-rBR",
}

# Qt numerus source text -> (android plurals name, placeholder mapping). The
# mapping says which Qt placeholder each Android positional argument becomes:
# "n" is the %n count, "1" is Qt's %1.
PLURAL_MAP = {
    # Android still calls this counter status_remembered; both apps spell it
    # "paired" in the UI.
    "%n paired": ("status_remembered", {"1": "n"}),
    "%1 of %n online": ("status_connected_of", {"1": "1", "2": "n"}),
}


def normalise(text: str) -> str:
    """Reduce a string to a form comparable across the two platforms."""
    text = html.unescape(text)
    # Android escapes apostrophes and quotes inside XML text; Qt does not.
    text = text.replace("\\'", "'").replace('\\"', '"').replace("\\n", "\n")
    # A literal percent is "%%" on Android and a bare "%" in Qt.
    text = text.replace("%%", "%")
    # Collapse both placeholder dialects to {k} so only the wording is compared.
    text = re.sub(r"%(\d+)\$[sd]", r"{\1}", text)
    text = re.sub(r"%(\d+)", r"{\1}", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip()


def android_to_qt(text: str) -> str:
    """Rewrite Android placeholder syntax as Qt's, leaving wording alone."""
    text = html.unescape(text)
    text = text.replace("\\'", "'").replace('\\"', '"').replace("\\n", "\n")
    text = re.sub(r"%(\d+)\$[sd]", r"%\1", text)
    text = re.sub(r"\s+", " ", text)
    return text.strip()


def placeholders(text: str) -> set[str]:
    return set(re.findall(r"%(\d+|n)", text))


def load_android(res_dir: Path, qualifier: str) -> tuple[dict, dict]:
    """Return ({name: string}, {name: {quantity: string}}) for one qualifier."""
    path = res_dir / qualifier / "strings.xml"
    if not path.is_file():
        raise SystemExit(f"missing Android catalogue: {path}")
    root = ET.parse(path).getroot()
    strings, plurals = {}, {}
    for node in root:
        name = node.get("name")
        if name is None:
            continue
        if node.tag == "string":
            strings[name] = "".join(node.itertext())
        elif node.tag == "plurals":
            plurals[name] = {
                item.get("quantity"): "".join(item.itertext()) for item in node
            }
    return strings, plurals


def seed(ts_path: Path, locale: str, base: dict, tr: dict,
         base_plurals: dict, tr_plurals: dict, dry_run: bool) -> tuple[int, list]:
    """Fill untranslated messages in one .ts from the Android maps."""
    by_english: dict[str, str] = {}
    collisions: set[str] = set()
    for name, english in base.items():
        key = normalise(english)
        if key in by_english and by_english[key] != name:
            # Two Android resources, identical English. Either translation is
            # defensible, so refuse to pick and leave it to the translator.
            collisions.add(key)
        by_english[key] = name

    tree = ET.parse(ts_path)
    seeded, skipped = 0, []

    for message in tree.getroot().iter("message"):
        source_node = message.find("source")
        target = message.find("translation")
        if source_node is None or target is None:
            continue
        source = source_node.text or ""

        if message.get("numerus") == "yes":
            entry = PLURAL_MAP.get(source)
            if entry is None:
                continue
            android_name, argmap = entry
            forms = tr_plurals.get(android_name)
            english_forms = base_plurals.get(android_name)
            if not forms or not english_forms:
                skipped.append((source, f"no Android plurals '{android_name}'"))
                continue
            slots = list(target.findall("numerusform"))
            if any((slot.text or "").strip() for slot in slots):
                continue  # already translated
            order = NUMERUS_ORDER[locale]
            if len(slots) != len(order):
                skipped.append((source, f"expected {len(order)} forms, .ts has {len(slots)}"))
                continue
            rendered = []
            for quantity in order:
                # Android may omit a form the language does not need; CLDR says
                # fall back to "other", which is what Android itself resolves to.
                text = forms.get(quantity) or forms.get("other")
                if text is None:
                    break
                text = html.unescape(text).replace("\\'", "'")
                for android_idx, qt_slot in argmap.items():
                    text = re.sub(rf"%{android_idx}\$[sd]", f"%{qt_slot}", text)
                rendered.append(re.sub(r"\s+", " ", text).strip())
            if len(rendered) != len(order):
                skipped.append((source, "incomplete Android plural forms"))
                continue
            if placeholders(rendered[0]) != placeholders(source):
                skipped.append((source, f"placeholder mismatch: {rendered[0]!r}"))
                continue
            if not dry_run:
                for slot, text in zip(slots, rendered):
                    slot.text = text
                target.attrib.pop("type", None)
            seeded += 1
            continue

        # A local translation always wins over the importer.
        if (target.text or "").strip():
            continue
        key = normalise(source)
        if key in collisions or key not in by_english:
            continue
        android_name = by_english[key]
        translated = tr.get(android_name)
        if not translated:
            continue
        candidate = android_to_qt(translated)
        if placeholders(candidate) != placeholders(source):
            skipped.append((source, f"placeholder mismatch: {candidate!r}"))
            continue
        if not dry_run:
            target.text = candidate
            target.attrib.pop("type", None)
        seeded += 1

    if not dry_run and seeded:
        # ElementTree drops the doctype, so put it back or the file stops being
        # a valid .ts. lupdate re-canonicalises the formatting afterwards.
        body = ET.tostring(tree.getroot(), encoding="unicode")
        ts_path.write_text(
            '<?xml version="1.0" encoding="utf-8"?>\n<!DOCTYPE TS>\n' + body + "\n",
            encoding="utf-8",
        )
    return seeded, skipped


def main() -> int:
    repo_root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--android", type=Path, default=repo_root.parent / "dish-android",
                        help="path to the dish-android checkout")
    parser.add_argument("--translations", type=Path, default=repo_root / "translations")
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    res_dir = args.android / "app" / "src" / "main" / "res"
    if not res_dir.is_dir():
        print(f"not an Android checkout: {args.android}", file=sys.stderr)
        return 2

    base, base_plurals = load_android(res_dir, "values")
    total = 0
    for locale, qualifier in LOCALES.items():
        ts_path = args.translations / f"dish_{locale}.ts"
        if not ts_path.is_file():
            print(f"  {locale}: no catalogue at {ts_path}, skipping")
            continue
        tr, tr_plurals = load_android(res_dir, qualifier)
        seeded, skipped = seed(ts_path, locale, base, tr, base_plurals,
                               tr_plurals, args.dry_run)
        total += seeded
        print(f"  {locale}: seeded {seeded}")
        for source, why in skipped:
            print(f"      skipped {source!r}: {why}")
    print(f"{'would seed' if args.dry_run else 'seeded'} {total} messages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
