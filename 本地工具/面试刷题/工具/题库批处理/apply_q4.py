# -*- coding: utf-8 -*-
"""面试题4 落地：
- 02 主观：插入 497~503（HR 附加题小节）+ 统计更新 + 合并说明（#14/#324/#433）
- 03 大题：插入 531（新小节 七、）+ 统计更新 + 合并说明（#409）+ 副本同步
- 01 客观：EOF 追加 315~318 + 统计更新
- gcc 验证 #531 代码；重建三个题库
"""
import io, sys, os, re, subprocess, hashlib, shutil
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8', line_buffering=True)
B = r"D:\github\面试刷题"
TOOL = B + r"\工具\题库批处理"
sys.path.insert(0, TOOL)
from new_2026_2p0_q4 import OBJ, SUBJ, BIGQ

SMD = B + r"\面试题刷题软件-主观题\题库\02_主观题.md"
DMD_A = B + r"\面试题刷题软件-大题\题库\03_编程画图大题.md"
DMD_B = B + r"\面试题刷题软件-主观题\题库\03_编程画图大题.md"
OMD = B + r"\面试题刷题软件-客观题\题库\01_客观题_选择题.md"

def rep(t, old, new, tag):
    c = t.count(old)
    assert c == 1, f"{tag}: 锚点出现 {c} 次（应为 1）: {old[:60]}"
    return t.replace(old, new, 1)

def load(path):
    b = open(path, 'rb').read()
    t = b.decode('utf-8')
    nl = '\r\n' if '\r\n' in t[:5000] else '\n'
    return t, nl

def save(path, t):
    open(path, 'wb').write(t.encode('utf-8'))

# ---------- gcc 验证 #531 ----------
code = re.findall(r"```c\n(.*?)```", BIGQ[531]['ans'], re.S)[0]
src_c = os.path.join(TOOL, "_q531.c")
exe = os.path.join(TOOL, "_q531.exe")
open(src_c, 'w', encoding='utf-8').write(code)
cp = subprocess.run(["gcc", "-Wall", "-o", exe, src_c], capture_output=True, text=True,
                    encoding='utf-8', errors='replace')
assert cp.returncode == 0 and not cp.stderr.strip(), f"#531 编译失败: {cp.stderr[:300]}"
rp = subprocess.run([exe], input="abc 12 !#\n", capture_output=True, text=True,
                    encoding='utf-8', errors='replace')
out = rp.stdout
assert "字母：3" in out and "数字：2" in out and "空格：2" in out and "其他：2" in out, f"#531 输出异常: {out!r}"
print("gcc 验证 #531 ✓ :", out.strip().replace('\n', ' | '))

# ---------- 02 主观 ----------
t, nl = load(SMD)

def sj_block(num, q):
    parts = [f"**{num}.** {q['stem']}", "",
             f"**题目解析：** {q['ana']}",
             f"**标准答案：** {q['ans']}"]
    if q.get('hot'):
        parts.append("**热点：** 是")
    parts += [f"**得分点：** {q['exp']}",
              f"**来源：** {q['src']}", "", "---", ""]
    return nl.join(parts)

title = "### 附加面试题2.0 · 面试题4（中山志源达翔·HR 附加题）"
section = nl.join([title, ""] + [sj_block(n, SUBJ[n]) for n in sorted(SUBJ)])
t = rep(t, "## 附：统计", section + nl + "## 附：统计", "02 小节插入")

t = rep(t, "- 总题量：**449 题**（与文件 1/3 重复的题目已合并，仅保留一处并标注来源）",
        "- 总题量：**461 题**（与文件 1/3 重复的题目已合并，仅保留一处并标注来源）", "02 总题量")
old_b = "- 第 492~496 题为附加面试题2.0·面试题3（基础篇：变量存储区域 / 条件表达式 / 变量定义简版 / 固件异常调试 / 提高稳定性，2026-10-12 新增）。"
t = rep(t, old_b, old_b + nl + "- 第 497~503 题为附加面试题2.0·面试题4（中山志源达翔 嵌入式工程师笔试题 HR 附加题：求职考虑因素 / 成功案例 / 职业规划 / 时间与加班 / 环境适应 / 成败因素 / 经历总结，2026-10-13 新增）。", "02 统计bullet")

t = rep(t, "⑥合并说明：同 2026面试题-1 天嵌部分第 4 题（关键字 static 的作用是什么），重复题已合并。",
        "⑥合并说明：同 2026面试题-1 天嵌部分第 4 题（关键字 static 的作用是什么），重复题已合并。 ⑦合并说明：同 附加面试题2.0·面试题4 第 7 题（关键字 static 的作用是什么），重复题已合并。", "02 #14")
t = rep(t, "③加分：提 #pragma once 或宏命名习惯。",
        "③加分：提 #pragma once 或宏命名习惯。 ④合并说明：同 附加面试题2.0·面试题4 第 5 题（头文件 ifndef/define/endif 的作用与好处），重复题已合并。", "02 #324")
t = rep(t, "③ 加分：RTOS 实践中统一加锁顺序、带超时获取、优先用单一锁或队列，或提到优先级继承。",
        "③ 加分：RTOS 实践中统一加锁顺序、带超时获取、优先用单一锁或队列，或提到优先级继承。 ④补充：同 附加面试题2.0·面试题4 第 6 题（解释死锁并给出避免策略），同一考点已合并。", "02 #433")

save(SMD, t)
print("02 主观 ✓（497~503 + 3 处合并说明 + 统计）")

# ---------- 03 大题 ----------
t, nl = load(DMD_A)
q = BIGQ[531]
blk = sj_block(531, q)
sec3 = nl.join(["## 七、附加面试题2.0 · 面试题4（编程 1 题，编号 531）", "", blk])
t = rep(t, "## 附：统计", sec3 + nl + "## 附：统计", "03 小节插入")
old_b3 = "- 附加面试题2.0 · 面试题3（编程篇）：**4 题** —— 编号 527~530（100~400 筛选 / 篮球反弹 / 字节数组转十六进制 / 学生成绩统计，2026-10-12 新增）"
t = rep(t, old_b3, old_b3 + nl + "- 附加面试题2.0 · 面试题4（编程）：**1 题** —— 编号 531（字符分类统计程序，2026-10-13 新增）", "03 统计bullet")
t = rep(t, "- 合计：**156 题**", "- 合计：**157 题**", "03 合计")
t = rep(t, "用熟悉的编程语言由小到大排序。）。",
        "用熟悉的编程语言由小到大排序。）。 ⑤补充：同 附加面试题2.0·面试题4 第 9 题（编写 C 函数实现整数数组的冒泡排序），重复题已合并。", "03 #409")
save(DMD_A, t)
shutil.copyfile(DMD_A, DMD_B)
assert hashlib.md5(open(DMD_A, 'rb').read()).hexdigest() == hashlib.md5(open(DMD_B, 'rb').read()).hexdigest()
print("03 大题 ✓（531 + #409 合并 + 统计；副本已同步）")

# ---------- 01 客观 ----------
t, nl = load(OMD)
blocks = []
for i, e in enumerate(OBJ):
    num = 315 + i
    nb = (f"**{num}.** {e['stem']}" + nl + nl
          + nl.join("- " + o for o in e['options']) + nl + nl
          + f"**答案：{e['answer']}**" + nl
          + "**解析：** " + e['ana'] + nl
          + "**来源：** " + e['src'] + nl + nl + "---")
    blocks.append(nb)
t = t.rstrip() + nl + nl + (nl + nl).join(blocks) + nl
t = rep(t, "总题量：**312 题**（其中选择题 270 题、判断题 35 题、多选/双选 5 题、特殊题 2 题）",
        "总题量：**316 题**（其中选择题 274 题、判断题 35 题、多选/双选 5 题、特殊题 2 题）", "01 总题量")
old_b1 = "- 收录范围：2022~2026 全部试卷 + 附加面试题（笔试题1/笔试题2/面试题4，第 214~286 题）中的选择题、判断题；第 288~294 题来自附加面试题·面试题6（选择题第 1~7 题）"
t = rep(t, old_b1, old_b1 + "；第 315~318 题为附加面试题2.0·面试题4（中山志源达翔 嵌入式工程师笔试题第 1~4 题，2026-10-13 新增）", "01 收录bullet")
t = rep(t, "（第 92、112、140、150、213 题）", "（第 92、112、140、150、213、316、317 题）", "01 存疑bullet")
save(OMD, t)
print("01 客观 ✓（315~318 + 统计）")

# ---------- 重建三库 ----------
print()
for folder in [r"面试题刷题软件-客观题", r"面试题刷题软件-主观题", r"面试题刷题软件-大题"]:
    cwd = os.path.join(B, folder)
    r = subprocess.run([sys.executable, 'core\\build_bank.py'], cwd=cwd,
                       capture_output=True, text=True, encoding='utf-8', errors='replace')
    print(f"[{folder}]", ' | '.join((r.stdout or '').strip().splitlines()) or r.stderr[:200])
print("面试题4 落地完成")
