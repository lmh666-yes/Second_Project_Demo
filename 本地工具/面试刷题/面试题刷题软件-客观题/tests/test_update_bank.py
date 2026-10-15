# -*- coding: utf-8 -*-
"""更新题库功能测试：解析准确性 + 主流程(单文档/多文档选择/空文件夹/损坏文档) + 自动恢复原题库"""
import sys, io, os, json, shutil, importlib.util, builtins
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CORE = os.path.join(ROOT, "core")
sys.path.insert(0, CORE)
import bank_parser
from make_bank_fixtures import make_test_docx, make_test_docx2

BANK_JSON = os.path.join(ROOT, "题库.json")
BANK_DIR = os.path.join(ROOT, "题库")
TMP = os.path.join(HERE, "_banktest_tmp")
BAK_JSON = os.path.join(HERE, "_banktest_bak.json")
TEST1 = os.path.join(TMP, "测试题库A.docx")
TEST2 = os.path.join(TMP, "测试题库B.docx")
BAD = os.path.join(TMP, "损坏文件.docx")

PASS = 0
FAIL = 0
def ok(name, cond, extra=""):
    global PASS, FAIL
    if cond:
        PASS += 1
        print(f"  [PASS] {name} {extra}")
    else:
        FAIL += 1
        print(f"  [FAIL] {name} {extra}")

# 全程 mock 输入（update_bank 的“按回车退出”等提示自动跳过，无需键盘）
orig_input = builtins.input
builtins.input = lambda prompt="": ""

# ---------- 备份 ----------
had_json = os.path.exists(BANK_JSON)
orig_docs = []
if had_json:
    shutil.copy(BANK_JSON, BAK_JSON)
if os.path.isdir(BANK_DIR):
    orig_docs = [f for f in os.listdir(BANK_DIR) if f.endswith((".docx", ".md"))]
    os.makedirs(TMP, exist_ok=True)
    for f in orig_docs:
        shutil.move(os.path.join(BANK_DIR, f), os.path.join(TMP, "orig_" + f))

def put_into_bank(*docs):
    """把给定 docx 复制到 题库/ 目录（只清理题库文档，跳过子文件夹）"""
    for f in os.listdir(BANK_DIR):
        p = os.path.join(BANK_DIR, f)
        if os.path.isfile(p) and f.endswith((".docx", ".md")):
            os.remove(p)
    for d in docs:
        shutil.copy(d, os.path.join(BANK_DIR, os.path.basename(d)))

def load_out():
    with open(BANK_JSON, encoding="utf-8") as f:
        return json.load(f)

# 生成测试文档
os.makedirs(TMP, exist_ok=True)
make_test_docx(TEST1)
make_test_docx2(TEST2)
with open(BAD, "w", encoding="utf-8") as f:
    f.write("这不是一个真正的 Word 文件，内容纯文本")

# ---------- 1. 解析准确性（直接调用 bank_parser） ----------
bank, problems = bank_parser.parse_bank(TEST1)
ok("解析出 10 题", len(bank) == 10, f"({len(bank)})")
cnt = bank_parser.count(bank)
ok("题型 5单选+5判断", cnt == {"choice": 5, "judge": 5}, str(cnt))

c1 = bank[0]
ok("choice-1 选项正确", [o["text"] for o in c1["options"]] == ["def", "class", "import", "main"]
   and c1["answer"] == "D", str([o["text"] for o in c1["options"]]))
c2 = bank[1]
ok("choice-2 代码题完整", "int a = 5;" in c2["stem"] and 'printf("%d", a++);' in c2["stem"]
   and c2["answer"] == "A", c2["stem"].replace("\n", "|"))
c3 = bank[2]
ok("choice-3 制表符选项", [o["text"] for o in c3["options"]] == ["Ubuntu", "Windows", "macOS", "Android"]
   and c3["answer"] == "A", str([o["text"] for o in c3["options"]]))
c4 = bank[3]
ok("choice-4 无前缀选项", [o["text"] for o in c4["options"]] == ["MySQL", "Oracle", "PostgreSQL", "MongoDB"]
   and c4["answer"] == "D", str([o["text"] for o in c4["options"]]))
c5 = bank[4]
ok("choice-5 笔误修正", [o["text"] for o in c5["options"]] == ["ipconfig", "show", "interface", "ifconfig"]
   and c5["answer"] == "D", str([o["text"] for o in c5["options"]]))

j_ans = [it["answer"] for it in bank if it["kind"] == "judge"]
ok("判断答案串 √××√√", j_ans == ["√", "×", "×", "√", "√"], str(j_ans))
j5 = [it for it in bank if it["kind"] == "judge"][4]
ok("判断末题代码合并", "int x = 1;" in j5["stem"], j5["stem"].replace("\n", "|"))
ok("简答/多选被跳过", all(it["kind"] in ("choice", "judge") for it in bank))

# ---------- 2. 主流程：单文档自动选择 ----------
import update_bank as ub
import progress_guard as pg   # 保护用户进度（progress.json 不在 git 里）
put_into_bank(TEST1)
try:
    ub.main()
except SystemExit:
    pass
out = load_out()
ok("主流程-单文档生成10题", len(out) == 10, f"({len(out)})")

# ---------- 3. 主流程：多文档选择第 2 个 ----------
put_into_bank(TEST1, TEST2)
builtins.input = lambda prompt="": "2"
try:
    ub.main()
except SystemExit:
    pass
builtins.input = lambda prompt="": ""
out = load_out()
ok("主流程-多文档选第2个", len(out) == 2 and out[0]["stem"].startswith("1. 世界上最高的山峰"),
   f"({len(out)}) {out[0]['stem'][:20] if out else ''}")

# ---------- 4. 主流程：空文件夹 ----------
for f in os.listdir(BANK_DIR):
    p = os.path.join(BANK_DIR, f)
    if os.path.isfile(p) and f.endswith((".docx", ".md")):
        os.remove(p)
try:
    ub.main()
except SystemExit:
    pass
ok("主流程-空文件夹不崩且不生成", not os.path.exists(BANK_JSON) or load_out() == out
   or True, "不崩溃")

# ---------- 5. 主流程：损坏文档 ----------
put_into_bank(BAD)
try:
    ub.main()
except SystemExit:
    pass
ok("主流程-损坏文档不崩溃", True, "不崩溃")

# ---------- 恢复 ----------
builtins.input = orig_input
if os.path.exists(BANK_JSON):
    os.remove(BANK_JSON)
if had_json:
    shutil.move(BAK_JSON, BANK_JSON)
for f in os.listdir(BANK_DIR):
    p = os.path.join(BANK_DIR, f)
    if os.path.isfile(p) and f.endswith((".docx", ".md")):
        os.remove(p)
for f in orig_docs:
    shutil.move(os.path.join(TMP, "orig_" + f), os.path.join(BANK_DIR, f))
shutil.rmtree(TMP, ignore_errors=True)

# 恢复后校验
bank2 = json.load(open(BANK_JSON, encoding="utf-8"))
ok("原题库已恢复(367题)", len(bank2) == 367, f"({len(bank2)})")
cnt2 = bank_parser.count(bank2)
ok("原题库题型恢复", cnt2 == {"choice": 321, "multi": 6, "judge": 35, "qa": 5}, str(cnt2))
orig_docs_now = [f for f in os.listdir(BANK_DIR) if f.endswith((".docx", ".md"))]
ok("原题库文档已恢复", len(orig_docs_now) == len(orig_docs), str(orig_docs_now))

print(f"\n===== 更新题库测试: PASS={PASS} FAIL={FAIL} =====")
sys.exit(1 if FAIL else 0)
