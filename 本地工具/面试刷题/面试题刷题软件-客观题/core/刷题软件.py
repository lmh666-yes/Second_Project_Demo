# -*- coding: utf-8 -*-
"""面试题刷题软件 · 客观题（单选/多选/判断）
题库来源：题库.json（由 build_bank.py 从 题库/*.md 生成）
功能：顺序/错题/考试；判分+解析+统计+错题本+笔记+重置进度。
错题机制（2.7.0）：累计错误次数永久记录；答错即入错题集（寿命初始 1）；
                 错误次数达 3/5/7/9 或 >10 时寿命 +1；
                 只有「错题考试」答对才寿命 -1，归零后移除（顺序/普通考试答对不清除）。
错题考试：从错题集随机抽 ≤10 题，选项随机，规则同普通考试（40 分钟/5 分每题/可暂停/成绩页）。
考试：30 题（选择题 20 含多选 + 判断 10）/ 40 分钟 / 每题 5 分；右上角开始考试；可暂停/退出；
      中断自动保存、下次打开恢复；出成绩后可点击错题号回顾；考试中禁止切换板块。
版本：2.7.0（面试题库版）"""
import sys, io, os, json, random, time
import tkinter as tk
from tkinter import ttk, messagebox

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)          # 上一级 = C刷题软件 根目录
BANK_PATH = os.path.join(ROOT, "题库.json")
PROG_PATH = os.path.join(ROOT, "progress.json")

COLOR_OK = "#1a7f37"
COLOR_NO = "#c62828"
COLOR_BLUE = "#1a5276"
EXAM_MIN = 40          # 考试时长（分钟）
EXAM_NUM = 30          # 考试总题数（20 单选 + 10 判断）
EXAM_CHOICE_NUM = 20   # 考试中单选题数
EXAM_JUDGE_NUM = 10    # 考试中判断题数
EXAM_SCORE = 5         # 每题分值
WEXAM_NUM = 10         # 错题考试抽题数（不足 10 道就全部抽）


def load_bank():
    """加载题库；损坏/空/缺失时返回 []（由调用方提示），不崩溃"""
    if not os.path.exists(BANK_PATH):
        return []
    try:
        with open(BANK_PATH, encoding="utf-8") as f:
            data = json.load(f)
        return data if isinstance(data, list) else []
    except Exception:
        return []


def load_progress():
    if os.path.exists(PROG_PATH):
        try:
            with open(PG_PATH := PROG_PATH, encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            pass
    return {}


def migrate_progress(progress):
    """旧数据兼容：补全错题机制字段（in_wrong / life / wrong_count）。
    错题集从旧规则（ok === false）推导一次；返回 (progress, 是否有改动)"""
    changed = False
    for qid, rec in progress.items():
        if not isinstance(rec, dict) or qid.startswith("_"):
            continue
        if "in_wrong" not in rec:
            rec["in_wrong"] = (rec.get("ok") is False)
            rec["life"] = 1 if rec["in_wrong"] else 0
            changed = True
        if "wrong_count" not in rec:
            rec["wrong_count"] = 0
            changed = True
        if not rec.get("in_wrong") and rec.get("life", 0) != 0:
            rec["life"] = 0
            changed = True
    return progress, changed


def plan_take(ids, plan, order_key, cursor_key, n):
    """计划式取段（模块级纯函数，便于测试）——环形连续段算法：
    将 ids 打乱成固定序列存于 plan[order_key]，每场从 plan[cursor_key] 起
    沿环形（到头回绕）连续取 n 个。数学性质：
    · 同一场考试绝不重复（取的是环上一段连续区间，内部元素唯一）；
    · 连续 ⌈len(ids)/n⌉+1 场必覆盖全部题目（如 176 题每场 20 题 → 连续 9 场全覆盖）；
    · 每题出现间隔恒定（⌈len(ids)/n⌉ 场左右），不出现长期漏考。"""
    n = max(0, min(int(n), len(ids)))
    if n == 0:
        return []
    order = plan.get(order_key)
    if not isinstance(order, list) or set(order) != set(ids):
        order = ids[:]
        random.shuffle(order)
        plan[cursor_key] = 0
    m = len(order)
    cur = plan.get(cursor_key, 0)
    if not isinstance(cur, int) or cur < 0 or cur >= m:
        cur = 0
    if n >= m:
        plan[order_key], plan[cursor_key] = order, 0
        return order[:]
    end = cur + n
    if end <= m:
        picked = order[cur:end]
    else:
        picked = order[cur:] + order[:end - m]
    plan[order_key], plan[cursor_key] = order, end % m
    return picked


def exam_pick_ids(bank, progress):
    """考试抽题：选择题（单选+多选）20 个、判断题 10 个，计划式抽题不重复覆盖"""
    choices = [it for it in bank if it.get("kind") in ("choice", "multi")]
    judges = [it for it in bank if it.get("kind") == "judge"]
    if not choices or not judges:
        return []
    sig = f"{len(bank)}:{bank[0].get('id')}:{bank[-1].get('id')}"
    plan = progress.get("_exam_plan")
    if not isinstance(plan, dict) or plan.get("sig") != sig:
        plan = {"sig": sig}
    picked_c = plan_take([it["id"] for it in choices], plan, "c", "cc", EXAM_CHOICE_NUM)
    picked_j = plan_take([it["id"] for it in judges], plan, "j", "jc", EXAM_JUDGE_NUM)
    progress["_exam_plan"] = plan
    ids = picked_c + picked_j
    random.shuffle(ids)
    return ids


def build_exam_queue(bank, ids):
    """按抽题顺序生成考试队列（保持打乱后的题目顺序，不再回退题库原顺序）"""
    by_id = {it.get("id"): it for it in bank}
    return [by_id[qid] for qid in ids if qid in by_id]


def build_exam_opts(queue):
    """为考试中的每道选择/多选题生成打乱的选项顺序与新答案 key（仅考试使用，不改原题库）。
    思路：记录答案选项的原位置索引，重排后按新位置分配 A/B/C/D/E，答案字母跟随内容移动；
    多题的多个答案全部按原索引映射，选项文本重复也不会错。"""
    opts_map = {}
    for it in queue:
        if it.get("kind") not in ("choice", "multi"):
            continue
        src = it.get("options", [])
        ans_key = str(it.get("answer", "")).strip().upper()
        ans_idx = [i for i, o in enumerate(src) if str(o.get("key", "")).upper() in ans_key]
        if not ans_idx:
            ans_idx = [0]        # 兜底
        shuffled = [dict(o, _i=i) for i, o in enumerate(src)]
        random.shuffle(shuffled)
        new_keys = []
        for i, o in enumerate(shuffled):
            o["key"] = "ABCDE"[i] if i < 5 else "?"
            if o.get("_i") in ans_idx:
                new_keys.append(o["key"])
            o.pop("_i", None)
        opts_map[it.get("id")] = {"options": shuffled,
                                  "answer": "".join(sorted(new_keys))}
    return opts_map


class App:
    def __init__(self, root):
        self.root = root
        self.root.title("面试题刷题软件 · 客观题")
        self._fit_window(720)
        self.bank = load_bank()
        if not self.bank:
            messagebox.showerror("题库错误",
                                 "题库为空或损坏，请重新运行 build_bank.py 生成题库。")
        self.progress, _migrated = migrate_progress(load_progress())
        if _migrated:
            self._save_progress()
        # 考试会话（持久化在 progress["exam"]，强制关闭后可恢复）
        ex = self.progress.get("exam")
        self.exam = ex if isinstance(ex, dict) else {}
        self.mode = "顺序"
        self.queue = []
        self.idx = 0
        self._seq_saved_idx = None      # 顺序板块本次会话中的位置（切板块返回时用）
        self._resume_notice = False     # 恢复进度时在题头提示一次
        self.choice_var = tk.StringVar()
        self._choice_rows = []           # 单选选项行（自定义选中态）
        self.exam_left = 0
        self._exam_timer = None
        self._note_save_job = None
        self._closing = False          # 窗口销毁中：不再挂新的 after 定时器
        self.note_win = None          # 笔记独立窗口
        self.note_text = None
        self.note_title = None
        self._build_ui()
        # 有未完成/未结算的考试 → 直接进入考试板块恢复
        if self.exam.get("active"):
            self.set_mode("考试")
        else:
            self.set_mode("顺序")
        self.show_question()
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.bind("<Destroy>", self._on_root_destroy, add="+")
        self._bind_keys()

    def _fit_window(self, base_h):
        """窗口自适应屏幕大小并居中（默认给出较大的初始尺寸，之后可自由缩放）"""
        try:
            self.root.update_idletasks()
            sw = self.root.winfo_screenwidth()
            sh = self.root.winfo_screenheight()
            w = max(1020, min(1760, int(sw * 0.9), sw - 40))
            h = max(680, min(base_h + 400, int(sh * 0.9), sh - 60))
            x = max(0, (sw - w) // 2)
            y = max(0, (sh - h) // 2 - 20)
            self.root.geometry(f"{w}x{h}+{x}+{y}")
        except Exception:
            self.root.geometry(f"1280x{base_h}")
        try:
            self.root.minsize(960, 620)
        except Exception:
            pass

    def _nav_allowed(self):
        """快捷翻题/作答仅在顺序板块或考试进行中生效"""
        return self.mode == "顺序" or self._exam_running()

    # ---------- 进度记忆 ----------
    def _q_done(self, it):
        """该题是否已完成：选择题/判断题已作答，简答/问答题已标记掌握/复习"""
        pr = self.progress.get(it.get("id", ""))
        if not isinstance(pr, dict):
            return False
        return pr.get("ok") is not None

    def _resume_index(self):
        """记忆位置 = 连续完成题目的最后一题。
        例：做了 1、2、3、4，跳过 5 又做了 6、7、8 → 回到第 4 题。"""
        last = -1
        for i, it in enumerate(self.queue):
            if self._q_done(it):
                last = i
            else:
                break
        return last if last >= 0 else 0

    def _bind_keys(self):
        """键盘快捷键：← 上一题 / → 下一题 / 回车 确认答案 / 1-4 或 A-D 选择选项
        （焦点在输入框/笔记时自动让位，不干扰打字）"""
        def wrap(fn, guard=None):
            def _h(_e):
                try:
                    w = self.root.focus_get()
                    if isinstance(w, (tk.Text, tk.Entry)):
                        return ""
                except Exception:
                    pass
                if guard is not None and not guard():
                    return ""
                fn()
                return "break"
            return _h
        self.root.bind("<Left>", wrap(self.prev_q, self._nav_allowed))
        self.root.bind("<Right>", wrap(self.next_q, self._nav_allowed))
        self.root.bind("<Return>", wrap(self.check, self._nav_allowed))
        for i, k in enumerate("ABCD", 1):
            self.root.bind(f"<Key-{i}>",
                           wrap(lambda kk=k: self._quick_pick(kk), self._nav_allowed))
            self.root.bind(f"<Key-{k.lower()}>",
                           wrap(lambda kk=k: self._quick_pick(kk), self._nav_allowed))

    def _quick_pick(self, key):
        """键盘快捷选择选项（1-4 / A-D），仅选择不提交；回车提交"""
        it = self._cur_item()
        if it is None or it.get("kind") not in ("choice", "multi"):
            return
        if key not in [o.get("key") for o in it.get("options", [])]:
            return
        if self.mode == "考试" and self.exam.get("answers", {}).get(it.get("id")) is not None:
            return
        self._select_option(key)

    def _on_close(self):
        """关闭程序：考试中先提醒；是则保存退出，否则继续"""
        if self.mode == "考试" and self._exam_active() and not self._exam_finished():
            self._stop_exam_timer()
            self.exam["paused"] = True
            self.exam["left"] = self.exam_left
            self._save_exam()
            if not messagebox.askyesno(
                    "退出程序",
                    "当前正在考试，确定要退出吗？\n（考试已自动保存为暂停，下次打开会直接回到考试）"):
                # 点“否”：恢复考试继续
                self.exam["paused"] = False
                self._save_exam()
                self._exam_tick()
                self._exam_render_q()
                return
        self._save_note()
        self._save_progress()
        self.root.destroy()

    # ---------- UI ----------
    def _bind_hover(self, widget, base, hover):
        """鼠标悬停变色（动态效果）"""
        def on_enter(_e):
            try:
                widget.config(bg=hover)
            except Exception:
                pass

        def on_leave(_e):
            try:
                widget.config(bg=base)
            except Exception:
                pass
        widget.bind("<Enter>", on_enter)
        widget.bind("<Leave>", on_leave)

    def _is_row_selected(self, key):
        """选中判断：单选存单个字母；多选存排序后的字母串（包含即选中）"""
        return key in self.choice_var.get()

    def _build_ui(self):
        self.root.grid_rowconfigure(1, weight=1)
        self.root.grid_columnconfigure(0, weight=1)
        # 顶部栏
        bar = tk.Frame(self.root, bg="#2c3e50")
        bar.grid(row=0, column=0, sticky="ew")
        tk.Label(bar, text="🎯 面试题刷题（客观题）", bg="#2c3e50", fg="white",
                 font=("Microsoft YaHei", 13, "bold"), padx=12).pack(side=tk.LEFT, pady=6)
        self.mode_btns = {}
        for m in ("顺序", "错题", "考试"):
            b = tk.Button(bar, text=m, command=lambda mm=m: self.set_mode(mm),
                          relief=tk.FLAT, padx=10, cursor="hand2",
                          font=("Microsoft YaHei", 10))
            b.pack(side=tk.LEFT, padx=3, pady=6)
            self.mode_btns[m] = b
        # 右上角：考试上下文按钮区（开始/暂停/退出/重新考试）
        self.exam_bar = tk.Frame(bar, bg="#2c3e50")
        self.exam_bar.pack(side=tk.RIGHT, padx=8)
        self.stat_label = tk.Label(bar, text="", bg="#2c3e50", fg="#ecf0f1",
                                   font=("Microsoft YaHei", 10, "bold"))
        self.stat_label.pack(side=tk.RIGHT, padx=12)

        # 主体：左=题目区，右=考试题号导航（仅考试时显示）
        body = tk.Frame(self.root, bg="#f5f7fa")
        body.grid(row=1, column=0, sticky="nsew")
        body.grid_rowconfigure(0, weight=1)
        body.grid_columnconfigure(0, weight=3)
        body.grid_columnconfigure(1, weight=1)

        left = tk.Frame(body, bg="#f5f7fa")
        left.grid(row=0, column=0, sticky="nsew")

        self.exam_nav = tk.Frame(body, bg="#f8f9fa", bd=1, relief=tk.GROOVE)
        self.exam_nav.grid(row=0, column=1, sticky="nsew", padx=(6, 0))
        self.exam_nav.grid_remove()          # 默认隐藏，仅考试时显示
        self._nav_last_w = 0
        self._nav_after = None
        self.root.bind("<Configure>", lambda e: self._root_on_configure())

        self.head_label = tk.Label(left, text="", bg="#eaf2f8", fg=COLOR_BLUE,
                                   font=("Microsoft YaHei", 12, "bold"),
                                   anchor="w", padx=12, pady=6)
        self.head_label.pack(fill=tk.X)
        # 题干区：高度随内容自适应，超过上限出现滚动条（长题干不丢内容）
        self.stem_frame = tk.Frame(left, bg="#ffffff")
        self.stem_frame.pack(fill=tk.X, pady=(4, 0))
        self.stem_frame.grid_columnconfigure(0, weight=1)
        self.stem_sb = tk.Scrollbar(self.stem_frame, orient="vertical")
        self.stem = tk.Text(self.stem_frame, font=("Consolas", 12), wrap="char",
                            bg="#ffffff", relief=tk.FLAT, padx=14, pady=8,
                            height=7, state=tk.DISABLED,
                            yscrollcommand=self.stem_sb.set)
        self.stem_sb.config(command=self.stem.yview)
        self.stem.grid(row=0, column=0, sticky="ew")
        self._stem_text = ""
        self._stem_w = 0
        self._stem_after = None
        self.stem.bind("<Configure>", self._stem_on_configure)

        # 选项区 + 解析区：放进「可上下拖动的分栏」——拖动中间的分隔条即可放大解析区
        self.split = ttk.Panedwindow(left, orient=tk.VERTICAL)
        self.split.pack(fill=tk.BOTH, expand=True, padx=10, pady=(2, 4))
        self._sash_user = False
        self._img_expand = False          # 当前题带配图 → 选项区自动占更大比例
        self._img_after = None            # 配图延迟绘制定时器
        self._img_widgets = []            # 配图相关控件（便于重绘/销毁）
        self.split.bind("<Configure>", self._on_split_configure)
        self.split.bind("<B1-Motion>", self._on_sash_drag)

        self.opt_wrap_frame = tk.Frame(self.split, bg="#f5f7fa")
        self.split.add(self.opt_wrap_frame, weight=58)
        self.opt_wrap_frame.grid_rowconfigure(0, weight=1)
        self.opt_wrap_frame.grid_columnconfigure(0, weight=1)
        self.opt_canvas = tk.Canvas(self.opt_wrap_frame, bg="#f5f7fa",
                                    highlightthickness=0, bd=0)
        self.opt_sb = ttk.Scrollbar(self.opt_wrap_frame, orient="vertical",
                                    command=self.opt_canvas.yview)
        self.opt_canvas.configure(yscrollcommand=self.opt_sb.set)
        self.opt_canvas.grid(row=0, column=0, sticky="nsew")
        self.opt_frame = tk.Frame(self.opt_canvas, bg="#f5f7fa")
        self._opt_win = self.opt_canvas.create_window((0, 0), window=self.opt_frame, anchor="nw")
        self.opt_frame.bind("<Configure>", self._on_opt_frame_configure)
        self.opt_canvas.bind("<Configure>", self._on_opt_canvas_configure)
        self._wrap_widgets = []          # 需要按窗口宽度自动换行的标签
        self._sync_job = None
        self.opt_widgets = []

        # 解析区：与选项区同处一个可上下拖动的分栏（默认高度约占 42%）
        # 内容超出可视区时自动出现垂直滚动条；拖动分隔条可随时放大/缩小
        self.fb_frame = tk.Frame(self.split, bg="#f5f7fa")
        self.split.add(self.fb_frame, weight=42)
        self.fb_frame.grid_columnconfigure(0, weight=1)
        self.fb_frame.grid_rowconfigure(1, weight=1)
        self.fb_head = tk.Label(
            self.fb_frame,
            text="📖 答案 / 解析（拖动上方分隔条可放大此区域，内容可滚动）",
            bg="#eaf2f8", fg=COLOR_BLUE, font=("Microsoft YaHei", 9, "bold"),
            anchor="w", padx=8, pady=2)
        self.fb_head.grid(row=0, column=0, columnspan=2, sticky="ew")
        self.fb_sb = tk.Scrollbar(self.fb_frame, orient="vertical")
        self.fb = tk.Text(self.fb_frame, font=("Microsoft YaHei", 11), wrap="char",
                          height=8, relief=tk.GROOVE, bd=1, padx=10, pady=6,
                          yscrollcommand=self._fb_on_scroll)
        self.fb_sb.config(command=self.fb.yview)
        self.fb.grid(row=1, column=0, sticky="nsew")
        self._fb_w = 0
        self._fb_h = 0
        self._fb_after = None
        self.fb.bind("<Configure>", self._fb_on_configure)

        # 底部导航
        nav = tk.Frame(self.root, bg="#f0f0f0")
        nav.grid(row=2, column=0, sticky="ew")
        self.nav_btns = []
        hover_map = {COLOR_BLUE: "#2471a3", COLOR_NO: "#e74c3c", "#8e44ad": "#a569bd"}
        for txt, fn, color in (("◀ 上一题", self.prev_q, COLOR_BLUE),
                               ("确认答案", self.check, COLOR_BLUE),
                               ("下一题 ▶", self.next_q, COLOR_BLUE),
                               ("📝 错题考试", self.start_wrong_exam, "#d35400"),
                               ("📝 我的笔记", self._open_note, "#8e44ad"),
                               ("🗑 重置进度", self.reset_progress, COLOR_NO)):
            b = tk.Button(nav, text=txt, command=fn, bg=color, fg="white",
                          cursor="hand2", padx=12, pady=5,
                          font=("Microsoft YaHei", 10))
            b.pack(side=tk.LEFT, padx=4, pady=6)
            self._bind_hover(b, color, hover_map.get(color, color))
            self.nav_btns.append((b, txt))

    # ---------- 模式 ----------
    def set_mode(self, mode):
        self._save_note()
        # 考试中禁止切换其他板块
        if (self.mode == "考试" and mode != "考试"
                and self._exam_active() and not self._exam_finished()):
            messagebox.showwarning("考试中", "考试进行中，不能切换到其他板块！\n请先交卷或退出考试。")
            return
        self._stop_exam_timer()
        if self.mode == "顺序" and mode != "顺序":
            self._seq_saved_idx = self.idx      # 记住本次浏览位置，回到顺序板块时恢复
        self.mode = mode
        for m, b in self.mode_btns.items():
            b.config(bg="#34495e", fg="white")
        self.mode_btns[mode].config(bg="#1a5276", fg="white")
        if mode == "顺序":
            self.queue = list(self.bank)
        elif mode == "错题":
            wrong = [it for it in self.bank
                     if self._rec_get(it.get("id", "")).get("in_wrong")]
            self.queue = wrong
            if not wrong:
                messagebox.showinfo("错题本", "太棒了，当前没有错题！\n\n"
                                            "（提示：错题需要在「错题考试」中答对、"
                                            "寿命归零后才会从错题本移除）")
        elif mode == "考试":
            self._enter_exam_board()
            return
        # 非考试板块：清空右上角考试按钮
        for w in self.exam_bar.winfo_children():
            w.destroy()
        self.exam_nav.grid_remove()
        # 顺序板块：首次进入（打开软件）跳到"连续完成题目的最后一题"；之后回到上次浏览位置
        if mode == "顺序":
            self.idx = self._resume_index() if self._seq_saved_idx is None \
                else self._seq_saved_idx
            if self.queue:
                self.idx = max(0, min(self.idx, len(self.queue) - 1))
            self._resume_notice = bool(self.idx > 0 and self._seq_saved_idx is None)
        else:
            self.idx = 0
        self.show_question()

    # ---------- 考试 ----------
    def _stop_exam_timer(self):
        """取消考试倒计时定时器"""
        if self._exam_timer:
            try:
                self.root.after_cancel(self._exam_timer)
            except Exception:
                pass
            self._exam_timer = None

    def _exam_active(self):
        return bool(self.exam.get("active"))

    def _exam_finished(self):
        return self._exam_active() and bool(self.exam.get("finished"))

    def _exam_running(self):
        return self._exam_active() and not self.exam.get("finished") and not self.exam.get("paused")

    def _exam_paused(self):
        return self._exam_active() and not self.exam.get("finished") and bool(self.exam.get("paused"))

    def _exam_qlist(self):
        """考试题目列表：按抽题顺序（exam.ids）返回，与题号导航/跳转顺序完全一致"""
        by_id = {it.get("id"): it for it in self.bank}
        qs = [by_id[qid] for qid in self.exam.get("ids", []) if qid in by_id]
        if qs:
            return qs
        ids = set(self.exam.get("ids", []))
        return [it for it in self.bank if it.get("id") in ids]

    def _by_id(self, qid):
        for it in self.bank:
            if it.get("id") == qid:
                return it
        return None

    def _save_exam(self):
        self.progress["exam"] = self.exam
        self._save_progress()

    def _enter_exam_board(self):
        """进入考试板块：按会话状态显示 待开始/继续/成绩"""
        if not self._exam_active():
            self._exam_show_ready()
        elif self._exam_finished():
            self._exam_show_result_page()
        else:
            self.queue = self._exam_qlist()
            self.idx = self.exam.get("idx", 0)
            if self.idx >= len(self.queue):
                self.idx = len(self.queue) - 1
            self.exam_left = self.exam.get("left", EXAM_MIN * 60)
            if self._exam_paused():
                self._exam_render_paused()
            else:
                self._exam_tick()
                self._exam_render_q()

    def _exam_update_topbar(self):
        """刷新右上角考试按钮（只在考试板块显示）"""
        for w in self.exam_bar.winfo_children():
            w.destroy()
        if self.mode != "考试":
            return
        wrong_kind = self.exam.get("kind") == "wrong"
        if not self._exam_active():
            b = tk.Button(self.exam_bar, text="▶ 开始考试", command=self._exam_start,
                          bg="#1a5276", fg="white", cursor="hand2", padx=12,
                          font=("Microsoft YaHei", 10))
            b.pack(side=tk.LEFT, padx=3)
        elif self._exam_finished():
            txt = "🔄 再考一次错题" if wrong_kind else "🔄 重新考试"
            cmd = (lambda: self._exam_start(kind="wrong")) if wrong_kind \
                else self._exam_restart_to_ready
            b = tk.Button(self.exam_bar, text=txt, command=cmd,
                          bg="#b9770e", fg="white", cursor="hand2", padx=12,
                          font=("Microsoft YaHei", 10))
            b.pack(side=tk.LEFT, padx=3)
        else:
            if self._exam_paused():
                b1 = tk.Button(self.exam_bar, text="▶ 继续考试", command=self._exam_toggle_pause,
                               bg="#1a5276", fg="white", cursor="hand2", padx=12,
                               font=("Microsoft YaHei", 10))
            else:
                b1 = tk.Button(self.exam_bar, text="⏸ 暂停考试", command=self._exam_toggle_pause,
                               bg="#b9770e", fg="white", cursor="hand2", padx=12,
                               font=("Microsoft YaHei", 10))
            b1.pack(side=tk.LEFT, padx=3)
            b2 = tk.Button(self.exam_bar, text="🚪 退出考试", command=self._exam_quit,
                           bg=COLOR_NO, fg="white", cursor="hand2", padx=12,
                           font=("Microsoft YaHei", 10))
            b2.pack(side=tk.LEFT, padx=3)

    def _exam_show_ready(self):
        """待开始页"""
        self._exam_update_topbar()
        for w in self.opt_frame.winfo_children():
            w.destroy()
        self.opt_widgets = []
        self.choice_var.set("")
        cnt = self.progress.get("_exam_count", 0)
        nc = sum(1 for it in self.bank if it.get("kind") in ("choice", "multi"))
        nj = sum(1 for it in self.bank if it.get("kind") == "judge")
        rounds_c = max(1, -(-nc // EXAM_CHOICE_NUM))
        rounds_j = max(1, -(-nj // EXAM_JUDGE_NUM))
        self.head_label.config(text=f"📝 模拟考试 · 第 {cnt + 1} 次考试")
        self._set_stem(
            "考试说明：\n\n"
            f"· 共 {EXAM_NUM} 题（选择题 {EXAM_CHOICE_NUM}（含多选）+ 判断 {EXAM_JUDGE_NUM}），限时 {EXAM_MIN} 分钟\n"
            f"· 每题 {EXAM_SCORE} 分，满分 {EXAM_NUM * EXAM_SCORE} 分\n"
            f"· 当前第 {cnt + 1} 次考试 · 已累计完成 {cnt} 次\n"
            f"· 环形抽题：同一场绝不重复；连续 {rounds_c} 场必覆盖全部选择题、连续 {rounds_j} 场必覆盖全部判断题\n"
            "· 每题只能作答一次，答错会自动进入错题库\n"
            "· 错题清除规则：只有「错题考试」答对才减少寿命，寿命归零后移除\n"
            "· 考试中题干上方标注题型：单选题 / 多选题 / 判断题\n"
            "· 考试中可暂停 / 退出；强制关闭程序会自动保存，下次打开继续\n"
            "· 考试中不能切换到其他板块，也不能打开笔记\n\n"
            "点击右上角「▶ 开始考试」按钮开始。")
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        self.fb.config(state=tk.DISABLED)
        self._add_btn("点击右上角「▶ 开始考试」按钮开始考试。", COLOR_BLUE, False)
        self.exam_nav.grid()
        self._exam_render_nav()
        self._update_stat()

    def _exam_pick_ids(self):
        """计划式抽题（调用模块级 exam_pick_ids，便于单元测试）"""
        return exam_pick_ids(self.bank, self.progress)

    def _exam_is_ok(self, it, sel):
        """考试判分：单选题用打乱后的答案 key，判断题用原答案"""
        oinfo = self.exam.get("opts", {}).get(it.get("id"))
        if oinfo:
            return str(sel).strip() == str(oinfo.get("answer"))
        return self._is_ok(it, sel)

    def _exam_start(self, kind="normal"):
        """开始考试：normal=常规模拟考试（计划式抽题）；wrong=错题考试（错题集随机抽 ≤10 题）"""
        self._stop_exam_timer()
        self.mode = "考试"
        if kind == "wrong":
            ids = [it.get("id") for it in self.bank
                   if self._rec_get(it.get("id", "")).get("in_wrong")]
            if not ids:
                messagebox.showinfo("错题考试", "当前没有错题，先去做题吧！")
                self.set_mode("错题")
                return
            ids = random.sample(ids, min(WEXAM_NUM, len(ids)))
        else:
            ids = self._exam_pick_ids()
        self.exam = {"active": True, "finished": False, "paused": False,
                     "kind": kind, "ids": ids, "idx": 0, "left": EXAM_MIN * 60,
                     "answers": {}}
        self.queue = build_exam_queue(self.bank, ids)      # 保持抽题时打乱的顺序
        self.exam["opts"] = build_exam_opts(self.queue)    # 选择题选项随机化
        self.idx = 0
        self.exam_left = EXAM_MIN * 60
        self._save_exam()
        self._exam_update_topbar()
        self.exam_nav.grid()
        self._exam_render_nav()
        self._exam_tick()
        self._exam_render_q()

    def start_wrong_exam(self):
        """错题板块（或底部按钮）：开始错题考试——从错题集随机抽最多 10 题"""
        if self.mode == "考试" and self._exam_active() and not self._exam_finished():
            messagebox.showwarning("考试中", "考试进行中，不能开始新的考试！\n请先交卷或退出考试。")
            return
        wrong_n = sum(1 for it in self.bank
                      if self._rec_get(it.get("id", "")).get("in_wrong"))
        if not wrong_n:
            messagebox.showinfo("错题考试", "当前没有错题，先去做题吧！")
            return
        self.set_mode("考试")
        self._exam_start(kind="wrong")

    def _exam_title(self):
        return "错题考试" if self.exam.get("kind") == "wrong" else "模拟考试"

    def _exam_tick(self):
        if self.mode != "考试" or not self._exam_running():
            self._exam_timer = None
            return
        if self.exam_left <= 0:
            self.finish_exam()
            return
        self.exam_left -= 1
        self.exam["left"] = self.exam_left
        mm, ss = divmod(self.exam_left, 60)
        typ = ""
        if 0 <= self.idx < len(self.queue):
            typ = " · " + self.queue[self.idx].get("kind_name", "")
        self.head_label.config(
            text=f"⏱ {self._exam_title()} · 剩余 {mm:02d}:{ss:02d} · 第 {self.idx + 1}/{len(self.queue)} 题{typ}")
        self._exam_timer = self.root.after(1000, self._exam_tick)

    def _exam_render_q(self):
        """考试模式显示当前题（含已作答态）"""
        if not self.queue or not (0 <= self.idx < len(self.queue)):
            return
        it = self.queue[self.idx]
        self.exam["idx"] = self.idx
        mm, ss = divmod(self.exam_left, 60)
        self.head_label.config(
            text=f"⏱ {self._exam_title()} · 剩余 {mm:02d}:{ss:02d} · 第 {self.idx + 1}/{len(self.queue)} 题"
                 f" · {it.get('kind_name', '')}")
        self._set_stem(str(it.get("stem", "")))
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        answers = self.exam.get("answers", {})
        sel = answers.get(it.get("id"))
        if sel is not None:
            ok = self._exam_is_ok(it, sel)
            tag = "✅ 正确" if ok else "❌ 错误"
            self.fb.insert(tk.END, f"已作答：{sel} · {tag}（此题不可更改）\n")
        else:
            hint = ("请作答（每题只能答一次）\n" if it.get("kind") != "multi"
                    else "请作答：本题为多选题，可选多个选项（每题只能答一次）\n")
            self.fb.insert(tk.END, hint)
        self.fb.config(state=tk.DISABLED)
        oinfo = self.exam.get("opts", {}).get(it.get("id"))
        self._render_options(it, exam_answered=sel, exam_opts=oinfo)
        self.exam_nav.grid()
        self._exam_render_nav()
        self._update_stat()

    def _exam_render_paused(self):
        """暂停页"""
        self._exam_update_topbar()
        for w in self.opt_frame.winfo_children():
            w.destroy()
        self.opt_widgets = []
        self.choice_var.set("")
        mm, ss = divmod(self.exam_left, 60)
        self.head_label.config(text=f"⏸ {self._exam_title()}已暂停")
        done = len(self.exam.get("answers", {}))
        self._set_stem(
            f"考试已暂停。\n\n剩余时间：{mm:02d}:{ss:02d}\n"
            f"已完成 {done}/{len(self._exam_qlist())} 题\n\n"
            "点击顶部「▶ 继续考试」恢复答题。")
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        self.fb.config(state=tk.DISABLED)
        self.exam_nav.grid()
        self._exam_render_nav()
        self._update_stat()

    def _root_on_configure(self):
        """主窗口尺寸变化时，防抖重排考试导航（避免子控件销毁触发递归）"""
        if self.mode != "考试" or not self._exam_active():
            return
        try:
            w = self.exam_nav.winfo_width()
        except Exception:
            return   # 窗口销毁过程中 exam_nav 已失效
        if w <= 0:
            return
        if abs(w - getattr(self, "_nav_last_w", 0)) < 30:
            return
        self._nav_last_w = w
        if self._nav_after:
            try:
                self.root.after_cancel(self._nav_after)
            except Exception:
                pass
        self._nav_after = self.root.after(120, self._exam_render_nav)

    def _exam_render_nav(self):
        """渲染考试题号导航：未答白色 / 答对绿色 / 答错红色，点击跳转；按钮按宽度自动换行。
        仅当答案状态或导航宽度变化时才重建（避免切题时无谓重建导致性能问题）。"""
        qs = self._exam_qlist()
        answers = self.exam.get("answers", {}) if self.exam else {}
        try:
            nav_w = self.exam_nav.winfo_width()
        except Exception:
            nav_w = 0
        sig = (len(qs), json.dumps(answers, ensure_ascii=False, sort_keys=True),
               nav_w // 42)
        if getattr(self, "_nav_sig", None) == sig:
            return
        self._nav_sig = sig
        for w in self.exam_nav.winfo_children():
            w.destroy()
        tk.Label(self.exam_nav, text="📋 题目导航", bg="#f8f9fa", fg=COLOR_BLUE,
                 font=("Microsoft YaHei", 11, "bold"), anchor="w",
                 padx=10, pady=8).pack(fill=tk.X)
        legend = tk.Frame(self.exam_nav, bg="#f8f9fa")
        legend.pack(fill=tk.X, padx=8)
        for txt, c in (("未做", "#ffffff"), ("答对", "#27ae60"), ("答错", "#c62828")):
            tk.Label(legend, text=txt, bg=c,
                     fg="#333333" if c == "#ffffff" else "#ffffff",
                     font=("Microsoft YaHei", 8), padx=6, pady=1, bd=1,
                     relief=tk.SOLID).pack(side=tk.LEFT, padx=2, pady=2)
        if not qs:
            return
        # 根据导航区当前宽度自适应每行按钮数（窗口放大/缩小自动调整排序）
        cols = 6 if nav_w <= 0 else max(3, min(10, nav_w // 42))
        row = tk.Frame(self.exam_nav, bg="#f8f9fa")
        row.pack(fill=tk.X, padx=6, pady=4)
        for k, it in enumerate(qs):
            if k % cols == 0 and k > 0:
                row = tk.Frame(self.exam_nav, bg="#f8f9fa")
                row.pack(fill=tk.X, padx=6, pady=4)
            ans = answers.get(it.get("id"))
            if ans is None:
                bg, fg = "#ffffff", "#333333"
            elif self._exam_is_ok(it, ans):
                bg, fg = "#27ae60", "#ffffff"
            else:
                bg, fg = "#c62828", "#ffffff"
            b = tk.Button(row, text=str(k + 1), width=3, bg=bg, fg=fg,
                          command=lambda q=k: self._exam_jump(q),
                          relief=tk.FLAT, cursor="hand2", font=("Microsoft YaHei", 9))
            b.grid(row=0, column=k % cols, padx=2, pady=3)

    def _exam_jump(self, k):
        """点击题号跳转：考试中跳到对应题作答；交卷后跳到该题回顾"""
        if not self._exam_active():
            return
        if not (0 <= k < len(self.queue)):
            return
        self.idx = k
        if self.exam.get("finished"):
            self._exam_review(self.queue[k].get("id"))
            return
        self.exam["idx"] = k
        self.exam["left"] = self.exam_left
        self._save_exam()
        self._exam_render_q()

    def _exam_toggle_pause(self):
        if self._exam_paused():
            self.exam["paused"] = False
            self._save_exam()
            self._exam_tick()
            self._exam_render_q()
        else:
            self._stop_exam_timer()
            self.exam["paused"] = True
            self.exam["left"] = self.exam_left
            self._save_exam()
            self._exam_render_paused()
        self._exam_update_topbar()

    def _exam_quit(self):
        """退出考试（作废）"""
        if not messagebox.askyesno("退出考试", "确定要退出考试吗？本次考试将作废。"):
            return
        self._stop_exam_timer()
        self.exam = {}
        self.progress.pop("exam", None)
        self._save_progress()
        self.set_mode("顺序")

    def _exam_restart_to_ready(self):
        """重新考试：先回到考试待开始页（说明页），点「开始考试」才正式开考"""
        self._stop_exam_timer()
        self.exam = {}
        self.progress.pop("exam", None)
        self._save_progress()
        self.mode = "考试"
        self._exam_show_ready()

    def _exam_answer(self, it, sel):
        """考试中作答（每题一次，答错自动进错题库；错题考试答对则寿命 -1）"""
        answers = self.exam.setdefault("answers", {})
        qid = it.get("id", "")
        if qid in answers:
            messagebox.showinfo("提示", "本题已作答，不能更改")
            return
        ok = self._exam_is_ok(it, sel)
        ctx = "wexam" if self.exam.get("kind") == "wrong" else "exam"
        cleared = self.record(qid, ok, ctx)
        answers[qid] = sel
        self.exam["left"] = self.exam_left
        self._save_exam()
        self._load_note(qid)                    # 考试作答后显示笔记
        self._exam_render_q()
        if self.exam.get("kind") == "wrong":
            self.fb.config(state=tk.NORMAL)
            if cleared:
                self.fb.insert(tk.END, "\n🎉 错题寿命归零，已从错题集移除！")
            else:
                life = self._rec_get(qid).get("life", 0)
                self.fb.insert(tk.END,
                               f"\n📕 错题考试已结算：当前寿命 {life}"
                               "（答对 -1；答错时累计错误达 3/5/7/9/>10 会 +1）")
            self.fb.config(state=tk.DISABLED)

    def _is_ok(self, it, sel):
        ans = str(it.get("answer", "")).upper().strip()
        sel_s = str(sel).strip()
        if it.get("kind") == "multi":
            norm = lambda s: "".join(sorted(c for c in s.upper() if c.isalpha()))
            return norm(sel_s) == norm(ans) and norm(ans) != ""
        return (sel_s == ans) or \
               (sel_s == "√" and ans in ("对", "T", "TRUE", "√")) or \
               (sel_s == "×" and ans in ("错", "F", "FALSE", "×"))

    def finish_exam(self):
        """交卷计分：每题 EXAM_SCORE 分（错题考试单独计数）"""
        self._stop_exam_timer()
        if not self._exam_active() or self.exam.get("finished"):
            return
        self.exam["finished"] = True
        self.exam["paused"] = False
        self.exam["left"] = self.exam_left
        # 完成交卷才计入考试次数（退出/放弃不计入）
        if self.exam.get("kind") == "wrong":
            self.progress["_wexam_count"] = self.progress.get("_wexam_count", 0) + 1
        else:
            self.progress["_exam_count"] = self.progress.get("_exam_count", 0) + 1
        self._save_exam()
        self._exam_show_result_page()

    def _exam_show_result_page(self):
        """成绩页：分数 + 错题号按钮（点击回顾）"""
        self._stop_exam_timer()
        self._exam_update_topbar()
        for w in self.opt_frame.winfo_children():
            w.destroy()
        self.opt_widgets = []
        self.choice_var.set("")
        answers = self.exam.get("answers", {})
        qs = self._exam_qlist()
        total = len(qs)
        correct = sum(1 for it in qs if self._exam_is_ok(it, answers.get(it.get("id"))))
        wrong = [it for it in qs
                 if it.get("id") in answers and not self._exam_is_ok(it, answers.get(it.get("id")))]
        unanswered = [it for it in qs if it.get("id") not in answers]
        score = correct * EXAM_SCORE
        wrong_kind = self.exam.get("kind") == "wrong"
        self.head_label.config(text=f"🏁 {self._exam_title()}结束")
        self._set_stem(f"🏆 {self._exam_title()}成绩：{score} 分 / 满分 {total * EXAM_SCORE} 分\n\n"
                       f"✅ 答对 {correct} 题 · ❌ 答错 {len(wrong)} 题 · ⭕ 未答 {len(unanswered)} 题")
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        if wrong_kind:
            cleared_n = sum(1 for it in qs
                            if it.get("id") in answers
                            and self._exam_is_ok(it, answers.get(it.get("id")))
                            and not self._rec_get(it.get("id", "")).get("in_wrong"))
            left_n = sum(1 for it in self.bank
                         if self._rec_get(it.get("id", "")).get("in_wrong"))
            self.fb.insert(tk.END,
                           f"得分：{score} 分（每题 {EXAM_SCORE} 分）　"
                           f"答题进度：{len(answers)}/{total}\n\n"
                           f"🎉 本次清除 {cleared_n} 道错题（寿命归零）；" 
                           f"答对的题寿命 -1，答错的题寿命不变。\n"
                           f"📕 剩余错题：{left_n} 道（在「错题」板块查看）")
        else:
            self.fb.insert(tk.END,
                           f"得分：{score} 分（每题 {EXAM_SCORE} 分）　"
                           f"答题进度：{len(answers)}/{total}\n\n"
                           "错题已自动进入「错题」板块；"
                           "错题需在「错题考试」中答对、寿命归零才会移除。")
        self.fb.config(state=tk.DISABLED)
        self._add_btn(f"🏆 成绩：{score} 分（答对 {correct}/{total}）",
                      COLOR_OK if score >= total * EXAM_SCORE * 0.6 else COLOR_NO, False)
        if wrong:
            self._add_btn("📌 点击错题号回顾题目与答案（红色为答错题）：", COLOR_NO, False)
            # 用 grid 每行 6 个按钮自动换行，避免错题多时挤在一起
            cols = 6
            for k, it in enumerate(wrong):
                if k % cols == 0:
                    row = tk.Frame(self.opt_frame, bg="#ffffff")
                    row.pack(fill=tk.X, pady=2)
                    self.opt_widgets.append(row)
                exam_idx = qs.index(it) + 1      # 考试内序号（与右侧题号导航对应）
                b = tk.Button(row, text=f"错题 {it.get('num', '?')}·第{exam_idx}题", width=13,
                              command=lambda q=it.get("id"): self._exam_review(q),
                              bg=COLOR_NO, fg="white", cursor="hand2",
                              font=("Microsoft YaHei", 10))
                b.grid(row=0, column=k % cols, padx=4, pady=3)
                self.opt_widgets.append(b)
        else:
            self._add_btn("🎉 全部答对，太棒了！", COLOR_OK, False)
        self.exam_nav.grid()
        self._exam_render_nav()
        self._update_stat()

    def _exam_review(self, qid):
        """回顾错题：显示题干/选项/正确答案/你的答案 + 返回成绩"""
        it = self._by_id(qid)
        if not it:
            return
        for w in self.opt_frame.winfo_children():
            w.destroy()
        self.opt_widgets = []
        self.choice_var.set("")
        self._render_images(it)
        self.head_label.config(text=f"📖 错题回顾 · 第 {it.get('num', '?')} 题")
        self._set_stem(str(it.get("stem", "")))
        oinfo = self.exam.get("opts", {}).get(qid)
        ans = str(oinfo["answer"] if oinfo else it.get("answer", "")).upper().strip()
        opts = oinfo["options"] if oinfo else it.get("options", [])
        your = self.exam.get("answers", {}).get(qid, "")
        if it.get("kind") in ("choice", "multi"):
            ans_set, your_set = set(ans), set(str(your))
            for o in opts:
                k = o.get("key", "?")
                mark, color = "", "#000000"
                if k in ans_set and k in your_set:
                    mark, color = "  ← 正确答案（你选了）", COLOR_OK
                elif k in ans_set:
                    mark, color = "  ← 正确答案", COLOR_OK
                elif k in your_set:
                    mark, color = "  ← 你的答案", COLOR_NO
                self._add_btn(f"{k}. {o.get('text', '')}{mark}", color, False)
        else:
            self._add_btn(f"正确答案：{ans}", COLOR_OK, False)
            if your:
                self._add_btn(f"你的答案：{your}", COLOR_NO if your != ans else COLOR_OK, False)
        if it.get("explain"):
            self._add_btn("解析：" + it.get("explain", ""), COLOR_BLUE, False)
        b = tk.Button(self.opt_frame, text="⬅ 返回成绩", command=self._exam_show_result_page,
                      bg=COLOR_BLUE, fg="white", cursor="hand2", padx=16, pady=6,
                      font=("Microsoft YaHei", 10))
        b.pack(pady=8)
        self.opt_widgets.append(b)
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        self.fb.insert(tk.END, f"你的答案：{your or '未作答'}　正确答案：{ans}")
        self.fb.config(state=tk.DISABLED)
        self._update_stat()

    # ---------- 题目显示 ----------
    def show_question(self):
        if not self.queue:
            return
        if self.idx >= len(self.queue):
            self.idx = len(self.queue) - 1
        if self.idx < 0:
            self.idx = 0
        it = self.queue[self.idx]
        if self.mode != "考试":
            head = (f"[{it.get('kind_name', it.get('kind', '?'))} "
                    f"{it.get('num', '')}]  "
                    f"{self.idx + 1}/{len(self.queue)}")
            if getattr(self, "_resume_notice", False):
                head += "　↩ 已回到上次进度"
                self._resume_notice = False
            self.head_label.config(text=head)
        self._set_stem(str(it.get("stem", "")))
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        rec = self.progress.get(it.get("id", ""), {})
        if rec.get("ok") is True:
            self.fb.insert(tk.END, "✅ 上次作答：正确\n")
        elif rec.get("ok") is False:
            self.fb.insert(tk.END, "❌ 上次作答：错误\n")
        else:
            self.fb.insert(tk.END, "未作答\n")
        wc = rec.get("wrong_count", 0)
        if wc:
            self.fb.insert(tk.END, f"🔁 本题累计错误：{wc} 次\n")
        if rec.get("in_wrong"):
            life = rec.get("life", 1)
            self.fb.insert(tk.END,
                           f"📕 本题在错题集中：错题寿命 {life}（在「错题考试」中答对 -1，归零后移除）\n")
        self.fb.config(state=tk.DISABLED)
        self._render_options(it)
        # 笔记窗口：始终同步当前题笔记（用户主动点「我的笔记」打开，未作答也显示历史笔记）
        self._refresh_note_win()
        self._update_stat()

    def _stem_on_configure(self, e):
        """窗口缩放导致题干宽度变化时，按新宽度重算显示行数（防抖，收敛）"""
        if e.width == getattr(self, "_stem_w", 0):
            return
        self._stem_w = e.width
        if self._stem_after:
            try:
                self.root.after_cancel(self._stem_after)
            except Exception:
                pass
        self._stem_after = self.root.after(150, self._stem_fit)

    def _stem_fit(self):
        """题干高度 = 实际显示行数（4~14 行）；超出 14 行自动出现滚动条，内容不丢"""
        self._stem_after = None
        try:
            self.stem.update_idletasks()
            if len(self.stem.get("1.0", "end-1c")) > 6000:
                raw = 15      # 超长文本必超 14 行：跳过昂贵的 displaylines 计算（Tk 对万字文本极慢）
            else:
                raw = int(self.stem.count("1.0", "end-1c", "displaylines")[0])
        except Exception:
            raw = str(getattr(self, "_stem_text", "")).count("\n") + 1
        if not isinstance(raw, int) or raw <= 0:
            raw = 1
        n = max(4, min(raw, 14))
        try:
            self.stem.config(height=n)
            if raw > n and not self.stem_sb.winfo_ismapped():
                self.stem_sb.grid(row=0, column=1, sticky="ns")
            elif raw <= n and self.stem_sb.winfo_ismapped():
                self.stem_sb.grid_remove()
        except Exception:
            pass

    def _on_root_destroy(self, e):
        """窗口销毁时取消所有挂起的 after 定时器（避免 Tcl 'invalid command name' 报错）"""
        try:
            if e.widget is not self.root:
                return
        except Exception:
            return
        self._closing = True
        for attr in ("_fb_after", "_stem_after", "_sync_job", "_img_after",
                     "_note_save_job", "_exam_timer"):
            job = getattr(self, attr, None)
            if job:
                try:
                    self.root.after_cancel(job)
                except Exception:
                    pass
                setattr(self, attr, None)

    def _on_split_configure(self, e):
        """分栏默认按 58% / 42% 分配（选项区 / 解析区）；带配图题给选项区 74%，
        免得配图把选项挤到滚动区外面；用户拖动过分隔条后不再自动改比例"""
        if getattr(self, "_sash_user", False) or e.height < 160:
            return
        ratio = 0.74 if getattr(self, "_img_expand", False) else 0.58
        try:
            self.split.sashpos(0, max(150, int(e.height * ratio)))
        except Exception:
            pass

    def _on_sash_drag(self, _e):
        """拖动分隔条 → 记住用户已手动调整比例（此后不再自动重置）"""
        self._sash_user = True

    def _fb_on_configure(self, e):
        """解析区尺寸变化（含拖动分隔条）时，重算滚动条显隐（防抖）"""
        if getattr(self, "_closing", False):
            return
        if e.width == getattr(self, "_fb_w", 0) and e.height == getattr(self, "_fb_h", 0):
            return
        self._fb_w, self._fb_h = e.width, e.height
        if self._fb_after:
            try:
                self.root.after_cancel(self._fb_after)
            except Exception:
                pass
        self._fb_after = self.root.after(120, self._fit_fb)

    def _fb_on_scroll(self, first, last):
        """文本可见比例变化（内容增减 / 滚动 / 缩放）→ 更新滚动条并稍后复核显隐"""
        try:
            self.fb_sb.set(first, last)
        except Exception:
            pass
        self._fit_fb_soon(60)

    def _fit_fb_soon(self, delay=60):
        """稍后再判定滚动条（等文本重新布局完成，避免时序抖动）"""
        if getattr(self, "_closing", False):
            return
        if self._fb_after:
            try:
                self.root.after_cancel(self._fb_after)
            except Exception:
                pass
        self._fb_after = self.root.after(delay, self._fit_fb)

    def _fb_set_sb(self, need):
        """显示/隐藏解析区滚动条"""
        try:
            if need and not self.fb_sb.winfo_ismapped():
                self.fb_sb.grid(row=1, column=1, sticky="ns")
            elif not need and self.fb_sb.winfo_ismapped():
                self.fb_sb.grid_remove()
        except Exception:
            pass

    def _fit_fb(self):
        """解析区滚动条判定：按“内容显示行数 vs 可视行数”（高度由可拖动分栏决定，不封顶行数）"""
        self._fb_after = None
        try:
            raw = int(self.fb.count("1.0", "end-1c", "displaylines")[0])
        except Exception:
            raw = str(self.fb.get("1.0", "end-1c")).count("\n") + 1
        if not isinstance(raw, int) or raw <= 0:
            raw = 1
        line_h = 0
        try:
            info = self.fb.dlineinfo("1.0")   # (x, y, w, h, baseline)
            if info:
                line_h = int(info[3])
        except Exception:
            line_h = 0
        if line_h <= 0:
            try:
                from tkinter import font as tkfont
                line_h = max(12, tkfont.Font(
                    font=self.fb.cget("font")).metrics("linespace"))
            except Exception:
                line_h = 20
        try:
            visible = max(1, (self.fb.winfo_height() - 12) // line_h)
        except Exception:
            visible = 1
        self._fb_set_sb(raw > visible)

    def _on_opt_canvas_configure(self, e):
        """Canvas 宽度变化 → 选项区 Frame 同步宽度（标签换行随之更新）"""
        try:
            self.opt_canvas.itemconfigure(self._opt_win, width=max(120, e.width))
        except Exception:
            pass

    def _on_opt_frame_configure(self, e):
        """选项区尺寸变化 → 更新滚动范围、标签换行宽度、滚动条显隐"""
        try:
            self.opt_canvas.configure(scrollregion=self.opt_canvas.bbox("all"))
        except Exception:
            pass
        for w in self._wrap_widgets:
            try:
                w.configure(wraplength=max(180, e.width - 48))
            except Exception:
                pass
        if self._sync_job:
            try:
                self.root.after_cancel(self._sync_job)
            except Exception:
                pass
        self._sync_job = self.root.after(60, self._sync_opt_scroll)

    def _sync_opt_scroll(self):
        """选项内容超出可视区时才显示滚动条"""
        self._sync_job = None
        try:
            need = self.opt_frame.winfo_reqheight() > self.opt_canvas.winfo_height() + 4
            if need and not self.opt_sb.winfo_ismapped():
                self.opt_sb.grid(row=0, column=1, sticky="ns")
            elif not need and self.opt_sb.winfo_ismapped():
                self.opt_sb.grid_remove()
                self.opt_canvas.yview_moveto(0)
        except Exception:
            pass

    def _reg_wrap(self, w):
        """登记需要按窗口宽度自动换行的标签"""
        self._wrap_widgets.append(w)
        try:
            w.configure(wraplength=max(180, self.opt_canvas.winfo_width() - 48))
        except Exception:
            pass

    def _bind_wheel_recursive(self, w):
        """选项区内所有子控件都能用滚轮滚动（避免鼠标停在标签上滚不动）"""
        try:
            w.bind("<MouseWheel>", self._on_opt_wheel)
            for c in w.winfo_children():
                self._bind_wheel_recursive(c)
        except Exception:
            pass

    def _on_opt_wheel(self, e):
        try:
            self.opt_canvas.yview_scroll(-1 if e.delta > 0 else 1, "units")
        except Exception:
            pass
        return "break"

    def _set_stem(self, text):
        self._stem_text = text
        self.stem.config(state=tk.NORMAL)
        self.stem.delete("1.0", "end")
        self.stem.insert("1.0", text)
        self._stem_fit()
        self.stem.config(state=tk.DISABLED)

    def _render_images(self, it):
        """显示题目配图；先占位，等分栏尺寸稳定后再按可用空间绘制（避免图把选项挤出屏幕）"""
        names = list(it.get("imgs") or [])
        self._img_expand = bool(names)          # 带配图：选项区自动占更大比例
        self._apply_split_ratio()
        self._clear_images()
        if self._img_after:
            try:
                self.root.after_cancel(self._img_after)
            except Exception:
                pass
            self._img_after = None
        if not names:
            return
        ph = tk.Label(self.opt_frame, text="⏳ 配图加载中…", bg="#f5f7fa", fg="#7f8c8d",
                      font=("Microsoft YaHei", 10))
        ph.pack(pady=6)
        self._img_widgets.append(ph)
        self._img_after = self.root.after(90, lambda: self._draw_images(names))

    def _clear_images(self):
        for w in getattr(self, "_img_widgets", []):
            try:
                w.destroy()
            except Exception:
                pass
        self._img_widgets = []

    def _draw_images(self, names):
        """真正绘制配图（此时分栏尺寸已生效，可算出合适大小）；
        图片插在选项之前（紧跟题干），保证选项仍全部可见"""
        self._img_after = None
        if getattr(self, "_closing", False):
            return
        self._clear_images()
        others = [w for w in self.opt_frame.winfo_children()
                  if w not in self._img_widgets]
        anchor = others[0] if others else None
        for name in names:
            path = os.path.join(ROOT, "imgs", name)
            img = self._load_image(path)
            if img is None:
                lbl = tk.Label(self.opt_frame, text=f"⚠ 配图加载失败：{name}\n（请确认 imgs 目录下有该图片）",
                               bg="#fdf2f2", fg="#c62828", font=("Microsoft YaHei", 10),
                               justify=tk.LEFT, padx=10, pady=8)
                lbl.pack(pady=4, fill=tk.X, before=anchor)
                self._img_widgets.append(lbl)
                continue
            lbl = tk.Label(self.opt_frame, image=img, bg="#f5f7fa",
                           bd=1, relief=tk.GROOVE, cursor="hand2")
            lbl.pack(pady=(4, 0), before=anchor)
            lbl.image = img            # 保持引用防回收
            lbl.bind("<Button-1>", lambda e, p=path: self._zoom_image(p))
            lbl.bind("<MouseWheel>", self._on_opt_wheel)     # 鼠标停在图上也能滚动选项区
            self._img_widgets.append(lbl)
            tip = tk.Label(self.opt_frame, text="🔍 看不清？点图片可放大查看（滚轮缩放 / 拖动平移）",
                           bg="#f5f7fa", fg="#7f8c8d", font=("Microsoft YaHei", 9))
            tip.pack(pady=(2, 6), before=anchor)
            self._img_widgets.append(tip)

    def _apply_split_ratio(self):
        """按当前是否为配图题重设分栏比例（解析区拖过分隔条则尊重用户设置）"""
        if getattr(self, "_sash_user", False):
            return
        try:
            h = self.split.winfo_height()
            if h > 200:
                self.split.sashpos(0, int(h * (0.74 if getattr(self, "_img_expand", False) else 0.58)))
        except Exception:
            pass

    def _zoom_image(self, path):
        """弹出大图窗口：自适应窗口显示，滚轮缩放、拖动平移、Esc 关闭"""
        try:
            from PIL import Image, ImageTk
        except Exception:
            messagebox.showinfo("提示", "缺少 Pillow（PIL）库，无法放大查看。")
            return
        try:
            src = Image.open(path)
        except Exception as e:
            messagebox.showinfo("提示", f"图片打不开：{e}")
            return
        win = tk.Toplevel(self.root)
        win.title("题目配图 · 放大查看（滚轮缩放，拖动平移，Esc 关闭）")
        sw, sh = win.winfo_screenwidth(), win.winfo_screenheight()
        w = max(640, min(1400, int(sw * 0.9)))
        h = max(480, min(1000, int(sh * 0.92)))
        win.geometry(f"{w}x{h}+{max(0, (sw - w) // 2)}+{max(0, (sh - h) // 2)}")
        win.transient(self.root)
        bar = tk.Frame(win, bg="#eaf2f8")
        bar.pack(fill=tk.X)
        canvas = tk.Canvas(win, bg="#3b4856", highlightthickness=0)
        canvas.pack(fill=tk.BOTH, expand=True)
        state = {"scale": 1.0, "photo": None, "box": None}

        def redraw(*_):
            s = state["scale"]
            tw, th = max(1, int(src.width * s)), max(1, int(src.height * s))
            im = src.resize((tw, th), Image.LANCZOS) if s != 1.0 else src
            photo = ImageTk.PhotoImage(im)
            canvas.delete("all")
            canvas.create_image(0, 0, anchor="nw", image=photo)
            canvas.configure(scrollregion=(0, 0, tw, th))
            state["photo"] = photo          # 保持引用
            state["box"] = (tw, th)
            canvas.yview_moveto(0.0)
            canvas.xview_moveto(0.0)

        def fit():
            cw, ch = max(200, canvas.winfo_width()), max(200, canvas.winfo_height())
            state["scale"] = min(1.0, cw / src.width, ch / src.height) or 1.0
            redraw()

        def zoom(f):
            state["scale"] = max(0.1, min(6.0, state["scale"] * f))
            redraw()

        def on_wheel(e):
            zoom(1.15 if e.delta > 0 else 1 / 1.15)
            return "break"

        def on_drag_start(e):
            canvas.scan_mark(e.x, e.y)

        def on_drag(e):
            canvas.scan_dragto(e.x, e.y, gain=1)

        for txt, cmd in (("🔍 放大", lambda: zoom(1.25)), ("🔎 缩小", lambda: zoom(1 / 1.25)),
                         ("⛶ 适应窗口", fit), ("1:1", lambda: (state.update(scale=1.0), redraw())),
                         ("✖ 关闭", win.destroy)):
            tk.Button(bar, text=txt, command=cmd, font=("Microsoft YaHei", 10),
                      padx=12, pady=4).pack(side=tk.LEFT, padx=3, pady=5)
        tk.Label(bar, text="滚轮缩放 · 按住左键拖动平移", bg="#eaf2f8", fg="#7f8c8d",
                 font=("Microsoft YaHei", 9)).pack(side=tk.LEFT, padx=10)
        canvas.bind("<MouseWheel>", on_wheel)
        canvas.bind("<ButtonPress-1>", on_drag_start)
        canvas.bind("<B1-Motion>", on_drag)
        win.bind("<Escape>", lambda e: win.destroy())
        win.bind("<MouseWheel>", on_wheel)
        win.update_idletasks()
        fit()
        win.grab_set()
        win.focus_set()

    def _load_image(self, path, max_w=None, max_h=None):
        """加载 PNG 并按可用区域缩放（宽度自适应、高度留出题号+选项的位置，不放大）"""
        if not path or not os.path.exists(path):
            return None
        if max_w is None:
            try:
                avail = self.opt_canvas.winfo_width() - 48
            except Exception:
                avail = 600
            max_w = max(360, min(1200, avail if avail > 120 else 600))
        if max_h is None:
            # 按当前选项区可视高度计算：给"请选择答案"标签 + 4 个选项 + 提示留出 ~300px
            try:
                ah = self.opt_canvas.winfo_height() - 340
            except Exception:
                ah = 0
            if ah < 190:                     # 拿不到有效高度时按窗口估算
                try:
                    ah = int(self.root.winfo_height() * 0.36)
                except Exception:
                    ah = 380
            max_h = min(760, max(190, ah))
        try:
            from PIL import Image, ImageTk
            im = Image.open(path)
            k = min(1.0, max_w / im.width, max_h / im.height)
            if k < 1.0:
                im = im.resize((max(1, int(im.width * k)), max(1, int(im.height * k))),
                               Image.LANCZOS)
            return ImageTk.PhotoImage(im)
        except Exception:
            pass
        try:
            img = tk.PhotoImage(file=path)
            if img.width() > max_w:
                f = (img.width() + max_w - 1) // max_w
                img = img.subsample(f, f)
            return img
        except Exception:
            return None

    def _render_options(self, it, exam_answered=None, exam_opts=None):
        for w in self.opt_frame.winfo_children():
            w.destroy()
        self.opt_widgets = []
        self._choice_rows = []
        self._wrap_widgets = []
        self.choice_var.set("")
        self._render_images(it)
        kind = it.get("kind", "qa")
        if kind in ("choice", "multi"):
            # 考试中选择/多选题使用打乱后的选项顺序（exam_opts），其余板块用原题库顺序
            opts = exam_opts["options"] if exam_opts else it.get("options", [])
            ans = str(exam_opts["answer"] if exam_opts else it.get("answer", "")).upper().strip()
            if exam_answered is not None:
                your = str(exam_answered)
                self._add_btn("本题已作答：", COLOR_BLUE, False)
                for o in opts:
                    k = o.get("key", "?")
                    mark, color = "", "#000000"
                    if k in ans and k in your:
                        mark, color = "  ← 正确答案（你选了）", COLOR_OK
                    elif k in ans:
                        mark, color = "  ← 正确答案", COLOR_OK
                    elif k in your:
                        mark, color = "  ← 你的答案", COLOR_NO
                    self._add_btn(f"{k}. {o.get('text', '')}{mark}", color, False)
            else:
                if kind == "choice":
                    self._add_btn("请选择答案：", COLOR_BLUE, False)
                else:
                    self._add_btn("本题为多选题（点击可多选、再点取消），选好后点「确认答案」：",
                                  COLOR_BLUE, False)
                # 自定义选项行：未选中右侧空白，点击后整行背景变蓝
                for o in opts:
                    k = o.get("key", "?")
                    row = tk.Frame(self.opt_frame, bg="#ffffff", cursor="hand2",
                                   highlightthickness=1, highlightbackground="#d5dbdb")
                    row.pack(fill=tk.X, pady=3)
                    lbl = tk.Label(row, text=f"{k}. {o.get('text', '')}",
                                   font=("Microsoft YaHei", 11), bg="#ffffff",
                                   anchor="w", justify="left", wraplength=560,
                                   padx=8, pady=4)
                    lbl.pack(fill=tk.X)
                    self._reg_wrap(lbl)
                    def row_enter(_e, row=row, lbl=lbl, k=k):
                        if not self._is_row_selected(k):
                            row.config(bg="#eaf2f8", highlightbackground="#aed6f1")
                            lbl.config(bg="#eaf2f8")

                    def row_leave(_e, row=row, lbl=lbl, k=k):
                        if not self._is_row_selected(k):
                            row.config(bg="#ffffff", highlightbackground="#d5dbdb")
                            lbl.config(bg="#ffffff")
                    for wid in (row, lbl):
                        wid.bind("<Enter>", row_enter)
                        wid.bind("<Leave>", row_leave)
                        wid.bind("<Button-1>",
                                 lambda e, kk=k: self._select_option(kk))
                    self._choice_rows.append({"frame": row, "label": lbl, "key": k})
                    self.opt_widgets.append(row)
        elif kind == "judge":
            if exam_answered is not None:
                ans = str(it.get("answer", "")).upper().strip()
                self._add_btn("本题已作答：", COLOR_BLUE, False)
                self._add_btn(f"正确答案：{ans}", COLOR_OK, False)
                self._add_btn(f"你的答案：{exam_answered}",
                              COLOR_NO if str(exam_answered) != ans else COLOR_OK, False)
            else:
                self._add_btn("请判断对错（选择后点「确认答案」）：", COLOR_BLUE, False)
                # 判断题也走“选择→确认”交互：点击高亮，确认答案后才判分
                for txt, key in (("✔ 正确", "√"), ("✘ 错误", "×")):
                    row = tk.Frame(self.opt_frame, bg="#ffffff", cursor="hand2",
                                   highlightthickness=1, highlightbackground="#d5dbdb")
                    row.pack(fill=tk.X, pady=3)
                    lbl = tk.Label(row, text=txt,
                                   font=("Microsoft YaHei", 11, "bold"), bg="#ffffff",
                                   anchor="w", padx=8, pady=4)
                    lbl.pack(fill=tk.X)
                    def jrow_enter(_e, row=row, lbl=lbl, key=key):
                        if not self._is_row_selected(key):
                            row.config(bg="#eaf2f8", highlightbackground="#aed6f1")
                            lbl.config(bg="#eaf2f8")

                    def jrow_leave(_e, row=row, lbl=lbl, key=key):
                        if not self._is_row_selected(key):
                            row.config(bg="#ffffff", highlightbackground="#d5dbdb")
                            lbl.config(bg="#ffffff")
                    for wid in (row, lbl):
                        wid.bind("<Enter>", jrow_enter)
                        wid.bind("<Leave>", jrow_leave)
                        wid.bind("<Button-1>",
                                 lambda e, kk=key: self._select_option(kk))
                    self._choice_rows.append({"frame": row, "label": lbl, "key": key})
                    self.opt_widgets.append(row)
        else:
            if it.get("options"):
                self._add_btn("本题选项（原卷拍摄不完整，仅供参考）：", COLOR_BLUE, False)
                for o in it.get("options", []):
                    self._add_btn(f"{o.get('key', '?')}. {o.get('text', '')}", "#000000", False)
            self._add_btn("本题无固定选项答案（存疑/写结果类）：先自行思考，再点「查看答案」对照说明。",
                          "#8e44ad", disabled=False)
            b = tk.Button(self.opt_frame, text="查看答案", command=self.reveal_qa,
                          cursor="hand2", font=("Microsoft YaHei", 11),
                          bg="#8e44ad", fg="white", padx=16, pady=6)
            b.pack(pady=8)
            self.opt_widgets.append(b)
            b2 = tk.Button(self.opt_frame, text="已掌握 ✔", command=lambda: self.mark_qa(True),
                           cursor="hand2", font=("Microsoft YaHei", 11),
                           bg=COLOR_OK, fg="white", padx=12, pady=6)
            b2.pack(side=tk.LEFT, padx=8)
            b3 = tk.Button(self.opt_frame, text="还需复习 ✘", command=lambda: self.mark_qa(False),
                           cursor="hand2", font=("Microsoft YaHei", 11),
                           bg=COLOR_NO, fg="white", padx=12, pady=6)
            b3.pack(side=tk.LEFT, padx=8)
            self.opt_widgets += [b2, b3]

    def _add_btn(self, text, color, disabled):
        lbl = tk.Label(self.opt_frame, text=text,
                       font=("Microsoft YaHei", 11, "bold"), bg="#f5f7fa",
                       fg=color, anchor="w", justify="left", wraplength=560)
        lbl.pack(fill=tk.X, pady=(6, 2))
        self._reg_wrap(lbl)
        self.opt_widgets.append(lbl)

    def _select_option(self, key):
        """选项选中：单选切换单项；多选点击切换该项（可多选）。
        选中行背景变蓝，其余恢复白色。"""
        it = self._cur_item()
        if it is not None and it.get("kind") == "multi":
            sel = set(c for c in self.choice_var.get())
            if key in sel:
                sel.discard(key)
            else:
                sel.add(key)
            self.choice_var.set("".join(sorted(sel)))
        else:
            self.choice_var.set(key)
        for item in getattr(self, "_choice_rows", []):
            if self._is_row_selected(item["key"]):
                item["frame"].config(bg=COLOR_BLUE, highlightbackground=COLOR_BLUE)
                item["label"].config(bg=COLOR_BLUE, fg="white")
            else:
                item["frame"].config(bg="#ffffff", highlightbackground="#d5dbdb")
                item["label"].config(bg="#ffffff", fg="#000000")

    # ---------- 作答 ----------
    def check(self):
        """单选/多选/判断提交：选择后点「确认答案」才判分"""
        it = self.queue[self.idx]
        if it.get("kind") not in ("choice", "multi", "judge"):
            return
        sel = self.choice_var.get()
        if not sel:
            messagebox.showinfo("提示", "请先选择一个答案")
            return
        if self.mode == "考试":
            self._exam_answer(it, sel)
            return
        ctx = "wrong" if self.mode == "错题" else "seq"
        self.record(it.get("id", ""), self._is_ok(it, sel), ctx)
        self._show_result(it, sel)
        self._load_note(it.get("id", ""))      # 确认答案后显示笔记
        self._update_stat()

    def answer_judge(self, key):
        """判断题选择：只高亮选中，点「确认答案」才判分（兼容旧调用）"""
        self._select_option(key)

    def reveal_qa(self):
        it = self.queue[self.idx]
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        if it.get("answer"):
            self.fb.insert(tk.END, "答案：\n" + it.get("answer", "") + "\n")
        if it.get("explain"):
            self.fb.insert(tk.END, "\n解析：\n" + it.get("explain", ""))
        if not it.get("answer") and not it.get("explain"):
            self.fb.insert(tk.END, "（未提供答案）")
        self.fb.config(state=tk.DISABLED)
        self._fit_fb()

    def mark_qa(self, ok):
        it = self.queue[self.idx]
        self.record(it.get("id", ""), ok)
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        self.fb.insert(tk.END, "✅ 已标记为掌握" if ok else "❌ 已标记为需复习")
        self.fb.config(state=tk.DISABLED)
        self._update_stat()

    # ---------- 错题记录（寿命机制） ----------
    def _rec(self, qid):
        """取/建一条题目记录（统一字段，兼容旧数据）"""
        rec = self.progress.setdefault(qid, {})
        rec.setdefault("ok", None)
        rec.setdefault("wrong_count", 0)
        rec.setdefault("in_wrong", False)
        rec.setdefault("life", 0)
        rec.setdefault("notes", "")
        return rec

    def _rec_get(self, qid):
        """只读取记录（不存在时返回空字典，不污染进度文件）"""
        rec = self.progress.get(qid)
        return rec if isinstance(rec, dict) else {}

    def _bump_wrong(self, rec):
        """答错：累计次数 +1；不在错题集则加入（初始寿命 1）；
        次数达到 3/5/7/9 或超过 10 时，寿命 +1"""
        rec["wrong_count"] = rec.get("wrong_count", 0) + 1
        wc = rec["wrong_count"]
        if not rec.get("in_wrong"):
            rec["in_wrong"] = True
            rec["life"] = 1
        if wc in (3, 5, 7, 9) or wc > 10:
            rec["life"] = rec.get("life", 1) + 1

    def _life_dec(self, rec):
        """错题考试答对：寿命 -1；归零则移出错题集。返回是否清除"""
        if not rec.get("in_wrong"):
            return False
        rec["life"] = max(0, rec.get("life", 1) - 1)
        if rec["life"] <= 0:
            rec["in_wrong"] = False
            return True
        return False

    def record(self, qid, ok, ctx="seq"):
        """记录作答。ctx：seq=顺序/错题重做等非考试，exam=普通考试，wexam=错题考试。
        规则：答错（任何地方）都累计次数并按阈值加寿命；
              只有错题考试答对才寿命 -1；顺序/考试答对不影响错题状态"""
        rec = self._rec(qid)
        rec["ok"] = ok
        cleared = False
        if not ok:
            self._bump_wrong(rec)
        elif ctx == "wexam":
            cleared = self._life_dec(rec)
        self._save_progress()
        return cleared

    def _show_result(self, it, sel):
        ans = str(it.get("answer", "")).upper()
        ok = self._is_ok(it, sel)
        rec = self.progress.get(it.get("id", ""), {})
        wc = rec.get("wrong_count", 0)
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        if ok:
            self.fb.insert(tk.END, "✅ 回答正确！\n", ("ok",))
        else:
            self.fb.insert(tk.END, f"❌ 回答错误。正确答案：{ans}（你选了 {sel}）\n", ("no",))
        if wc:
            self.fb.insert(tk.END, f"🔁 本题累计错误：{wc} 次\n")
        if rec.get("in_wrong"):
            life = rec.get("life", 1)
            self.fb.insert(tk.END,
                           f"📕 本题在错题集中：错题寿命 {life}（在「错题考试」中答对 -1，归零后移除）\n")
        self.fb.tag_configure("ok", foreground=COLOR_OK)
        self.fb.tag_configure("no", foreground=COLOR_NO)
        if it.get("explain"):
            self.fb.insert(tk.END, "\n解析：\n" + it.get("explain", ""))
        self.fb.config(state=tk.DISABLED)

    # ---------- 笔记（独立窗口） ----------
    def _open_note(self):
        """打开 / 聚焦当前题目的笔记窗口（考试中禁止）"""
        if self.mode == "考试" and self._exam_active() and not self._exam_finished():
            messagebox.showwarning("考试中", "考试进行中，不能打开笔记！")
            return
        if self.note_win is not None and self.note_win.winfo_exists():
            self.note_win.lift()
            self._refresh_note_win()
            return
        win = tk.Toplevel(self.root)
        win.title("📝 我的笔记")
        try:
            sw, sh = win.winfo_screenwidth(), win.winfo_screenheight()
            w = max(480, min(680, int(sw * 0.34)))
            h = max(560, min(880, int(sh * 0.72)))
            win.geometry(f"{w}x{h}+{max(0, sw - w - 50)}+60")
        except Exception:
            win.geometry("580x720")
        win.minsize(420, 380)
        win.grid_rowconfigure(1, weight=1)
        win.grid_columnconfigure(0, weight=1)
        self.note_title = tk.Label(win, text="", bg="#fdf6e3", fg="#7b5804",
                                   font=("Microsoft YaHei", 11, "bold"),
                                   anchor="w", padx=10, pady=6)
        self.note_title.grid(row=0, column=0, sticky="ew")
        self.note_text = tk.Text(win, font=("Microsoft YaHei", 10), wrap="word",
                                 bg="#fffdf5", relief=tk.GROOVE, bd=1,
                                 padx=8, pady=6)
        self.note_text.grid(row=1, column=0, sticky="nsew", padx=8, pady=4)
        self.note_text.bind("<KeyRelease>", lambda e: self._note_edited())
        tk.Label(win, text="笔记自动保存；窗口可自由缩放，切换题目时内容跟随当前题",
                 bg="#fdf6e3", fg="#a08030", font=("Microsoft YaHei", 9),
                 anchor="w", padx=10, pady=4).grid(row=2, column=0, sticky="ew")
        self.note_win = win
        win.protocol("WM_DELETE_WINDOW", self._close_note)
        self._refresh_note_win()

    def _close_note(self):
        if self.note_win is not None and self.note_win.winfo_exists():
            self.note_win.destroy()
        self.note_win = None
        self.note_text = None

    def _refresh_note_win(self):
        """把当前题目的笔记同步到笔记窗口（未打开则忽略）"""
        if self.note_win is None or self.note_text is None \
                or not self.note_win.winfo_exists():
            return
        it = self._cur_item()
        qid = it.get("id", "") if it else ""
        self.note_title.config(text=f"📝 我的笔记 · {qid}")
        self.note_text.delete("1.0", "end")
        rec = self.progress.get(qid, {})
        self.note_text.insert("1.0", rec.get("notes", ""))

    def _note_edited(self):
        it = self._cur_item()
        if it is None or self.note_text is None:
            return
        qid = it.get("id", "")
        rec = self.progress.setdefault(qid, {"ok": None,
                                             "wrong_count": 0, "notes": ""})
        rec["notes"] = self.note_text.get("1.0", "end-1c")
        if self._note_save_job:
            try:
                self.root.after_cancel(self._note_save_job)
            except Exception:
                pass
        self._note_save_job = self.root.after(800, self._save_progress)

    def _cur_item(self):
        if self.queue and 0 <= self.idx < len(self.queue):
            return self.queue[self.idx]
        return None

    def _load_note(self, qid):
        """同步笔记窗口到当前题（窗口未开则忽略）"""
        self._refresh_note_win()

    def _clear_note(self):
        """未作答时清空笔记窗口（若已打开）"""
        if self._note_save_job:
            try:
                self.root.after_cancel(self._note_save_job)
            except Exception:
                pass
            self._note_save_job = None
        if self.note_text is not None:
            self.note_text.delete("1.0", "end")

    def _save_note(self):
        it = self._cur_item()
        if it is None or self.note_text is None:
            return
        qid = it.get("id", "")
        rec = self.progress.setdefault(qid, {"ok": None,
                                             "wrong_count": 0, "notes": ""})
        rec["notes"] = self.note_text.get("1.0", "end-1c")
        self._save_progress()

    # ---------- 重置 ----------
    def reset_progress(self):
        """清除记忆：3 次确认 + 5 秒冷静期"""
        if not messagebox.askyesno("重置进度（1/3）",
                                   "确定要清除全部做题记录吗？\n（此操作不可恢复！）"):
            return
        if not messagebox.askyesno("重置进度（2/3）",
                                   "再次确认：将清空做题记录、错题次数、笔记和考试记录！"):
            return
        if not self._confirm_cool_down():
            return
        # 执行重置（先清队列与状态，避免 set_mode 内部 _save_note 回写）
        self._stop_exam_timer()
        self.exam = {}
        self.progress = {}
        self.queue = []
        self.idx = 0
        self.mode = "顺序"
        self._seq_saved_idx = 0
        self._resume_notice = False
        self._save_progress()
        if self.note_text is not None:
            self.note_text.delete("1.0", "end")
        for w in self.exam_bar.winfo_children():
            w.destroy()
        self.fb.config(state=tk.NORMAL)
        self.fb.delete("1.0", "end")
        self.fb.insert(tk.END, "🗑 已清除全部做题记录，重新开始！\n")
        self.fb.config(state=tk.DISABLED)
        self.set_mode("顺序")
        self._update_stat()
        self.show_question()

    def _confirm_cool_down(self, seconds=5):
        """最后一次确认：5 秒冷静期后才能点确定"""
        tl = tk.Toplevel(self.root)
        tl.title("重置确认（3/3）")
        tl.geometry("380x190")
        tl.transient(self.root)
        tl.grab_set()
        tl.resizable(False, False)
        var = tk.StringVar(value=f"最后一次确认（3/3）\n\n请等待 {seconds} 秒后才能点击确定…")
        tk.Label(tl, textvariable=var, font=("Microsoft YaHei", 11),
                 justify="center", pady=18).pack()
        btn = tk.Button(tl, text="确 定", state=tk.DISABLED, width=12,
                        font=("Microsoft YaHei", 10))
        btn.pack(pady=4)
        tk.Button(tl, text="取 消", command=tl.destroy, width=12,
                  font=("Microsoft YaHei", 10)).pack(pady=2)
        left = [seconds]
        result = [False]

        def tick():
            if left[0] <= 0:
                var.set("最后一次确认（3/3）\n\n现在可以点击确定进行重置。")
                btn.config(state=tk.NORMAL)
                return
            var.set(f"最后一次确认（3/3）\n\n请等待 {left[0]} 秒后才能点击确定…")
            left[0] -= 1
            tl.after(1000, tick)

        def do_ok():
            result[0] = True
            tl.destroy()

        btn.config(command=do_ok)
        tick()
        tl.wait_window()
        return result[0]

    # ---------- 导航 ----------
    def prev_q(self):
        self._save_note()
        if self.idx > 0:
            self.idx -= 1
            if self.mode == "考试":
                self.exam["idx"] = self.idx
                self.exam["left"] = self.exam_left
                self._save_exam()
                self._exam_render_q()
            else:
                self.show_question()
        else:
            messagebox.showinfo("提示", "已经是第一题")

    def next_q(self):
        self._save_note()
        if self.idx < len(self.queue) - 1:
            self.idx += 1
            if self.mode == "考试":
                self.exam["idx"] = self.idx
                self.exam["left"] = self.exam_left
                self._save_exam()
                self._exam_render_q()
            else:
                self.show_question()
        else:
            if self.mode == "考试":
                if messagebox.askyesno("交卷", "已到最后一题，确定交卷吗？"):
                    self.finish_exam()
            else:
                messagebox.showinfo("提示", "已经是最后一题")

    # ---------- 统计 ----------
    def _update_stat(self):
        done = [it for it in self.bank
                if self.progress.get(it.get("id", ""), {}).get("ok") is not None]
        ok = [it for it in done
              if self.progress.get(it.get("id", ""), {}).get("ok") is True]
        wrong = sum(1 for it in self.bank
                    if self.progress.get(it.get("id", ""), {}).get("in_wrong"))
        rate = round(len(ok) / len(done) * 100) if done else 0
        self.stat_label.config(
            text=f"已做 {len(done)}/{len(self.bank)} · 正确率 {rate}% · 错题 {wrong}")
        # 页面渲染后：反馈区高度自适应 + 选项区滚轮绑定
        self._fit_fb()
        self._bind_wheel_recursive(self.opt_frame)

    def _save_progress(self):
        try:
            with open(PROG_PATH, "w", encoding="utf-8") as f:
                json.dump(self.progress, f, ensure_ascii=False, indent=1)
        except Exception:
            pass


def _enable_dpi_awareness():
    """Windows 下启用 DPI 感知：高分屏下窗口与文字更清晰（其他系统自动忽略）"""
    try:
        import ctypes
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)    # Win 8.1+
        except Exception:
            ctypes.windll.user32.SetProcessDPIAware()         # 旧系统
    except Exception:
        pass


def main():
    if not os.path.exists(BANK_PATH):
        messagebox.showerror("缺少题库", "未找到 题库.json，请先运行 build_bank.py 生成题库。")
        return
    _enable_dpi_awareness()
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
