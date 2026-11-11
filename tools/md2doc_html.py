# -*- coding: utf-8 -*-
"""Render a technical-design Markdown draft into a page-furniture HTML document.

Used as the Stage-2 typesetting step of the Word pipeline. The HTML follows
the conversion engine contract: SimSun 12pt body text, h1-h3 headings,
@page margin boxes for header/footer, cover + TOC sections, page restart.

Usage:
  python md2doc_html.py --md <draft.md> --html <out.html> \
      --title "..." --subtitle "..." --docno "..." --version "..." --date "..."
"""
import argparse
import html
import os
import re

CSS = """
@page {
  @top-center { content: "{shorttitle}"; }
  @bottom-center { content: "第 " counter(page) " 页"; }
}
@page cover {
  @top-center { content: none; }
  @bottom-center { content: none; }
}
section[role="cover"] { page: cover; }
body { font-family: "SimSun", serif; color: #1f2937; }
p, li, td, th { font-family: "SimSun", serif; font-size: 12pt; line-height: 1.55; }
p { text-indent: 2em; margin: 4pt 0; }
h1 { font-family: "SimHei", sans-serif; font-size: 17pt; color: #123a6b;
     border-bottom: 1.5pt solid #2f6fb3; padding-bottom: 4pt; margin-top: 20pt; }
h2 { font-family: "SimHei", sans-serif; font-size: 14pt; color: #1b4f8c; margin-top: 14pt; }
h3 { font-family: "SimHei", sans-serif; font-size: 12.5pt; color: #23415f; margin-top: 10pt; }
table { border-collapse: collapse; width: 100%; margin: 8pt 0; }
th, td { border: 0.75pt solid #9fb6cf; padding: 4pt 6pt; text-indent: 0;
         font-size: 10.5pt; vertical-align: top; }
th { background-color: #dce9f7; font-family: "SimHei", sans-serif; font-weight: bold;
     text-align: center; }
td p { text-indent: 0; margin: 0; }
ul, ol { margin: 4pt 0 8pt 0; padding-left: 22pt; }
li { text-indent: 0; margin: 2pt 0; }
pre { font-family: "Consolas", monospace; font-size: 9.5pt; line-height: 1.3;
      background-color: #f4f7fb; border: 0.75pt solid #cdd9e7;
      padding: 6pt; white-space: pre-wrap; text-indent: 0; }
code { font-family: "Consolas", monospace; font-size: 10pt; color: #a3302c; }
strong { font-weight: bold; }
.cover-title { font-family: "SimHei", sans-serif; font-size: 30pt; font-weight: bold;
               text-align: center; color: #123a6b; text-indent: 0; margin-top: 150pt; }
.cover-sub { font-family: "SimHei", sans-serif; font-size: 16pt; text-align: center;
             color: #3c5a7a; text-indent: 0; margin-top: 18pt; }
.cover-meta { font-family: "SimSun", serif; font-size: 12pt; text-align: center;
              text-indent: 0; margin-top: 10pt; }
.cover-rule { border-bottom: 2pt solid #2f6fb3; margin: 24pt 90pt; }
.toc-title { font-family: "SimHei", sans-serif; font-size: 20pt; font-weight: bold;
             text-align: center; text-indent: 0; }
.toc-list, .toc-list ol, .toc-list ul { list-style: none; }
.toc-list a { color: #1f2937; }
.src-note { background-color: #f2f6fb; font-size: 10pt; text-indent: 0; }
"""


def inline(text):
    text = html.escape(text, quote=False)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    return text


def is_table_sep(line):
    return bool(re.match(r"^\s*\|?\s*:?-{2,}", line)) and "-" in line


def render(md_text, cfg):
    lines = md_text.splitlines()
    out = []
    toc = []
    i = 0
    n_h1 = 0
    n_h2 = 0
    pending_para = []

    def flush_para():
        if pending_para:
            out.append("<p>%s</p>" % inline(" ".join(pending_para)))
            pending_para.clear()

    while i < len(lines):
        line = lines[i]
        stripped = line.strip()
        if not stripped:
            flush_para()
            i += 1
            continue

        if stripped.startswith("```"):
            flush_para()
            lang = stripped[3:].strip()
            i += 1
            buf = []
            while i < len(lines) and not lines[i].strip().startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1
            out.append("<pre>%s</pre>" % html.escape("\n".join(buf)))
            if lang:
                out.append('<p class="src-note">代码清单 · %s</p>' % inline(lang))
            continue

        m = re.match(r"^(#{1,3})\s+(.*)$", stripped)
        if m:
            flush_para()
            level = len(m.group(1))
            title = m.group(2).strip()
            if level == 1:
                n_h1 += 1
                anchor = "section-%d" % n_h1
                toc.append((1, title, anchor))
                out.append('<h1 id="%s">%s</h1>' % (anchor, inline(title)))
            elif level == 2:
                n_h2 += 1
                anchor = "subsection-%d" % n_h2
                toc.append((2, title, anchor))
                out.append('<h2 id="%s">%s</h2>' % (anchor, inline(title)))
            else:
                out.append("<h3>%s</h3>" % inline(title))
            i += 1
            continue

        if stripped.startswith("|") and i + 1 < len(lines) and is_table_sep(lines[i + 1]):
            flush_para()
            header = [c.strip() for c in stripped.strip("|").split("|")]
            i += 2
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                rows.append([c.strip() for c in lines[i].strip().strip("|").split("|")])
                i += 1
            out.append("<table>")
            out.append("<thead><tr>%s</tr></thead>" %
                       "".join("<th>%s</th>" % inline(c) for c in header))
            out.append("<tbody>")
            for row in rows:
                cells = row + [""] * (len(header) - len(row))
                out.append("<tr>%s</tr>" %
                           "".join("<td>%s</td>" % inline(c) for c in cells[:len(header)]))
            out.append("</tbody></table>")
            continue

        lm = re.match(r"^(\s*)([-*]|\d+[.)])\s+(.*)$", line)
        if lm:
            flush_para()
            ordered = not lm.group(2).startswith(("-", "*"))
            tag = "ol" if ordered else "ul"
            items = []
            while i < len(lines):
                mm = re.match(r"^(\s*)([-*]|\d+[.)])\s+(.*)$", lines[i])
                if not mm:
                    break
                items.append(mm.group(3).strip())
                i += 1
            out.append("<%s>%s</%s>" %
                       (tag, "".join("<li>%s</li>" % inline(x) for x in items), tag))
            continue

        if stripped.startswith(">"):
            flush_para()
            out.append('<p class="src-note">%s</p>' % inline(stripped.lstrip("> ")))
            i += 1
            continue

        pending_para.append(stripped)
        i += 1
    flush_para()

    toc_html = ['<nav class="doc-toc" aria-label="文档目录">',
                '<p class="toc-title">目 录</p>', '<ol class="toc-list">']
    idx = 0
    while idx < len(toc):
        level, title, anchor = toc[idx]
        toc_html.append('<li><a href="#%s">%s</a>' % (anchor, inline(title)))
        if idx + 1 < len(toc) and toc[idx + 1][0] == 2:
            toc_html.append("<ol>")
            while idx + 1 < len(toc) and toc[idx + 1][0] == 2:
                idx += 1
                _, sub, subanchor = toc[idx]
                toc_html.append('<li><a href="#%s">%s</a></li>' %
                                (subanchor, inline(sub)))
            toc_html.append("</ol>")
        toc_html.append("</li>")
        idx += 1
    toc_html.append("</ol></nav>")

    css = CSS.replace("{shorttitle}", cfg["shorttitle"])
    doc = [
        "<!DOCTYPE html>", '<html lang="zh-CN"><head><meta charset="utf-8">',
        "<style>%s</style></head><body>" % css,
        '<section role="cover">',
        '<p class="cover-title">%s</p>' % inline(cfg["title"]),
        '<p class="cover-sub">%s</p>' % inline(cfg["subtitle"]),
        '<div class="cover-rule"></div>',
        '<p class="cover-meta">文档编号：%s　　版本：%s</p>' %
        (inline(cfg["docno"]), inline(cfg["version"])),
        '<p class="cover-meta">编制日期：%s　　编制单位：课程设计组</p>' % inline(cfg["date"]),
        '<p class="cover-meta">技术栈：C++17 · FLTK 1.4.5 · SQLite 3 · CMake</p>',
        "</section>",
        '<section role="cover">', "".join(toc_html), "</section>",
        '<section role="body" data-page-restart="1">',
        "".join(out), "</section></body></html>",
    ]
    return "\n".join(doc)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--md", required=True)
    ap.add_argument("--html", required=True)
    ap.add_argument("--title", required=True)
    ap.add_argument("--subtitle", required=True)
    ap.add_argument("--docno", required=True)
    ap.add_argument("--version", default="V1.0")
    ap.add_argument("--date", default="2026-11-11")
    ap.add_argument("--shorttitle", required=True)
    a = ap.parse_args()
    with open(a.md, "r", encoding="utf-8") as fh:
        md = fh.read()
    cfg = dict(title=a.title, subtitle=a.subtitle, docno=a.docno,
               version=a.version, date=a.date, shorttitle=a.shorttitle)
    result = render(md, cfg)
    os.makedirs(os.path.dirname(os.path.abspath(a.html)), exist_ok=True)
    with open(a.html, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(result)
    print("HTML_OK %s (%d bytes)" % (a.html, len(result)))


if __name__ == "__main__":
    main()
