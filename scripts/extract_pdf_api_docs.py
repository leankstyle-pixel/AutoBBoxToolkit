#!/usr/bin/env python3
"""Extract key Creo TOOLKIT API documentation from PTC PDFs.

Reads tkuse.pdf (TOOLKIT User's Guide) and extracts:
- API function signatures and descriptions
- Code examples
- Key concepts

Outputs formatted .h-style comment blocks and CLAUDE.md knowledge entries.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any

import pdfplumber

DEFAULT_PDF = Path(r"D:\Program Files\PTC\Creo 10.0.8.0\Common Files\protoolkit\tkuse.pdf")
DEFAULT_OUTDIR = Path("docs/api_extracts")

# Section patterns to identify API documentation content
SECTION_HEADERS = [
    r"Chapter\s+\d+",
    r"^\d+\.\d+",
    r"Pro[A-Z]\w+\s*\(\)",
    r"Synopsis",
    r"Description",
    r"Returns",
    r"See Also",
    r"Example",
    r"Sample Code",
]

API_FUNC_RE = re.compile(
    r"\b(Pro[A-Za-z_]\w+)\s*\([^)]*\)",
    re.MULTILINE,
)

PRO_TK_RE = re.compile(r"\bPRO_TK_[A-Z0-9_]+\b")

HEADER_LINE_RE = re.compile(r"^(Chapter\s+\d+|\d+\.\d+(?:\.\d+)?)\s+(.+)")


def clean_text(text: str) -> str:
    """Clean extracted PDF text."""
    text = re.sub(r"\s+", " ", text)
    text = text.strip()
    return text


def extract_api_sections(pdf_path: Path) -> list[dict[str, Any]]:
    """Extract API-related sections from the PDF."""
    sections: list[dict[str, Any]] = []
    current_section: dict[str, Any] | None = None
    current_text: list[str] = []

    with pdfplumber.open(str(pdf_path)) as pdf:
        for page_idx, page in enumerate(pdf.pages):
            text = page.extract_text()
            if not text:
                continue

            lines = text.split("\n")
            for line in lines:
                line = line.strip()
                if not line:
                    continue

                # Check for section headers
                header_match = HEADER_LINE_RE.match(line)
                if header_match:
                    if current_section:
                        current_section["content"] = clean_text(" ".join(current_text))
                        sections.append(current_section)
                    current_section = {
                        "page": page_idx + 1,
                        "section_num": header_match.group(1),
                        "title": clean_text(header_match.group(2)),
                        "apis": [],
                        "return_codes": [],
                    }
                    current_text = [line]
                    continue

                if current_section is not None:
                    current_text.append(line)
                    # Extract API names
                    for api_match in API_FUNC_RE.finditer(line):
                        api_name = api_match.group(1)
                        if api_name not in current_section["apis"]:
                            current_section["apis"].append(api_name)
                    # Extract return codes
                    for tk_match in PRO_TK_RE.finditer(line):
                        code = tk_match.group(0)
                        if code not in current_section["return_codes"]:
                            current_section["return_codes"].append(code)

    # Final section
    if current_section:
        current_section["content"] = clean_text(" ".join(current_text))
        sections.append(current_section)

    return sections


def filter_api_sections(sections: list[dict], min_api_count: int = 1) -> list[dict]:
    """Keep only sections that contain API references."""
    return [s for s in sections if len(s["apis"]) >= min_api_count]


def generate_h_comment(section: dict, max_content_len: int = 500) -> str:
    """Generate a .h-style comment block from a section."""
    content = section["content"]
    if len(content) > max_content_len:
        content = content[:max_content_len] + "..."

    apis = ", ".join(section["apis"][:10])
    codes = ", ".join(section["return_codes"][:8])

    lines = [
        "/**",
        f" * @section {section.get('section_num', '')} {section.get('title', '')}",
        f" * @page {section.get('page', '')}",
    ]
    if apis:
        lines.append(f" * @apis {apis}")
    if codes:
        lines.append(f" * @returns {codes}")
    lines.append(f" *")
    # Word-wrap content
    words = content.split()
    line = " * "
    for word in words:
        if len(line) + len(word) + 1 > 90:
            lines.append(line.rstrip())
            line = " * " + word + " "
        else:
            line += word + " "
    lines.append(line.rstrip())
    lines.append(" */")
    return "\n".join(lines)


def generate_claude_md_entry(section: dict) -> str:
    """Generate a CLAUDE.md knowledge entry."""
    apis = section["apis"][:8]
    title = section.get("title", "Untitled")
    section_num = section.get("section_num", "")

    entry = f"### {section_num} {title}\n\n"
    if apis:
        entry += "**Key APIs:** " + ", ".join(f"`{a}`" for a in apis) + "\n\n"
    if section.get("return_codes"):
        entry += "**Return codes:** " + ", ".join(f"`{c}`" for c in section["return_codes"][:6]) + "\n\n"

    content = section["content"]
    if len(content) > 800:
        content = content[:800] + "..."
    entry += content + "\n"
    return entry


def build_api_index(sections: list[dict]) -> dict[str, list[str]]:
    """Build an inverted index: API name → section titles."""
    index: dict[str, list[str]] = {}
    for s in sections:
        title = f"{s.get('section_num', '')} {s.get('title', '')}"
        for api in s["apis"]:
            index.setdefault(api, []).append(title)
    return index


def main() -> int:
    pdf_path = DEFAULT_PDF
    if not pdf_path.exists():
        print(f"PDF not found: {pdf_path}")
        return 1

    print(f"Reading: {pdf_path.name}")
    all_sections = extract_api_sections(pdf_path)
    print(f"Total sections: {len(all_sections)}")

    api_sections = filter_api_sections(all_sections, min_api_count=1)
    print(f"Sections with APIs: {len(api_sections)}")

    outdir = DEFAULT_OUTDIR
    outdir.mkdir(parents=True, exist_ok=True)

    # 1. Generate .h-style consolidated comments
    h_path = outdir / "tkuse_api_reference.h"
    with h_path.open("w", encoding="utf-8") as f:
        f.write("// Auto-generated from PTC Creo TOOLKIT User's Guide (tkuse.pdf)\n")
        f.write("// Key API reference — for use with Claude Code context\n")
        f.write("// Generated by: scripts/extract_pdf_api_docs.py\n\n")
        for s in api_sections[:120]:  # Top 120 most API-rich sections
            f.write(generate_h_comment(s))
            f.write("\n\n")

    print(f"Written: {h_path} ({len(api_sections[:120])} entries)")

    # 2. Generate CLAUDE.md knowledge entries
    claude_path = outdir / "CLAUDE_KNOWLEDGE_tkuse.md"
    with claude_path.open("w", encoding="utf-8") as f:
        f.write("# Creo TOOLKIT API Knowledge (from tkuse.pdf)\n\n")
        f.write("Auto-extracted key API sections from the official PTC documentation.\n\n")
        f.write("---\n\n")
        for s in api_sections[:100]:
            f.write(generate_claude_md_entry(s))
            f.write("\n---\n\n")

    print(f"Written: {claude_path} ({len(api_sections[:100])} entries)")

    # 3. Build API index JSON
    api_index = build_api_index(api_sections)
    idx_path = outdir / "tkuse_api_index.json"
    with idx_path.open("w", encoding="utf-8") as f:
        json.dump(
            {
                "source": str(pdf_path),
                "total_sections": len(all_sections),
                "api_sections": len(api_sections),
                "unique_apis": len(api_index),
                "index": api_index,
            },
            f,
            ensure_ascii=False,
            indent=2,
        )
    print(f"Written: {idx_path} ({len(api_index)} unique APIs indexed)")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
