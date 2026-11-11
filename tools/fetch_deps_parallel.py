# -*- coding: utf-8 -*-
"""Parallel Range-based downloader for portable build dependencies.

The single-connection bandwidth to GitHub Releases is heavily throttled on
this machine, while 12-16 concurrent Range connections reach >3 MB/s.
Each chunk is stored separately so the download is fully resumable.
"""
import concurrent.futures as cf
import os
import sys
import time
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEST = os.path.join(ROOT, "tools", "_downloads")
PARTS = os.path.join(DEST, ".parts")
os.makedirs(PARTS, exist_ok=True)

CHUNK = 2 * 1048576
WORKERS = 16

FILES = [
    ("mingw64-gcc.zip",
     "https://github.com/brechtsanders/winlibs_mingw/releases/download/"
     "16.2.0posix-23.1.3-14.0.0-ucrt-r3/"
     "winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.zip"),
    ("cmake.zip",
     "https://cmake.org/files/v3.31/cmake-3.31.6-windows-x86_64.zip"),
    ("fltk-1.4.5-source.tar.gz",
     "https://github.com/fltk/fltk/releases/download/release-1.4.5/"
     "fltk-1.4.5-source.tar.gz"),
    ("sqlite-amalgamation.zip",
     "https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip"),
]


def head_size(url):
    req = urllib.request.Request(url, method="HEAD",
                                 headers={"User-Agent": "Mozilla/5.0"})
    with urllib.request.urlopen(req, timeout=45) as resp:
        return int(resp.headers["Content-Length"])


def get_chunk(args):
    url, start, end, part_path = args
    if os.path.exists(part_path) and os.path.getsize(part_path) == end - start + 1:
        return end - start + 1
    req = urllib.request.Request(
        url, headers={"User-Agent": "Mozilla/5.0",
                      "Range": "bytes=%d-%d" % (start, end)})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(req, timeout=90) as resp:
                data = resp.read()
            if len(data) != end - start + 1:
                raise IOError("short chunk %d != %d" %
                              (len(data), end - start + 1))
            with open(part_path, "wb") as fh:
                fh.write(data)
            return len(data)
        except Exception:  # noqa: BLE001 - retry transient network failures
            time.sleep(2 + attempt * 2)
    raise IOError("chunk failed: %s" % part_path)


def download(name, url):
    final = os.path.join(DEST, name)
    if os.path.exists(final) and os.path.getsize(final) > 1024:
        print("[SKIP] %s" % name, flush=True)
        return True
    size = head_size(url)
    count = (size + CHUNK - 1) // CHUNK
    part_dir = os.path.join(PARTS, name)
    os.makedirs(part_dir, exist_ok=True)
    jobs = []
    for idx in range(count):
        start = idx * CHUNK
        end = min(size - 1, start + CHUNK - 1)
        jobs.append((url, start, end, os.path.join(part_dir, "%05d" % idx)))
    print("[GET ] %s  %.1f MB in %d chunks" %
          (name, size / 1048576, count), flush=True)
    t0 = time.time()
    done = 0
    with cf.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        futures = {pool.submit(get_chunk, j): j for j in jobs}
        for fut in cf.as_completed(futures):
            try:
                fut.result()
            except Exception as exc:  # noqa: BLE001
                print("[FAIL] %s: %s" % (name, exc), flush=True)
                return False
            done += 1
            if done % 10 == 0 or done == count:
                elapsed = time.time() - t0
                print("[%s] %d/%d chunks  %.1f MB/s" %
                      (name, done, count,
                       done * CHUNK / 1048576 / max(elapsed, 0.01)), flush=True)
    with open(final, "wb") as out:
        for idx in range(count):
            with open(jobs[idx][3], "rb") as fh:
                out.write(fh.read())
    if os.path.getsize(final) != size:
        print("[FAIL] size mismatch %s" % name, flush=True)
        return False
    if name.endswith(".zip"):
        with zipfile.ZipFile(final) as zf:
            if zf.testzip() is not None:
                print("[FAIL] corrupt zip %s" % name, flush=True)
                return False
    print("[OK  ] %s %.1f MB in %.0fs" %
          (name, size / 1048576, time.time() - t0), flush=True)
    return True


def main():
    ok = True
    for name, url in FILES:
        try:
            if not download(name, url):
                ok = False
        except Exception as exc:  # noqa: BLE001
            print("[ERROR] %s: %s: %s" %
                  (name, type(exc).__name__, str(exc)[:200]), flush=True)
            ok = False
    print("ALL_DONE=%s" % ok, flush=True)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
