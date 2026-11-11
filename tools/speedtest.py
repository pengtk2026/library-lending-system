# -*- coding: utf-8 -*-
"""Probe mirror bandwidth for the portable toolchain download (stdlib only)."""
import concurrent.futures as cf
import time
import urllib.request

GH = ("https://github.com/brechtsanders/winlibs_mingw/releases/download/"
      "16.2.0posix-23.1.3-14.0.0-ucrt-r3/"
      "winlibs-x86_64-posix-seh-gcc-16.2.0-mingw-w64ucrt-14.0.0-r3.7z")
CANDIDATES = {
    "github-direct": GH,
    "ghfast": "https://ghfast.top/" + GH,
    "gh-proxy-com": "https://gh-proxy.com/" + GH,
    "gitmirror": "https://hub.gitmirror.com/" + GH,
    "moeyy": "https://github.moeyy.xyz/" + GH,
    "tuna-release": ("https://mirrors.tuna.tsinghua.edu.cn/github-release/"
                     "brechtsanders/winlibs_mingw/"),
}


def fetch(url, start, length, timeout=12):
    req = urllib.request.Request(
        url, headers={"User-Agent": "Mozilla/5.0",
                      "Range": "bytes=%d-%d" % (start, start + length - 1)})
    t = time.time()
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        data = resp.read()
    return len(data), time.time() - t


def single(name, url):
    try:
        n, dt = fetch(url, 0, 1024 * 1024)
        return name, n / 1048576 / max(dt, 0.01)
    except Exception as exc:  # noqa: BLE001
        return name, "ERR %s %s" % (type(exc).__name__, str(exc)[:60])


print("== single connection ==", flush=True)
best = (None, 0.0)
for name, url in CANDIDATES.items():
    label, rate = single(name, url)
    print("%-16s %s" % (label, rate), flush=True)
    if isinstance(rate, float) and rate > best[1]:
        best = (name, rate)

print("\n== 12 parallel connections (github-direct) ==", flush=True)
url = CANDIDATES.get(best[0], GH) if best[0] and best[0] != "tuna-release" else GH
t = time.time()
with cf.ThreadPoolExecutor(max_workers=12) as pool:
    futs = [pool.submit(fetch, url, i * 1048576, 1048576) for i in range(12)]
    total = 0
    for f in futs:
        try:
            n, _ = f.result()
            total += n
        except Exception:
            pass
dt = time.time() - t
print("parallel %.2f MB in %.1fs = %.2f MB/s" %
      (total / 1048576, dt, total / 1048576 / max(dt, 0.01)), flush=True)
