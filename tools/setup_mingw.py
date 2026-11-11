# -*- coding: utf-8 -*-
"""Build a portable MinGW-w64 toolchain from a fast MSYS2 mirror.

Instead of downloading a 260 MB monolithic toolchain from GitHub, this
script reads the repository database, computes the dependency closure of
gcc/g++/make/ninja/pkgconf, downloads only those .pkg.tar.zst packages and
extracts them into tools/msys2root/mingw64.
"""
import concurrent.futures as cf
import io
import os
import re
import sys
import tarfile
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DL = os.path.join(ROOT, "tools", "_downloads")
PKG = os.path.join(DL, "msys2pkgs")
DEST = os.path.join(ROOT, "tools", "msys2root")
BASE = "https://mirrors.tuna.tsinghua.edu.cn/msys2/mingw/mingw64/"
os.makedirs(PKG, exist_ok=True)
os.makedirs(DEST, exist_ok=True)

SEEDS = [
    "mingw-w64-x86_64-gcc",
    "mingw-w64-x86_64-make",
    "mingw-w64-x86_64-ninja",
    "mingw-w64-x86_64-pkgconf",
]


def get(url, timeout=60):
    req = urllib.request.Request(url, headers={"User-Agent": "Mozilla/5.0"})
    return urllib.request.urlopen(req, timeout=timeout).read()


def open_tar(blob):
    """Return a tarfile object from gzip / zstd / raw tar bytes."""
    if blob[:4] == b"\x28\xb5\x2f\xfd":
        import zstandard as zstd
        with zstd.ZstdDecompressor().stream_reader(io.BytesIO(blob)) as rd:
            return tarfile.open(fileobj=io.BytesIO(rd.read()), mode="r:")
    return tarfile.open(fileobj=io.BytesIO(blob), mode="r:*")


def parse_db():
    db_path = os.path.join(DL, "mingw64.db")
    if not os.path.exists(db_path):
        print("[DB  ] downloading repository database", flush=True)
        with open(db_path, "wb") as fh:
            fh.write(get(BASE + "mingw64.db", timeout=90))
    with open(db_path, "rb") as fh:
        tf = open_tar(fh.read())
    pkgs = {}
    for member in tf.getmembers():
        if not member.name.endswith("/desc") or not member.isfile():
            continue
        text = tf.extractfile(member).read().decode("utf-8", "ignore")
        fields = {}
        cur = None
        for line in text.splitlines():
            m = re.match(r"^%([A-Z]+)%$", line.strip())
            if m:
                cur = m.group(1)
                fields[cur] = []
            elif cur and line.strip():
                fields[cur].append(line.strip())
        name = fields.get("NAME", [None])[0]
        if not name:
            continue
        pkgs[name] = {
            "name": name,
            "filename": fields.get("FILENAME", [None])[0],
            "depends": fields.get("DEPENDS", []),
            "provides": fields.get("PROVIDES", []),
        }
    print("[DB  ] %d packages indexed" % len(pkgs), flush=True)
    return pkgs


def dep_name(dep):
    return re.split(r"[<>=]", dep, 1)[0].strip()


def closure(pkgs):
    by_provides = {}
    for name, pkg in pkgs.items():
        by_provides.setdefault(name, name)
        for prov in pkg["provides"]:
            by_provides.setdefault(dep_name(prov), name)
    todo = list(SEEDS)
    chosen, missing = [], set()
    while todo:
        raw = todo.pop(0)
        name = dep_name(raw)
        target = by_provides.get(name)
        if not target:
            missing.add(name)
            continue
        if target in chosen:
            continue
        chosen.append(target)
        todo.extend(pkgs[target]["depends"])
    print("[PLAN] %d packages; unresolved (non-mingw runtime): %s" %
          (len(chosen), ", ".join(sorted(missing))), flush=True)
    return [pkgs[n] for n in chosen if pkgs[n]["filename"]]


def download_one(pkg):
    name = pkg["filename"]
    path = os.path.join(PKG, name)
    if os.path.exists(path) and os.path.getsize(path) > 1024:
        return name, os.path.getsize(path)
    for attempt in range(4):
        try:
            data = get(BASE + name, timeout=120)
            with open(path, "wb") as fh:
                fh.write(data)
            return name, len(data)
        except Exception as exc:  # noqa: BLE001
            if attempt == 3:
                raise
            time.sleep(2 + 2 * attempt)
    return name, 0


def extract_one(pkg):
    path = os.path.join(PKG, pkg["filename"])
    with open(path, "rb") as fh:
        tf = open_tar(fh.read())
    count = 0
    for member in tf.getmembers():
        parts = member.name.split("/")
        if any(p.startswith(".") for p in parts):
            continue
        if member.isdir():
            continue
        if not (member.isfile() or member.islnk() or member.issym()):
            continue
        try:
            tf.extract(member, path=DEST, filter="data")
            count += 1
        except Exception:  # noqa: BLE001 - skip exotic links / device files
            continue
    return pkg["filename"], count


def main():
    try:
        import zstandard  # noqa: F401
    except ImportError:
        print("[ERR ] pip install zstandard first", flush=True)
        return 1
    pkgs = parse_db()
    plan = closure(pkgs)
    total = 0
    t0 = time.time()
    print("[GET ] downloading packages", flush=True)
    with cf.ThreadPoolExecutor(max_workers=8) as pool:
        futs = {pool.submit(download_one, p): p for p in plan}
        for i, fut in enumerate(cf.as_completed(futs), 1):
            name, size = fut.result()
            total += size
            if i % 5 == 0 or i == len(plan):
                print("[GET ] %d/%d  %.1f MB" %
                      (i, len(plan), total / 1048576), flush=True)
    print("[GET ] done %.1f MB in %.0fs" %
          (total / 1048576, time.time() - t0), flush=True)
    print("[UNPK] extracting", flush=True)
    with cf.ThreadPoolExecutor(max_workers=4) as pool:
        futs = [pool.submit(extract_one, p) for p in plan]
        files = 0
        for fut in cf.as_completed(futs):
            _, n = fut.result()
            files += n
    print("[UNPK] %d files extracted" % files, flush=True)
    for exe in ("g++.exe", "gcc.exe", "mingw32-make.exe", "ninja.exe"):
        p = os.path.join(DEST, "mingw64", "bin", exe)
        print("[CHK ] %-20s %s" % (exe, "OK" if os.path.exists(p) else "MISSING"),
              flush=True)
    print("SETUP_DONE", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
