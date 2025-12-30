import os
import glob
import statistics

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
CORPUS_DIR = os.path.join(BASE_DIR, "corpus")

SOURCES = ["ruwiki", "ru_wikisource"]

def human_bytes(n: int) -> str:
    units = ["B", "KB", "MB", "GB"]
    x = float(n)
    u = 0
    while x >= 1024 and u < len(units) - 1:
        x /= 1024
        u += 1
    if u == 0:
        return f"{n} {units[u]}"
    return f"{n} B ({x:.2f} {units[u]})"

def folder_stats(pattern: str):
    paths = sorted(glob.glob(pattern))
    if not paths:
        return None

    sizes = [os.path.getsize(p) for p in paths]
    return {
        "files": len(paths),
        "total_bytes": sum(sizes),
        "min": min(sizes),
        "median": int(statistics.median(sizes)),
        "mean": int(statistics.mean(sizes)),
        "max": max(sizes),
    }

def print_stats(title: str, st):
    if st is None:
        print(f"{title}: NO FILES")
        return
    print(title)
    print(f"  files:  {st['files']}")
    print(f"  total:  {human_bytes(st['total_bytes'])}")
    print(f"  min:    {human_bytes(st['min'])}")
    print(f"  median: {human_bytes(st['median'])}")
    print(f"  mean:   {human_bytes(st['mean'])}")
    print(f"  max:    {human_bytes(st['max'])}")

def main():
    for src in SOURCES:
        print("=" * 60)
        print(f"SOURCE: {src}")

        docs_pat = os.path.join(CORPUS_DIR, src, "docs", "*.txt")
        raw_pat  = os.path.join(CORPUS_DIR, src, "raw", "*.html")

        docs = folder_stats(docs_pat)
        raw  = folder_stats(raw_pat)

        print_stats("EXTRACTED TEXT (docs/*.txt)", docs)
        print_stats("RAW HTML (raw/*.html)", raw)

    print("=" * 60)
    all_docs = folder_stats(os.path.join(CORPUS_DIR, "*", "docs", "*.txt"))
    all_raw  = folder_stats(os.path.join(CORPUS_DIR, "*", "raw", "*.html"))
    print_stats("ALL SOURCES EXTRACTED", all_docs)
    print_stats("ALL SOURCES RAW", all_raw)

if __name__ == "__main__":
    main()
