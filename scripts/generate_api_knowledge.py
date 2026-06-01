#!/usr/bin/env python3
"""Generate API knowledge from Creo HTML documentation (v2 - table-aware).

Parses the actual table-based HTML structure in protkdoc/api/*.html.
"""
from __future__ import annotations

import argparse
import json
import re
from collections import defaultdict
from pathlib import Path
from typing import Any

DEFAULT_CREO_ROOT = Path(r"D:\Program Files\PTC\Creo 10.0.8.0")
DEFAULT_DOC_DIR = Path(r"Common Files\protoolkit\protkdoc\api")
DEFAULT_OUTDIR = Path("docs/api_extracts")

PRIORITY_APIS = {
    "ProUIDialogCreate", "ProUIDialogActivate", "ProUIDialogDestroy",
    "ProUIDialogExit", "ProUITableColumnnamesSet", "ProUITableRownamesSet",
    "ProUITableCellLabelSet", "ProUITableCellComponentCopy",
    "ProUIPushbuttonActivateActionSet", "ProUICheckbuttonGetState",
    "ProUIInputpanelValueGet", "ProUIOptionmenuSelectActionSet",
    "ProSimprepCreate", "ProSimprepDelete", "ProSimprepdataGet",
    "ProSimprepdataSet", "ProSimprepdataNameSet", "ProSimprepdataitemInit",
    "ProSimprepdataitemAdd", "ProSolidSimprepVisit",
    "ProAsmcompMdlGet", "ProAsmcomppathInit", "ProSolidDispCompVisit",
    "ProParameterValueGet", "ProParameterValueSet",
    "ProFamtableRead", "ProMdlCurrentGet", "ProMdlNameGet",
    "ProSelect", "ProNotificationSet", "ProToolkitApplTextPathGet",
    "ProStringarrayFree", "ProDrawingDimensionCreate",
    "ProAnnotationShow", "ProDrawingViewCreate", "ProFeatureTypeGet",
    "ProMousePickGet", "ProAnnotationDelete", "ProSimprepActivate",
    "ProSimprepdataitemInit", "ProSimprepActionInit", "ProSimprepdataAlloc",
    "ProSimprepdataFree", "ProSimprepdataNameGet",
}

TAG_RE = re.compile(r"<[^>]+>")
WS_RE = re.compile(r"\s+")
ENTITY_RE = re.compile(r"&[a-zA-Z]+;")


def clean(text: str) -> str:
    """Strip tags, entities, collapse whitespace."""
    text = ENTITY_RE.sub(" ", text)
    text = TAG_RE.sub(" ", text)
    return WS_RE.sub(" ", text).strip()


def extract_title(html: str) -> str | None:
    """Extract API name from TITLE tag."""
    m = re.search(r"<TITLE>\s*(.+?)\s*</TITLE>", html, re.I)
    if m:
        return clean(m.group(1))
    return None


def extract_description(html: str) -> str:
    """Extract description from the Description row."""
    # Match from "Description" header to next section
    m = re.search(
        r"Description\s*</FONT>\s*</B>\s*</TD>\s*</TR>\s*<TR>\s*<TD[^>]*>(.*?)</TD>\s*</TR>",
        html, re.I | re.S,
    )
    if m:
        return clean(m.group(1))[:800]
    return ""


def extract_synopsis(html: str) -> str:
    """Extract synopsis/signature."""
    m = re.search(
        r"Synopsis\s*</FONT>\s*</B>\s*</TD>\s*</TR>\s*(.*?)</TABLE>",
        html, re.I | re.S,
    )
    if m:
        return clean(m.group(1))[:600]
    return ""


def extract_returns(html: str) -> str:
    """Extract Returns section."""
    m = re.search(
        r"Returns\s*</FONT>\s*</B>\s*</TD>\s*</TR>\s*<TR>\s*<TD[^>]*>(.*?)</TD>\s*</TR>",
        html, re.I | re.S,
    )
    if m:
        return clean(m.group(1))[:400]
    return ""


def extract_see_also(html: str) -> str:
    """Extract See Also links."""
    m = re.search(
        r"See\s+Also\s*</FONT>\s*</B>\s*</TD>\s*</TR>\s*<TR>\s*<TD[^>]*>(.*?)</TD>\s*</TR>",
        html, re.I | re.S,
    )
    if m:
        links = re.findall(r'<A\s+HREF="[^"]+">([^<]+)</A>', m.group(1), re.I)
        return ", ".join(links[:12])
    return ""


def extract_manual_refs(html: str) -> str:
    """Extract manual references."""
    m = re.search(
        r"Manual\s+References\s*</FONT>\s*</B>\s*</TD>\s*</TR>\s*<TR>\s*<TD[^>]*>(.*?)</TD>\s*</TR>",
        html, re.I | re.S,
    )
    if m:
        return clean(m.group(1))[:300]
    return ""


def scan_docs(doc_dir: Path, max_files: int = 800) -> dict[str, dict]:
    results: dict[str, dict] = {}
    count = 0

    for html_file in sorted(doc_dir.glob("*.html")):
        if count >= max_files:
            break
        try:
            text = html_file.read_text(encoding="utf-8", errors="replace")
        except Exception:
            continue

        api_name = extract_title(text)
        if not api_name:
            continue

        # Skip non-API pages (category pages, index pages, etc.)
        if api_name in ("API Wizard", "creo_object_toolkit_welcome", "") or not api_name[0].isupper():
            continue

        desc = extract_description(text)
        syn = extract_synopsis(text)
        ret = extract_returns(text)
        see = extract_see_also(text)
        manual = extract_manual_refs(text)

        # Skip entries with no useful content
        if not desc and not syn:
            continue

        results[api_name] = {
            "file": html_file.name,
            "synopsis": syn,
            "description": desc,
            "returns": ret,
            "see_also": see,
            "manual_refs": manual,
        }
        count += 1

    return results


def generate_h_comment(api_name: str, info: dict) -> str:
    parts = [f"/**", f" * @api {api_name}"]
    syn = info.get("synopsis", "")
    if syn:
        parts.append(f" *")
        parts.append(f" * Synopsis: {syn[:250]}")
    desc = info.get("description", "")
    if desc:
        parts.append(f" *")
        parts.append(f" * {desc[:400]}")
    ret = info.get("returns", "")
    if ret:
        parts.append(f" * @returns {ret[:200]}")
    see = info.get("see_also", "")
    if see:
        parts.append(f" * @see {see[:200]}")
    parts.append(f" */")
    return "\n".join(parts)


def generate_claude_md(api_name: str, info: dict) -> str:
    parts = [f"### `{api_name}`\n"]
    syn = info.get("synopsis", "")
    if syn:
        parts.append(f"```c\n{syn}\n```\n")
    desc = info.get("description", "")
    if desc:
        parts.append(f"{desc}\n")
    ret = info.get("returns", "")
    if ret:
        parts.append(f"\n**Returns:** {ret}\n")
    see = info.get("see_also", "")
    if see:
        parts.append(f"\n**See also:** {see}\n")
    manual = info.get("manual_refs", "")
    if manual:
        parts.append(f"\n**Manual:** {manual}\n")
    parts.append("")
    return "\n".join(parts)


CATEGORY_MAP = {
    "ProUIDialog": "Dialog", "ProUITable": "Table",
    "ProUIPushbutton": "Pushbutton", "ProUICheckbutton": "Checkbutton",
    "ProUIOptionmenu": "OptionMenu", "ProUIInputpanel": "InputPanel",
    "ProUILabel": "Label", "ProUIMessage": "Message",
    "ProSimprep": "SimplifiedRep", "ProSimprepdata": "SimplifiedRep",
    "ProAsmcomp": "Assembly", "ProAsmcomppath": "Assembly",
    "ProSolid": "Solid", "ProFeature": "Feature",
    "ProParameter": "Parameter", "ProFam": "FamilyTable",
    "ProDrawing": "Drawing", "ProMdl": "Model",
    "ProSelect": "Selection", "ProNotification": "Notification",
    "ProToolkit": "Toolkit", "ProGraphics": "Graphics",
    "ProArray": "Utilities", "ProString": "Utilities",
    "ProWstring": "Utilities", "ProCmd": "Command",
    "ProAnnotation": "Annotation", "ProDimension": "Dimension",
    "ProMessage": "Message", "ProLayer": "Layer",
    "ProColor": "Appearance", "ProMouse": "Interaction",
    "ProSelection": "Selection",
}


def categorize(api_name: str) -> str:
    for prefix, cat in CATEGORY_MAP.items():
        if api_name.startswith(prefix):
            return cat
    return "Other"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--creo-root", default=str(DEFAULT_CREO_ROOT))
    parser.add_argument("--doc-dir", default=str(DEFAULT_DOC_DIR))
    parser.add_argument("--outdir", default=str(DEFAULT_OUTDIR))
    parser.add_argument("--priority-only", action="store_true")
    parser.add_argument("--max-files", type=int, default=800)
    args = parser.parse_args()

    creo_root = Path(args.creo_root).resolve()
    doc_dir = creo_root / args.doc_dir
    if not doc_dir.exists():
        print(f"Not found: {doc_dir}")
        return 1

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)

    print(f"Scanning {doc_dir}...")
    all_results = scan_docs(doc_dir, args.max_files)
    print(f"Extracted {len(all_results)} APIs with documentation")

    if args.priority_only:
        all_results = {k: v for k, v in all_results.items() if k in PRIORITY_APIS}
        print(f"Filtered to {len(all_results)} priority APIs")

    # 1. .h reference
    h_path = outdir / "tkuse_api_reference.h"
    with h_path.open("w", encoding="utf-8") as f:
        f.write("// Creo TOOLKIT API Reference — auto-extracted from protkdoc/api/*.html\n")
        f.write("// For Claude Code context injection\n\n")
        for api_name in sorted(all_results):
            f.write(generate_h_comment(api_name, all_results[api_name]))
            f.write("\n\n")
    print(f"  {h_path}")

    # 2. CLAUDE.md
    claude_path = outdir / "CLAUDE_KNOWLEDGE_apis.md"
    with claude_path.open("w", encoding="utf-8") as f:
        f.write(f"# Creo TOOLKIT API Reference\n\n")
        f.write(f"{len(all_results)} APIs from {doc_dir}\n\n---\n\n")
        for api_name in sorted(all_results):
            f.write(generate_claude_md(api_name, all_results[api_name]))
            f.write("---\n\n")
    print(f"  {claude_path}")

    # 3. Per-category
    by_cat: dict[str, list[str]] = defaultdict(list)
    for api_name in sorted(all_results):
        by_cat[categorize(api_name)].append(api_name)

    for cat, apis in sorted(by_cat.items()):
        cat_path = outdir / f"api_category_{cat}.md"
        with cat_path.open("w", encoding="utf-8") as f:
            f.write(f"# {cat} APIs\n\n")
            for api_name in apis:
                info = all_results[api_name]
                syn = info.get("synopsis", "")
                desc = info.get("description", "")
                f.write(f"## `{api_name}`\n\n")
                if syn:
                    f.write(f"```c\n{syn}\n```\n\n")
                if desc:
                    f.write(f"{desc[:300]}\n\n")
                ret = info.get("returns", "")
                if ret:
                    f.write(f"Returns: {ret[:150]}\n\n")
                f.write("---\n\n")
        print(f"  {cat_path} ({len(apis)} APIs)")

    # 4. JSON quickref
    quickref = {}
    for api_name, info in sorted(all_results.items()):
        quickref[api_name] = {
            "category": categorize(api_name),
            "synopsis": info.get("synopsis", "")[:200],
            "returns": info.get("returns", "")[:100],
        }
    qr_path = outdir / "api_quickref.json"
    with qr_path.open("w", encoding="utf-8") as f:
        json.dump({
            "source": str(doc_dir),
            "total": len(quickref),
            "categories": {c: len(a) for c, a in by_cat.items()},
            "apis": quickref,
        }, f, ensure_ascii=False, indent=2)
    print(f"  {qr_path}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
