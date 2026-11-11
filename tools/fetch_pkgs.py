# -*- coding: utf-8 -*-
"""Low-concurrency retrying downloader for the MSYS2 package closure."""
import concurrent.futures as cf
import os
import sys
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import setup_mingw as s  # noqa: E402

os.makedirs(s.PKG, exist_ok=True)
# The mirror rejects browser-like User-Agent on package GET (403) while
# accepting plain urllib requests with a Range header.
HEADERS = {"Range": "bytes=0-"}


def fetch(pkg):
    name = pkg["filename"]
    path = os.path.join(s.PKG, name)
    if os.path.exists(path) and os.path.getsize(path) > 1024:
        return name, os.path.getsize(path), "cached"
    for attempt in range(6):
        try:
            req = urllib.request.Request(s.BASE + name, headers=HEADERS)
            with urllib.request.urlopen(req, timeout=180) as resp:
                data = resp.read()
            with open(path, "wb") as fh:
                fh.write(data)
            return name, len(data), "ok"
        except Exception as exc:  # noqa: BLE001
            print("[RETRY] %s #%d %s" % (name, attempt + 1, str(exc)[:60]), flush=True)
            time.sleep(3 + 3 * attempt)
    return name, 0, "FAILED"


def main():
    pkgs = s.parse_db()
    plan = s.closure(pkgs)
    total = 0
    failed = []
    with cf.ThreadPoolExecutor(max_workers=3) as pool:
        futs = [pool.submit(fetch, p) for p in plan]
        for i, fut in enumerate(cf.as_completed(futs), 1):
            name, size, status = fut.result()
            total += size
            if status == "FAILED":
                failed.append(name)
            print("[%02d/%02d] %-70s %8.2f MB %s" %
                  (i, len(plan), name[:70], size / 1048576, status), flush=True)
    print("TOTAL %.1f MB, FAILED %d" % (total / 1048576, len(failed)), flush=True)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
