#!/usr/bin/env python3
"""Build a project-local search index from the official Creo installation."""

from __future__ import annotations

import argparse
import html
import json
import re
from datetime import datetime, timezone
from pathlib import Path

DEFAULT_CREO_ROOT = Path(r"D:\Program Files\PTC\Creo 10.0.8.0")
DEFAULT_INDEX_DIR = Path(r".autobbox\index\creo_install")

SCAN_TARGETS = [
    ("ptk_headers", Path(r"Common Files\protoolkit\includes"), "ptk"),
    ("ptk_docs", Path(r"Common Files\protoolkit\protkdoc"), "doc"),
    ("ptk_samples", Path(r"Common Files\protoolkit\protk_appls"), "sample"),
    ("ptk_registry", Path(r"Common Files\protoolkit\protk.dat"), "packaging"),
    ("ptk_build", Path(r"Common Files\protoolkit\x86e_win64"), "build"),
    ("otk_headers", Path(r"Common Files\otk\otk_cpp\include"), "otk_cpp"),
    ("otk_samples", Path(r"Common Files\otk\otk_cpp\otk_examples"), "sample"),
    ("otk_build", Path(r"Common Files\otk\otk_cpp\x86e_win64"), "build"),
    ("otk_docs", Path(r"Common Files\otk_cpp_doc\objecttoolkit_Creo\api"), "doc"),
    ("afx_resource", Path(r"Common Files\afx\text\resource"), "ui"),
    ("afx_ribbon", Path(r"Common Files\afx\text\ribbon"), "ui"),
    ("parametric_text", Path(r"Parametric\text"), "ui"),
    # Creo's own UI/resource layer contains useful evidence for resource
    # attributes that are not always present in the Toolkit sample trees.
    # Example: (.TitleBar False) for titleless floating dialogs/toolbars.
    ("proe_uitools_resource", Path(r"Common Files\proe\uitools\text\resource"), "ui"),
    ("emx_resource", Path(r"Common Files\applications\emx\text\resource"), "ui"),
    ("emx_shc_resource", Path(r"Common Files\applications\emx\shc\text\resource"), "ui"),
    ("ifx_resource", Path(r"Common Files\ifx\text\resource"), "ui"),
    ("ptk_gsg", Path(r"Common Files\protoolkit\Creo_Toolkit_GSG.pdf"), "doc"),
    ("ptk_relnotes", Path(r"Common Files\protoolkit\Creo_Toolkit_RelNotes.pdf"), "doc"),
    ("ptk_tkuse", Path(r"Common Files\protoolkit\tkuse.pdf"), "doc"),
]

MANIFEST_EXTS = {
    ".h", ".hpp", ".c", ".cc", ".cpp", ".cxx", ".txt", ".res", ".rbn",
    ".mnu", ".dat", ".xml", ".html", ".htm", ".pdf", ".png", ".gif",
    ".jpg", ".jpeg", ".ico", ".lib", ".dll", ".exe", ".bat",
}
SOURCE_EXTS = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
UI_EXTS = {".res", ".rbn", ".mnu"}
DOC_EXTS = {".html", ".htm", ".pdf"}
BUILD_EXTS = {".lib", ".dll", ".exe", ".bat"}

DEFAULT_ALIAS_MAP = {
    "schema_version": 1,
    "generated_by": "scripts/build_creo_install_index.py",
    "aliases": {
        "\u53c2\u6570": ["parameter", "param", "ProParameter", "ProParam", "wfcParameter", "pfcParameter", "ptu_param"],
        "\u88c5\u914d": ["assembly", "asm", "ProAssembly", "ProAsm", "wfcAssembly", "otk_examples_asm", "ptu_asm"],
        "\u5de5\u7a0b\u56fe": ["drawing", "drw", "ProDrawing", "ProDrw", "wfcDrawing", "otk_examples_drw", "ptu_drawing"],
        "\u5efa\u89c6\u56fe": ["drawing view", "view", "ProDrawingView", "ProDtl", "ptu_dwg", "tkdrwview"],
        "\u89c6\u56fe": ["view", "drawing view", "ProView", "ProDrawingView", "drwview"],
        "\u5bf9\u8bdd\u6846": ["dialog", "ProUIDialog", ".res", "resource", "ui"],
        "\u65e0\u6807\u9898\u5f39\u7a97": ["dialog", ".res", ".TitleBar False", "TitleBar False", "no titlebar", "titleless dialog", "floating dialog", "MiniToolbarDlg", "fdb_floatbox", "popup_preview"],
        "\u65e0\u6807\u9898\u680f\u5f39\u7a97": ["dialog", ".res", ".TitleBar False", "TitleBar False", "no titlebar", "titleless dialog", "floating dialog", "MiniToolbarDlg", "fdb_floatbox", "popup_preview"],
        "\u65e0\u6807\u9898\u680f": [".TitleBar False", "TitleBar False", "no titlebar", "titleless", "IsTitleBarVisible", "SetTitleBarVisible"],
        "\u5c0f\u5f39\u7a97": ["floating dialog", "popup", ".TitleBar False", "MiniToolbarDlg", "popup_preview", "livepalette"],
        "\u6d6e\u52a8\u5f39\u7a97": ["floating dialog", "floatbox", ".TitleBar False", "fdb_floatbox", "MiniToolbarDlg", "AlwaysOnTop"],
        "\u5de5\u5177\u6761\u5f39\u7a97": ["mini toolbar", "MiniToolbarDlg", ".ToolStyle True", ".TitleBar False"],
        "\u83dc\u5355": ["menu", ".mnu", "ProMenu", "menubar"],
        "\u6d88\u606f": ["message", "msg", "usermsg", "message file", ".txt"],
        "\u56fe\u6807": ["icon", "image", ".png", "resource"],
        "ribbon": ["ribbon", ".rbn", "Ribbon", "toolkitribbonui"],
        "res": [".res", "resource", "dialog", "ui resource"],
        "protk.dat": ["protk.dat", "registry", "startup", "exec_file", "text_dir", "allow_stop"],
        "\u6ce8\u518c": ["registration", "ProCmdActionAdd", "ProCmdDesignate", "protk.dat", "command"],
        "\u547d\u4ee4": ["command", "ProCmd", "ProCmdActionAdd", "ProCmdDesignate", "ui command"],
        "\u9009\u62e9": ["selection", "ProSelect", "ProSelection"],
        "\u7279\u5f81": ["feature", "ProFeature", "feat", "otk_examples_feat"],
        "\u65cf\u8868": ["family table", "family", "ProFam", "Famtab", "ptu_famtab"],
        "\u5173\u7cfb": ["relation", "relations", "ProRelation", "rels", "ptu_rels"],
        "\u989c\u8272": ["color", "colour", "ProColor", "appearance"],
        "\u5750\u6807\u7cfb": ["coordinate system", "csys", "ProCsys"],
        "\u6a21\u578b": ["model", "ProMdl", "wfcModel", "otk_examples_model"],
        "\u5b9e\u4f53": ["solid", "ProSolid", "wfcSolid", "otk_examples_solid"],
        "\u66f2\u7ebf": ["curve", "ProCurve", "curv"],
        "\u66f2\u9762": ["surface", "ProSurface", "surf"],
        "\u5c3a\u5bf8": ["dimension", "dim", "ProDimension", "tkdimension"],
        "\u5355\u4f4d": ["unit", "ProUnit"],
        "\u6750\u6599": ["material", "ProMaterial"],
        "\u5c42": ["layer", "ProLayer", "tkdeflayer"],
        "\u5bfc\u51fa": ["export", "output", "write"],
        "\u5bfc\u5165": ["import", "input", "read"],
    },
}

TOKEN_RE = re.compile(r"[0-9A-Za-z_\.\-]+|[\u4e00-\u9fff]+")
TAG_RE = re.compile(r"<[^>]+>")
TITLE_RE = re.compile(r"<title[^>]*>(.*?)</title>", re.I | re.S)
H1_RE = re.compile(r"<h1[^>]*>(.*?)</h1>", re.I | re.S)
INCLUDE_RE = re.compile(r"#\s*include\s+[<\"]([^>\"]+)[>\"]")
SYMBOL_PATTERNS = [
    re.compile(r"\b(?:extern\s+)?(?:ProError|ProBoolean|int|void|double|float|long|short|char|wchar_t|[A-Za-z_]\w+(?:\s*\*)?)\s+(Pro[A-Za-z_]\w+)\s*\([^;{}]*\)\s*;", re.M),
    re.compile(r"\btypedef\s+(?:struct|enum)\s+(?:\w+\s*)?\{[^{}]*\}\s*(Pro[A-Za-z_]\w+)\s*;", re.M | re.S),
    re.compile(r"\btypedef\s+[^;{}()]+\s+(Pro[A-Za-z_]\w+)\s*;", re.M),
    re.compile(r"\b(?:class|struct|enum)\s+((?:x)?(?:wfc|pfc)[A-Za-z_]\w+)\b", re.M),
    re.compile(r"\b#define\s+(PRO_[A-Za-z0-9_]+)\b", re.M),
]
PROTO_RE = re.compile(
    r"\b(?:extern\s+)?(?P<return>[A-Za-z_]\w+(?:\s*\*+)?)\s+"
    r"(?P<symbol>(?:Pro|wfc|pfc)[A-Za-z_]\w+)\s*\((?P<params>.*?)\)\s*;",
    re.M | re.S,
)
CALLBACK_TYPEDEF_RE = re.compile(
    r"\btypedef\s+(?P<return>[A-Za-z_]\w+(?:\s*\*+)?)\s*"
    r"\(\s*\*\s*(?P<symbol>(?:Pro|wfc|pfc)[A-Za-z_]\w+)\s*\)\s*"
    r"\((?P<params>.*?)\)\s*;",
    re.M | re.S,
)
HTML_LINK_RE = re.compile(r"<a\s+[^>]*href=[\"'](?P<href>[^\"']+)[\"'][^>]*>(?P<label>.*?)</a>", re.I | re.S)
RES_DIALOG_RE = re.compile(r"\(\s*Dialog\s+([A-Za-z_][A-Za-z0-9_\-]*)", re.I)
RES_LAYOUT_RE = re.compile(r"\(\s*Layout\s+([A-Za-z_][A-Za-z0-9_\-]*)", re.I)
RES_COMPONENT_RE = re.compile(r"\(\s*(Tab|SubLayout|Separator|MenuBar|Table|Label|InputPanel|PushButton|CheckButton|RadioGroup|OptionMenu|List|DrawingArea|Slider|Tree|TextArea)\s+([A-Za-z_][A-Za-z0-9_\-]*)", re.I)
RES_LABEL_RE = re.compile(r"\(\s*(?:[A-Za-z0-9_.-]+)\.Label\s+\"([^\"]+)\"", re.I)
RES_PROPERTY_RE = re.compile(r"\(\s*([A-Za-z0-9_.-]+)\s+(\"[^\"]*\"|[^\s()]+)\s*\)", re.I)
STRING_LITERAL_RE = re.compile(r'"([^"\n\r]{2,160})"')


def now_utc() -> str:
    return datetime.now(timezone.utc).isoformat().replace("+00:00", "Z")


def rel(path: Path, root: Path) -> str:
    return str(path.resolve().relative_to(root.resolve())).replace("/", "\\")


def norm_rel(path: Path) -> str:
    return str(path).replace("/", "\\")


def read_text(path: Path, max_bytes: int = 1_500_000) -> str:
    data = path.read_bytes()[:max_bytes]
    for enc in ("utf-8", "utf-16", "latin-1"):
        try:
            return data.decode(enc, errors="replace")
        except Exception:
            pass
    return data.decode("utf-8", errors="replace")


def clean_html(value: str) -> str:
    return re.sub(r"\s+", " ", html.unescape(TAG_RE.sub(" ", value))).strip()


def page_title(path: Path) -> str:
    text = read_text(path, 350_000)
    for pattern in (TITLE_RE, H1_RE):
        match = pattern.search(text)
        if match:
            title = clean_html(match.group(1))
            if title:
                return title
    return path.stem


def strip_c_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//.*", " ", text)
    return text


def compact_ws(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def clean_param(value: str) -> str:
    return compact_ws(value.replace("\n", " ").replace("\t", " "))


def split_params(params: str) -> list[str]:
    params = clean_param(params)
    if not params or params == "void":
        return []
    result = []
    current = []
    depth = 0
    for char in params:
        if char == "(":
            depth += 1
        elif char == ")":
            depth = max(0, depth - 1)
        if char == "," and depth == 0:
            value = clean_param("".join(current))
            if value:
                result.append(value)
            current = []
            continue
        current.append(char)
    value = clean_param("".join(current))
    if value:
        result.append(value)
    return result


def terms(*values) -> list[str]:
    result = set()
    for value in values:
        if value is None:
            continue
        if isinstance(value, (list, tuple, set)):
            for item in value:
                result.update(terms(item))
            continue
        for token in TOKEN_RE.findall(str(value)):
            result.add(token)
            result.add(token.lower())
    return sorted(result, key=lambda x: (x.lower(), x))


def flatten_detail_terms(value) -> list[str]:
    if not value:
        return []
    if isinstance(value, dict):
        result: list[str] = []
        for item in value.values():
            result.extend(flatten_detail_terms(item))
        return result
    if isinstance(value, (list, tuple, set)):
        result: list[str] = []
        for item in value:
            result.extend(flatten_detail_terms(item))
        return result
    return [str(value)]


def kind_for(path: Path, bucket: str) -> str:
    ext = path.suffix.lower()
    name = path.name.lower()
    if name == "protk.dat" or ext == ".dat":
        return "packaging_file"
    if ext in {".h", ".hpp"} and bucket in {"ptk", "otk_cpp"}:
        return "header_file"
    if ext in {".html", ".htm"}:
        return "doc_html"
    if ext == ".pdf":
        return "doc_pdf"
    if ext in SOURCE_EXTS:
        return "sample_source"
    if ext == ".res":
        return "ui_res"
    if ext == ".rbn":
        return "ui_ribbon"
    if ext == ".mnu":
        return "ui_menu"
    if ext == ".txt":
        return "message_text"
    if ext in BUILD_EXTS or bucket == "build":
        return "build_file"
    return "asset_file"


def family_for(path: Path, bucket: str) -> str:
    low = str(path).lower()
    if bucket == "ptk" or "protoolkit" in low:
        return "protoolkit"
    if bucket == "otk_cpp" or "otk" in low:
        return "otk_cpp"
    if bucket == "ui":
        return "ui"
    if bucket == "sample":
        return "sample"
    if bucket == "build":
        return "build"
    return bucket


def record_type_for(path: Path, bucket: str) -> str | None:
    ext = path.suffix.lower()
    name = path.name.lower()
    if name == "protk.dat" or ext == ".dat":
        return "packaging"
    if ext in {".h", ".hpp"} and bucket in {"ptk", "otk_cpp"}:
        return "header"
    if ext in DOC_EXTS:
        return "doc_page"
    if ext in SOURCE_EXTS:
        return "sample_source"
    if ext == ".rbn":
        return "ribbon"
    if ext in {".res", ".mnu"}:
        return "ui_resource"
    if ext == ".txt":
        return "message_file"
    if ext in BUILD_EXTS or bucket == "build":
        return "build_path"
    return None


def iter_files(creo_root: Path):
    seen = set()
    for target, relative, bucket in SCAN_TARGETS:
        full = creo_root / relative
        if not full.exists():
            continue
        files = [full] if full.is_file() else (p for p in full.rglob("*") if p.is_file())
        for path in files:
            if path.suffix.lower() not in MANIFEST_EXTS and path.name.lower() != "protk.dat":
                continue
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            yield target, path, bucket


def app_root(relative_path: str) -> str | None:
    parts = Path(relative_path).parts
    lower = [p.lower() for p in parts]
    for marker in ("protk_appls", "otk_examples", "otk_async_examples"):
        if marker in lower:
            i = lower.index(marker)
            if len(parts) > i + 1:
                return norm_rel(Path(*parts[: i + 2]))
    return None


def related_map(manifest: list[dict]) -> dict[str, list[str]]:
    grouped: dict[str, list[str]] = {}
    for item in manifest:
        rp = item["relative_path"]
        root = app_root(rp)
        if root and Path(rp).suffix.lower() in SOURCE_EXTS | UI_EXTS | {".txt"}:
            grouped.setdefault(root, []).append(rp)
    for key in list(grouped):
        grouped[key] = sorted(set(grouped[key]))[:60]
    result = {}
    for item in manifest:
        rp = item["relative_path"]
        root = app_root(rp)
        result[rp] = [p for p in grouped.get(root, []) if p != rp][:25] if root else []
    return result


def header_symbols(path: Path) -> list[str]:
    text = read_text(path)
    found = set()
    for pattern in SYMBOL_PATTERNS:
        found.update(match.group(1) for match in pattern.finditer(text))
    for api in header_api_records(path):
        found.add(api["symbol"])
    return sorted(found)


def header_api_records(path: Path) -> list[dict]:
    text = strip_c_comments(read_text(path))
    records: dict[str, dict] = {}
    for pattern, kind in ((PROTO_RE, "function"), (CALLBACK_TYPEDEF_RE, "callback_typedef")):
        for match in pattern.finditer(text):
            symbol = match.group("symbol")
            params = split_params(match.group("params"))
            signature = f"{clean_param(match.group('return'))} {symbol}({', '.join(params)})"
            records[symbol] = {
                "symbol": symbol,
                "signature": signature,
                "return_type": clean_param(match.group("return")),
                "params": params,
                "declaration_kind": kind,
            }
    for symbol in header_symbols_without_api_records(text):
        records.setdefault(symbol, {"symbol": symbol})
    return sorted(records.values(), key=lambda row: row["symbol"].lower())


def header_symbols_without_api_records(text: str) -> list[str]:
    found = set()
    for pattern in SYMBOL_PATTERNS[1:]:
        found.update(match.group(1) for match in pattern.finditer(text))
    return sorted(found)


def extract_html_doc_details(path: Path) -> dict:
    raw = read_text(path, 1_200_000)
    cleaned = clean_html(raw)
    links = [
        {"href": match.group("href"), "label": clean_html(match.group("label"))}
        for match in HTML_LINK_RE.finditer(raw)
    ]
    include_refs = sorted({link["label"] for link in links if link["label"].endswith(".h")})
    sample_refs = [
        f"{link['label']} -> {link['href']}"
        for link in links
        if "../samples/" in link["href"].replace("\\", "/")
    ][:60]
    manual_refs = [
        f"{link['label']} -> {link['href']}"
        for link in links
        if "../manual" in link["href"].replace("\\", "/")
    ][:40]
    api_refs = sorted({label for link in links for label in [link["label"].rstrip("()")] if re.match(r"^(?:Pro|wfc|pfc)[A-Za-z_]\w+$", label)})[:120]
    return_values = sorted(set(re.findall(r"\bPRO_TK_[A-Z0-9_]+\b", cleaned)))[:60]
    description = ""
    m = re.search(r"\bDescription\b(.*?)(?:\bSynopsis\b|\bReturns\b|\bManual References\b|\bSample Code References\b)", cleaned, re.I | re.S)
    if m:
        description = compact_ws(m.group(1))[:700]
    replacement_otk = ""
    m = re.search(r"Replacement in Object TOOLKIT\s*:?\s*(.{0,300})", cleaned, re.I | re.S)
    if m:
        replacement_otk = compact_ws(m.group(1))[:300]
    return {
        "description": description,
        "include_refs": include_refs[:20],
        "return_values": return_values,
        "sample_refs": sample_refs,
        "manual_refs": manual_refs,
        "api_refs": api_refs,
        "replacement_otk": replacement_otk,
    }


def extract_res_details(path: Path) -> dict:
    text = read_text(path, 900_000)
    dialog_names = sorted(set(RES_DIALOG_RE.findall(text)))
    layout_names = sorted(set(RES_LAYOUT_RE.findall(text)))[:120]
    components = []
    widgets = set()
    for match in RES_COMPONENT_RE.finditer(text):
        widget_type, widget_name = match.group(1), match.group(2)
        widgets.add(widget_name)
        if len(components) < 160:
            components.append({"type": widget_type, "name": widget_name})
    labels = sorted(set(RES_LABEL_RE.findall(text)))[:80]
    quoted = sorted({s for s in STRING_LITERAL_RE.findall(text) if len(s) <= 80})[:120]
    dialog_properties: dict[str, list[str]] = {}
    component_properties: dict[str, list[str]] = {}
    property_pairs: list[str] = []
    for match in RES_PROPERTY_RE.finditer(text):
        key = match.group(1)
        raw_value = match.group(2).strip()
        value = raw_value[1:-1] if raw_value.startswith('"') and raw_value.endswith('"') else raw_value
        if not key or "." not in key:
            continue
        pair = f"{key} {value}"
        if len(property_pairs) < 240 and pair not in property_pairs:
            property_pairs.append(pair)
        if key.startswith("."):
            values = dialog_properties.setdefault(key, [])
            if value not in values and len(values) < 20:
                values.append(value)
        else:
            values = component_properties.setdefault(key, [])
            if value not in values and len(values) < 20:
                values.append(value)
    titlebar_values = dialog_properties.get(".TitleBar", [])
    return {
        "dialog_names": dialog_names,
        "layout_names": layout_names,
        "widget_names": sorted(widgets)[:160],
        "components": components,
        "labels": labels,
        "quoted_strings": quoted,
        "dialog_properties": {k: dialog_properties[k] for k in sorted(dialog_properties)[:120]},
        "component_properties": {k: component_properties[k] for k in sorted(component_properties)[:120]},
        "property_pairs": property_pairs,
        "titlebar": titlebar_values[0] if titlebar_values else "",
    }


def ascii_strings(path: Path, min_len: int = 4) -> list[str]:
    data = path.read_bytes()[:2_000_000]
    strings = re.findall(rb"[\x20-\x7e]{%d,}" % min_len, data)
    return [s.decode("ascii", errors="ignore") for s in strings]


def extract_rbn_details(path: Path) -> dict:
    strings = ascii_strings(path)
    tokens = sorted({token for s in strings for token in re.findall(r"[A-Za-z][A-Za-z0-9_.:-]{2,}", s)})
    command_ids = [t for t in tokens if re.search(r"(Cmd|Command|Pro|Creo|OTK|Tk|Action)", t, re.I)][:120]
    icons = [t for t in tokens if re.search(r"\.(?:png|gif|ico)$", t, re.I)][:80]
    return {
        "ascii_strings": strings[:120],
        "tokens": tokens[:200],
        "command_ids": command_ids,
        "icons": icons,
    }


def extract_message_details(path: Path) -> dict:
    keys = []
    lines = [line.strip() for line in read_text(path, 700_000).splitlines()]
    expect_key = True
    for line in lines:
        if not line or line.startswith(("#", "!", "//")):
            continue
        if expect_key:
            if re.match(r"^[A-Za-z_][A-Za-z0-9_.:-]{1,80}$", line):
                keys.append(line)
            expect_key = False
        else:
            expect_key = True
    explicit_keys = sorted(set(re.findall(r"\b[A-Za-z_][A-Za-z0-9_.:-]{2,}\b", "\n".join(lines))))[:120]
    return {"message_keys": sorted(set(keys))[:120], "tokens": explicit_keys}


def extract_source_details(path: Path, known_resource_names: set[str] | None = None) -> dict:
    text = read_text(path, 900_000)
    includes = sorted(set(INCLUDE_RE.findall(text)))
    api_refs = sorted(set(re.findall(r"\b(?:Pro|wfc|pfc)[A-Za-z_]\w+\b", text)))[:200]
    literals = sorted(set(STRING_LITERAL_RE.findall(text)))[:200]
    known = known_resource_names or set()
    resource_refs = []
    for literal in literals:
        low = literal.lower()
        if low in known or Path(low).stem in known or low.endswith((".res", ".rbn", ".mnu", ".txt")):
            resource_refs.append(literal)
    define_strings = []
    for match in re.finditer(r"#\s*define\s+([A-Za-z_][A-Za-z0-9_]*)\s+\"([^\"]+)\"", text):
        define_strings.append({"name": match.group(1), "value": match.group(2)})
    return {
        "includes": includes,
        "api_refs": api_refs,
        "string_literals": literals[:80],
        "resource_refs": sorted(set(resource_refs))[:80],
        "define_strings": define_strings[:120],
    }


def sample_terms(path: Path) -> list[str]:
    text = read_text(path, 500_000)
    includes = INCLUDE_RE.findall(text)
    mentions = sorted(set(re.findall(r"\b(?:Pro|wfc|pfc)[A-Za-z_]\w+\b", text)))[:80]
    return terms(path.name, includes, mentions)


def snapshot(creo_root: Path) -> dict:
    latest = datetime(2000, 1, 1, tzinfo=timezone.utc)
    tracked = []
    for name, relative, bucket in SCAN_TARGETS:
        full = creo_root / relative
        item = {"name": name, "relative_path": norm_rel(relative), "bucket": bucket, "exists": full.exists()}
        if full.exists():
            files = [full] if full.is_file() else [p for p in full.rglob("*") if p.is_file() and p.suffix.lower() in MANIFEST_EXTS]
            if files:
                current = max(datetime.fromtimestamp(p.stat().st_mtime, tz=timezone.utc) for p in files)
                item["latest_mtime_utc"] = current.isoformat().replace("+00:00", "Z")
                latest = max(latest, current)
        tracked.append(item)
    return {
        "creo_root": str(creo_root),
        "latest_input_write_time_utc": latest.isoformat().replace("+00:00", "Z"),
        "tracked_inputs": tracked,
    }


def write_jsonl(path: Path, rows: list[dict]) -> int:
    with path.open("w", encoding="utf-8", newline="\n") as stream:
        for row in rows:
            stream.write(json.dumps(row, ensure_ascii=False, sort_keys=True) + "\n")
    return len(rows)


def load_alias(index_dir: Path) -> dict:
    path = index_dir / "alias_map.json"
    default_aliases = DEFAULT_ALIAS_MAP.get("aliases", {})
    if path.exists():
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
            if isinstance(data, dict) and isinstance(data.get("aliases"), dict):
                changed = False
                aliases = data["aliases"]
                for alias_name, alias_terms in default_aliases.items():
                    if alias_name not in aliases:
                        aliases[alias_name] = alias_terms
                        changed = True
                        continue
                    existing = aliases.get(alias_name)
                    if not isinstance(existing, list):
                        aliases[alias_name] = alias_terms
                        changed = True
                        continue
                    for term in alias_terms:
                        if term not in existing:
                            existing.append(term)
                            changed = True
                if changed:
                    data.setdefault("schema_version", DEFAULT_ALIAS_MAP["schema_version"])
                    data.setdefault("generated_by", DEFAULT_ALIAS_MAP["generated_by"])
                    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
                return data
        except Exception:
            pass
    path.write_text(json.dumps(DEFAULT_ALIAS_MAP, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return DEFAULT_ALIAS_MAP


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", default=".")
    parser.add_argument("--creo-root", default=str(DEFAULT_CREO_ROOT))
    parser.add_argument("--index-dir", default=str(DEFAULT_INDEX_DIR))
    args = parser.parse_args()

    repo_root = Path(args.repo_root).resolve()
    creo_root = Path(args.creo_root).resolve()
    index_dir = Path(args.index_dir)
    if not index_dir.is_absolute():
        index_dir = repo_root / index_dir
    index_dir.mkdir(parents=True, exist_ok=True)
    if not creo_root.exists():
        raise SystemExit(f"Creo root not found: {creo_root}")

    alias = load_alias(index_dir)
    manifest = []
    for _target, path, bucket in iter_files(creo_root):
        st = path.stat()
        manifest.append({
            "kind": kind_for(path, bucket),
            "source_root": str(creo_root),
            "relative_path": rel(path, creo_root),
            "extension": path.suffix.lower(),
            "size": st.st_size,
            "mtime_utc": datetime.fromtimestamp(st.st_mtime, tz=timezone.utc).isoformat().replace("+00:00", "Z"),
            "bucket": bucket,
        })
    manifest.sort(key=lambda row: row["relative_path"].lower())
    related = related_map(manifest)
    known_resource_names = {
        value.lower()
        for item in manifest
        for value in (Path(item["relative_path"]).name, Path(item["relative_path"]).stem)
        if Path(item["relative_path"]).suffix.lower() in {".res", ".rbn", ".mnu", ".txt"}
    }
    detail_cache: dict[str, dict] = {}
    header_api_cache: dict[str, list[dict]] = {}

    for item in manifest:
        rp = item["relative_path"]
        path = creo_root / rp
        ext = path.suffix.lower()
        bucket = item["bucket"]
        if ext in {".h", ".hpp"} and bucket in {"ptk", "otk_cpp"}:
            header_api_cache[rp] = header_api_records(path)
        elif ext in {".html", ".htm"}:
            detail_cache[rp] = extract_html_doc_details(path)
        elif ext == ".res":
            detail_cache[rp] = extract_res_details(path)
        elif ext == ".rbn":
            detail_cache[rp] = extract_rbn_details(path)
        elif ext == ".txt":
            detail_cache[rp] = extract_message_details(path)
        elif ext in SOURCE_EXTS:
            detail_cache[rp] = extract_source_details(path, known_resource_names)

    symbol_to_headers: dict[str, list[str]] = {}
    for rp, api_rows in header_api_cache.items():
        for api in api_rows:
            symbol_to_headers.setdefault(api["symbol"], []).append(rp)

    resource_name_to_paths: dict[str, list[str]] = {}
    for item in manifest:
        rp = item["relative_path"]
        path = Path(rp)
        for key in {path.name.lower(), path.stem.lower()}:
            resource_name_to_paths.setdefault(key, []).append(rp)

    search = []
    seen = set()

    def add(record_type: str, title: str, symbol: str, rp: str, family: str, extra, derived_from: str, related_paths=None, details=None):
        key = (record_type, symbol, rp)
        if key in seen:
            return
        seen.add(key)
        row = {
            "record_type": record_type,
            "title": title,
            "symbol": symbol,
            "search_terms": terms(title, symbol, rp, extra, flatten_detail_terms(details)),
            "relative_path": rp,
            "api_family": family,
            "related_paths": related_paths or [],
            "derived_from": derived_from,
        }
        if details:
            row["details"] = details
        search.append(row)

    for item in manifest:
        rp = item["relative_path"]
        path = creo_root / rp
        bucket = item["bucket"]
        rt = record_type_for(path, bucket)
        family = family_for(path, bucket)
        if rt:
            title = page_title(path) if rt == "doc_page" and path.suffix.lower() in {".html", ".htm"} else path.name
            symbol = "protk.dat" if path.name.lower() == "protk.dat" else path.stem
            extra = [item["kind"], bucket]
            record_details = detail_cache.get(rp, {})
            if path.suffix.lower() in SOURCE_EXTS:
                extra += sample_terms(path)
                for ref in record_details.get("resource_refs", []):
                    for ref_path in resource_name_to_paths.get(ref.lower(), []):
                        if ref_path not in related.setdefault(rp, []):
                            related[rp].append(ref_path)
                for include in record_details.get("includes", []):
                    for ref_path in resource_name_to_paths.get(include.lower(), []):
                        if ref_path not in related.setdefault(rp, []):
                            related[rp].append(ref_path)
            add(rt, title, symbol, rp, family, extra, rp, related.get(rp, []), record_details)
        if rt == "header":
            for api in header_api_cache.get(rp, []):
                symbol = api["symbol"]
                add("api_symbol", symbol, symbol, rp, family, [path.name, "header"], rp, related.get(rp, []), api)
        elif rt == "doc_page" and path.suffix.lower() in {".html", ".htm"}:
            details = detail_cache.get(rp, {})
            add("doc_page", page_title(path), path.stem, rp, family, ["html", "documentation"], rp, related.get(rp, []), details)

    for name, relative, bucket in SCAN_TARGETS:
        if bucket == "build" and (creo_root / relative).exists():
            rp = norm_rel(relative)
            add("build_path", Path(rp).name, Path(rp).name, rp, "build", [name, "x86e_win64", "library", "obj", "binary"], rp, [])

    search.sort(key=lambda row: (row["record_type"], row["symbol"].lower(), row["relative_path"].lower()))
    manifest_count = write_jsonl(index_dir / "file_manifest.jsonl", manifest)
    search_count = write_jsonl(index_dir / "search_index.jsonl", search)
    metadata = {
        "schema_version": 1,
        "generated_at_utc": now_utc(),
        "generator": "scripts/build_creo_install_index.py",
        "repo_root": str(repo_root),
        "index_dir": str(index_dir),
        "source_snapshot": snapshot(creo_root),
        "counts": {
            "file_manifest": manifest_count,
            "search_index": search_count,
            "aliases": len(alias.get("aliases", {})),
        },
    }
    (index_dir / "metadata.json").write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"Creo install index updated: {index_dir}")
    print(f"Manifest records: {manifest_count}")
    print(f"Search records: {search_count}")
    print(f"Latest input UTC: {metadata['source_snapshot']['latest_input_write_time_utc']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
