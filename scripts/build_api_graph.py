#!/usr/bin/env python3
"""Build an API co-occurrence graph from project source code.

Scans all C++ source files under src/ and extracts Pro* / wfc* / pfc* API
symbols, recording which APIs appear together in the same file.  Outputs a
JSON co-occurrence matrix stored under .autobbox/index/api_graph.json.

This graph enables:
- "Related APIs" suggestions when querying an API
- Understanding which APIs are commonly used together in the project
- Context-aware fallback when a token-only query returns weak results
"""

from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

DEFAULT_REPO_ROOT = Path(".")
DEFAULT_SRC_DIRS = ["src"]
DEFAULT_INDEX_DIR = Path(".autobbox/index")

API_RE = re.compile(
    r"\b(?:(?:Pro|wfc|pfc)[A-Za-z_]\w+|PRO_TK_[A-Z0-9_]+)\b"
)


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def extract_apis(text: str) -> list[str]:
    """Extract unique Creo API symbols from source text."""
    found: set[str] = set()
    for match in API_RE.finditer(text):
        symbol = match.group(0)
        # Filter out noise: ProMdl, ProError etc. are types, not APIs.
        # But include them anyway — they're legit Creo symbols.
        found.add(symbol)
    return sorted(found)


def build_graph(src_roots: list[Path]) -> dict:
    """Build co-occurrence graph from all source files.

    Returns:
        dict with:
        - nodes: {api_symbol: {files: [paths], count: int}}
        - edges: {api_a: {api_b: weight}}  (co-occurrence frequency)
        - metadata: generation info
    """
    api_files: dict[str, list[str]] = defaultdict(list)
    api_count: dict[str, int] = defaultdict(int)
    cooccur: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))

    total_files = 0
    total_apis = 0

    for src_root in src_roots:
        if not src_root.exists():
            continue
        for cpp_file in src_root.rglob("*.cpp"):
            total_files += 1
            try:
                text = cpp_file.read_text(encoding="utf-8", errors="replace")
            except Exception:
                continue

            apis = extract_apis(text)
            rel_path = str(cpp_file.resolve()).replace("\\", "/")

            for api in apis:
                api_files[api].append(rel_path)
                api_count[api] += 1
                total_apis += 1

            # Record co-occurrence pairs
            for i in range(len(apis)):
                for j in range(i + 1, len(apis)):
                    a, b = apis[i], apis[j]
                    cooccur[a][b] += 1
                    cooccur[b][a] += 1

    # Convert to serializable format
    nodes = {}
    for api, files in sorted(api_files.items()):
        nodes[api] = {
            "files": sorted(set(files))[:30],  # top 30 files
            "file_count": len(set(files)),
            "total_occurrences": api_count[api],
        }

    edges = {}
    for a, neighbors in sorted(cooccur.items()):
        # Only keep top 20 co-occurring APIs per node
        top = sorted(neighbors.items(), key=lambda x: -x[1])[:20]
        edges[a] = {b: w for b, w in top if w >= 2}  # min 2 co-occurrences

    return {
        "schema_version": 1,
        "generated_at_utc": now_utc(),
        "generator": "scripts/build_api_graph.py",
        "stats": {
            "files_scanned": total_files,
            "unique_apis": len(nodes),
            "total_occurrences": total_apis,
            "edges": sum(len(v) for v in edges.values()),
        },
        "nodes": nodes,
        "edges": edges,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=str(DEFAULT_REPO_ROOT))
    parser.add_argument("--index-dir", default=str(DEFAULT_INDEX_DIR))
    parser.add_argument("--json", action="store_true", help="Output JSON only")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    index_dir = Path(args.index_dir)
    if not index_dir.is_absolute():
        index_dir = repo_root / index_dir
    index_dir.mkdir(parents=True, exist_ok=True)

    src_roots = [repo_root / d for d in DEFAULT_SRC_DIRS]
    graph = build_graph(src_roots)

    out_path = index_dir / "api_graph.json"
    out_path.write_text(
        json.dumps(graph, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    if not args.json:
        print(f"API graph written: {out_path}")
        print(f"  Files scanned : {graph['stats']['files_scanned']}")
        print(f"  Unique APIs   : {graph['stats']['unique_apis']}")
        print(f"  Total edges   : {graph['stats']['edges']}")
    else:
        print(json.dumps(graph["stats"], ensure_ascii=False))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
