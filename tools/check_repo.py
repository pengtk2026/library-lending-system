# -*- coding: utf-8 -*-
"""Repository integrity checks used by GitHub Actions.

Deliberately stdlib-only. Verifies that the documentation, sources, third-party
sources and ignore rules stay consistent after each push.

This project is a Windows GUI application (WinMain + FLTK + MinGW), so a Linux
runner cannot compile it. This workflow checks repository structure instead;
run build.bat / run_tests.bat locally for the real build and tests.
"""
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
errors = []


def check(condition, message):
    print(("  OK   " if condition else "  FAIL ") + message, flush=True)
    if not condition:
        errors.append(message)


def git(*args):
    return subprocess.run(
        ["git", "-C", ROOT] + list(args), capture_output=True, text=True
    )


def read(path):
    with open(os.path.join(ROOT, path), encoding="utf-8", errors="ignore") as f:
        return f.read()


print("== required files ==")
required = [
    "README.md", "LICENSE", "CONTRIBUTING.md", "CODE_OF_CONDUCT.md",
    "CHANGELOG.md", "CMakeLists.txt", "build.bat", ".gitignore",
    ".gitattributes", "tools/THIRD_PARTY_NOTICES.md", "tools/check_repo.py",
    "docs/概要设计说明书.docx", "docs/详细设计说明书.docx",
    "docs/项目源码导读.docx",
    "docs/screenshots/books-tab.png", "docs/screenshots/overview.png",
    "tools/fltk-1.4.5/CMakeLists.txt", "tools/fltk-1.4.5/COPYING",
    "tools/sqlite-amalgamation-3540100/sqlite3.c",
    "tools/sqlite-amalgamation-3540100/sqlite3.h",
    ".github/ISSUE_TEMPLATE.md", ".github/ISSUE_TEMPLATE/bug_report.md",
    ".github/ISSUE_TEMPLATE/feature_request.md",
    ".github/pull_request_template.md", ".github/workflows/ci.yml",
]
for path in required:
    check(os.path.isfile(os.path.join(ROOT, path)), "exists: " + path)

print("\n== CMake source references ==")
cmake = read("CMakeLists.txt")
refs = sorted(set(re.findall(r"\b(?:src|include)/([\w.-]+\.(?:cpp|h))\b", cmake)))
check(len(refs) >= 6, "found %d source references in CMakeLists.txt" % len(refs))
for name in refs:
    ok = os.path.isfile(os.path.join(ROOT, "src", name)) or os.path.isfile(
        os.path.join(ROOT, "include", name)
    )
    check(ok, "source exists: " + name)

print("\n== README local links ==")
readme = read("README.md")
links = sorted(set(re.findall(r"\]\(([^)]+)\)", readme)))
local = [
    link for link in links
    if not link.startswith(("http://", "https://", "#", "mailto:"))
]
check(len(local) > 0, "found %d local links in README" % len(local))
for link in local:
    target = link.split("#")[0]
    check(os.path.exists(os.path.join(ROOT, target)), "link target: " + link)

print("\n== .gitignore rules ==")
must_ignore = [
    "data/library.db", "build/cmake/LibraryApp.exe",
    "tools/fltk-install/lib/libfltk.a",
    "tools/msys2root/mingw64/bin/g++.exe", "output/gui_window.png",
]
for path in must_ignore:
    check(git("check-ignore", "-q", path).returncode == 0, "ignored: " + path)

must_track = [
    "tools/fltk-1.4.5/CMakeLists.txt",
    "tools/fltk-1.4.5/libdecor/build/fl_libdecor.c",
    "tools/fltk-1.4.5/jpeg/change.log",
    "tools/sqlite-amalgamation-3540100/sqlite3.c",
    "docs/screenshots/books-tab.png",
]
for path in must_track:
    check(git("check-ignore", "-q", path).returncode != 0, "not ignored: " + path)

print("\n== tracked file hygiene ==")
tracked = git("ls-files").stdout.splitlines()
bad_ext = (".db", ".exe", ".ini", ".log", ".o", ".obj", ".a", ".dll")
bad_prefix = ("data/", "build/", "tools/fltk-install/", "tools/msys2root/",
              "tools/cmake-", "tools/_downloads/")
allowed = {"tools/fltk-1.4.5/jpeg/change.log"}  # upstream FLTK changelog
bad = [
    f for f in tracked
    if f not in allowed
    and (f.lower().endswith(bad_ext) or f.startswith(bad_prefix))
]
check(not bad, "no binaries / private data tracked (bad: %s)" % bad[:5])
check(len(tracked) > 100, "%d files tracked" % len(tracked))

print()
if errors:
    print("FAILED: %d check(s)" % len(errors))
    for item in errors[:10]:
        print("  - " + item)
    sys.exit(1)
print("ALL CHECKS PASSED")
