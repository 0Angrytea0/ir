import os
import subprocess
from flask import Flask, request

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SEARCH_EXE = os.path.join(BASE_DIR, "bin", "search.exe")
INDEX_BIN  = os.path.join(BASE_DIR, "index", "index.bin")

app = Flask(__name__)

def run_search(query: str, offset: int, limit: int = 50):
    if not os.path.isfile(SEARCH_EXE):
        return 0, [], f"not found: {SEARCH_EXE}"
    if not os.path.isfile(INDEX_BIN):
        return 0, [], f"not found: {INDEX_BIN}"

    p = subprocess.run(
        [SEARCH_EXE, INDEX_BIN, "--offset", str(offset), "--limit", str(limit)],
        input=(query.strip() + "\n").encode("utf-8"),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )

    out = p.stdout.decode("utf-8", errors="replace").splitlines()
    err = p.stderr.decode("utf-8", errors="replace").strip()

    if not out:
        return 0, [], (err or "no output")

    header = out[0].split("\t")
    if not header or header[0] != "OK":
        return 0, [], f"bad output: {out[0]}"

    total = 0
    for x in header:
        if x.startswith("total="):
            try:
                total = int(x.split("=", 1)[1])
            except:
                total = 0

    items = []
    for line in out[1:]:
        parts = line.split("\t")
        if len(parts) < 4:
            continue
        doc_id = parts[0]
        page_id = parts[1]
        title = parts[2]
        url = parts[3]
        items.append((doc_id, page_id, title, url))

    return total, items, err

@app.get("/")
def home():
    return """
<!doctype html>
<html lang="ru">
<head><meta charset="utf-8"><title>Boolean Search</title></head>
<body>
<h2>Булев поиск</h2>
<form method="get" action="/search">
<input type="text" name="q" style="width:600px" placeholder="Введите запрос">
<button type="submit">Искать</button>
</form>
<p>Синтаксис: пробел/&& = И, || = ИЛИ, ! = НЕ, скобки разрешены</p>
</body>
</html>
"""

@app.get("/search")
def search():
    q = (request.args.get("q") or "").strip()
    offset = request.args.get("offset") or "0"
    try:
        offset_i = max(0, int(offset))
    except:
        offset_i = 0

    if not q:
        return """
<!doctype html>
<html lang="ru">
<head><meta charset="utf-8"><title>Search</title></head>
<body>
<p>Пустой запрос.</p>
<a href="/">На главную</a>
</body>
</html>
"""

    total, items, err = run_search(q, offset_i, 50)

    prev_off = max(0, offset_i - 50)
    next_off = offset_i + 50

    nav = []
    if offset_i > 0:
        nav.append(f'<a href="/search?q={q}&offset={prev_off}">Назад 50</a>')
    if next_off < total:
        nav.append(f'<a href="/search?q={q}&offset={next_off}">Следующие 50</a>')
    nav_html = " | ".join(nav) if nav else ""

    shown_from = offset_i
    shown_to = min(offset_i + 50, total)

    rows = []
    for doc_id, page_id, title, url in items:
        rows.append(f'<li><a href="{url}">{title}</a> <small>doc_id={doc_id}, page_id={page_id}</small></li>')

    err_html = f"<pre>{err}</pre>" if err else ""

    return f"""
<!doctype html>
<html lang="ru">
<head><meta charset="utf-8"><title>Results</title></head>
<body>
<h2>Результаты</h2>
<form method="get" action="/search">
<input type="text" name="q" value="{q}" style="width:600px">
<input type="hidden" name="offset" value="0">
<button type="submit">Искать</button>
</form>
<p>Всего: {total}. Показаны {shown_from}..{max(shown_to-1, shown_from)}</p>
<p>{nav_html}</p>
{err_html}
<ol>
{''.join(rows)}
</ol>
<p>{nav_html}</p>
<p><a href="/">На главную</a></p>
</body>
</html>
"""

if __name__ == "__main__":
    app.run(host="127.0.0.1", port=8080, debug=False)
