#!/usr/bin/env python3
"""Merge learned aliases from find_creo_context --learn into alias_map.json.

Reads .autobbox/index/learned_aliases.jsonl and merges query→API mappings
into the existing alias_map.json, adding new aliases where needed.
"""

from __future__ import annotations

import argparse
import json
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

DEFAULT_REPO_ROOT = Path(".")
DEFAULT_INDEX_DIR = Path(".autobbox/index")


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=str(DEFAULT_REPO_ROOT))
    parser.add_argument("--index-dir", default=str(DEFAULT_INDEX_DIR))
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    index_dir = Path(args.index_dir)
    if not index_dir.is_absolute():
        index_dir = repo_root / index_dir

    learned_path = index_dir / "learned_aliases.jsonl"
    alias_path = index_dir / "creo_install" / "alias_map.json"

    if not learned_path.exists():
        print("No learned aliases found. Nothing to merge.")
        return 0

    # Load learned aliases
    learned: dict[str, set[str]] = defaultdict(set)
    with learned_path.open("r", encoding="utf-8") as stream:
        for line in stream:
            if line.strip():
                entry = json.loads(line)
                query = (entry.get("query") or "").strip()
                symbols = entry.get("matched_symbols") or []
                if query and symbols:
                    learned[query].update(symbols)

    if not learned:
        print("No valid learned entries.")
        return 0

    # Load alias map
    alias_data = {"schema_version": 1, "aliases": {}}
    if alias_path.exists():
        alias_data = json.loads(alias_path.read_text(encoding="utf-8"))

    aliases = alias_data.get("aliases", {})
    if not isinstance(aliases, dict):
        aliases = {}
        alias_data["aliases"] = aliases

    added = 0
    for query, symbols in sorted(learned.items()):
        if query not in aliases:
            aliases[query] = sorted(symbols)
            added += 1
            print(f"  NEW: {query} -> {', '.join(sorted(symbols))}")
        else:
            existing = set(aliases[query])
            new_symbols = symbols - existing
            if new_symbols:
                aliases[query] = sorted(existing | new_symbols)
                print(f"  UPDATED: {query} + {', '.join(sorted(new_symbols))}")
                added += 1

    if added == 0:
        print("No new aliases to add.")
        return 0

    alias_data["schema_version"] = alias_data.get("schema_version", 1)
    alias_data["generated_by"] = "scripts/merge_learned_aliases.py"
    alias_data["last_merged_utc"] = now_utc()

    if not args.dry_run:
        alias_path.parent.mkdir(parents=True, exist_ok=True)
        alias_path.write_text(
            json.dumps(alias_data, ensure_ascii=False, indent=2) + "\n",
            encoding="utf-8",
        )
        # Archive learned entries after merge
        archive_path = index_dir / "learned_aliases_archive.jsonl"
        with learned_path.open("r", encoding="utf-8") as src:
            with archive_path.open("a", encoding="utf-8", newline="\n") as dst:
                dst.write(src.read())
        learned_path.unlink()
        print(f"\nMerged {added} aliases into {alias_path}")
        print(f"Archived learned entries to {archive_path}")
    else:
        print(f"\n[DRY RUN] Would merge {added} aliases into {alias_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
