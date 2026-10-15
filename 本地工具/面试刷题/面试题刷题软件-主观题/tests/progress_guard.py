# -*- coding: utf-8 -*-
"""测试用进度保护：安全备份 / 恢复用户的 progress.json。

背景：刷题软件的进度文件（progress.json）不在 git 里，一旦被测试覆盖就无法找回。
用法：
    import progress_guard as pg
    had = pg.backup(ss.PROG_PATH)     # 测试开始处调用
    ...
    pg.restore()                      # 测试结束处调用（建议放 finally）
    if not had and os.path.exists(ss.PROG_PATH):
        os.remove(ss.PROG_PATH)       # 原本没有进度文件 → 收尾删掉测试留下的
"""
import atexit
import os
import shutil

_prog = None
_tmp = None


def backup(path):
    """备份 path 到 '<path>.testbak.<pid>'；返回原本是否存在该文件"""
    global _prog, _tmp
    _prog, _tmp = path, None
    if not os.path.exists(path):
        return False
    _tmp = f"{path}.testbak.{os.getpid()}"
    shutil.copy2(path, _tmp)
    atexit.register(restore)          # 异常退出也能恢复
    return True


def restore():
    global _tmp
    if _tmp and os.path.exists(_tmp):
        try:
            shutil.copy2(_tmp, _prog)
        except Exception:
            pass
        try:
            os.remove(_tmp)
        except OSError:
            pass
    _tmp = None
