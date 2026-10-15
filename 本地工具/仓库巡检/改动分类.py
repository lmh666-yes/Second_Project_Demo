"""改动分类：把"相对某个 git 版本被改过的文件"分成三类。

分类依据（从宽到严）：
  1. 代码改动 —— 去掉注释、压掉空白后仍不同（含字符串内容变化）
  2. 仅字符串/文案 —— 去注释 + 把字符串字面量掩码成 @@ 后相同，只有字面量内容不同
  3. 仅注释 —— 去注释后完全相同，只有注释内容变化

用途：盘点"这一轮到底动了哪些文件的代码，哪些只是注释/文案"，给改动清单当依据。

用法：
    python 改动分类.py <git 版本> [目录 ...]
    python 改动分类.py HEAD                       # 默认三个目录，对照 HEAD
    python 改动分类.py HEAD EnvMonitor/Project_2_数据通信端
输出写到 stdout（UTF-8），也可自行重定向。
"""

import os
import re
import subprocess
import sys

def _find_repo(start):
    """向上找含 .git 或 EnvMonitor 的目录作仓库根——脚本挪到哪一层都能用，不写死盘符。"""
    d = os.path.abspath(start)
    while True:
        if os.path.isdir(os.path.join(d, '.git')) or os.path.isdir(os.path.join(d, 'EnvMonitor')):
            return d
        p = os.path.dirname(d)
        if p == d:
            return os.path.abspath(start)
        d = p


REPO = _find_repo(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DIRS = [
    r"EnvMonitor\Project_1_检测端",
    r"EnvMonitor\Project_2_数据通信端",
    r"EnvMonitor\QT_project\SerialMonitor",
]
EXTS = {".c", ".h", ".cpp", ".hpp", ".ui", ".pro", ".js", ".py", ".ps1"}


def decode(raw: bytes) -> str:
    for enc in ("utf-8", "gbk"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", "replace")


def strip_comments(text: str) -> str:
    """去掉 // 与 /* */ 注释，保留字符串与字符字面量内容。"""
    out = []
    i, n = 0, len(text)
    state = 0  # 0=代码 1=行注释 2=块注释 3=字符串 4=字符
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""
        if state == 0:
            if c == "/" and nxt == "/":
                state = 1
                i += 2
                continue
            if c == "/" and nxt == "*":
                state = 2
                i += 2
                continue
            if c == '"':
                state = 3
            elif c == "'":
                state = 4
            out.append(c)
        elif state == 1:
            if c == "\n":
                state = 0
                out.append(c)
        elif state == 2:
            if c == "*" and nxt == "/":
                state = 0
                i += 2
                continue
        elif state in (3, 4):
            out.append(c)
            if c == "\\":
                if i + 1 < n:
                    out.append(text[i + 1])
                    i += 2
                    continue
            elif (state == 3 and c == '"') or (state == 4 and c == "'"):
                state = 0
        i += 1
    return "".join(out)


def mask_strings(text: str) -> str:
    """把字符串/字符字面量的内容掩码成 @@（引号保留），用于区分"只改文案"。"""
    out = []
    i, n = 0, len(text)
    state = 0
    while i < n:
        c = text[i]
        if state == 0:
            if c == '"':
                state = 3
                out.append(c)
                out.append("@@")
            elif c == "'":
                state = 4
                out.append(c)
                out.append("@@")
            else:
                out.append(c)
            i += 1
            continue
        if c == "\\":
            i += 2
            continue
        if (state == 3 and c == '"') or (state == 4 and c == "'"):
            state = 0
            out.append(c)
        i += 1
    return "".join(out)


def squeeze(text: str) -> str:
    return re.sub(r"\s+", " ", text).strip()


def git(*args) -> bytes:
    r = subprocess.run(["git"] + list(args), cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL)
    return r.stdout


def changed_files(rev: str, path: str):
    out = git("-c", "core.quotepath=false", "diff", "--name-status", rev, "--", path)
    rows = []
    for line in decode(out).splitlines():
        if not line.strip():
            continue
        parts = line.split("\t")
        st = parts[0][0]
        f = parts[-1].strip()
        rows.append((st, f))
    return rows


def code_of(rev: str, path: str):
    raw = git("show", "%s:%s" % (rev, path.replace("\\", "/")))
    return decode(raw)


def main() -> int:
    rev = sys.argv[1] if len(sys.argv) > 1 else "HEAD"
    dirs = sys.argv[2:] or DEFAULT_DIRS
    for d in dirs:
        rows = changed_files(rev, d.replace("\\", "/"))
        code_chg, text_chg, cmt_chg, other = [], [], [], []
        for st, f in rows:
            ext = os.path.splitext(f)[1].lower()
            if st != "M" or ext not in EXTS:
                other.append((st, f))
                continue
            try:
                old = code_of(rev, f)
            except Exception:
                other.append((st, f))
                continue
            new_path = os.path.join(REPO, f.replace("/", os.sep))
            if not os.path.exists(new_path):
                other.append((st, f))
                continue
            new = decode(open(new_path, "rb").read())
            o_nc, n_nc = squeeze(strip_comments(old)), squeeze(strip_comments(new))
            if o_nc == n_nc:
                cmt_chg.append(f)
            elif squeeze(mask_strings(o_nc)) == squeeze(mask_strings(n_nc)):
                text_chg.append(f)
            else:
                code_chg.append(f)
        print("=" * 72)
        print("目录 %s    对照 %s" % (d, rev))
        print("  代码逻辑改动   %3d 个文件" % len(code_chg))
        print("  仅字符串/文案  %3d 个文件" % len(text_chg))
        print("  仅注释         %3d 个文件" % len(cmt_chg))
        if other:
            print("  跳过（非 M 或扩展名不在表内） %d 个：%s" % (len(other), ", ".join(o[1] for o in other[:8])))
        print("  --- 代码逻辑改动清单 ---")
        for f in code_chg:
            print("    " + f)
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    raise SystemExit(main())
