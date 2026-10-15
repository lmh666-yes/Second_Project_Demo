# -*- coding: utf-8 -*-
r"""git_backend.py —— Git 上传工具的 git 执行后端。

为什么要有这一层：
    本机环境下 Qt 的 QProcess 无法创建子进程（实测 `started=0`，即使给绝对路径
    也一样），而 Python 的 subprocess 一切正常。工具箱既有工具（进度跟踪、链路仿真等）
    本来就是「Qt 界面 + Python 干活」的分工，这里沿用同一套做法。

约定：所有子命令都输出一行 JSON 到 stdout，便于 Qt 端解析。
     {"ok": true, "code": 0, "out": "...", "data": {...}}
     {"ok": false, "code": 2, "out": "...", "error": "可读原因"}

子命令：
    status   <folder>                     仓库状态
    is-repo  <folder>                     是否 git 仓库
    init     <folder> [branch]            git init + 切分支
    remote   <folder> [url]               读/写 origin（给 url 就是写）
    identity <folder> <name> <email> [--global]
    add      <folder>                     git add -A
    unstage  <folder> <path> [path...]    git reset/rm --cached
    commit   <folder> <message>           git commit -m
    push     <folder> <branch>            推送（临时故障自动重试一次）
    pull     <folder> <branch>            git pull --no-rebase
    staged   <folder>                     列出已暂存的变更
    bigfiles <folder> [--warn N] [--stop N] <path> [path...]   大文件检查
    explain  <文本>                        解读一段 git 错误
    gitver                                 git 版本
"""

import json
import os
import re
import subprocess
import sys

# ----------------------------------------------------------------------
# git 定位
# ----------------------------------------------------------------------


def _find_git():
    """优先 PATH 里的 git，找不到再按常见安装位置找。"""
    candidates = ["git"]
    for env in ("ProgramFiles", "ProgramFiles(x86)", "LOCALAPPDATA"):
        base = os.environ.get(env)
        if not base:
            continue
        candidates += [
            os.path.join(base, "Git", "cmd", "git.exe"),
            os.path.join(base, "Git", "bin", "git.exe"),
            os.path.join(base, "Programs", "Git", "cmd", "git.exe"),
        ]
    candidates += [
        r"C:\msys64\mingw64\bin\git.exe",
        r"C:\Program Files (x86)\Git\cmd\git.exe",
    ]
    for c in candidates:
        try:
            r = subprocess.run([c, "--version"], capture_output=True, timeout=10)
            if r.returncode == 0:
                return c
        except Exception:
            continue
    return None


GIT = _find_git()


def _env():
    e = dict(os.environ)
    # 绝不因交互提示卡住
    e["GIT_TERMINAL_PROMPT"] = "0"
    e["GIT_ASKPASS"] = "echo"
    e["LC_ALL"] = "C.UTF-8"
    # 关键：地址不通 / 需要认证时，git 默认会反复调凭据管理器并多重试，
    # 一次 push 能拖到近 5 分钟。下面三项把它压到「最多试一次、几十秒内出结果」。
    e["GCM_INTERACTIVE"] = "never"          # 凭据管理器不弹窗
    e["GIT_HTTP_LOW_SPEED_LIMIT"] = "1000"
    e["GIT_HTTP_LOW_SPEED_TIME"] = "15"
    return e


def run_git(folder, args, timeout=180):
    """执行 git，返回 (code, text)。输出按 UTF-8 解，含替换符再退回本地编码。"""
    if GIT is None:
        return -1, "未检测到 git。请先安装 Git for Windows：https://git-scm.com/download/win"
    # 给网络类操作加超时，避免连不上的远程把界面卡死（尤其地址打错/断网时）
    cmd = [GIT,
           "-c", "core.quotepath=false",
           "-c", "http.lowSpeedLimit=1000",      # 低于 1KB/s
           "-c", "http.lowSpeedTime=15",          # 持续 15 秒即放弃
           "-c", "http.connectTimeout=15",        # TCP 连接超时
           "-c", "credential.interactive=never",  # 不弹凭据窗
           ] + list(args)
    try:
        r = subprocess.run(cmd, cwd=folder or None, capture_output=True,
                           timeout=timeout, env=_env())
    except subprocess.TimeoutExpired:
        return -2, "git 命令超时（%d 秒）：git %s\n" \
                   "可能是网络不通或远程地址不可达。" % (timeout, " ".join(args))
    except Exception as exc:                      # noqa: BLE001
        return -1, "无法执行 git：%s" % exc

    def dec(b):
        if not b:
            return ""
        s = b.decode("utf-8", "replace")
        if "\ufffd" in s:
            s = b.decode("mbcs" if os.name == "nt" else "latin-1", "replace")
        return s

    text = dec(r.stdout)
    err = dec(r.stderr)
    if err:
        text = (text + "\n" + err) if text else err
    return r.returncode, text


# ----------------------------------------------------------------------
# 可读的失败原因
# ----------------------------------------------------------------------

_PERM = re.compile(r"Authentication|could not read Username|terminal prompts disabled|"
                   r"403|Permission|Invalid username or password|"
                   r"unable to get password from user|"
                   r"could not read Password|no credential|"
                   r"support for password authentication was removed", re.I)
_NONFF = re.compile(r"rejected|non-fast-forward|fetch first", re.I)
_NOTFOUND = re.compile(r"not found|404|does not exist|Repository not found", re.I)


def explain(text):
    if _PERM.search(text):
        return "需要登录 GitHub。请先在任意终端执行一次 git push 完成登录" \
               "（或配置凭据管理器 / PAT），再回到这里重试。"
    if _NONFF.search(text):
        return "远程有本地没有的新提交。先点「拉取合并」把远程同步下来，再重新上传。"
    if _NOTFOUND.search(text):
        return "仓库地址不对或没有权限。请到 GitHub 确认仓库已建好、地址正确、账号有权限。"
    if "CONFLICT" in text:
        return "合并冲突。请在编辑器里解决冲突文件后重新提交。"
    if "nothing to commit" in text:
        return "没有需要提交的变更。"
    if "Please tell me who you are" in text:
        return "git 提交身份未配置。请到「账户设置」填写 user.name / user.email 并保存。"
    if re.search(r"src refspec .* does not match any", text, re.I) \
            or re.search(r"failed to push some refs", text, re.I):
        return "本地还没有任何提交，无法推送。请先提交一次（本工具会自动提交后推送）。"
    if "超时" in text and ("网络" in text or "不可达" in text):
        return "连接远程仓库超时。请检查网络，以及「账户设置」里的仓库地址是否正确。"
    return "请查看下面的 git 原始输出定位问题。"


# ----------------------------------------------------------------------
# 子命令
# ----------------------------------------------------------------------


def cmd_status(folder):
    code, out = run_git(folder, ["rev-parse", "--is-inside-work-tree"])
    if code != 0 or out.strip() != "true":
        return {"isRepo": False}

    st = {"isRepo": True}
    _, tl = run_git(folder, ["rev-parse", "--show-toplevel"])
    st["toplevel"] = tl.strip()

    c, br = run_git(folder, ["symbolic-ref", "--short", "HEAD"])
    if c != 0 or not br.strip():
        c, br = run_git(folder, ["rev-parse", "--abbrev-ref", "HEAD"])
    branch = br.strip()
    if not branch or branch == "HEAD":
        branch = "main"
    st["branch"] = branch

    c, rm = run_git(folder, ["remote", "get-url", "origin"])
    st["remote"] = rm.strip() if c == 0 else ""
    st["hasRemote"] = bool(st["remote"])

    staged = unstaged = untracked = 0
    c, ps = run_git(folder, ["status", "--porcelain"])
    if c == 0:
        for line in ps.splitlines():
            if line.startswith("??"):
                untracked += 1
                continue
            if len(line) < 2:
                continue
            if line[0] not in (" ", "?"):
                staged += 1
            if line[1] not in (" ", "?"):
                unstaged += 1
    st.update(staged=staged, unstaged=unstaged, untracked=untracked)

    ahead = behind = 0
    if st["hasRemote"]:
        upstream = "%s@{upstream}" % branch
        c, lr = run_git(folder, ["rev-list", "--left-right", "--count",
                                 "%s...%s" % (branch, upstream)])
        if c == 0:
            parts = lr.split()
            if len(parts) >= 2:
                ahead, behind = int(parts[0]), int(parts[1])
    st.update(ahead=ahead, behind=behind)

    c, lg = run_git(folder, ["log", "--oneline", "-8"])
    st["recent"] = lg.splitlines() if c == 0 else []

    _, un = run_git(folder, ["config", "user.name"])
    _, ue = run_git(folder, ["config", "user.email"])
    st["userName"] = un.strip()
    st["userEmail"] = ue.strip()
    return st


def cmd_is_repo(folder):
    code, out = run_git(folder, ["rev-parse", "--is-inside-work-tree"])
    return {"isRepo": code == 0 and out.strip() == "true"}


def cmd_init(folder, branch="main"):
    branch = (branch or "main").strip() or "main"
    c1, o1 = run_git(folder, ["init"])
    if c1 != 0:
        return c1, o1
    run_git(folder, ["checkout", "-b", branch])
    run_git(folder, ["symbolic-ref", "HEAD", "refs/heads/" + branch])
    return 0, "已初始化仓库，分支 %s" % branch


_URL_OK = re.compile(r"^(https?://|git@|ssh://|git://|file://|/|[A-Za-z]:[\\/])", re.I)


def cmd_remote(folder, url=None):
    if url:
        url = (url or "").strip()
        if not url:
            return 1, "远程地址为空。"
        if not _URL_OK.match(url):
            return 1, ("远程地址看起来不合法：%s\n"
                       "常见形态：https://github.com/用户名/仓库.git\n"
                       "或        git@github.com:用户名/仓库.git") % url
        c, _ = run_git(folder, ["remote", "get-url", "origin"])
        if c == 0:
            cc, oo = run_git(folder, ["remote", "set-url", "origin", url])
        else:
            cc, oo = run_git(folder, ["remote", "add", "origin", url])
        if cc != 0:
            return cc, oo
        # 连通性探测：ls-remote 只读不推送；失败多为地址错 / 未登录 / 断网。
        # 绑定本身已成功，这里只把探测结果作为提示附上，不判为硬失败。
        lc, lo = run_git(folder, ["ls-remote", "--heads", "origin"], timeout=30)
        if lc == 0:
            return 0, "已绑定远程：%s\n连通性正常。" % url
        return 0, ("已绑定远程：%s\n"
                   "但连通性探测未通过（可先手动登录一次再重试）：\n%s\n%s"
                   % (url, lo.strip(), explain(lo)))
    c, out = run_git(folder, ["remote", "get-url", "origin"])
    # 没绑定 origin 时 git 会以非 0 退出，此时应回空串而不是把错误文本当成地址
    if c != 0:
        return 0, ""
    return 0, out.strip()


def cmd_identity(folder, name, email, global_scope=False):
    scope = ["--global"] if global_scope else []
    c = 0
    o = ""
    if name:
        c, o = run_git(folder, ["config"] + scope + ["user.name", name])
    if email:
        c2, o2 = run_git(folder, ["config"] + scope + ["user.email", email])
        if c2 != 0:
            return c2, o2
    return c, o


def cmd_add(folder):
    """暂存全部变更，但默认排除常见临时/构建产物，避免误提交。
    需要精确控制时用 .gitignore 助手；这里只做一层保守兜底。"""
    args = ["add", "-A", "--", ".",
            ":(exclude)build", ":(exclude)build_verify", ":(exclude)__pycache__",
            ":(exclude).cache", ":(exclude)tmp",
            ":(exclude)*.o", ":(exclude)*.obj", ":(exclude)*.log", ":(exclude)*.tmp"]
    c, o = run_git(folder, args)
    if c == 0:
        note = "（已默认排除 build / build_verify / __pycache__ / tmp / *.o / *.log 等临时产物）"
        o = (o + "\n" + note) if o else note
    return c, o


def cmd_unstage(folder, paths):
    if not paths:
        return 0, ""
    c, _ = run_git(folder, ["rev-parse", "--verify", "HEAD"])
    if c == 0:
        return run_git(folder, ["reset", "-q", "--"] + paths)
    return run_git(folder, ["rm", "--cached", "-q", "-r", "--"] + paths)


def cmd_commit(folder, message):
    return run_git(folder, ["commit", "-m", message])


def cmd_push(folder, branch):
    branch = (branch or "main").strip() or "main"
    c, o = run_git(folder, ["push", "-u", "origin", branch], timeout=90)
    if c == 0:
        return c, o
    # 认证/冲突/不存在/超时：重试也没用，直接给结论（避免界面白等）
    if _PERM.search(o) or _NONFF.search(o) or _NOTFOUND.search(o) or c == -2:
        return c, o
    # 其余归为网络类临时故障，重试一次
    c2, o2 = run_git(folder, ["push", "-u", "origin", branch], timeout=90)
    return c2, ("[重试] 首次推送未完成，已自动重试一次\n" + o2) if c2 == 0 else o2


def cmd_pull(folder, branch=None):
    return run_git(folder, ["pull", "--no-rebase", "origin"], timeout=120)


def cmd_staged(folder):
    c, out = run_git(folder, ["diff", "--cached", "--name-only"])
    files = out.splitlines() if c == 0 else []
    added = modified = deleted = 0
    c2, ns = run_git(folder, ["diff", "--cached", "--name-status"])
    if c2 == 0:
        for line in ns.splitlines():
            if line.startswith("A"):
                added += 1
            elif line.startswith("D"):
                deleted += 1
            elif line.strip():
                modified += 1
    return {"files": files, "added": added, "modified": modified, "deleted": deleted}


def cmd_bigfiles(folder, paths, warn_mb=50, stop_mb=100):
    big = []
    for rel in paths:
        p = os.path.join(folder, rel)
        if not os.path.isfile(p):
            continue
        try:
            n = os.path.getsize(p)
        except OSError:
            continue
        if n > warn_mb * 1024 * 1024:
            big.append({"path": rel, "bytes": n, "hard": n > stop_mb * 1024 * 1024})
    return {"big": big}


def cmd_gitver():
    if GIT is None:
        return 1, "未检测到 git"
    return 0, subprocess.run([GIT, "--version"], capture_output=True).stdout.decode(
        "utf-8", "replace").strip()


# ----------------------------------------------------------------------
# 入口
# ----------------------------------------------------------------------


def main(argv):
    if not argv:
        emit(False, 2, "缺少子命令")
        return 2

    cmd = argv[0]
    a = argv[1:]
    try:
        data = {}
        if cmd == "gitver":
            code, out = cmd_gitver()
        elif cmd == "status":
            code, out, data = 0, "", cmd_status(a[0])
            data = {"state": data}
        elif cmd == "is-repo":
            code, out, data = 0, "", cmd_is_repo(a[0])
            data = {"state": data}
        elif cmd == "init":
            code, out = cmd_init(a[0], a[1] if len(a) > 1 else "main")
        elif cmd == "remote":
            if len(a) > 1 and a[1]:
                code, out = cmd_remote(a[0], a[1])
            else:
                code, out = cmd_remote(a[0])
        elif cmd == "identity":
            g = "--global" in a
            rest = [x for x in a if x != "--global"]
            code, out = cmd_identity(rest[0], rest[1] if len(rest) > 1 else "",
                                     rest[2] if len(rest) > 2 else "", g)
        elif cmd == "add":
            code, out = cmd_add(a[0])
        elif cmd == "unstage":
            code, out = cmd_unstage(a[0], a[1:])
        elif cmd == "commit":
            code, out = cmd_commit(a[0], a[1])
        elif cmd == "push":
            code, out = cmd_push(a[0], a[1] if len(a) > 1 else "main")
        elif cmd == "pull":
            code, out = cmd_pull(a[0], a[1] if len(a) > 1 else None)
        elif cmd == "staged":
            code, out, data = 0, "", cmd_staged(a[0])
            data = {"staged": data}
        elif cmd == "bigfiles":
            folder, warn, stop, paths = a[0], 50, 100, []
            rest, i = a[1:], 0
            while i < len(rest):
                if rest[i] == "--warn" and i + 1 < len(rest) and rest[i + 1].isdigit():
                    warn = int(rest[i + 1]); i += 2
                elif rest[i] == "--stop" and i + 1 < len(rest) and rest[i + 1].isdigit():
                    stop = int(rest[i + 1]); i += 2
                else:
                    paths.append(rest[i]); i += 1
            code, out, data = 0, "", cmd_bigfiles(folder, paths, warn, stop)
            data = {"big": data["big"]}
        elif cmd == "explain":
            code, out, data = 0, explain(a[0]), {"reason": explain(a[0])}
        else:
            emit(False, 2, "未知子命令：%s" % cmd)
            return 2

        ok = code == 0
        # 失败时把「可读原因」放在顶层 error，同时保留在 data.error 里向后兼容
        err = None
        if not ok and cmd not in ("explain", "gitver"):
            err = explain(out)
            data = dict(data, error=err)
        emit(ok, code, out, data, err)
        return 0 if ok else 1
    except IndexError:
        emit(False, 2, "参数不足：%s" % cmd)
        return 2
    except Exception as exc:                       # noqa: BLE001
        emit(False, 1, "内部错误：%s" % exc)
        return 1


def emit(ok, code, out, data=None, error=None):
    payload = {"ok": bool(ok), "code": code, "out": out or ""}
    if data:
        payload["data"] = data
    if error:
        payload["error"] = error
    sys.stdout.write(json.dumps(payload, ensure_ascii=False))
    sys.stdout.write("\n")
    sys.stdout.flush()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
