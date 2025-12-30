import os
import re
import sys
import time
import sqlite3
import hashlib
import urllib.parse
from typing import Optional, Tuple, Dict, Any, List

import yaml
import requests


def die(msg: str, code: int = 1):
    print("ERROR:", msg, file=sys.stderr)
    sys.exit(code)


TAG_RE = re.compile(r"<[^>]+>")
WS_RE = re.compile(r"[ \t\r\f\v]+")
NL_RE = re.compile(r"\n{3,}")


def html_to_text(html: str) -> str:
    if not html:
        return ""
    html = re.sub(r"(?is)<script.*?>.*?</script>", " ", html)
    html = re.sub(r"(?is)<style.*?>.*?</style>", " ", html)
    html = re.sub(r"(?i)<br\s*/?>", "\n", html)
    html = re.sub(r"(?i)</p\s*>", "\n", html)
    text = TAG_RE.sub(" ", html)
    text = text.replace("\u00a0", " ")
    text = WS_RE.sub(" ", text).strip()
    text = NL_RE.sub("\n\n", text)
    return text


TRACKING_PREFIXES = ("utm_",)
TRACKING_KEYS = {"gclid", "fbclid", "yclid"}


def normalize_url(url: str) -> str:
    url = (url or "").strip()
    p = urllib.parse.urlsplit(url)
    scheme = (p.scheme or "http").lower()
    netloc = (p.netloc or "").lower()

    if netloc.endswith(":80") and scheme == "http":
        netloc = netloc[:-3]
    if netloc.endswith(":443") and scheme == "https":
        netloc = netloc[:-4]

    path = p.path or "/"
    fragment = ""

    q = urllib.parse.parse_qsl(p.query, keep_blank_values=True)
    nq = []
    for k, v in q:
        kl = k.lower()
        if kl in TRACKING_KEYS:
            continue
        if any(kl.startswith(pref) for pref in TRACKING_PREFIXES):
            continue
        nq.append((k, v))
    nq.sort(key=lambda kv: (kv[0], kv[1]))
    query = urllib.parse.urlencode(nq, doseq=True)

    return urllib.parse.urlunsplit((scheme, netloc, path, query, fragment))


def sha1_bytes(b: bytes) -> str:
    return hashlib.sha1(b).hexdigest()


SCHEMA = """
PRAGMA journal_mode=WAL;

CREATE TABLE IF NOT EXISTS docs (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  url TEXT NOT NULL,
  norm_url TEXT NOT NULL UNIQUE,
  source TEXT NOT NULL,
  html BLOB NOT NULL,
  fetched_at INTEGER NOT NULL,
  etag TEXT,
  last_modified TEXT,
  content_hash TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS frontier (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  url TEXT NOT NULL,
  norm_url TEXT NOT NULL UNIQUE,
  source TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending',
  tries INTEGER NOT NULL DEFAULT 0,
  last_try INTEGER,
  next_fetch_at INTEGER NOT NULL DEFAULT 0,
  err TEXT
);

CREATE INDEX IF NOT EXISTS idx_frontier_next ON frontier(status, next_fetch_at, id);
"""


def db_connect(path: str) -> sqlite3.Connection:
    conn = sqlite3.connect(path)
    conn.execute("PRAGMA foreign_keys=ON;")
    conn.executescript(SCHEMA)
    conn.commit()
    return conn


def db_upsert_frontier(conn: sqlite3.Connection, url: str, source: str):
    norm = normalize_url(url)
    if not norm:
        return
    conn.execute(
        "INSERT OR IGNORE INTO frontier(url, norm_url, source, status, next_fetch_at) VALUES(?,?,?,?,?)",
        (url, norm, source, "pending", 0)
    )


def db_get_next_task(conn: sqlite3.Connection, now: int) -> Optional[Tuple[int, str, str, str, int]]:
    row = conn.execute(
        "SELECT id, url, norm_url, source, tries FROM frontier "
        "WHERE status='pending' AND next_fetch_at<=? "
        "ORDER BY next_fetch_at ASC, id ASC LIMIT 1",
        (now,)
    ).fetchone()
    return row


def db_get_doc_meta(conn: sqlite3.Connection, norm_url: str) -> Optional[Tuple[Optional[str], Optional[str], str]]:
    row = conn.execute(
        "SELECT etag, last_modified, content_hash FROM docs WHERE norm_url=?",
        (norm_url,)
    ).fetchone()
    return row


def db_save_doc(conn: sqlite3.Connection, url: str, norm_url: str, source: str,
                html: bytes, fetched_at: int, etag: Optional[str], last_modified: Optional[str], content_hash: str):
    conn.execute(
        "INSERT INTO docs(url, norm_url, source, html, fetched_at, etag, last_modified, content_hash) "
        "VALUES(?,?,?,?,?,?,?,?) "
        "ON CONFLICT(norm_url) DO UPDATE SET "
        "url=excluded.url, source=excluded.source, html=excluded.html, fetched_at=excluded.fetched_at, "
        "etag=excluded.etag, last_modified=excluded.last_modified, content_hash=excluded.content_hash",
        (url, norm_url, source, html, fetched_at, etag, last_modified, content_hash)
    )


def db_mark_task(conn: sqlite3.Connection, task_id: int, status: str, tries: int, now: int,
                 next_fetch_at: int, err: Optional[str]):
    conn.execute(
        "UPDATE frontier SET status=?, tries=?, last_try=?, next_fetch_at=?, err=? WHERE id=?",
        (status, tries, now, next_fetch_at, err, task_id)
    )


def seed_from_file(conn: sqlite3.Connection, seed_file: str, source: str):
    if not os.path.exists(seed_file):
        die(f"seed_file not found: {seed_file}")
    with open(seed_file, "r", encoding="utf-8") as f:
        for line in f:
            u = line.strip()
            if not u:
                continue
            db_upsert_frontier(conn, u, source)


def seed_from_wiki_category(conn: sqlite3.Connection, api: str, category: str, max_seed: int, ua: str,
                           source: str, page_url_pattern: str, timeout_sec: int):
    sess = requests.Session()
    sess.headers["User-Agent"] = ua
    cont = None
    seeded = 0

    while seeded < max_seed:
        params = {
            "action": "query",
            "list": "categorymembers",
            "cmtitle": category,
            "cmlimit": "500",
            "cmtype": "page",
            "format": "json",
        }
        if cont:
            params["cmcontinue"] = cont
        r = sess.get(api, params=params, timeout=timeout_sec)
        r.raise_for_status()
        data = r.json()

        members = data.get("query", {}).get("categorymembers", [])
        if not members:
            break

        for m in members:
            if seeded >= max_seed:
                break
            pageid = int(m.get("pageid", 0))
            if pageid <= 0:
                continue
            url = page_url_pattern.format(pageid=pageid)
            db_upsert_frontier(conn, url, source)
            seeded += 1

        cont = data.get("continue", {}).get("cmcontinue")
        if not cont:
            break

    print(f"[seed] {source}: {seeded} urls inserted (or already existed)")


def do_seeding(conn: sqlite3.Connection, cfg: Dict[str, Any], ua: str, timeout_sec: int):
    logic = cfg.get("logic") or {}
    sources = cfg.get("sources")
    max_seed_default = int(logic.get("max_seed", 50000))

    if sources and isinstance(sources, list) and len(sources) > 0:
        for s in sources:
            name = s.get("name")
            seed = s.get("seed") or {}
            kind = seed.get("kind")
            if not name or not kind:
                die("Invalid sources[] entry: missing name or seed.kind")

            if kind == "wiki_category":
                api = seed.get("api")
                cat = seed.get("category")
                pattern = seed.get("page_url_pattern")
                max_seed = int(seed.get("max_seed", max_seed_default))
                if not api or not cat or not pattern:
                    die(f"Invalid wiki_category seed for source '{name}'")
                seed_from_wiki_category(conn, api, cat, max_seed, ua, name, pattern, timeout_sec)
            elif kind == "seed_file":
                path = seed.get("path")
                if not path:
                    die(f"Invalid seed_file seed for source '{name}'")
                seed_from_file(conn, path, name)
                print(f"[seed] {name}: from file done")
            else:
                die(f"Unknown seed kind for source '{name}': {kind}")
        return

    wiki_api = logic.get("wikipedia_api")
    wiki_cat = logic.get("wikipedia_category")
    seed_file = logic.get("seed_file")
    source = logic.get("source_name", "source")

    if wiki_api and wiki_cat:
        seed_from_wiki_category(
            conn,
            str(wiki_api),
            str(wiki_cat),
            max_seed_default,
            ua,
            source,
            "https://ru.wikipedia.org/?curid={pageid}",
            timeout_sec
        )
    elif seed_file:
        seed_from_file(conn, str(seed_file), source)
        print("[seed] from file done")
    else:
        die("No seeds: specify sources[] OR logic.seed_file OR (logic.wikipedia_api + logic.wikipedia_category).")


def main():
    if len(sys.argv) != 2:
        die("Usage: python robot.py config.yaml", 2)

    cfg_path = sys.argv[1]
    if not os.path.exists(cfg_path):
        die(f"config not found: {cfg_path}")

    with open(cfg_path, "r", encoding="utf-8") as f:
        cfg = yaml.safe_load(f)

    db_cfg = cfg.get("db") or {}
    logic = cfg.get("logic") or {}

    if db_cfg.get("type") != "sqlite":
        die("Only sqlite db.type is implemented in this template.")

    db_path = db_cfg.get("path", "crawler.db")
    delay_sec = float(logic.get("delay_sec", 0.2))
    ua = logic.get("user_agent", "MAI-IR-LAB2/1.0")
    timeout_sec = int(logic.get("timeout_sec", 30))
    recrawl_every = int(logic.get("recrawl_every_sec", 86400))

    save_text = bool(logic.get("save_text", False))
    docs_dir = logic.get("docs_dir", "corpus/docs")
    min_text_chars = int(logic.get("min_text_chars", 800))

    os.makedirs(os.path.dirname(db_path) or ".", exist_ok=True)
    if save_text:
        os.makedirs(docs_dir, exist_ok=True)

    conn = db_connect(db_path)

    cnt = conn.execute("SELECT COUNT(*) FROM frontier").fetchone()[0]
    if cnt == 0:
        do_seeding(conn, cfg, ua, timeout_sec)
        conn.commit()

    sess = requests.Session()
    sess.headers["User-Agent"] = ua

    print("=== LAB2 ROBOT START ===")
    print("db:", db_path)
    print("delay_sec:", delay_sec)
    print("recrawl_every_sec:", recrawl_every)
    sys.stdout.flush()

    try:
        while True:
            now = int(time.time())
            task = db_get_next_task(conn, now)
            if not task:
                time.sleep(0.25)
                continue

            task_id, url, norm_url, src, tries = task
            tries = int(tries)

            meta = db_get_doc_meta(conn, norm_url)
            headers: Dict[str, str] = {}
            if meta:
                etag, last_mod, _hash = meta
                if etag:
                    headers["If-None-Match"] = etag
                if last_mod:
                    headers["If-Modified-Since"] = last_mod

            try:
                r = sess.get(url, headers=headers, timeout=timeout_sec, allow_redirects=True)
                fetched_at = int(time.time())

                if r.status_code == 304:
                    db_mark_task(conn, task_id, "pending", 0, fetched_at, fetched_at + recrawl_every, None)
                    conn.commit()
                    time.sleep(delay_sec)
                    continue

                r.raise_for_status()
                html = r.content or b""
                etag = r.headers.get("ETag")
                last_mod = r.headers.get("Last-Modified")
                h = sha1_bytes(html)

                changed = True
                if meta:
                    _, _, old_hash = meta
                    if old_hash == h:
                        changed = False

                if changed:
                    db_save_doc(conn, url, norm_url, src, html, fetched_at, etag, last_mod, h)

                    if save_text:
                        text = html_to_text(html.decode("utf-8", errors="ignore"))
                        if len(text) >= min_text_chars:
                            fn = hashlib.sha1(norm_url.encode("utf-8", errors="ignore")).hexdigest() + ".txt"
                            with open(os.path.join(docs_dir, fn), "w", encoding="utf-8", newline="\n") as f:
                                f.write(text)

                db_mark_task(conn, task_id, "pending", 0, fetched_at, fetched_at + recrawl_every, None)
                conn.commit()

                print(f"[ok] {url} source={src} changed={int(changed)} next_in={recrawl_every}s")
                sys.stdout.flush()
                time.sleep(delay_sec)

            except KeyboardInterrupt:
                raise
            except Exception as e:
                tries2 = tries + 1
                backoff = min(3600, 2 ** min(tries2, 12))
                t = int(time.time())
                db_mark_task(conn, task_id, "pending", tries2, t, t + backoff, str(e))
                conn.commit()
                print(f"[fail] {url} tries={tries2} backoff={backoff}s err={e}", file=sys.stderr)
                sys.stderr.flush()
                time.sleep(delay_sec)

    except KeyboardInterrupt:
        print("\n[stop] interrupted by user (Ctrl+C). State is in DB, resume is supported.")
    finally:
        conn.close()


if __name__ == "__main__":
    main()
