#!/usr/bin/env python3
"""Render the canonical repository README.md as the distribution PDF."""

import html
import re
import sys
from pathlib import Path
from urllib.parse import urlparse

from reportlab.lib import colors
from reportlab.lib.enums import TA_CENTER, TA_RIGHT
from reportlab.lib.pagesizes import letter
from reportlab.lib.styles import ParagraphStyle
from reportlab.lib.units import inch
from reportlab.lib.utils import ImageReader
from reportlab.platypus import (
    HRFlowable,
    Image,
    Paragraph,
    SimpleDocTemplate,
    Spacer,
    Table,
    TableStyle,
)


SCRIPT_DIR = Path(__file__).resolve().parent
README = (SCRIPT_DIR / ".." / ".." / "README.md").resolve()
ICON = SCRIPT_DIR / "app-icon.png"
OUT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("ScanTailor Spectre Readme.pdf")

LEFT_MARGIN = 0.9 * inch
RIGHT_MARGIN = 0.9 * inch
CONTENT_WIDTH = letter[0] - LEFT_MARGIN - RIGHT_MARGIN

S = {}
S["body"] = ParagraphStyle(
    "body", fontName="Helvetica", fontSize=9.5, leading=13, spaceAfter=6
)
S["title"] = ParagraphStyle(
    "title",
    parent=S["body"],
    fontName="Helvetica-Bold",
    fontSize=16,
    leading=20,
    spaceAfter=4,
)
S["h1"] = ParagraphStyle(
    "h1",
    parent=S["body"],
    fontName="Helvetica-Bold",
    fontSize=12.5,
    leading=16,
    spaceBefore=14,
    spaceAfter=5,
)
S["h2"] = ParagraphStyle(
    "h2",
    parent=S["body"],
    fontName="Helvetica-Bold",
    fontSize=11,
    leading=14,
    spaceBefore=10,
    spaceAfter=4,
)
S["h3"] = ParagraphStyle(
    "h3",
    parent=S["body"],
    fontName="Helvetica-Bold",
    fontSize=9.5,
    leading=13,
    spaceBefore=8,
    spaceAfter=3,
)
S["b1"] = ParagraphStyle(
    "b1", parent=S["body"], leftIndent=14, bulletIndent=4, spaceAfter=3
)
S["b2"] = ParagraphStyle(
    "b2", parent=S["body"], leftIndent=28, bulletIndent=18, spaceAfter=2
)
S["cell"] = ParagraphStyle(
    "cell", parent=S["body"], fontSize=9, leading=11.5, spaceAfter=0
)
S["cellh"] = ParagraphStyle(
    "cellh", parent=S["cell"], fontName="Helvetica-Bold"
)
S["cell_right"] = ParagraphStyle("cell_right", parent=S["cell"], alignment=TA_RIGHT)
S["cell_center"] = ParagraphStyle("cell_center", parent=S["cell"], alignment=TA_CENTER)
S["cellh_right"] = ParagraphStyle(
    "cellh_right", parent=S["cellh"], alignment=TA_RIGHT
)
S["cellh_center"] = ParagraphStyle(
    "cellh_center", parent=S["cellh"], alignment=TA_CENTER
)

INLINE_RE = re.compile(
    r"\[([^\]]+)\]\(([^)]+)\)"
    r"|`([^`]+)`"
    r"|\*\*(.+?)\*\*"
    r"|(?<!\*)\*([^*]+)\*(?!\*)"
)
HTML_IMAGE_RE = re.compile(r"^\s*<img\b([^>]*)/?>\s*$", re.IGNORECASE)
MARKDOWN_IMAGE_RE = re.compile(r"^\s*!\[([^\]]*)\]\(([^)]+)\)\s*$")
BULLET_RE = re.compile(r"^(\s*)-\s+(.*)$")
TABLE_SEPARATOR_RE = re.compile(r"^:?-{3,}:?$")


def inline(text):
    """Convert the README's inline Markdown subset to ReportLab markup."""
    result = []
    position = 0
    for match in INLINE_RE.finditer(text):
        result.append(html.escape(text[position : match.start()], quote=False))
        if match.group(1) is not None:
            label, url = match.group(1), match.group(2)
            result.append(
                '<a href="{}" color="#1a0dab"><u>{}</u></a>'.format(
                    html.escape(url, quote=True), inline(label)
                )
            )
        elif match.group(3) is not None:
            result.append(
                '<font face="Courier" size="8.7">{}</font>'.format(
                    html.escape(match.group(3), quote=False)
                )
            )
        elif match.group(4) is not None:
            result.append("<b>{}</b>".format(inline(match.group(4))))
        else:
            result.append("<i>{}</i>".format(inline(match.group(5))))
        position = match.end()
    result.append(html.escape(text[position:], quote=False))
    return "".join(result)


def table_cells(line):
    return [cell.strip() for cell in line.strip().strip("|").split("|")]


def is_table_separator(cells):
    return bool(cells) and all(TABLE_SEPARATOR_RE.fullmatch(cell) for cell in cells)


def column_widths(rows):
    column_count = max(len(row) for row in rows)
    weights = []
    for column in range(column_count):
        longest = max(
            (len(re.sub(r"[*`]", "", row[column])) if column < len(row) else 0)
            for row in rows
        )
        weights.append(max(5, min(longest, 42)))
    total = sum(weights)
    return [CONTENT_WIDTH * weight / total for weight in weights]


def make_table(raw_rows):
    separator = next((row for row in raw_rows if is_table_separator(row)), [])
    alignments = []
    for marker in separator:
        if marker.startswith(":") and marker.endswith(":"):
            alignments.append("center")
        elif marker.endswith(":"):
            alignments.append("right")
        else:
            alignments.append("left")
    rows = [row for row in raw_rows if not is_table_separator(row)]
    width = max(len(row) for row in rows)
    rows = [row + [""] * (width - len(row)) for row in rows]
    rendered = []
    for row_number, row in enumerate(rows):
        rendered_row = []
        for column, cell in enumerate(row):
            style = "cellh" if row_number == 0 else "cell"
            alignment = alignments[column] if column < len(alignments) else "left"
            if alignment != "left":
                style += "_" + alignment
            rendered_row.append(Paragraph(inline(cell), S[style]))
        rendered.append(rendered_row)
    table = Table(
        rendered,
        colWidths=column_widths(rows),
        hAlign="LEFT",
        repeatRows=1,
    )
    table.setStyle(
        TableStyle(
            [
                ("LINEABOVE", (0, 0), (-1, 0), 0.75, colors.black),
                ("LINEBELOW", (0, 0), (-1, 0), 0.4, colors.black),
                ("LINEBELOW", (0, -1), (-1, -1), 0.75, colors.black),
                ("VALIGN", (0, 0), (-1, -1), "TOP"),
                ("TOPPADDING", (0, 0), (-1, -1), 2.5),
                ("BOTTOMPADDING", (0, 0), (-1, -1), 2.5),
                ("LEFTPADDING", (0, 0), (-1, -1), 5),
                ("RIGHTPADDING", (0, 0), (-1, -1), 8),
            ]
        )
    )
    return table


def image_source(line):
    match = MARKDOWN_IMAGE_RE.match(line)
    if match:
        return match.group(2), match.group(1)
    match = HTML_IMAGE_RE.match(line)
    if not match:
        return None
    attributes = match.group(1)
    src = re.search(r"\bsrc=[\"']([^\"']+)[\"']", attributes, re.IGNORECASE)
    alt = re.search(r"\balt=[\"']([^\"']*)[\"']", attributes, re.IGNORECASE)
    return (src.group(1), alt.group(1) if alt else "") if src else None


def make_image(src, alt, icon_already_rendered):
    parsed = urlparse(src)
    remote = parsed.scheme in ("http", "https")
    is_app_icon = "scantailor" in alt.lower() or "spectre" in alt.lower()

    if remote:
        if icon_already_rendered or not is_app_icon:
            return None, icon_already_rendered
        path = ICON
    else:
        path = (README.parent / src).resolve()
        if not path.is_file():
            return None, icon_already_rendered

    if path == ICON or is_app_icon:
        return Image(str(ICON), width=0.85 * inch, height=0.85 * inch, hAlign="LEFT"), True

    image_width, image_height = ImageReader(str(path)).getSize()
    scale = min(CONTENT_WIDTH / image_width, (3 * inch) / image_height, 1)
    return Image(
        str(path), width=image_width * scale, height=image_height * scale, hAlign="LEFT"
    ), icon_already_rendered


def is_structural(line):
    stripped = line.strip()
    return bool(
        not stripped
        or re.match(r"^#{1,4}\s+", line)
        or stripped == "---"
        or line.lstrip().startswith("|")
        or BULLET_RE.match(line)
        or image_source(line)
    )


def parse_readme(markdown):
    lines = markdown.splitlines()
    story = []
    icon_rendered = False
    index = 0

    while index < len(lines):
        line = lines[index]
        stripped = line.strip()
        if not stripped:
            index += 1
            continue

        heading = re.match(r"^(#{1,4})\s+(.+?)\s*$", line)
        if heading:
            level = len(heading.group(1))
            style = {1: "title", 2: "h1", 3: "h2", 4: "h3"}[level]
            story.append(Paragraph(inline(heading.group(2)), S[style]))
            index += 1
            continue

        source = image_source(line)
        if source:
            image, icon_rendered = make_image(source[0], source[1], icon_rendered)
            if image is not None:
                story.extend((Spacer(1, 2), image, Spacer(1, 8)))
            index += 1
            continue

        if stripped == "---":
            story.extend(
                (
                    Spacer(1, 6),
                    HRFlowable(width="100%", thickness=0.5, color=colors.grey),
                    Spacer(1, 6),
                )
            )
            index += 1
            continue

        if line.lstrip().startswith("|"):
            raw_rows = []
            while index < len(lines) and lines[index].lstrip().startswith("|"):
                raw_rows.append(table_cells(lines[index]))
                index += 1
            story.extend((make_table(raw_rows), Spacer(1, 5)))
            continue

        bullet = BULLET_RE.match(line)
        if bullet:
            indent, item = bullet.group(1), bullet.group(2).strip()
            index += 1
            continuation = []
            while index < len(lines):
                candidate = lines[index]
                if is_structural(candidate) or not candidate[:1].isspace():
                    break
                continuation.append(candidate.strip())
                index += 1
            if continuation:
                item = " ".join((item, *continuation))
            nested = len(indent.expandtabs(2)) >= 2
            story.append(
                Paragraph(
                    inline(item),
                    S["b2" if nested else "b1"],
                    bulletText="–" if nested else "•",
                )
            )
            continue

        paragraph_lines = [stripped]
        index += 1
        while index < len(lines) and not is_structural(lines[index]):
            paragraph_lines.append(lines[index].strip())
            index += 1
        story.append(Paragraph(inline(" ".join(paragraph_lines)), S["body"]))

    return story


document = SimpleDocTemplate(
    str(OUT),
    pagesize=letter,
    leftMargin=LEFT_MARGIN,
    rightMargin=RIGHT_MARGIN,
    topMargin=0.8 * inch,
    bottomMargin=0.8 * inch,
    title="ScanTailor Spectre Readme",
    author="ScanTailor Spectre",
)
document.build(parse_readme(README.read_text(encoding="utf-8")))
print("wrote", OUT)
