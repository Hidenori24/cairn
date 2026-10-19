# JIRA取り込み用CSVの互換チェック（標準のCSV取り込みで問題になる点を機械的に確認する）
# 使い方: python3 tools/jira_check.py [CSV ...]   省略すると data/ の下のCSVをすべて調べる。
# 各CSVをアプリに読み込み、「JIRAに取り込む用」で書き出した結果を調べます（要 Playwright: pip install playwright）。
import csv, io, re, sys, glob, os
from playwright.sync_api import sync_playwright
STD_TYPES = {"Epic","Task","Sub-task","Story","Bug"}
STD_COLS = {"Issue Id","Issue key","Issue Type","Summary","Parent","Epic Name","Epic Link","Priority","Status","Assignee","Start date","Due date","Resolved","Original estimate","Labels","Description","Outward issue link (Blocks)","Comment"}
APP_COLS = {"Earliest start","Deadline","Remaining (weeks)","Duration (weeks)"}
PRIO = {"Highest","High","Medium","Low","Lowest"}
EST = re.compile(r"^(\d+w)?( ?\d+d)?( ?\d+h)?( ?\d+m)?$")
def check(name, text):
    rows = list(csv.reader(io.StringIO(text))); h = rows[0]; body = rows[1:]; errs = []
    col = lambda r, c: r[h.index(c)] if c in h else ""
    cols = lambda r, c: [r[i] for i, x in enumerate(h) if x == c and r[i]]
    # 列: 標準列が先、アプリ独自列は後ろだけ
    first_app = min([i for i, c in enumerate(h) if c in APP_COLS or c.startswith("Duration (weeks) @")] or [len(h)])
    for i, c in enumerate(h):
        if c in STD_COLS and i > first_app: errs.append(f"標準列 {c} がアプリ独自列より後ろ")
        if c not in STD_COLS and c not in APP_COLS and not c.startswith("Duration (weeks) @"): errs.append(f"不明な列 {c}")
    ids = [col(r, "Issue Id") for r in body]; idset = set(ids); T = {col(r, "Issue Id"): col(r, "Issue Type") for r in body}
    if len(ids) != len(idset): errs.append("Issue Id が重複")
    epicnames = [col(r, "Epic Name") for r in body if col(r, "Issue Type") == "Epic"]
    if len(epicnames) != len(set(epicnames)): errs.append("Epic Name が重複")
    for r in body:
        i = col(r, "Issue Id"); t = col(r, "Issue Type"); p = col(r, "Parent")
        if t not in STD_TYPES: errs.append(f"{i}: 標準にない種別 {t}")
        if not col(r, "Summary").strip() or len(col(r, "Summary")) > 255: errs.append(f"{i}: Summary が空か255文字超")
        if t == "Epic" and not col(r, "Epic Name"): errs.append(f"{i}: Epic に Epic Name がない")
        if t == "Sub-task" and not p: errs.append(f"{i}: Sub-task に Parent がない")
        if p:
            if p not in idset: errs.append(f"{i}: Parent {p} が存在しない")
            elif T[p] == "Sub-task": errs.append(f"{i}: Sub-task の下に子（Parent={p}）")
            elif t == "Sub-task" and T[p] == "Epic": errs.append(f"{i}: Sub-task の親が Epic")
            elif t != "Sub-task" and T[p] != "Epic": errs.append(f"{i}: Task の親が Epic 以外（{T[p]}）")
        if col(r, "Priority") and col(r, "Priority") not in PRIO: errs.append(f"{i}: 優先度 {col(r,'Priority')}")
        for l in cols(r, "Labels"):
            if re.search(r"\s", l): errs.append(f"{i}: ラベルに空白 '{l}'")
        e = col(r, "Original estimate")
        if e and not EST.match(e): errs.append(f"{i}: 見積の書式 {e}")
        for d in ("Start date", "Due date", "Resolved"):
            v = col(r, d)
            if v and not re.match(r"^\d{4}-\d{2}-\d{2}$", v): errs.append(f"{i}: {d} の書式 {v}")
        for b in cols(r, "Outward issue link (Blocks)"):
            if b not in idset or b == i: errs.append(f"{i}: Blocks 先 {b} が不正")
        for c in cols(r, "Comment"):
            if not re.match(r"^\d{2}/[A-Z][a-z]{2}/\d{2} \d{2}:\d{2} [AP]M;[^;]*;", c): errs.append(f"{i}: コメントの書式")
        if col(r, "Start date") and col(r, "Due date") and col(r, "Start date") > col(r, "Due date"): errs.append(f"{i}: Start > Due")
    print(f"{name}: {len(body)}件 / 列 {len(h)} / " + ("OK" if not errs else f"NG {len(errs)}件"))
    for e in errs[:12]: print("   -", e)
    return not errs
root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
paths = sys.argv[1:] or sorted(glob.glob(os.path.join(root, "data", "**", "*.csv"), recursive=True))
with sync_playwright() as p:
    b = p.chromium.launch()
    pg = b.new_context().new_page(); pg.goto("file://" + os.path.join(root, "index.html")); pg.wait_for_timeout(800)
    try: pg.click(".hint-bar button")
    except Exception: pass
    allok = True
    for path in paths:
        pg.evaluate("(c)=>window.__rm.ops.doImport(c,'check','new')", open(path, encoding="utf-8").read()); pg.wait_for_timeout(200)
        allok &= check(os.path.relpath(path, root), pg.evaluate("window.__rm.ops.exportText('jira')"))
    b.close()
print("ALL OK" if allok else "HAS ISSUES")
sys.exit(0 if allok else 1)
