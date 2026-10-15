# -*- coding: utf-8 -*-
"""编译验证：把本批 md 中的代码块配最小 harness，用 gcc 编译/运行。"""
import io, re, os, subprocess, sys
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", line_buffering=True)

P = r"D:\github\面试刷题\面试题刷题软件-大题\题库\03_编程画图大题.md"
OUT = r"D:\github\面试刷题\工具\题库批处理\_big1_compile"
os.makedirs(OUT, exist_ok=True)
lines = io.open(P, encoding="utf-8").read().split("\n")

def extract(num):
    idx = None
    in_code = False
    for i, ln in enumerate(lines):
        s = ln.rstrip()
        if s.strip().startswith("```"):
            in_code = not in_code
            continue
        if in_code:
            continue
        if re.match(rf"^\*\*{num}\.\*\*", s):
            idx = i
            break
    assert idx is not None, f"题号 {num} 未找到"
    j = idx
    while j < len(lines) and not lines[j].startswith("**标准答案："):
        j += 1
    assert j < len(lines), f"{num} 未找到标准答案行"
    code, grab = [], False
    for k in range(j, len(lines)):
        s = lines[k]
        if not grab:
            if s.strip().startswith("```c"):
                grab = True
            continue
        if s.strip() == "```":
            break
        code.append(s)
    return "\n".join(code)

NUMS = [6, 29, 32, 33, 50, 51, 75, 147, 208, 209, 264, 334, 386]
C = {n: extract(n) for n in NUMS}

H208 = ("#include <stdio.h>\n#include <stdlib.h>\nunsigned char P0;\n"
        "static int cnt = 0;\n"
        "void delayms(int ms) { (void)ms; printf(\"%02X\\n\", P0); if (++cnt >= 8) exit(0); }\n")

def wrap(n):
    code = C[n]
    if n == 6:
        return code
    if n == 29:
        return code + "\n\nint main(void){ return 0; }   /* 不调用：避免向 0x67a9 写数据 */"
    if n == 32:
        return code + "\n\nint main(void){ int t = MAX(1,2); return t == 2 ? 0 : 1; }"
    if n == 33:
        return code + "\n\nint main(void){ set_bit3(); if (a != 8) return 1; clear_bit3(); return a == 0 ? 0 : 2; }"
    if n == 50:
        return code + "\n\nint main(void){ return MIN(4,5) == 4 ? 0 : 1; }"
    if n == 51:
        return code + "\n\nint main(void){ if (!is_float_zero(1e-7f)) return 1; if (is_float_zero(1.0f)) return 2; return 0; }"
    if n == 75:
        return "int main(void){\n" + code + "\nreturn 0;\n}"
    if n == 147:
        return ("#include <stdio.h>\nstruct node { int v; struct node *next; };\n" + code + """
int main(void)
{
    struct node n1, n2, n3, n4;
    n1.v = 1; n1.next = &n2; n2.v = 2; n2.next = &n3;
    n3.v = 3; n3.next = &n4; n4.v = 4; n4.next = NULL;
    struct node *h = reverse(&n1);
    int expect = 4;
    for (struct node *p = h; p; p = p->next) { if (p->v != expect) return 1; expect--; }
    return expect == 0 ? 0 : 2;
}""")
    if n in (208, 209):
        return H208 + code
    if n == 264:
        return "int a;\n" + code + "\n\nint main(void){ a = 0; a_bit3_ops(); return a == 8 ? 0 : 1; }"
    if n == 334:
        return ("#include <stdlib.h>\nstruct node { int v; struct node *next; };\n" + code + """
int main(void)
{
    struct node *a = malloc(sizeof *a), *b = malloc(sizeof *b), *c = malloc(sizeof *c);
    a->v = 1; a->next = b; b->v = 2; b->next = c; c->v = 3; c->next = NULL;
    struct node *head = a;
    remove_node(&head, NULL, a);
    if (head != b) return 1;
    remove_node(&head, b, c);
    if (b->next != NULL) return 2;
    free(b);
    return 0;
}""")
    if n == 386:
        return code + "\n\nint main(void){ return MIN(7,3) == 3 ? 0 : 1; }"
    raise AssertionError(n)

for n in NUMS:
    src = wrap(n)
    f = os.path.join(OUT, f"f{n}.c")
    io.open(f, "w", encoding="utf-8").write(src)
    exe = os.path.join(OUT, f"f{n}.exe")
    r = subprocess.run(["gcc", "-std=c99", f, "-o", exe], capture_output=True, text=True)
    if r.returncode != 0:
        print(f"#{n}: 编译失败\n{r.stderr.strip()[:400]}")
        continue
    if n == 6:
        r2 = subprocess.run([exe], input="3\n", capture_output=True, text=True, timeout=10)
        out = r2.stdout
        nl = out.count("\n")
        ok = (nl == 5) and ("3 2 1" in out) and ("1 2 3" in out)
        print(f"#6: 编译 OK；运行(输入3) -> {nl} 行输出，含 123/321：{'通过' if ok else '异常 ' + repr(out)}")
        continue
    if n == 29:
        print("#29: 编译 OK（未运行）")
        continue
    r2 = subprocess.run([exe], capture_output=True, text=True, timeout=10)
    if n == 208:
        exp = ["08", "00"] * 4
        got = [l for l in r2.stdout.split() if l]
        print(f"#208: 运行 -> {got}，{'通过' if got == exp else '异常'}")
    elif n == 209:
        exp = ["01", "02", "04", "08", "10", "20", "40", "80"]
        got = [l for l in r2.stdout.split() if l]
        print(f"#209: 运行 -> {got}，{'通过' if got == exp else '异常'}")
    else:
        print(f"#{n}: 编译+运行 OK" if r2.returncode == 0 else f"#{n}: 运行返回码 {r2.returncode}（异常）")
