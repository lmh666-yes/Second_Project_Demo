# -*- coding: utf-8 -*-
r"""板级配置读取器（专属版换装口，只读，不改任何文件）。

用途：把各检查器里"写死的板级常量"集中到一份 JSON，换板（换芯片/换工程目录/
换调试口/换 Keil 日志名/换 LoRa 或 Flash 头文件/换进度文档）时**只改这一份**：
    本地工具/工具箱/速查数据/rules/board_envmonitor.json
各检查器（引脚规划 / RTOS / LoRa / 协议校验 / 编码守卫 / 断网缓存 / 进度跟踪）
算出仓库根后调用 load(root) / boards(root) 优先读它；读不到就各自回退内置默认，
行为与老版本完全一致 —— 所以"配置文件没了"不会让检查崩掉或静默失效。

读取口径（与 rtos_check.py 读 freertos_isr_rules.json 同一思路）：
  · 文件缺失 / 解析失败 / 结构不对 → 一律返回 None，**绝不抛异常**；
  · 定位文件：先从传入的 root 拼 本地工具/工具箱/速查数据/rules/board_envmonitor.json，
    再从本文件自身位置逐级上溯兜底（与各脚本 find_root 的兜底同思路），
    工具箱整体挪位置后仍能命中。

只依赖标准库。fallback=False 仅供各脚本 --selftest 模拟"配置缺失/被改名"用，
正常调用不要传（默认 True）。
"""
import json
import os

# 配置文件相对仓库根（以及相对工具箱内各级目录）的路径
REL = os.path.join('本地工具', '工具箱', '速查数据', 'rules', 'board_envmonitor.json')


def _fallback_paths():
    """兜底候选：从本文件所在目录逐级上溯，拼出规则文件可能的位置。"""
    out = []
    d = os.path.dirname(os.path.abspath(__file__))
    for _ in range(6):
        out.append(os.path.join(d, REL))
        out.append(os.path.join(d, '速查数据', 'rules', 'board_envmonitor.json'))
        p = os.path.dirname(d)
        if p == d:
            break
        d = p
    return out


def _paths(root, fallback):
    out = []
    # TOOLBOX_BOARDCFG 排最前：用来做「加一个假工程，看检查器认不认」的验收（D3），
    # 也方便临时换一份配置试跑 —— 不必动仓库里的真配置。
    env = os.environ.get('TOOLBOX_BOARDCFG', '').strip()
    if env:
        out.append(env)
    if root:
        out.append(os.path.join(root, REL))
    if fallback:
        out.extend(_fallback_paths())
    seen, uniq = set(), []
    for p in out:
        k = os.path.normcase(os.path.normpath(p))
        if k not in seen:
            seen.add(k)
            uniq.append(p)
    return uniq


def load(root, fallback=True):
    """读到就返回 dict（配置内容）；读不到 / 解析失败返回 None（不抛异常）。"""
    for p in _paths(root, fallback):
        try:
            with open(p, 'r', encoding='utf-8') as f:
                d = json.load(f)
        except Exception:
            continue
        if isinstance(d, dict):
            return d
    return None


def boards(root, fallback=True):
    """返回 [(板名, 工程相对路径), ...]；读不到返回 None（不抛异常）。"""
    cfg = load(root, fallback)
    if not isinstance(cfg, dict):
        return None
    bl = cfg.get('boards')
    if not isinstance(bl, list):
        return None
    out = []
    for b in bl:
        if isinstance(b, dict) and b.get('name') and b.get('dir'):
            out.append((b['name'], b['dir']))
    return out or None
