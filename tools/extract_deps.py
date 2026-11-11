# -*- coding: utf-8 -*-
"""Extract the downloaded CMake / FLTK / SQLite archives into tools/."""
import os
import tarfile
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DL = os.path.join(ROOT, "tools", "_downloads")
TOOLS = os.path.join(ROOT, "tools")


def extract_zip(name, dest):
    path = os.path.join(DL, name)
    if not os.path.exists(path):
        print("[SKIP] missing", name)
        return
    with zipfile.ZipFile(path) as zf:
        zf.extractall(dest)
    print("[OK  ]", name, "->", dest)


def extract_tgz(name, dest):
    path = os.path.join(DL, name)
    if not os.path.exists(path):
        print("[SKIP] missing", name)
        return
    with tarfile.open(path, "r:gz") as tf:
        tf.extractall(dest, filter="data")
    print("[OK  ]", name, "->", dest)


def main():
    extract_zip("cmake.zip", TOOLS)
    extract_tgz("fltk-1.4.5-source.tar.gz", TOOLS)
    extract_zip("sqlite-amalgamation.zip", TOOLS)
    for check in [
        "cmake-3.31.6-windows-x86_64/bin/cmake.exe",
        "fltk-1.4.5/CMakeLists.txt",
        "sqlite-amalgamation-3540100/sqlite3.c",
        "msys2root/mingw64/bin/g++.exe",
    ]:
        path = os.path.join(TOOLS, *check.split("/"))
        print("[CHK ] %-56s %s" % (check, "OK" if os.path.exists(path) else "MISSING"))


if __name__ == "__main__":
    main()
