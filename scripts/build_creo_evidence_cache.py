#!/usr/bin/env python3
"""Build a project-local cache of AutoBBoxToolkit Creo usage and official evidence."""

from __future__ import annotations

import argparse
import json
import re
import sys
from collections import defaultdict
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

TRACKED_INPUTS = [
    "src",
    "include",
    "resource",
    "ribbon",
    "text",
    "deploy",
    "runtime",
    "autobbox_msg.txt",
    "protk.dat",
    ".autobbox/index/feature_index.json",
]

SOURCE_EXTS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
RESOURCE_EXTS = {".res", ".rbn", ".mnu", ".txt", ".dat"}
SCAN_EXTS = SOURCE_EXTS | RESOURCE_EXTS
TOKEN_RE = re.compile(r"[0-9A-Za-z_\.\-]+|[\u4e00-\u9fff]+")
PRO_SYMBOL_RE = re.compile(r"\bPro[A-Za-z_]\w+\b")
RETURN_CODE_RE = re.compile(r"\bPRO_TK_[A-Z0-9_]+\b")
INCLUDE_RE = re.compile(r"#\s*include\s+[<\"]([^>\"]+)[>\"]")
STRING_RE = re.compile(r'"([^"\n\r]{2,180})"')
RES_DIALOG_RE = re.compile(r"\(\s*Dialog\s+([A-Za-z_][A-Za-z0-9_\-]*)", re.I)
RES_COMPONENT_RE = re.compile(r"\(\s*(Tab|SubLayout|Separator|MenuBar|Table|Label|InputPanel|PushButton|CheckButton|RadioGroup|OptionMenu|List|DrawingArea|Slider|Tree|TextArea)\s+([A-Za-z_][A-Za-z0-9_\-]*)", re.I)
RES_PROP_RE = re.compile(r"\(\s*([A-Za-z0-9_.-]+)\s+(\"[^\"]*\"|[^\s()]+)\s*\)", re.I)
MESSAGE_KEY_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_.:-]{1,80}$")

# These are common C typedefs/enums, not useful as first-class API evidence.
NOISE_PRO_SYMBOLS = {
    "ProError", "ProBoolean", "ProCharLine", "ProLine", "ProName", "ProPath",
    "ProWstring", "ProAppData", "ProValueData", "ProArray", "ProMatrix",
    "ProVector", "ProPoint3d", "ProFileName", "ProMdl", "ProSolid",
}

UI_QUERY_ALIASES = {
    "无标题弹窗": ["TitleBar False", ".TitleBar False", "MiniToolbarDlg", "fdb_floatbox", "popup_preview"],
    "无标题栏弹窗": ["TitleBar False", ".TitleBar False", "MiniToolbarDlg", "fdb_floatbox", "popup_preview"],
    "无标题栏": ["TitleBar False", ".TitleBar False", "no titlebar", "titleless dialog"],
    "小弹窗": ["TitleBar False", "floating dialog", "popup_preview", "MiniToolbarDlg"],
    "浮动弹窗": ["TitleBar False", "floating dialog", "fdb_floatbox", "AlwaysOnTop"],
}


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def rel(path: Path, root: Path) -> str:
    return str(path.resolve().relative_to(root.resolve())).replace("/", "\\")


def is_inside(path: Path, root: Path) -> bool:
    try:
        path.resolve().relative_to(root.resolve())
        return True
    except ValueError:
        return False


def rel_slash(path: str) -> str:
    return path.replace("\\", "/")


def read_text(path: Path, max_bytes: int = 1_200_000) -> str:
    data = path.read_bytes()[:max_bytes]
    for enc in ("utf-8", "utf-16", "latin-1"):
        try:
            return data.decode(enc, errors="replace")
        except Exception:
            pass
    return data.decode("utf-8", errors="replace")


def normalize(text: str | None) -> str:
    if not text:
        return ""
    text = str(text).lower()
    text = re.sub(r"[^0-9a-zA-Z_\.\-\u4e00-\u9fff]+", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def terms(*values: Any) -> list[str]:
    out: set[str] = set()
    for value in values:
        if value is None:
            continue
        if isinstance(value, dict):
            out.update(terms(*value.keys(), *value.values()))
            continue
        if isinstance(value, (list, tuple, set)):
            out.update(terms(*value))
            continue
        text = str(value)
        for token in TOKEN_RE.findall(text):
            out.add(token)
            out.add(token.lower())
    return sorted(out, key=lambda item: (item.lower(), item))


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
    with path.open("r", encoding="utf-8-sig") as stream:
        return json.load(stream)


def iter_project_files(repo_root: Path) -> list[Path]:
    roots = ["src", "include", "resource", "ribbon", "text", "deploy", "runtime"]
    result: list[Path] = []
    for root_name in roots:
        root = repo_root / root_name
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if not path.is_file():
                continue
            if not is_inside(path, repo_root):
                continue
            if path.suffix.lower() in SCAN_EXTS:
                result.append(path)
    for extra in ("autobbox_msg.txt", "protk.dat"):
        path = repo_root / extra
        if path.exists() and path.is_file():
            result.append(path)
    return sorted(set(result), key=lambda p: str(p).lower())


def latest_write_time(repo_root: Path, relative_inputs: list[str]) -> str:
    latest = datetime(2000, 1, 1, tzinfo=timezone.utc)
    for item in relative_inputs:
        path = repo_root / item.replace("/", "\\")
        if not path.exists():
            continue
        files = [path] if path.is_file() else [p for p in path.rglob("*") if p.is_file()]
        for file in files:
            if not is_inside(file, repo_root):
                continue
            if file.suffix.lower() not in SCAN_EXTS and file.name not in {"feature_index.json", "autobbox_msg.txt", "protk.dat"}:
                continue
            mtime = datetime.fromtimestamp(file.stat().st_mtime, tz=timezone.utc)
            latest = max(latest, mtime)
    return latest.isoformat().replace("+00:00", "Z")


def load_feature_index(repo_root: Path) -> tuple[dict[str, Any], dict[str, set[str]]]:
    data = load_json(repo_root / ".autobbox" / "index" / "feature_index.json", {"features": []})
    path_to_features: dict[str, set[str]] = defaultdict(set)
    for feature in listify(data.get("features")):
        fid = str(feature.get("feature_id", ""))
        paths = feature.get("paths") or {}
        if not fid or not isinstance(paths, dict):
            continue
        for group in ("main", "application", "ui", "creo", "include"):
            for path in listify(paths.get(group)):
                if path:
                    path_to_features[rel_slash(str(path)).lower()].add(fid)
    return data, path_to_features


def feature_ids_for_paths(paths: list[str], path_to_features: dict[str, set[str]]) -> list[str]:
    found: set[str] = set()
    for path in paths:
        key = rel_slash(path).lower()
        found.update(path_to_features.get(key, set()))
    return sorted(found)


class UsageAccumulator:
    def __init__(self, path_to_features: dict[str, set[str]]) -> None:
        self.path_to_features = path_to_features
        self.rows: dict[str, dict[str, Any]] = {}

    def add(self, usage_type: str, symbol: str, project_path: str | None, line: int | None, derived_from: str, extra_terms: list[Any] | None = None) -> None:
        if not symbol:
            return
        usage_key = f"{usage_type}:{symbol}"
        row = self.rows.setdefault(usage_key, {
            "usage_key": usage_key,
            "usage_type": usage_type,
            "symbol": symbol,
            "project_paths": [],
            "feature_ids": [],
            "line_refs": [],
            "search_terms": [],
            "derived_from": derived_from,
        })
        if project_path and project_path not in row["project_paths"]:
            row["project_paths"].append(project_path)
        if project_path and line:
            line_ref = {"path": project_path, "line": line}
            if line_ref not in row["line_refs"] and len(row["line_refs"]) < 80:
                row["line_refs"].append(line_ref)
        fids = feature_ids_for_paths(row["project_paths"], self.path_to_features)
        row["feature_ids"] = sorted(set(row.get("feature_ids", [])) | set(fids))
        row["search_terms"] = terms(row.get("search_terms", []), usage_type, symbol, usage_key, project_path, extra_terms, row["feature_ids"])

    def merge_project_path(self, usage_key: str, project_path: str, extra_terms: list[Any] | None = None) -> None:
        row = self.rows.get(usage_key)
        if not row:
            return
        if project_path not in row["project_paths"]:
            row["project_paths"].append(project_path)
        row["feature_ids"] = sorted(set(row.get("feature_ids", [])) | set(feature_ids_for_paths(row["project_paths"], self.path_to_features)))
        row["search_terms"] = terms(row.get("search_terms", []), project_path, extra_terms, row["feature_ids"])

    def records(self) -> list[dict[str, Any]]:
        for row in self.rows.values():
            row["project_paths"] = sorted(row["project_paths"], key=str.lower)
            row["feature_ids"] = sorted(set(row["feature_ids"]))
            row["search_terms"] = terms(row["search_terms"], row["project_paths"], row["feature_ids"])
        return sorted(self.rows.values(), key=lambda r: (r["usage_type"], r["symbol"].lower(), r["usage_key"]))


def collect_known_resources(files: list[Path], repo_root: Path) -> dict[str, list[str]]:
    known: dict[str, list[str]] = defaultdict(list)
    for path in files:
        ext = path.suffix.lower()
        if ext not in {".res", ".rbn", ".mnu", ".txt"}:
            continue
        rp = rel(path, repo_root)
        for key in {path.name.lower(), path.stem.lower()}:
            known[key].append(rp)
    return known


def scan_source(path: Path, repo_root: Path, acc: UsageAccumulator, known_resources: dict[str, list[str]]) -> None:
    rp = rel(path, repo_root)
    text = read_text(path)
    lines = text.splitlines()
    for idx, line in enumerate(lines, start=1):
        for inc in INCLUDE_RE.findall(line):
            if inc.startswith("Pro"):
                symbol = Path(inc).stem
                acc.add("api", symbol, rp, idx, rp, [inc, "include"])
        for sym in PRO_SYMBOL_RE.findall(line):
            if sym in NOISE_PRO_SYMBOLS:
                continue
            acc.add("api", sym, rp, idx, rp)
        for code in RETURN_CODE_RE.findall(line):
            acc.add("return_code", code, rp, idx, rp)
        for literal in STRING_RE.findall(line):
            low = literal.lower().replace("/", "\\")
            stem = Path(low).stem.lower()
            leaf = Path(low).name.lower()
            if leaf in known_resources or stem in known_resources or low.endswith((".res", ".rbn", ".mnu", ".txt")):
                usage_type = "message" if low.endswith(".txt") else ("ribbon" if low.endswith(".rbn") else "resource")
                symbol = Path(literal).name if "." in Path(literal).name else literal
                if stem in known_resources and not symbol.lower().endswith((".res", ".rbn", ".mnu", ".txt")):
                    # Prefer the actual project resource filename when a base name is used in code.
                    first = known_resources[stem][0]
                    symbol = Path(first).name
                    usage_type = "message" if symbol.lower().endswith(".txt") else ("ribbon" if symbol.lower().endswith(".rbn") else "resource")
                acc.add(usage_type, symbol, rp, idx, rp, [literal, stem])
                for resource_path in known_resources.get(stem, []) + known_resources.get(leaf, []):
                    acc.merge_project_path(f"{usage_type}:{symbol}", resource_path, [literal])


def scan_res(path: Path, repo_root: Path, acc: UsageAccumulator) -> None:
    rp = rel(path, repo_root)
    text = read_text(path)
    acc.add("resource", path.name, rp, 1, rp, [path.stem, ".res", "resource"])
    for match in RES_DIALOG_RE.finditer(text):
        line = text.count("\n", 0, match.start()) + 1
        acc.add("dialog", match.group(1), rp, line, rp, [path.name, path.stem, "dialog"])
    widgets = []
    for match in RES_COMPONENT_RE.finditer(text):
        widgets.append(match.group(2))
    props = []
    for match in RES_PROP_RE.finditer(text):
        key = match.group(1)
        raw = match.group(2).strip()
        value = raw[1:-1] if raw.startswith('"') and raw.endswith('"') else raw
        if key.startswith("."):
            props.append(f"{key} {value}")
            if key.lower() == ".titlebar":
                line = text.count("\n", 0, match.start()) + 1
                symbol = f"{key} {value}"
                alias_terms = []
                if value.lower() == "false":
                    alias_terms = ["TitleBar False", "无标题弹窗", "无标题栏弹窗", "无标题栏", "titleless dialog", "no titlebar"]
                acc.add("ui_property", symbol, rp, line, rp, [path.name, path.stem, *alias_terms])
    acc.rows[f"resource:{path.name}"]["search_terms"] = terms(acc.rows[f"resource:{path.name}"]["search_terms"], widgets[:120], props[:80])


def scan_ribbon(path: Path, repo_root: Path, acc: UsageAccumulator) -> None:
    rp = rel(path, repo_root)
    text = read_text(path)
    tokens = sorted(set(re.findall(r"[A-Za-z][A-Za-z0-9_.:-]{2,}", text)))[:200]
    acc.add("ribbon", path.name, rp, 1, rp, [path.stem, tokens])
    for token in tokens:
        if "AutoBBox" in token or token.startswith("AB"):
            acc.add("ribbon", token, rp, None, rp, [path.name, path.stem])


def scan_message(path: Path, repo_root: Path, acc: UsageAccumulator) -> None:
    rp = rel(path, repo_root)
    acc.add("message", path.name, rp, 1, rp, [path.stem, "message"])
    lines = read_text(path, 700_000).splitlines()
    expect_key = True
    for idx, line in enumerate(lines, start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith(("#", "!", "//")):
            continue
        if expect_key and MESSAGE_KEY_RE.match(stripped):
            acc.add("message", stripped, rp, idx, rp, [path.name])
        expect_key = not expect_key


def scan_packaging(path: Path, repo_root: Path, acc: UsageAccumulator) -> None:
    rp = rel(path, repo_root)
    if path.name.lower() == "protk.dat":
        acc.add("packaging", "protk.dat", rp, 1, rp, ["registry", "startup", "text_dir"])


def evidence_kind_for_usage(usage_type: str) -> str:
    if usage_type in {"api", "return_code"}:
        return "api"
    if usage_type in {"dialog", "resource", "ribbon", "message", "ui_property"}:
        return "ui"
    if usage_type == "packaging":
        return "path"
    return "all"


def evidence_queries_for_usage(row: dict[str, Any]) -> list[str]:
    usage_type = row["usage_type"]
    symbol = row["symbol"]
    queries = [symbol]
    if usage_type == "ui_property" and symbol.lower() == ".titlebar false":
        queries = ["TitleBar False", "无标题弹窗"]
    elif usage_type == "resource" and symbol.lower().endswith(".res"):
        queries.append(Path(symbol).stem)
    elif usage_type == "ribbon" and symbol.lower().endswith(".rbn"):
        queries.append(Path(symbol).stem)
    elif usage_type == "message" and symbol.lower().endswith(".txt"):
        queries.append(Path(symbol).stem)
    for alias, alias_terms in UI_QUERY_ALIASES.items():
        if alias == symbol:
            queries.extend(alias_terms)
    out = []
    for query in queries:
        if query and query not in out:
            out.append(query)
    return out[:4]


def make_evidence_cache(rows: list[dict[str, Any]], repo_root: Path, creo_root: Path, install_index_dir: Path, top_per_usage: int = 4) -> list[dict[str, Any]]:
    install_records = install_finder.load_records(install_index_dir)
    alias_map = install_finder.load_json(install_index_dir / "alias_map.json", {"aliases": {}})
    install_metadata = install_finder.load_json(install_index_dir / "metadata.json", {})
    generated = install_metadata.get("generated_at_utc", "")
    evidence: list[dict[str, Any]] = []
    seen: set[tuple[str, str]] = set()
    exact_map: dict[str, list[dict[str, Any]]] = defaultdict(list)
    for record in install_records:
        rel_path = str(record.get("relative_path", ""))
        leaf = Path(rel_path).name
        stem = Path(rel_path).stem
        key_values = {record.get("symbol"), record.get("title")}
        # API symbol records often share the same header path. Do not index the
        # header basename for every API in that header, or a query for
        # "ProUIDialog" would incorrectly return every symbol declared in
        # ProUIDialog.h. Header/doc/resource records still get filename keys.
        if record.get("record_type") != "api_symbol":
            key_values.update({leaf, stem})
        for key in key_values:
            norm_key = install_finder.normalize_loose(str(key or ""))
            if norm_key:
                exact_map[norm_key].append(record)
    for key in list(exact_map):
        exact_map[key].sort(
            key=lambda r: (
                install_finder.RECORD_ORDER.get(str(r.get("record_type")), 99),
                str(r.get("relative_path", "")).lower(),
            )
        )
    query_cache: dict[tuple[str, str], list[Any]] = {}

    def should_cache_evidence(row: dict[str, Any]) -> bool:
        usage_type = row["usage_type"]
        symbol = str(row["symbol"])
        if usage_type in {"return_code"}:
            return False
        if usage_type == "message" and not symbol.lower().endswith(".txt"):
            return False
        if usage_type == "ribbon" and not symbol.lower().endswith(".rbn"):
            return False
        return True

    def lookup(query: str, kind: str) -> list[Any]:
        cache_key = (query, kind)
        if cache_key in query_cache:
            return query_cache[cache_key]
        qnorm = install_finder.normalize_loose(query)
        # Most project usages are exact API/header/resource symbols. Use an
        # exact map first so building the cache does not rescan the full
        # install index for every already-known API call.
        if qnorm and re.match(r"^[0-9a-zA-Z_.\-]+$", query) and " " not in query:
            allowed = install_finder.KIND_TYPES.get(kind, install_finder.KIND_TYPES["all"])
            matches = [
                install_finder.Match(record=record, score=15000, reasons=["evidence exact cache"])
                for record in exact_map.get(qnorm, [])
                if record.get("record_type") in allowed
            ]
            query_cache[cache_key] = matches[:top_per_usage]
            return query_cache[cache_key]
        matches = install_finder.query_records(install_records, query, kind, alias_map, exact=False)
        query_cache[cache_key] = matches[:top_per_usage]
        return query_cache[cache_key]

    for row in rows:
        if not should_cache_evidence(row):
            continue
        kind = evidence_kind_for_usage(row["usage_type"])
        for query in evidence_queries_for_usage(row):
            matches = lookup(query, kind)
            for match in matches:
                result = match.as_result(creo_root, explain=True)
                install_rel = result.get("relative_path") or ""
                evidence_key = f"{row['usage_key']}|{install_rel}|{result.get('record_type')}"
                key = (row["usage_key"], evidence_key)
                if key in seen:
                    continue
                seen.add(key)
                evidence.append({
                    "usage_key": row["usage_key"],
                    "evidence_key": evidence_key,
                    "source_query": query,
                    "install_record_type": result.get("record_type"),
                    "title": result.get("title"),
                    "symbol": result.get("symbol"),
                    "install_relative_path": install_rel,
                    "install_absolute_path": result.get("absolute_path"),
                    "details": result.get("details") or {},
                    "score": result.get("score"),
                    "reasons": result.get("reasons") or [],
                    "verified_at_utc": now_utc(),
                    "creo_install_index_generated_at_utc": generated,
                })
    return sorted(evidence, key=lambda r: (r["usage_key"], -(r.get("score") or 0), str(r.get("install_relative_path", "")).lower()))


def write_jsonl(path: Path, rows: list[dict[str, Any]]) -> int:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for row in rows:
            stream.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
    return len(rows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--creo-root", default=str(DEFAULT_CREO_ROOT))
    parser.add_argument("--cache-dir", default=str(DEFAULT_CACHE_DIR))
    parser.add_argument("--install-index-dir", default=str(DEFAULT_INSTALL_INDEX_DIR))
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    creo_root = Path(args.creo_root).resolve()
    cache_dir = Path(args.cache_dir)
    if not cache_dir.is_absolute():
        cache_dir = repo_root / cache_dir
    install_index_dir = Path(args.install_index_dir)
    if not install_index_dir.is_absolute():
        install_index_dir = repo_root / install_index_dir
    if not (install_index_dir / "search_index.jsonl").exists():
        raise SystemExit(f"Creo install index missing: {install_index_dir}. Run update_creo_install_index.ps1 -Full first.")

    cache_dir.mkdir(parents=True, exist_ok=True)
    feature_index, path_to_features = load_feature_index(repo_root)
    files = iter_project_files(repo_root)
    known_resources = collect_known_resources(files, repo_root)
    acc = UsageAccumulator(path_to_features)

    for path in files:
        ext = path.suffix.lower()
        if ext in SOURCE_EXTS:
            scan_source(path, repo_root, acc, known_resources)
        elif ext == ".res":
            scan_res(path, repo_root, acc)
        elif ext == ".rbn":
            scan_ribbon(path, repo_root, acc)
        elif ext == ".txt":
            scan_message(path, repo_root, acc)
        elif ext == ".dat" or path.name.lower() == "protk.dat":
            scan_packaging(path, repo_root, acc)

    usage_rows = acc.records()
    evidence_rows = make_evidence_cache(usage_rows, repo_root, creo_root, install_index_dir)
    usage_count = write_jsonl(cache_dir / "project_usage.jsonl", usage_rows)
    evidence_count = write_jsonl(cache_dir / "evidence_cache.jsonl", evidence_rows)

    install_metadata = load_json(install_index_dir / "metadata.json", {})
    metadata = {
        "schema_version": 1,
        "generated_at_utc": now_utc(),
        "repo_root": str(repo_root),
        "cache_dir": str(cache_dir),
        "project_snapshot": {
            "latest_input_write_time_utc": latest_write_time(repo_root, TRACKED_INPUTS),
            "tracked_inputs": TRACKED_INPUTS,
        },
        "creo_install_index": {
            "index_dir": str(install_index_dir),
            "generated_at_utc": install_metadata.get("generated_at_utc", ""),
            "creo_root": install_metadata.get("source_snapshot", {}).get("creo_root", str(creo_root)),
        },
        "feature_index": {
            "path": str(repo_root / ".autobbox" / "index" / "feature_index.json"),
            "generated_at_utc": feature_index.get("generated_at_utc", ""),
        },
        "counts": {
            "project_usage": usage_count,
            "evidence_cache": evidence_count,
        },
    }
    (cache_dir / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Creo evidence cache updated: {cache_dir}")
    print(f"Project usage records: {usage_count}")
    print(f"Evidence records: {evidence_count}")
    print(f"Latest project input UTC: {metadata['project_snapshot']['latest_input_write_time_utc']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
