import os
import time
import csv
import requests

MIN_CHARS = 200 
DELAY = 0.15 

SOURCES = [
    {
        "name": "ruwiki",
        "api": "https://ru.wikipedia.org/w/api.php",
        "category": "Категория:Фильмы по алфавиту",
    },
    {
        "name": "ru_wikisource",
        "api": "https://ru.wikisource.org/w/api.php",
        "category": "Категория:Произведения по алфавиту",
    },
]

UA = "MAI-IR-Lab1-Corpus/1.0 (student project)"

def ensure_dirs(base_dir: str):
    os.makedirs(os.path.join(base_dir, "docs"), exist_ok=True)
    os.makedirs(os.path.join(base_dir, "raw"), exist_ok=True)

def log(path: str, msg: str):
    print(msg)
    with open(path, "a", encoding="utf-8") as f:
        f.write(msg + "\n")

def clean_tail_sections(text: str) -> str:
    cut_markers = [
        "\nСсылки\n", "\nПримечания\n", "\nЛитература\n", "\nИсточники\n",
        "\nСм. также\n", "\nВнешние ссылки\n"
    ]
    pos = None
    for m in cut_markers:
        p = text.find(m)
        if p != -1:
            pos = p if pos is None else min(pos, p)
    return text if pos is None else text[:pos].strip()

def fetch_category_pageids(session: requests.Session, api: str, category: str):
    results = []
    cmcontinue = None

    while True:
        params = {
            "action": "query",
            "format": "json",
            "list": "categorymembers",
            "cmtitle": category,
            "cmtype": "page",
            "cmnamespace": "0",
            "cmlimit": "500",
        }
        if cmcontinue:
            params["cmcontinue"] = cmcontinue

        r = session.get(api, params=params, timeout=60)
        r.raise_for_status()
        data = r.json()

        members = data.get("query", {}).get("categorymembers", [])
        for m in members:
            results.append((m["pageid"], m["title"]))

        cont = data.get("continue", {})
        if "cmcontinue" in cont:
            cmcontinue = cont["cmcontinue"]
            time.sleep(DELAY)
        else:
            break

    return results

def fetch_extract_text(session: requests.Session, api: str, pageid: int) -> str:
    params = {
        "action": "query",
        "format": "json",
        "prop": "extracts",
        "pageids": str(pageid),
        "explaintext": "1",
        "exsectionformat": "plain",
        "redirects": "1",
    }
    r = session.get(api, params=params, timeout=60)
    r.raise_for_status()
    data = r.json()
    page = data.get("query", {}).get("pages", {}).get(str(pageid), {})
    return page.get("extract", "") or ""

def fetch_raw_html(session: requests.Session, api: str, pageid: int) -> str:
    params = {
        "action": "parse",
        "format": "json",
        "pageid": str(pageid),
        "prop": "text",
        "redirects": "1",
    }
    r = session.get(api, params=params, timeout=60)
    r.raise_for_status()
    data = r.json()
    return (data.get("parse", {}).get("text", {}) or {}).get("*", "") or ""

def crawl_one_source(src: dict):
    name = src["name"]
    api = src["api"]
    category = src["category"]

    base_dir = os.path.join("corpus", name)
    docs_dir = os.path.join(base_dir, "docs")
    raw_dir  = os.path.join(base_dir, "raw")
    meta_path = os.path.join(base_dir, "meta.tsv")
    log_path  = os.path.join(base_dir, "crawl.log")

    ensure_dirs(base_dir)

    session = requests.Session()
    session.headers.update({"User-Agent": UA})

    log(log_path, "=== CRAWL START ===")
    log(log_path, f"source={name}")
    log(log_path, f"api={api}")
    log(log_path, f"category={category}")

    items = fetch_category_pageids(session, api, category)
    log(log_path, f"Total pageids in category (raw): {len(items)}")

    saved = 0
    skipped_short = 0
    failed = 0

    with open(meta_path, "w", encoding="utf-8", newline="") as meta:
        w = csv.writer(meta, delimiter="\t")
        w.writerow(["doc_id", "page_id", "title", "source", "raw_file", "text_file"])

        for i, (pageid, title) in enumerate(items, start=1):
            if i % 200 == 0:
                log(log_path, f"[prog] processed={i}/{len(items)} saved={saved} skipped={skipped_short} failed={failed}")

            try:
                text = fetch_extract_text(session, api, pageid)
                text = clean_tail_sections(text)

                if len(text.strip()) < MIN_CHARS:
                    skipped_short += 1
                    continue

                doc_id = saved + 1
                text_file = f"{doc_id:08d}.txt"
                raw_file  = f"{doc_id:08d}.html"

                html = fetch_raw_html(session, api, pageid)

                with open(os.path.join(docs_dir, text_file), "w", encoding="utf-8") as f:
                    f.write(text)
                with open(os.path.join(raw_dir, raw_file), "w", encoding="utf-8") as f:
                    f.write(html)

                w.writerow([doc_id, pageid, title, name, raw_file, text_file])
                saved += 1

                time.sleep(DELAY)

            except Exception as e:
                failed += 1
                if failed <= 20:
                    log(log_path, f"[err] pageid={pageid} title={title} error={e}")
                time.sleep(0.5)

    log(log_path, "=== CRAWL END ===")
    log(log_path, f"Saved docs: {saved}")
    log(log_path, f"Skipped (too short): {skipped_short}")
    log(log_path, f"Failed downloads: {failed}")

def main():
    os.makedirs("corpus", exist_ok=True)
    for src in SOURCES:
        crawl_one_source(src)

if __name__ == "__main__":
    main()
