# -*- coding: utf-8 -*-
r"""一次读完所有插件 manifest（B1 性能修补）。

问题：工具箱启动时 `discoverPlugins()` 原来是**逐个插件起一个 python 进程**读 manifest。
插件从 1 个涨到 7 个后，冷启动多花 ~1.5-2s（实测 24 页 2574ms，超了 1500ms 的线）。

改法：让工具箱只起**一个** python 进程，把 `检查/*/check.py` 一起跑一遍 --manifest，
一次把全部元信息以 JSON 数组打出来。约定没变（每个插件照样回答 --manifest），
只是"问"的方式从"挨个敲门"变成"一次问完"。

输出（stdout 最后一行）：[{"name":..., "group":..., "desc":..., "report":..., "script":"<绝对路径>"}, ...]
跳过所有读不出 manifest 的目录（插件写坏了最多是这一页不出现）。
"""
import contextlib
import glob
import io
import json
import os
import runpy
import sys


def main():
    here = os.path.dirname(os.path.abspath(__file__))       # 工具箱/检查
    out = []
    for d in sorted(glob.glob(os.path.join(here, '*'))):
        if not os.path.isdir(d):
            continue                                    # 只认 检查/<目录>/check.py
        script = os.path.join(d, 'check.py')
        if not os.path.isfile(script):
            continue
        buf = io.StringIO()
        argv0 = sys.argv[0]
        try:
            sys.argv = [script, '--manifest']
            with contextlib.redirect_stdout(buf):
                runpy.run_path(script, run_name='__main__')
        except SystemExit:
            pass
        except Exception:
            pass
        finally:
            sys.argv = [argv0]
        m = None
        for line in reversed((buf.getvalue() or '').strip().splitlines()):
            try:
                m = json.loads(line)                # manifest 是最后一行 JSON
                break
            except ValueError:
                continue
        if not isinstance(m, dict) or not (m.get('name') or '').strip():
            continue
        m['script'] = script
        out.append(m)
    print(json.dumps(out, ensure_ascii=False))


if __name__ == '__main__':
    main()
