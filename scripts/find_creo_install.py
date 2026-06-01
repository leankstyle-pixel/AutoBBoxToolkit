#!/usr/bin/env python3
"""Fast query and bundle tool for the project-local Creo installation index."""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterable

DEFAULT_CREO_ROOT = Path(r"D:\Program Files\PTC\Creo 10.0.8.0")
DEFAULT_INDEX_DIR = Path(r".autobbox\index\creo_install")

KIND_TYPES = {
    "api": {"api_symbol", "header", "doc_page"},
    "ui": {"ui_resource", "message_file", "ribbon"},
    "sample": {"sample_source"},
    "doc": {"doc_page"},
    "path": {"packaging", "build_path", "header", "doc_page", "ui_resource", "message_file", "ribbon", "sample_source"},
    "all": {"api_symbol", "doc_page", "header", "ui_resource", "message_file", "ribbon", "sample_source", "packaging", "build_path"},
}

TYPE_BOOSTS = {
    "api_symbol": 2200,
    "header": 1400,
    "doc_page": 1300,
    "ui_resource": 700,
    "ribbon": 700,
    "message_file": 450,
    "packaging": 700,
    "build_path": 300,
    "sample_source": 100,
}

RECORD_ORDER = {
    "api_symbol": 0,
    "header": 1,
    "doc_page": 2,
    "ui_resource": 3,
    "ribbon": 4,
    "message_file": 5,
    "sample_source": 6,
    "packaging": 7,
    "build_path": 8,
}

TOKEN_RE = re.compile(r"[0-9A-Za-z_\.\-]+|[\u4e00-\u9fff]+")
NOISE_TOKENS = {"h", "c", "cpp", "cxx", "html", "htm", "txt", "res", "rbn", "mnu"}


@dataclass
class Match:
    record: dict[str, Any]
    score: int
    reasons: list[str] = field(default_factory=list)

    def as_result(self, creo_root: Path, explain: bool = True) -> dict[str, Any]:
        rel = self.record.get("relative_path", "")
        related_paths = listify(self.record.get("related_paths"))
        result = {
            "score": self.score,
            "record_type": self.record.get("record_type"),
            "title": self.record.get("title"),
            "symbol": self.record.get("symbol"),
            "api_family": self.record.get("api_family"),
            "relative_path": rel,
            "absolute_path": str((creo_root / rel).resolve()) if rel else None,
            "related_paths": related_paths,
            "related_absolute_paths": [str((creo_root / p).resolve()) for p in related_paths],
            "derived_from": self.record.get("derived_from"),
        }
        if self.record.get("details"):
            result["details"] = self.record.get("details")
        if explain:
            result["reasons"] = self.reasons
        return result


def normalize_text(text: str | None) -> str:
    if not text:
        return ""
    lowered = str(text).lower()
    lowered = re.sub(r"[^0-9a-zA-Z_\.\-\u4e00-\u9fff]+", " ", lowered)
    return re.sub(r"\s+", " ", lowered).strip()


def normalize_loose(text: str | None) -> str:
    return normalize_text(text).replace(" ", "")


def tokens(text: str | None) -> list[str]:
    normalized = normalize_text(text)
    if not normalized:
        return []
    return sorted(set(normalized.split()))


def listify(value: Any) -> list[Any]:
    if value is None:
        return []
    if isinstance(value, list):
        return value
    if isinstance(value, tuple):
        return list(value)
    return [value]


def load_json(path: Path, default: Any) -> Any:
    if not path.exists():
        return default
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def load_records(index_dir: Path) -> list[dict[str, Any]]:
    search_path = index_dir / "search_index.jsonl"
    if not search_path.exists():
        raise FileNotFoundError(f"Creo install search index not found: {search_path}")
    records: list[dict[str, Any]] = []
    with search_path.open("r", encoding="utf-8") as stream:
        for line in stream:
            if line.strip():
                records.append(json.loads(line))
    return records


def expanded_query_terms(query: str, alias_map: dict[str, Any]) -> tuple[list[str], list[str]]:
    expanded = set(tokens(query))
    alias_hits: list[str] = []
    query_loose = normalize_loose(query)
    aliases = alias_map.get("aliases", {}) if isinstance(alias_map, dict) else {}
    for alias_name, alias_terms in aliases.items():
        alias_loose = normalize_loose(alias_name)
        if query_loose == alias_loose or (alias_loose and (query_loose in alias_loose or alias_loose in query_loose)):
            for term in listify(alias_terms):
                alias_hits.append(str(term))
                expanded.update(tokens(str(term)))
    return sorted(expanded), alias_hits


def hay_values(record: dict[str, Any]) -> list[str]:
    rel = str(record.get("relative_path", ""))
    leaf = os.path.basename(rel)
    values: list[str] = [
        str(record.get("symbol", "")),
        str(record.get("title", "")),
        rel,
        leaf,
        Path(leaf).stem,
        str(record.get("api_family", "")),
        str(record.get("record_type", "")),
    ]
    values.extend(str(v) for v in listify(record.get("search_terms")))
    return values


def intent_boost(record: dict[str, Any], query: str, expanded: Iterable[str]) -> tuple[int, list[str]]:
    q = normalize_loose(query)
    terms_set = {normalize_loose(t) for t in expanded}
    symbol = normalize_loose(record.get("symbol"))
    title = normalize_loose(record.get("title"))
    rel = normalize_loose(record.get("relative_path"))
    rtype = str(record.get("record_type", ""))
    reasons: list[str] = []
    score = 0

    def add(value: int, reason: str) -> None:
        nonlocal score
        score += value
        if reason not in reasons:
            reasons.append(reason)

    if q in {"参数", "parameter", "param", "proparameter"}:
        if "proparameter" in symbol or "proparameter" in title:
            add(2600, "intent: parameter api")
        if "ptu_param" in rel:
            add(900, "intent: parameter sample")
    if q in {"建视图", "视图", "工程图视图", "drawingview", "drawingviewcreate"} or {"prodrawingview", "drawing", "view"} & terms_set:
        if "prodrawingview" in symbol or "prodrawingview" in title or "prodrawingview" in rel:
            add(2600, "intent: drawing view api")
        elif symbol == "proview":
            add(900, "intent: generic view api")
        if "ptu_dwg" in rel or "tkdrwview" in rel:
            add(900, "intent: drawing view sample")
    if q in {"对话框", "dialog", "prouidialog"}:
        if "prouidialog" in symbol or "prouidialog" in title or "prouidialog" in rel:
            if rtype in {"api_symbol", "header", "doc_page"} and ("prouidialog" in symbol or "prouidialog" in title):
                add(2600, "intent: dialog api")
            elif rtype in {"header", "doc_page"}:
                add(1400, "intent: dialog header/doc")
        if rtype in {"ui_resource", "ribbon"}:
            add(900, "intent: ui resource")
    if q in {"protk.dat", "protkdat"} and rtype == "packaging":
        add(3000, "intent: registry file")
    return score, reasons


def res_property_boost(record: dict[str, Any], query: str, expanded: Iterable[str]) -> tuple[int, list[str]]:
    """Rank exact .res property matches above incidental token matches.

    Queries such as "TitleBar False" should prefer records that actually have
    dialog property (.TitleBar False), not arbitrary resources containing a
    component named TitleBar plus some unrelated False value.
    """
    if record.get("record_type") != "ui_resource":
        return 0, []
    details = record.get("details") or {}
    if not isinstance(details, dict):
        return 0, []
    score = 0
    reasons: list[str] = []
    query_terms = {normalize_loose(token) for token in [query, *list(expanded)] if normalize_loose(token)}
    query_joined = "".join(sorted(query_terms))

    def wants(*needles: str) -> bool:
        return all(any(needle in term for term in query_terms) or needle in query_joined for needle in needles)

    dialog_props = details.get("dialog_properties") or {}
    titlebar_values = []
    if isinstance(dialog_props, dict):
        raw_values = dialog_props.get(".TitleBar") or dialog_props.get("TitleBar") or []
        titlebar_values = [normalize_loose(v) for v in listify(raw_values)]
    if wants("titlebar", "false") and "false" in titlebar_values:
        score += 3600
        reasons.append("property: .TitleBar False")
    if wants("titlebar", "true") and "true" in titlebar_values:
        score += 3600
        reasons.append("property: .TitleBar True")

    property_pairs = [normalize_loose(v) for v in listify(details.get("property_pairs"))]
    for pair in property_pairs:
        if query_terms and any(term and term in pair for term in query_terms):
            score += 160
            reasons.append("property pair")
            break
    return score, reasons


def score_record(record: dict[str, Any], query: str, expanded: list[str], alias_hits: list[str], *, exact: bool = False) -> Match | None:
    query_loose = normalize_loose(query)
    values = hay_values(record)
    loose_values = [normalize_loose(v) for v in values if normalize_loose(v)]
    symbol = str(record.get("symbol", ""))
    title = str(record.get("title", ""))
    rel = str(record.get("relative_path", ""))
    leaf = os.path.basename(rel)
    stem = Path(leaf).stem
    score = TYPE_BOOSTS.get(str(record.get("record_type")), 0)
    reasons: list[str] = []

    for value in (symbol, title, leaf, stem):
        loose = normalize_loose(value)
        if loose and loose == query_loose:
            score += 12000
            reasons.append(f"exact: {value}")

    if exact and not reasons:
        return None

    for value in loose_values:
        if value and (query_loose in value or value in query_loose):
            score += 900
            reasons.append("phrase")
            break

    for alias_term in alias_hits:
        alias_loose = normalize_loose(alias_term)
        if alias_loose and any(alias_loose == v or alias_loose in v for v in loose_values):
            score += 1400
            reasons.append(f"alias: {alias_term}")

    for token in expanded:
        token_loose = normalize_loose(token)
        if not token_loose or token_loose in NOISE_TOKENS:
            continue
        if any(token_loose in value for value in loose_values):
            score += 140
            reasons.append(f"term: {token}")

    for related in listify(record.get("related_paths")):
        if query_loose and query_loose in normalize_loose(str(related)):
            score += 180
            reasons.append("related path")
            break

    iboost, ireasons = intent_boost(record, query, expanded)
    score += iboost
    reasons.extend(ireasons)

    pboost, preasons = res_property_boost(record, query, expanded)
    score += pboost
    reasons.extend(preasons)

    if "." in query and record.get("record_type") == "api_symbol" and normalize_loose(symbol) != query_loose:
        score -= 3500
        reasons.append("penalty: filename query")

    if score <= TYPE_BOOSTS.get(str(record.get("record_type")), 0):
        return None
    return Match(record=record, score=score, reasons=dedupe(reasons))


def dedupe(values: Iterable[str]) -> list[str]:
    result: list[str] = []
    seen = set()
    for value in values:
        if value and value not in seen:
            result.append(value)
            seen.add(value)
    return result


def query_records(records: list[dict[str, Any]], query: str, kind: str, alias_map: dict[str, Any], *, exact: bool = False) -> list[Match]:
    expanded, alias_hits = expanded_query_terms(query, alias_map)
    allowed = KIND_TYPES[kind]
    matches = []
    for record in records:
        if record.get("record_type") not in allowed:
            continue
        match = score_record(record, query, expanded, alias_hits, exact=exact)
        if match:
            matches.append(match)
    matches.sort(key=lambda m: (-m.score, RECORD_ORDER.get(str(m.record.get("record_type")), 99), str(m.record.get("symbol", "")).lower(), str(m.record.get("relative_path", "")).lower()))
    return matches


def record_matches_bundle_seed(record: dict[str, Any], seeds: set[str], seed_paths: set[str]) -> bool:
    rel = str(record.get("relative_path", ""))
    symbol = normalize_loose(record.get("symbol"))
    title = normalize_loose(record.get("title"))
    leaf = normalize_loose(os.path.basename(rel))
    stem = normalize_loose(Path(rel).stem)
    if rel in seed_paths:
        return True
    for seed in seeds:
        if not seed:
            continue
        if seed in {symbol, title, leaf, stem}:
            return True
        if seed and (seed in symbol or seed in title or seed in stem):
            return True
    for related in listify(record.get("related_paths")):
        if str(related) in seed_paths:
            return True
    return False


def make_bundle(records: list[dict[str, Any]], matches: list[Match], query: str, creo_root: Path, explain: bool) -> dict[str, Any]:
    primary = matches[0] if matches else None
    min_seed_score = int(primary.score * 0.5) if primary else 0
    seed_matches = [
        match for match in matches[:12]
        if match.score >= min_seed_score or any(reason.startswith("exact:") for reason in match.reasons)
    ][:8]
    seeds = {normalize_loose(query)}
    seed_paths: set[str] = set()
    for match in seed_matches:
        rec = match.record
        seeds.add(normalize_loose(rec.get("symbol")))
        seeds.add(normalize_loose(rec.get("title")))
        seeds.add(normalize_loose(Path(str(rec.get("relative_path", ""))).stem))
        seed_paths.add(str(rec.get("relative_path", "")))
        seed_paths.update(str(p) for p in listify(rec.get("related_paths")))

    grouped = {
        "api_symbols": [],
        "headers": [],
        "docs": [],
        "ui_resources": [],
        "ribbons": [],
        "message_files": [],
        "samples": [],
        "packaging": [],
        "build_paths": [],
        "related": [],
    }
    type_to_group = {
        "api_symbol": "api_symbols",
        "header": "headers",
        "doc_page": "docs",
        "ui_resource": "ui_resources",
        "ribbon": "ribbons",
        "message_file": "message_files",
        "sample_source": "samples",
        "packaging": "packaging",
        "build_path": "build_paths",
    }
    seen_group_paths: dict[str, set[str]] = {key: set() for key in grouped}
    ranked_candidates: list[Match] = []
    for record in records:
        if not record_matches_bundle_seed(record, seeds, seed_paths):
            continue
        match = score_record(record, query, sorted(seeds), [], exact=False) or Match(record, TYPE_BOOSTS.get(str(record.get("record_type")), 0), ["bundle related"])
        ranked_candidates.append(match)
    ranked_candidates.sort(key=lambda m: (-m.score, RECORD_ORDER.get(str(m.record.get("record_type")), 99), str(m.record.get("relative_path", "")).lower()))

    for match in ranked_candidates:
        rtype = str(match.record.get("record_type"))
        group = type_to_group.get(rtype, "related")
        rel = str(match.record.get("relative_path", ""))
        if rel in seen_group_paths[group]:
            continue
        grouped[group].append(match.as_result(creo_root, explain=explain))
        seen_group_paths[group].add(rel)
        limit = 12 if group in {"api_symbols", "samples", "related"} else 8
        if len(grouped[group]) > limit:
            grouped[group] = grouped[group][:limit]

    return {
        "primary": primary.as_result(creo_root, explain=explain) if primary else None,
        **grouped,
    }


def print_results(results: list[dict[str, Any]], *, no_related: bool) -> None:
    if not results:
        print("No Creo install index matches.")
        return
    for idx, result in enumerate(results, start=1):
        print(f"{idx}. {result['title']} [{result['record_type']}] score={result['score']}")
        print(f"   symbol: {result['symbol']}")
        print(f"   family: {result['api_family']}")
        print(f"   path: {result['absolute_path']}")
        details = result.get("details") or {}
        if details.get("signature"):
            print(f"   signature: {details['signature']}")
        if details.get("return_type"):
            print(f"   return: {details['return_type']}")
        if details.get("return_values"):
            print(f"   return values: {', '.join(details['return_values'][:8])}")
        if details.get("dialog_names"):
            print(f"   dialogs: {', '.join(details['dialog_names'][:8])}")
        if details.get("titlebar"):
            print(f"   titlebar: {details['titlebar']}")
        if details.get("dialog_properties") and ".TitleBar" in details["dialog_properties"]:
            print(f"   .TitleBar: {', '.join(details['dialog_properties']['.TitleBar'][:4])}")
        if details.get("widget_names"):
            print(f"   widgets: {', '.join(details['widget_names'][:10])}")
        if details.get("message_keys"):
            print(f"   message keys: {', '.join(details['message_keys'][:10])}")
        if details.get("resource_refs"):
            print(f"   resource refs: {', '.join(details['resource_refs'][:10])}")
        if details.get("api_refs") and result["record_type"] in {"sample_source", "doc_page"}:
            print(f"   api refs: {', '.join(details['api_refs'][:10])}")
        if details.get("sample_refs") and result["record_type"] == "doc_page":
            print(f"   sample refs: {len(details['sample_refs'])}")
        if details.get("replacement_otk"):
            print(f"   OTK replacement: {details['replacement_otk']}")
        if result.get("reasons"):
            print(f"   reasons: {'; '.join(result['reasons'][:8])}")
        if not no_related and result.get("related_absolute_paths"):
            print("   related:")
            for related in result["related_absolute_paths"][:5]:
                print(f"     - {related}")


def print_bundle(bundle: dict[str, Any], *, no_related: bool) -> None:
    primary = bundle.get("primary")
    if primary:
        print("Primary:")
        print_results([primary], no_related=no_related)
    section_names = [
        ("api_symbols", "API symbols"),
        ("headers", "Headers"),
        ("docs", "Docs"),
        ("ui_resources", "UI resources"),
        ("ribbons", "Ribbons"),
        ("message_files", "Message files"),
        ("samples", "Samples"),
        ("packaging", "Packaging"),
        ("build_paths", "Build paths"),
    ]
    for key, label in section_names:
        items = bundle.get(key) or []
        if not items:
            continue
        print(f"\n{label}:")
        for item in items[:8]:
            print(f"- {item['title']} [{item['record_type']}]")
            print(f"  {item['absolute_path']}")
            details = item.get("details") or {}
            if details.get("signature"):
                print(f"  signature: {details['signature']}")
            elif details.get("return_values"):
                print(f"  return values: {', '.join(details['return_values'][:6])}")
            elif details.get("dialog_names"):
                print(f"  dialogs: {', '.join(details['dialog_names'][:6])}")
            elif details.get("resource_refs"):
                print(f"  resource refs: {', '.join(details['resource_refs'][:6])}")


def main(argv: list[str] | None = None) -> int:
    if hasattr(sys.stdout, "reconfigure"):
        sys.stdout.reconfigure(encoding="utf-8")
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query")
    parser.add_argument("--kind", choices=sorted(KIND_TYPES), default="all")
    parser.add_argument("--top", type=int, default=10)
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--creo-root", default=str(DEFAULT_CREO_ROOT))
    parser.add_argument("--index-dir", default=str(DEFAULT_INDEX_DIR))
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--bundle", action="store_true")
    parser.add_argument("--exact", action="store_true")
    parser.add_argument("--explain", action="store_true")
    parser.add_argument("--no-related", action="store_true")
    args = parser.parse_args(argv)

    repo_root = Path(args.repo_root).resolve()
    index_dir = Path(args.index_dir)
    if not index_dir.is_absolute():
        index_dir = repo_root / index_dir
    metadata = load_json(index_dir / "metadata.json", {})
    creo_root = Path(metadata.get("source_snapshot", {}).get("creo_root") or args.creo_root).resolve()
    alias_map = load_json(index_dir / "alias_map.json", {"aliases": {}})
    records = load_records(index_dir)
    matches = query_records(records, args.query, args.kind, alias_map, exact=args.exact)
    results = [m.as_result(creo_root, explain=(args.explain or args.json)) for m in matches[: args.top]]

    output = {
        "query": args.query,
        "kind": args.kind,
        "top": args.top,
        "creo_root": str(creo_root),
        "index_dir": str(index_dir),
        "results": results,
    }
    if args.bundle:
        output["bundle"] = make_bundle(records, matches, args.query, creo_root, explain=(args.explain or args.json))

    if args.json:
        print(json.dumps(output, ensure_ascii=False, indent=2))
        return 0

    if args.bundle:
        print_bundle(output["bundle"], no_related=args.no_related)
    else:
        print_results(results, no_related=args.no_related)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
