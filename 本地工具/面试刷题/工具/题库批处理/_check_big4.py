# -*- coding: utf-8 -*-
"""批4（大题 473~505，32 题）自检：md 区间格式 + JSON 字段 + 代码编译/运行验证。"""
import importlib.util, os, re, subprocess, sys, io, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

MD = r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md"
JSON = r"D:\github\面试刷题\面试题刷题软件-大题\题库.json"
BASE = r"D:\github\面试刷题\工具\题库批处理\_check_big4"
os.makedirs(BASE, exist_ok=True)

NUMS = [473, 474, 475, 476, 478, 479, 480, 481, 482, 483, 484, 485, 486, 487, 488,
        489, 490, 491, 492, 493, 494, 495, 496, 497, 498, 499, 500, 501, 502, 503,
        504, 505]

# ---------- 1) md 区间统计 ----------
md = io.open(MD, encoding="utf-8").read()
seg = md[md.index("**473.**"):md.index("## 附：统计")]
print("[md] 473~505 区间: 题目解析=%d 标准答案=%d 参考答案=%d 得分点=%d"
      % (seg.count("**题目解析：**"), seg.count("**标准答案：**"),
         seg.count("**参考答案：**"), seg.count("**得分点：**")))

# ---------- 2) JSON 抽查 ----------
data = json.load(io.open(JSON, encoding="utf-8"))
qmap = {}
for d in data:
    n = d.get("num")
    if isinstance(n, int):
        qmap[n] = d
miss = []
for n in NUMS:
    d = qmap.get(n)
    if not d:
        miss.append((n, "缺条目")); continue
    if not d.get("analysis"): miss.append((n, "缺 analysis"))
    if not d.get("answer"): miss.append((n, "缺 answer"))
    if not d.get("explain"): miss.append((n, "缺 explain"))
print("[json] 32 题缺失:", miss if miss else "无")
print("[json] #473 answer 首行:", qmap[473]["answer"].split("\n")[0][:60])

# ---------- 3) 代码提取 ----------
spec = importlib.util.spec_from_file_location("bd", r"D:\github\面试刷题\工具\题库批处理\batch_big_4_data.py")
mod = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)
BLOCKS = {n: re.findall(r"```([a-zA-Z]*)\n(.*?)\n```", s, re.S) for n, (a, s) in mod.NEW.items()}

def compile_run(name, src, run=None, expect=None, gxx=False, std=None, preprocess=False):
    ext = "cpp" if gxx else "c"
    fp = os.path.join(BASE, name + "." + ext)
    io.open(fp, "w", encoding="utf-8").write(src)
    if preprocess:
        cc = "g++" if gxx else "gcc"
        r = subprocess.run([cc, "-E", fp], capture_output=True, text=True,
                           encoding="utf-8", errors="replace")
        if r.returncode != 0:
            return ("PRE_FAIL", r.stderr.strip()[:300])
        bad = [s for s in (expect or []) if s not in r.stdout]
        return ("PRE_FAIL", str(bad)) if bad else ("PRE_OK", "")
    exe = fp + ".exe"
    cc = "g++" if gxx else "gcc"
    stdflag = "-std=" + (std or ("c++11" if gxx else "gnu99"))
    r = subprocess.run([cc, stdflag, "-Wall", "-o", exe, fp], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    if r.returncode != 0:
        return ("COMPILE_FAIL", r.stderr.strip()[:500])
    if run is None and expect is None:
        return ("COMPILE_OK", "")
    try:
        rr = subprocess.run([exe], input=run or "", capture_output=True, text=True,
                            encoding="utf-8", errors="replace", timeout=10)
    except subprocess.TimeoutExpired:
        return ("TIMEOUT", "")
    out = rr.stdout + rr.stderr
    bad = [s for s in (expect or []) if s not in out]
    if bad:
        return ("RUN_FAIL", str(bad) + " | out=" + out[:200].replace("\n", "\\n"))
    if rr.returncode != 0:
        return ("EXIT_FAIL", "code=%d out=%s" % (rr.returncode, out[:200].replace("\n", "\\n")))
    return ("RUN_OK", out[:120].replace("\n", "\\n"))

results = {}

# --- 473 滑窗 ---
code = BLOCKS[473][0][1]
results[473] = compile_run("f473", code + """
int main(void) {
    if (length_of_longest_substring("abcabcbb") != 3) return 1;
    if (length_of_longest_substring("bbbbb") != 1) return 2;
    if (length_of_longest_substring("pwwkew") != 3) return 3;
    if (length_of_longest_substring("") != 0) return 4;
    return 0;
}
""")

# --- 478 按键（STM32 桩）---
code = BLOCKS[478][0][1]
stub = """static int fake_cnt = 0;
static int GPIO_ReadInputDataBit(int port, int pin) { (void)port; (void)pin; fake_cnt++; return (fake_cnt <= 2) ? 0 : 1; }
static void delay_ms(int ms) { (void)ms; }
#define KEY_GPIO_PORT 0
#define KEY_PIN 0
"""
results[478] = compile_run("f478", stub + code + "\nint main(void) { key_scan(); return fake_cnt >= 3 ? 0 : 1; }\n",
                           expect=["Key Pressed!"])

# --- 481 位操作 ---
code = BLOCKS[481][0][1]
results[481] = compile_run("f481", code + """
int main(void) {
    a = 0; set_bit();
    if (a != (0x1<<5)) return 1;
    clear_bit();
    if (a != 0) return 2;
    a = 0xFFFF; set_bit();
    if (a != 0xFFFF) return 3;
    a = 0; clear_bit();
    if (a != 0) return 4;
    return 0;
}
""")

# --- 482 软件 SPI（STM32 桩）---
code = BLOCKS[482][0][1]
stub = """static void GPIO_ResetBits(int port, int pin) { (void)port; (void)pin; }
static void GPIO_SetBits(int port, int pin) { (void)port; (void)pin; }
static int GPIO_ReadInputDataBit(int port, int pin) { (void)port; (void)pin; return 0; }
#define GPIOA 0
#define GPIO_Pin_10 10
#define GPIO_Pin_11 11
#define GPIO_Pin_12 12
#define GPIO_Pin_13 13
"""
results[482] = compile_run("f482", stub + code + """
int main(void) { unsigned char b[3]; spi_flash_read(0x000123UL, b, 3); return (b[0] == 0) ? 0 : 1; }
""")

# --- 483 数字滤波 ---
code = BLOCKS[483][0][1]
results[483] = compile_run("f483", code + """
static int seq[5] = {50, 10, 30, 20, 40};
static int si = 0;
static int get_adc(void) { return seq[si++ % 5]; }
int main(void) {
    int i, r;
    if (limit_filter(130, 100) != 100) return 1;   /* 差值超限取旧值 */
    if (limit_filter(110, 100) != 110) return 2;   /* 未超限取新值 */
    if (median_filter(get_adc) != 30) return 3;    /* 中值 30 */
    for (i = 0; i < 8; i++) r = avg_filter(100);   /* 第 8 次均值 100 */
    if (r != 100) return 4;
    for (i = 0; i < 8; i++) r = lpf_filter(80);    /* 一阶低通第 8 次 50 */
    if (r != 50) return 5;
    return 0;
}
""")

# --- 484 my_printf（串口桩改 stdout）---
code = BLOCKS[484][0][1]
old_putc = "static void uart_putc(char c) { /* 把 c 写入 UART 发送寄存器并等待发送完成（按平台实现） */ }"
assert old_putc in code, "484 uart_putc 行未找到"
code = code.replace(old_putc, "static void uart_putc(char c) { putchar(c); }")
results[484] = compile_run("f484", "#include <stdio.h>\n" + code + """
int main(void) {
    my_printf("v=%d u=%u x=%x X=%X c=%c s=%s w=%05d p=%p%%\\n",
              -123, 42u, 0xABC, 0xDEF, 'Q', "hi", 7, (void *)0);
    return 0;
}
""", expect=["v=-123", "u=42", "x=abc", "X=DEF", "c=Q", "s=hi", "w=00007", "p=0x00000000", "%"])

# --- 485 双向链表（含 main）---
results[485] = compile_run("f485", BLOCKS[485][0][1], expect=["0 1 2", "2 1 0"])

# --- 486 swap ---
code = BLOCKS[486][0][1]
results[486] = compile_run("f486", code + """
int main(void) {
    int x = 3, y = 9;
    swap(&x, &y);
    if (x != 9 || y != 3) return 1;
    swap_any(&x, &y, sizeof(int));
    if (x != 3 || y != 9) return 2;
    swap_xor(&x, &y);
    if (x != 9 || y != 3) return 3;
    SWAP(x, y);
    if (x != 3 || y != 9) return 4;
    swap_xor(&x, &x);            /* 同址保护：不得清零 */
    if (x != 3) return 5;
    return 0;
}
""", std="gnu99")

# --- 487 strcpy ---
results[487] = compile_run("f487", "#include <string.h>\n" + BLOCKS[487][0][1] + """
int main(void) { char b[32]; strcpy_m(b, "hello"); return (strcmp(b, "hello") == 0) ? 0 : 1; }
""")

# --- 488 strlen ---
results[488] = compile_run("f488", BLOCKS[488][0][1] + """
int main(void) {
    if (strlen_p("abc") != 3) return 1;
    if (strlen_c("") != 0) return 2;
    if (strlen_r("hello") != 5) return 3;
    return 0;
}
""")

# --- 489/490/491 含 main ---
results[489] = compile_run("f489", BLOCKS[489][0][1], run="world\n", expect=["world"])
results[490] = compile_run("f490", BLOCKS[490][0][1], expect=["helloWorld"])
results[491] = compile_run("f491", BLOCKS[491][0][1], expect=["s1 != s2"])

# --- 492 插入排序 ---
results[492] = compile_run("f492", BLOCKS[492][0][1] + """
int main(void) {
    int a[] = {5, 2, 4, 1, 3, 3, 9, -1};
    int i;
    insertion_sort(a, 8);
    for (i = 1; i < 8; i++)
        if (a[i - 1] > a[i]) return 1;
    return (a[0] == -1 && a[7] == 9) ? 0 : 2;
}
""")

# --- 493 宏展开 ---
spec_src = """#define parser(cmd,payload,len) parser ##cmd(payload,len)
void parsera(int b, int c);
int main(void) { parser(a, 1, 2); return 0; }
"""
results[493] = compile_run("f493a", spec_src, preprocess=True, expect=["parsera(1,2)"])
spec_src2 = """#define CAT_(x, y) x ## y
#define CAT(x, y)  CAT_(x, y)
#define parser(cmd, payload, len)  CAT(parser, cmd)(payload, len)
void parsera(int b, int c);
int main(void) { parser(a, 1, 2); return 0; }
"""
r2 = compile_run("f493b", spec_src2, preprocess=True, expect=["parsera(1, 2)"])
results[493] = (results[493][0] + "+" + r2[0], results[493][1] or r2[1])

# --- 494 函数指针表 ---
b0, b1, b2 = BLOCKS[494][0][1], BLOCKS[494][1][1], BLOCKS[494][2][1]
harness = """typedef struct {
    char ID;
    char (*A)(char *);
    void (*B)(void);
} MyDef;

""" + b0 + "\n" + b1 + "\n" + b2 + """
int main(void) {
    dispatch(0x12, 0);
    dispatch(0x23, 0);
    dispatch(0x99, 0);
    return 0;
}
"""
results[494] = compile_run("f494", harness)

# --- 495 拷贝函数 ---
results[495] = compile_run("f495", "#include <stddef.h>\n#include <string.h>\n" + BLOCKS[495][0][1] + """
int main(void) {
    char buf[16];
    char dst[8] = {0};
    unsigned int v = 0x11223344u, w = 0;
    if (my_strcpy_s(buf, "hello", sizeof(buf)) != buf) return 1;
    if (strcmp(buf, "hello") != 0) return 2;
    if (my_strcpy_s(dst, "toolongstring", sizeof(dst)) != dst) return 3;
    if (dst[7] != '\\0') return 4;
    my_memcpy(&w, &v, sizeof(v));
    if (w != v) return 5;
    return 0;
}
""")

# --- 496 递归倒序（含 main）---
results[496] = compile_run("f496", BLOCKS[496][0][1], run="12345\n", expect=["54321"])
r2 = compile_run("f496b", BLOCKS[496][0][1], run="-2468\n", expect=["-8642"])
results[496] = (results[496][0] + "+" + r2[0], results[496][1] or r2[1])

# --- 497 最长公共子串 ---
results[497] = compile_run("f497", BLOCKS[497][0][1], expect=["bccade", "长度 6"])

# --- 498 四函数 ---
results[498] = compile_run("f498", BLOCKS[498][0][1] + """
int main(void) {
    char b[32];
    if (my_strlen("abc") != 3) return 1;
    my_strcpy(b, "ab");
    my_strcat(b, "cd");
    if (my_strlen(b) != 4) return 2;
    if (my_strcmp("abc", "abc") != 0) return 3;
    if (my_strcmp("abc", "abd") >= 0) return 4;
    if (my_strcmp("abd", "abc") <= 0) return 5;
    return 0;
}
""")

# --- 499 makefile ---
mk = os.path.join(BASE, "mk")
os.makedirs(os.path.join(mk, "project_1"), exist_ok=True)
os.makedirs(os.path.join(mk, "project_2"), exist_ok=True)
io.open(os.path.join(mk, "Makefile"), "w", encoding="utf-8", newline="\n").write(BLOCKS[499][0][1] + "\n")
io.open(os.path.join(mk, "main.c"), "w").write('#include <stdio.h>\nint f1(void);int f2(void);int f3(void);int f4(void);\nint main(void){printf("%d\\n",f1()+f2()+f3()+f4());return 0;}\n')
io.open(os.path.join(mk, "project_1", "example_1.c"), "w").write("int f1(void){return 1;}\n")
io.open(os.path.join(mk, "project_1", "example_2.c"), "w").write("int f2(void){return 2;}\n")
io.open(os.path.join(mk, "project_2", "example_3.c"), "w").write("int f3(void){return 3;}\n")
io.open(os.path.join(mk, "project_2", "example_4.c"), "w").write("int f4(void){return 4;}\n")
r = subprocess.run(["mingw32-make"], cwd=mk, capture_output=True, text=True, encoding="utf-8", errors="replace")
if r.returncode != 0:
    results[499] = ("MAKE_FAIL", (r.stdout + r.stderr)[:300].replace("\n", "\\n"))
else:
    rr = subprocess.run([os.path.join(mk, "main.exe")], capture_output=True, text=True,
                        encoding="utf-8", errors="replace", timeout=10)
    results[499] = ("RUN_OK", rr.stdout.strip()) if rr.stdout.strip() == "10" else ("RUN_FAIL", rr.stdout)

# --- 500 判环 ---
results[500] = compile_run("f500", BLOCKS[500][0][1] + """
int main(void) {
    Node n1 = {1, 0}, n2 = {2, 0}, n3 = {3, 0}, n4 = {4, 0};
    n1.next = &n2; n2.next = &n3; n3.next = &n4; n4.next = 0;
    if (has_loop(&n1) != 0) return 1;
    if (loop_entry(&n1) != 0) return 2;
    n4.next = &n2;
    if (has_loop(&n1) != 1) return 3;
    if (loop_entry(&n1) != &n2) return 4;
    return 0;
}
""")

# --- 501 16/32 位 ---
results[501] = compile_run("f501", BLOCKS[501][0][1], gxx=True, expect=["int 占 4 字节", ">=32 位"])

# --- 502 倒三角形（含 main）---
results[502] = compile_run("f502", BLOCKS[502][0][1], run="7\n",
                           expect=["*******", " *****", "  ***", "   *"])
r2 = compile_run("f502b", BLOCKS[502][0][1], run="8\n",
                 expect=["********", " ******", "  ****", "   **"])
results[502] = (results[502][0] + "+" + r2[0], results[502][1] or r2[1])

# --- 503 零值比较（片段包装）---
code = BLOCKS[503][0][1]
code = code.replace("#include <math.h>\n", "").replace(
    "#define EPS 1e-6                        /* 容差按数据量级选取（也可用 FLT_EPSILON） */\n", "")
src = "#include <stddef.h>\n#include <math.h>\n#define EPS 1e-6\nvoid compare_zero(int n, int flag, float f, void *p)\n{\n" + code + "\n}\n" + """
int main(void) {
    compare_zero(0, 1, 0.0f, (void *)0);
    compare_zero(1, 0, 1.0f, (void *)8);
    return 0;
}
"""
results[503] = compile_run("f503", src)

# --- 504 C++ 输入10数 ---
results[504] = compile_run("f504", BLOCKS[504][0][1], gxx=True,
                           run="1 2 3 4 5 6 7 8 9 10\n",
                           expect=["1 2 3 4 5 6 7 8 9 10", "总和：55"])

# --- 505 位反转 ---
results[505] = compile_run("f505", BLOCKS[505][0][1] + """
int main(void) {
    unsigned char b[10]; int i;
    if (reverse_bits(0x01) != 0x80) return 1;
    if (reverse_bits(0xF0) != 0x0F) return 2;
    build_rev_table();
    for (i = 0; i < 10; i++) b[i] = (unsigned char)i;
    reverse_buffer(b, 10);
    if (b[1] != 0x80) return 3;
    if (b[9] != 0x90) return 4;
    return 0;
}
""")

# ---------- 4) 汇总 ----------
print()
print("=== 编译/运行验证汇总 ===")
okc = badc = skipc = 0
for n in sorted(results):
    st, det = results[n]
    mark = "PASS" if all(s in ("RUN_OK", "COMPILE_OK", "PRE_OK") for s in st.split("+")) else ("SKIP" if st == "SKIP" else "FAIL")
    if mark == "PASS": okc += 1
    elif mark == "FAIL": badc += 1
    else: skipc += 1
    print("#%d %-9s %s %s" % (n, st, mark, ("" if mark == "PASS" else det[:200])))
print()
print("无代码题（图/描述题）: 474,475,476,480 | 伪代码未编译: 479 | 片段编译: 493(预处理)")
print("PASS=%d FAIL=%d" % (okc, badc))
