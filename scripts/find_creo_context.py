#!/usr/bin/env python3
"""Unified project Creo context finder: feature index -> evidence cache -> install index."""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

import find_creo_install as install_finder  # noqa: E402

DEFAULT_CREO_ROOT = Path(r"D:\Program Files\PTC\Creo 10.0.8.0")
DEFAULT_CACHE_DIR = Path(r".autobbox\index\creo_evidence_cache")
DEFAULT_INSTALL_INDEX_DIR = Path(r".autobbox\index\creo_install")
TOKEN_RE = re.compile(r"[0-9A-Za-z_\.\-]+|[\u4e00-\u9fff]+")
SCAN_EXTS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".res", ".rbn", ".mnu", ".txt", ".dat", ".json"}

KIND_USAGE_TYPES = {
    "api": {"api", "return_code"},
    "ui": {"dialog", "resource", "ribbon", "message", "ui_property"},
    "feature": {"api", "return_code", "dialog", "resource", "ribbon", "message", "ui_property", "packaging"},
    "sample": set(),
    "doc": set(),
    "path": {"packaging"},
    "all": {"api", "return_code", "dialog", "resource", "ribbon", "message", "ui_property", "packaging"},
}

CONTEXT_ALIASES = {
    "无标题弹窗": ["TitleBar False", ".TitleBar False", "无标题栏", "titleless dialog", "quick_rename", "autobbox_quick_rename"],
    "无标题栏弹窗": ["TitleBar False", ".TitleBar False", "无标题弹窗", "titleless dialog"],
    "快速重命名": ["quick_rename", "QuickRename", "autobbox_quick_rename", "AutoBBox.QuickRename"],
}


def load_json(path: Path, default: Any) -> Any:
    if not path.exists():
        return default
    with path.open("r", encoding="utf-8-sig") as stream:
        return json.load(stream)


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    rows = []
    with path.open("r", encoding="utf-8-sig") as stream:
        for line in stream:
            if line.strip():
                rows.append(json.loads(line))
    return rows


def normalize(text: str | None) -> str:
    if not text:
        return ""
    text = str(text).lower()
    text = re.sub(r"[^0-9a-zA-Z_\.\-\u4e00-\u9fff]+", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def loose(text: str | None) -> str:
    return normalize(text).replace(" ", "")


def tokens(text: str | None) -> list[str]:
    norm = normalize(text)
    return sorted(set(norm.split())) if norm else []


def looks_like_creo_api_query(query: str) -> bool:
    text = (query or "").strip()
    return bool(re.fullmatch(r"Pro[A-Za-z_]\w+|PRO_TK_[A-Z0-9_]+", text))


def listify(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, list):
        return value
    if isinstance(value, tuple):
        return list(value)
    return [value]


def string_values(value: Any) -> list[str]:
    """Flatten loosely shaped index values into printable strings.

    Older/current feature-index records may contain path groups as lists, single
    strings, or nested dictionaries.  Text output should never fail just because
    one index version stored an empty object or a richer path record.
    """
    out: list[str] = []
    if value is None:
        return out
    if isinstance(value, str):
        return [value] if value else []
    if isinstance(value, dict):
        for nested in value.values():
            out.extend(string_values(nested))
        return out
    if isinstance(value, (list, tuple, set)):
        for item in value:
            out.extend(string_values(item))
        return out
    text = str(value)
    return [text] if text else []


def expand_query(query: str) -> tuple[list[str], list[str]]:
    expanded = set(tokens(query))
    alias_hits = []
    qloose = loose(query)
    for alias, values in CONTEXT_ALIASES.items():
        aloose = loose(alias)
        if qloose == aloose or (aloose and (qloose in aloose or aloose in qloose)):
            alias_hits.extend(values)
            for value in values:
                expanded.update(tokens(value))
    return sorted(expanded), alias_hits


def current_latest(repo_root: Path, tracked_inputs: list[str]) -> str:
    latest = datetime(2000, 1, 1, tzinfo=timezone.utc)
    for relative in tracked_inputs:
        path = repo_root / str(relative).replace("/", "\\")
        if not path.exists():
            continue
        files = [path] if path.is_file() else [p for p in path.rglob("*") if p.is_file()]
        for file in files:
            if file.suffix.lower() not in SCAN_EXTS and file.name not in {"protk.dat", "autobbox_msg.txt"}:
                continue
            latest = max(latest, datetime.fromtimestamp(file.stat().st_mtime, tz=timezone.utc))
    return latest.isoformat().replace("+00:00", "Z")


def parse_dt(value: str | None) -> datetime:
    if not value:
        return datetime(2000, 1, 1, tzinfo=timezone.utc)
    return datetime.fromisoformat(value.replace("Z", "+00:00")).astimezone(timezone.utc)


def cache_stale(repo_root: Path, cache_meta: dict[str, Any], install_meta: dict[str, Any]) -> tuple[bool, str]:
    tracked = listify(cache_meta.get("project_snapshot", {}).get("tracked_inputs"))
    current = current_latest(repo_root, [str(x) for x in tracked]) if tracked else "2000-01-01T00:00:00Z"
    cached = cache_meta.get("project_snapshot", {}).get("latest_input_write_time_utc")
    if parse_dt(current) > parse_dt(cached):
        return True, f"project input newer than cache ({current} > {cached})"
    cached_install = cache_meta.get("creo_install_index", {}).get("generated_at_utc")
    current_install = install_meta.get("generated_at_utc")
    if current_install != cached_install:
        return True, "creo_install index generation changed"
    return False, ""


def refresh_cache(repo_root: Path, creo_root: Path) -> None:
    script = repo_root / "scripts" / "update_creo_evidence_cache.ps1"
    completed = subprocess.run([
        "powershell", "-ExecutionPolicy", "Bypass", "-File", str(script), "-Full", "-Root", str(repo_root), "-CreoRoot", str(creo_root)
    ], cwd=str(repo_root), text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if completed.returncode != 0:
        raise RuntimeError(completed.stdout)


def hay_feature(feature: dict[str, Any]) -> list[str]:
    values = [feature.get("feature_id", ""), feature.get("title", ""), feature.get("notes", "")]
    values.extend(listify(feature.get("aliases")))
    values.extend(listify(feature.get("keywords")))
    values.extend(listify(feature.get("commands")))
    values.extend(listify(feature.get("message_keys")))
    paths = feature.get("paths") or {}
    if isinstance(paths, dict):
        for value in paths.values():
            values.extend(str(v) for v in listify(value))
    values.extend(listify(feature.get("search_terms")))
    return [str(v) for v in values if v]


def score_feature(feature: dict[str, Any], query: str, expanded: list[str], alias_hits: list[str], strict_exact: bool = False) -> tuple[int, list[str]]:
    q = loose(query)
    values = hay_feature(feature)
    loose_values = [loose(v) for v in values if loose(v)]
    score = 0
    reasons = []
    for value in [feature.get("feature_id"), feature.get("title"), *listify(feature.get("aliases"))]:
        if loose(value) == q:
            score += 5000
            reasons.append(f"exact feature: {value}")
            break
    if strict_exact and score == 0:
        return 0, []
    if any(q and (q in v or v in q) for v in loose_values):
        score += 1200
        reasons.append("feature phrase")
    for alias in alias_hits:
        aloose = loose(alias)
        if aloose and any(aloose in v or v in aloose for v in loose_values):
            score += 900
            reasons.append(f"alias: {alias}")
    for token in expanded:
        t = loose(token)
        if t and any(t in v for v in loose_values):
            score += 100
            reasons.append(f"term: {token}")
    return score, list(dict.fromkeys(reasons))


def query_features(feature_index: dict[str, Any], query: str, top: int, kind: str = "all") -> list[dict[str, Any]]:
    expanded, alias_hits = expand_query(query)
    strict_exact = kind == "api" and looks_like_creo_api_query(query)
    results = []
    for feature in listify(feature_index.get("features")):
        score, reasons = score_feature(feature, query, expanded, alias_hits, strict_exact=strict_exact)
        if score <= 0:
            continue
        results.append({
            "source": "feature_index",
            "cache_hit": False,
            "score": score,
            "feature_id": feature.get("feature_id"),
            "title": feature.get("title"),
            "commands": listify(feature.get("commands")),
            "message_keys": listify(feature.get("message_keys")),
            "paths": feature.get("paths") or {},
            "reasons": reasons,
        })
    results.sort(key=lambda r: (-r["score"], str(r.get("feature_id", ""))))
    return results[:top]


def evidence_hay(row: dict[str, Any]) -> list[str]:
    values = [row.get("usage_key"), row.get("usage_type"), row.get("symbol"), row.get("derived_from")]
    values.extend(listify(row.get("project_paths")))
    values.extend(listify(row.get("feature_ids")))
    values.extend(listify(row.get("search_terms")))
    for ref in listify(row.get("line_refs")):
        if isinstance(ref, dict):
            values.append(ref.get("path", ""))
    return [str(v) for v in values if v]


def score_usage(row: dict[str, Any], query: str, expanded: list[str], alias_hits: list[str], feature_ids: set[str]) -> tuple[int, list[str]]:
    q = loose(query)
    values = evidence_hay(row)
    loose_values = [loose(v) for v in values if loose(v)]
    score = 0
    reasons = []
    if str(row.get("usage_type")) in {"api", "return_code"}:
        score += 800
    elif str(row.get("usage_type")) in {"dialog", "resource", "ribbon", "message", "ui_property"}:
        score += 700
    else:
        score += 300
    for value in [row.get("usage_key"), row.get("symbol")]:
        if loose(value) == q:
            score += 8000
            reasons.append(f"exact usage: {value}")
            break
    if any(q and (q in v or v in q) for v in loose_values):
        score += 1100
        reasons.append("usage phrase")
    for alias in alias_hits:
        aloose = loose(alias)
        if aloose and any(aloose in v or v in aloose for v in loose_values):
            score += 900
            reasons.append(f"alias: {alias}")
    for token in expanded:
        t = loose(token)
        if t and any(t in v for v in loose_values):
            score += 100
            reasons.append(f"term: {token}")
    overlap = set(listify(row.get("feature_ids"))) & feature_ids
    if overlap:
        score += 2500
        reasons.append("feature evidence: " + ", ".join(sorted(overlap)))
    # Do not return generic rows on base score alone.
    if len(reasons) == 0:
        return 0, []
    return score, list(dict.fromkeys(reasons))


def query_evidence(usage_rows: list[dict[str, Any]], evidence_rows: list[dict[str, Any]], query: str, kind: str, top: int, feature_results: list[dict[str, Any]]) -> list[dict[str, Any]]:
    expanded, alias_hits = expand_query(query)
    allowed_types = KIND_USAGE_TYPES.get(kind, KIND_USAGE_TYPES["all"])
    feature_ids = {str(r.get("feature_id")) for r in feature_results if r.get("feature_id")}
    strict_api_lookup = kind == "api" and looks_like_creo_api_query(query)
    qloose = loose(query)
    evidence_by_usage: dict[str, list[dict[str, Any]]] = {}
    for ev in evidence_rows:
        evidence_by_usage.setdefault(str(ev.get("usage_key")), []).append(ev)
    results = []
    for row in usage_rows:
        if allowed_types and row.get("usage_type") not in allowed_types:
            continue
        if strict_api_lookup and loose(row.get("symbol")) != qloose and loose(row.get("usage_key")) != qloose:
            continue
        score, reasons = score_usage(row, query, expanded, alias_hits, feature_ids)
        if score <= 0:
            continue
        evidence = evidence_by_usage.get(str(row.get("usage_key")), [])[:8]
        if row.get("usage_type") == "api" and not evidence and loose(row.get("symbol")) != qloose:
            continue
        results.append({
            "source": "evidence_cache",
            "cache_hit": True,
            "score": score,
            "usage_key": row.get("usage_key"),
            "usage_type": row.get("usage_type"),
            "symbol": row.get("symbol"),
            "feature_ids": listify(row.get("feature_ids")),
            "project_paths": listify(row.get("project_paths")),
            "line_refs": listify(row.get("line_refs"))[:12],
            "official_evidence": evidence,
            "reasons": reasons,
        })
    results.sort(key=lambda r: (-r["score"], str(r.get("usage_key", ""))))
    return results[:top]


def query_install(repo_root: Path, creo_root: Path, install_index_dir: Path, query: str, kind: str, top: int) -> list[dict[str, Any]]:
    records = install_finder.load_records(install_index_dir)
    alias_map = install_finder.load_json(install_index_dir / "alias_map.json", {"aliases": {}})
    install_kind = kind if kind in install_finder.KIND_TYPES else "all"
    if install_kind == "feature":
        install_kind = "all"
    matches = install_finder.query_records(records, query, install_kind, alias_map, exact=False)
    meta = install_finder.load_json(install_index_dir / "metadata.json", {})
    actual_creo_root = Path(meta.get("source_snapshot", {}).get("creo_root") or str(creo_root)).resolve()
    results = []
    for match in matches[:top]:
        item = match.as_result(actual_creo_root, explain=True)
        item["source"] = "creo_install"
        item["cache_hit"] = False
        results.append(item)
    return results


def print_text(payload: dict[str, Any]) -> None:
    print(f"Query: {payload['query']}")
    print(f"Kind: {payload['kind']}")
    if payload.get("stale"):
        print(f"WARNING: evidence cache is stale: {payload.get('stale_reason')}")
    for section, label in (("feature_results", "Feature index"), ("evidence_results", "Evidence cache"), ("install_results", "Creo install fallback")):
        rows = payload.get(section) or []
        if not rows:
            continue
        print(f"\n{label}:")
        for idx, row in enumerate(rows, start=1):
            if row.get("source") == "feature_index":
                print(f"{idx}. {row.get('title')} [{row.get('feature_id')}] score={row.get('score')}")
                for group, paths in (row.get("paths") or {}).items():
                    values = string_values(paths)
                    if values:
                        print(f"   {group}: {', '.join(values[:4])}")
                if row.get("reasons"):
                    print(f"   reasons: {'; '.join(row['reasons'][:6])}")
            elif row.get("source") == "evidence_cache":
                print(f"{idx}. {row.get('symbol')} [{row.get('usage_type')}] score={row.get('score')} cache_hit=true")
                if row.get("feature_ids"):
                    print(f"   features: {', '.join(string_values(row['feature_ids'])[:8])}")
                if row.get("project_paths"):
                    print("   project:")
                    for path in string_values(row["project_paths"])[:5]:
                        print(f"     - {path}")
                if row.get("official_evidence"):
                    print("   official evidence:")
                    for ev in row["official_evidence"][:4]:
                        print(f"     - {ev.get('install_absolute_path')} [{ev.get('install_record_type')}]")
                if row.get("reasons"):
                    print(f"   reasons: {'; '.join(row['reasons'][:8])}")
            else:
                print(f"{idx}. {row.get('title')} [{row.get('record_type')}] score={row.get('score')} source=creo_install")
                print(f"   path: {row.get('absolute_path')}")
                details = row.get("details") or {}
                if details.get("signature"):
                    print(f"   signature: {details['signature']}")
                if row.get("reasons"):
                    print(f"   reasons: {'; '.join(row['reasons'][:6])}")
    if not payload.get("feature_results") and not payload.get("evidence_results") and not payload.get("install_results") and not payload.get("graph_results"):
        print("No Creo context matches.")

    if payload.get("graph_results"):
        print("\nAPI Graph (project usage patterns):")
        for idx, row in enumerate(payload["graph_results"], start=1):
            print(f"{idx}. {row['api']} — used in {row['file_count']} files ({row['total_occurrences']} occurrences)")
            if row.get("files"):
                print(f"   files: {', '.join(str(f) for f in row['files'][:4])}")
            if row.get("related_apis"):
                related_str = ", ".join(
                    f"{r['api']}({r['cooccurrences']})"
                    for r in row["related_apis"][:8]
                )
                print(f"   commonly used with: {related_str}")


def main() -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query")
    parser.add_argument("--kind", choices=sorted(KIND_USAGE_TYPES), default="all")
    parser.add_argument("--top", type=int, default=8)
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--creo-root", default=str(DEFAULT_CREO_ROOT))
    parser.add_argument("--cache-dir", default=str(DEFAULT_CACHE_DIR))
    parser.add_argument("--install-index-dir", default=str(DEFAULT_INSTALL_INDEX_DIR))
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--explain", action="store_true")
    parser.add_argument("--refresh-if-stale", action="store_true")
    parser.add_argument("--learn", action="store_true", help="Record successful API lookups for alias learning")
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    creo_root = Path(args.creo_root).resolve()
    cache_dir = Path(args.cache_dir)
    if not cache_dir.is_absolute():
        cache_dir = repo_root / cache_dir
    install_index_dir = Path(args.install_index_dir)
    if not install_index_dir.is_absolute():
        install_index_dir = repo_root / install_index_dir

    cache_meta = load_json(cache_dir / "metadata.json", {})
    install_meta = load_json(install_index_dir / "metadata.json", {})
    stale, stale_reason = cache_stale(repo_root, cache_meta, install_meta) if cache_meta else (True, "evidence cache missing")
    if stale and args.refresh_if_stale:
        refresh_cache(repo_root, creo_root)
        cache_meta = load_json(cache_dir / "metadata.json", {})
        install_meta = load_json(install_index_dir / "metadata.json", {})
        stale, stale_reason = cache_stale(repo_root, cache_meta, install_meta)

    feature_index = load_json(repo_root / ".autobbox" / "index" / "feature_index.json", {"features": []})
    usage_rows = load_jsonl(cache_dir / "project_usage.jsonl")
    evidence_rows = load_jsonl(cache_dir / "evidence_cache.jsonl")
    api_graph = load_json(repo_root / ".autobbox" / "index" / "api_graph.json", {})

    feature_results = query_features(feature_index, args.query, args.top, args.kind)
    evidence_results = [] if not usage_rows else query_evidence(usage_rows, evidence_rows, args.query, args.kind, args.top, feature_results)
    exact_evidence_hit = any(loose(row.get("symbol")) == loose(args.query) or loose(row.get("usage_key")) == loose(args.query) for row in evidence_results)
    obvious_new_api = args.kind == "api" and looks_like_creo_api_query(args.query) and not exact_evidence_hit
    need_fallback = stale or not evidence_results or args.kind in {"sample", "doc"} or obvious_new_api
    install_results = query_install(repo_root, creo_root, install_index_dir, args.query, args.kind, args.top) if need_fallback else []

    # --- Layer 4: API graph patterns ---
    graph_results: list[dict[str, Any]] = []
    has_strong_match = bool(feature_results or evidence_results or install_results)
    if not has_strong_match or args.kind in {"api", "all"}:
        qloose = loose(args.query)
        nodes = api_graph.get("nodes", {}) if isinstance(api_graph, dict) else {}
        edges = api_graph.get("edges", {}) if isinstance(api_graph, dict) else {}

        # Try direct API symbol match in graph
        for api_name, node in nodes.items():
            if not isinstance(node, dict):
                continue
            api_loose = loose(api_name)
            if qloose == api_loose or (qloose and api_loose and qloose in api_loose):
                related = edges.get(api_name, {})
                top_related = sorted(related.items(), key=lambda x: -x[1])[:12]
                graph_results.append({
                    "source": "api_graph",
                    "api": api_name,
                    "file_count": node.get("file_count", 0),
                    "total_occurrences": node.get("total_occurrences", 0),
                    "files": listify(node.get("files"))[:8],
                    "related_apis": [
                        {"api": r_api, "cooccurrences": r_weight}
                        for r_api, r_weight in top_related
                    ],
                })

        # Sort by file_count descending
        graph_results.sort(key=lambda r: -r.get("file_count", 0))
        graph_results = graph_results[:6]

    # --- Auto alias learning ---
    if args.learn and exact_evidence_hit:
        learned_path = repo_root / ".autobbox" / "index" / "learned_aliases.jsonl"
        timestamp = datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")
        learned_entry = {
            "query": args.query,
            "kind": args.kind,
            "timestamp_utc": timestamp,
        }
        # Record matching evidence symbols
        matching_symbols = []
        for row in evidence_results:
            sym = row.get("symbol", "")
            if sym and loose(sym) == loose(args.query):
                matching_symbols.append(sym)
        if matching_symbols:
            learned_entry["matched_symbols"] = matching_symbols
            with learned_path.open("a", encoding="utf-8", newline="\n") as lf:
                lf.write(json.dumps(learned_entry, ensure_ascii=False) + "\n")

    payload = {
        "query": args.query,
        "kind": args.kind,
        "top": args.top,
        "source_order": ["feature_index", "evidence_cache", "creo_install", "api_graph"],
        "cache_dir": str(cache_dir),
        "stale": stale,
        "stale_reason": stale_reason,
        "feature_results": feature_results,
        "evidence_results": evidence_results,
        "install_results": install_results,
        "graph_results": graph_results,
    }
    if args.json:
        print(json.dumps(payload, ensure_ascii=False, indent=2))
    else:
        print_text(payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
