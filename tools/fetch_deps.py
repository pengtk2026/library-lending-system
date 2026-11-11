# -*- coding: utf-8 -*-
"""Download portable build dependencies into tools/_downloads.

This script is deliberately dependency-free (stdlib only) and supports
resume + retries so it can run unattended on a slow link.
"""
import os
import sys
import time
import zipfile
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "tools", "_downloads")
os.makedirs(DEST, exist_ok=True)

FILES = [
    (
        "mingw64-gcc.zip",
        "https://github.com/brechtsanders/winlibs_mingw/releases/download/"
        "16.2.0posix-23.1.3-14.0.0-ucrt-r3/"
        "winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.zip",
    ),
    (
        "cmake.zip",
        "https://cmake.org/files/v3.31/cmake-3.31.6-windows-x86_64.zip",
    ),
    (
        "fltk-1.4.5-source.tar.gz",
        "https://github.com/fltk/fltk/releases/download/release-1.4.5/fltk-1.4.5-source.tar.gz",
    ),
    (
        "sqlite-amalgamation.zip",
        "https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip",
    ),
]


def download(name, url):
    path = os.path.join(DEST, name)
    part = path + ".part"
    for attempt in range(1, 5):
        pos = os.path.getsize(part) if os.path.exists(part) else 0
        headers = {"User-Agent": "Mozilla/5.0"}
        if pos:
            headers["Range"] = "bytes=%d-" % pos
        req = urllib.request.Request(url, headers=headers)
        try:
            resp = urllib.request.urlopen(req, timeout=60)
            mode = "ab" if resp.status == 206 and pos else "wb"
            if mode == "wb":
                pos = 0
            total = int(resp.headers.get("Content-Length") or 0) + pos
            start = time.time()
            last = start
            last_bytes = pos
            with open(part, mode) as fh:
                while True:
                    chunk = resp.read(1024 * 512)
                    if not chunk:
                        break
                    fh.write(chunk)
                    pos += len(chunk)
                    now = time.time()
                    if now - last >= 5:
                        speed = (pos - last_bytes) / (now - last) / 1048576
                        print("[%s] %.1f/%.1f MB  %.2f MB/s" %
                              (name, pos / 1048576, total / 1048576, speed), flush=True)
                        last, last_bytes = now, pos
            if total and pos < total:
                raise IOError("incomplete: %d < %d" % (pos, total))
            os.replace(part, path)
            if name.endswith(".zip"):
                with zipfile.ZipFile(path) as zf:
                    bad = zf.testzip()
                if bad:
                    raise IOError("corrupt zip member: %s" % bad)
            print("[OK] %s  %.1f MB" % (name, os.path.getsize(path) / 1048576), flush=True)
            return True
        except Exception as exc:  # noqa: BLE001 - retry on any network error
            print("[RETRY %d] %s: %s: %s" %
                  (attempt, name, type(exc).__name__, str(exc)[:160]), flush=True)
            time.sleep(3)
    return False


def main():
    ok = True
    for name, url in FILES:
        path = os.path.join(DEST, name)
        if os.path.exists(path) and os.path.getsize(path) > 1024:
            print("[SKIP] %s already present" % name, flush=True)
            continue
        print("[GET ] %s" % url, flush=True)
        if not download(name, url):
            ok = False
    print("ALL_DONE=%s" % ok, flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
