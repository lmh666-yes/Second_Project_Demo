"""注释改写校验：确认"只改注释、不改代码"。

做法：把 HEAD 版本与工作区版本各自去掉注释、压缩空白，然后逐行比对。
代码行有任何差异就报出来，注释怎么改都不影响结论。

用法：
    python 注释改写校验.py                 # 检查默认目录
    python 注释改写校验.py 目录1 目录2 ...  # 检查指定目录（相对仓库根）
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
    r"EnvMonitor\Project_1_检测端\Project",
    r"EnvMonitor\Project_2_数据通信端\Project",
    r"EnvMonitor\QT_project\SerialMonitor",
]
EXTS = {".c", ".h", ".cpp", ".hpp", ".ui", ".pro"}


def decode(raw: bytes) -> str:
    for enc in ("utf-8", "gbk"):
        try:
            return raw.decode(enc)
        except UnicodeDecodeError:
            continue
    return raw.decode("utf-8", "replace")


def strip_comments(text: str) -> str:
    """去掉 // 与 /* */ 注释，保留字符串与字符字面量。"""
    out = []
    i, n = 0, len(text)
    state = 0  # 0=代码 1=行注释 2=块注释 3=字符串 4=字符
    while i < n:
        c = text[i]
        if state == 0:
            if c == "/" and i + 1 < n and text[i + 1] == "/":
                state, i = 1, i + 2
                continue
            if c == "/" and i + 1 < n and text[i + 1] == "*":
                state, i = 2, i + 2
                continue
            if c == '"':
                state = 3
            elif c == "'":
                state = 4
            out.append(c)
            i += 1
            continue
        if state == 1:
            if c == "\n":
                state = 0
                out.append(c)
            i += 1
            continue
        if state == 2:
            if c == "*" and i + 1 < n and text[i + 1] == "/":
                state, i = 0, i + 2
                continue
            if c == "\n":
                out.append(c)
            i += 1
            continue
        out.append(c)  # 字符串/字符内部原样保留
        if c == "\\" and i + 1 < n:
            out.append(text[i + 1])
            i += 2
            continue
        if (state == 3 and c == '"') or (state == 4 and c == "'"):
            state = 0
        i += 1
    return "".join(out)


def normalize(text: str) -> str:
    lines = [re.sub(r"\s+", " ", ln).strip() for ln in strip_comments(text).splitlines()]
    return "\n".join(ln for ln in lines if ln)


def git_show(relpath: str):
    try:
        raw = subprocess.run(
            ["git", "show", "HEAD:" + relpath.replace("\\", "/")],
            cwd=REPO, capture_output=True, check=False,
        )
    except OSError as exc:
        print("调用 git 失败:", exc)
        sys.exit(2)
    if raw.returncode != 0:
        return None
    return decode(raw.stdout)


def main() -> int:
    dirs = sys.argv[1:] or DEFAULT_DIRS
    checked = skipped_untracked = 0
    bad = []
    for d in dirs:
        root = os.path.join(REPO, d)
        for cur, dirs_, files in os.walk(root):
            dirs_[:] = [x for x in dirs_ if x not in ("build_verify", "build_dbg", "RTE", "Objects", "Listings")]
            for fn in files:
                if os.path.splitext(fn)[1].lower() not in EXTS:
                    continue
                full = os.path.join(cur, fn)
                rel = os.path.relpath(full, REPO)
                old = git_show(rel)
                if old is None:
                    skipped_untracked += 1
                    continue
                with open(full, "rb") as fh:
                    new = decode(fh.read())
                checked += 1
                a, b = normalize(old), normalize(new)
                if a != b:
                    al, bl = a.splitlines(), b.splitlines()
                    diff = []
                    for i in range(max(len(al), len(bl))):
                        x = al[i] if i < len(al) else "<缺行>"
                        y = bl[i] if i < len(bl) else "<缺行>"
                        if x != y:
                            diff.append((i + 1, x, y))
                        if len(diff) >= 5:
                            break
                    bad.append((rel, len(al), len(bl), diff))

    print("=== 注释改写校验（只比代码，忽略注释与空白）===")
    print("参与比对的文件: %d 个；HEAD 里不存在（新文件）跳过: %d 个" % (checked, skipped_untracked))
    if not bad:
        print("结论: 所有已跟踪文件的代码部分与 HEAD 完全一致，改写只动了注释。")
        return 0
    print("结论: %d 个文件的代码部分发生变化，需要人工确认：" % len(bad))
    for rel, na, nb, diff in bad:
        print("\n  %s   （代码行数 HEAD %d → 现在 %d）" % (rel, na, nb))
        for ln, x, y in diff:
            print("    第 %d 行代码:" % ln)
            print("      HEAD: %s" % x[:160])
            print("      现在: %s" % y[:160])
    return 1


if __name__ == "__main__":
    sys.exit(main())
