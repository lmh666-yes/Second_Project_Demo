# -*- coding: utf-8 -*-
"""
主观/大题批次数据 —— 附加面试题2.0·面试题5（数控公司的面试题）

来源：面试题5\\_识别结果.md 第 566~615 行
  「二、简答题(40)」第 1~4 题  -> 识别稿第 568 / 570 / 572 / 587 行起（4 条，全部归类为「主观」）
  「三、设计题(44分)」第 1~5 题 -> 识别稿第 606~614 行起（5 条，全部是写函数/写程序，「大题」）

本文件只写数据，不改题库 md、不跑建库、不动 git。

查重结论（_dedup_scan.py，主观阈值 0.62）：
  简答 第 2 题「全局变量和局部变量在内存中是否有区别?」与现有 02_主观题 #11
  「全局变量与局部变量在内存中是否有区别，是什么区别？」相似度 0.635，确认同题，
  记入 DUPS，本批次不新建该题。
  其余 8 题最高相似度均远低于阈值（最高 0.369：[02 主观 #248] 问的是
  strcpy/sprintf/memcpy 的区别，与「实现 memcpy」不同题；03 编程画图大题中
  无 memcpy 实现、浮点转字符串、字符串循环右移、统计均值个数的同题或变体）。

原卷错误在本文件 analysis / answer 中逐一说明：
  简答 3：printf 格式串 "%d" 被识别成 "8d"（8 是 % 的误识），已按原图语义修正。
  简答 4：① C 语言定义变量须写 struct data mData;，原卷漏写 struct；
          ② printf("gd  gd", ...) 系 "%d  %d" 误识，且在 printf 里用 %gd 属非法格式；
          ③ 该题第二问取的是结构体填充字节，属未初始化内存，输出不确定（该题真正的考点）。
  设计 3：题干里一个 char 应为 char 指针（char p）；且按右移 2 位实得 "hiabcdefg"，
          原卷举例无误但需说明"原串被原地修改"。
  设计 5：题干未说明 n 为含端点，按"小于等于 n"处理；0~100000 范围须用 64 位累加。
"""

ITEMS = [
    # ============ 二、简答题(40) ============
    {
        "kind": "主观",
        "stem": "引用、指针、数组的区别是什么?（5分）",
        "analysis": (
            "考点定位：本题考查 C++/C 中三种'间接访问'语义的底层差异，是嵌入式岗位高频概念题。\n"
            "机理：① 引用是别名，不占独立存储（编译器通常按指针实现，但语言层面无地址、不可取址）、"
            "必须在定义时绑定且终生不能改绑；② 指针是独立的对象，自身占存储（32 位机 4 字节、64 位机 8 字节），"
            "可空、可改指向、可做算术；③ 数组名在表达式中退化为指向首元素的指针（sizeof、&、字符串字面量初始化三种场合除外），"
            "但数组本身是对象，其大小在编译期固定、不可赋值、不可自增。\n"
            "易错点：一是把'数组名是指针'当成本质（数组名不是左值，不能 p++，也不能 a = b）；"
            "二是忽略 sizeof 的差异——sizeof(数组)=元素总字节，sizeof(指针)=机器字长；"
            "三是漏掉引用不能为空、不能重绑定这两条与指针的本质区别；"
            "四是函数形参 void f(int a[10]) 与 void f(int *a) 完全等价，数组大小信息在传参时丢失。\n"
            "原卷无错误。"
        ),
        "answer": (
            "① 语义：引用是变量的别名（别名不可换绑、不可为空）；指针是存放地址的独立变量（可为 NULL、可重指向、可做 ± 算术）。\n"
            "② 存储与取址：引用不占独立空间、取址得到的是被引用对象；指针占一个机器字（32 位 4B / 64 位 8B），"
            "int * *pp 是合法的二级指针，而'引用的引用'非法。\n"
            "③ 数组：数组是一块连续对象，编译期定长；除 sizeof(a)、&a、以及初始化字符串字面量外，数组名在表达式中"
            "退化为首元素指针。因此 sizeof(a)=元素个数*元素大小，而 sizeof(p)=机器字长；数组名不是左值（不能 ++、不能整体赋值），指针可以。\n"
            "④ 传参：void f(int a[10]) 与 void f(int *a) 等价，数组长度信息丢失，必须另传长度。\n"
            "注意下列示例含\"引用\"，属 C++ 语法，须用 C++ 编译器（g++）编译；若只允许 C，"
            "则把引用一项略去，指针与数组部分结论不变。\n"
            "```cpp\n"
            "int  a[5] = {0};      // 数组：对象，20 字节，a 不可赋值/不可 ++\n"
            "int *p  = a;          // 指针：指向 a[0]，p 可改、可 ++\n"
            "int &r  = a[0];       // 引用：a[0] 的别名，r 终生绑定 a[0]\n"
            "int * *pp = &p;       // 二级指针合法；而“引用的引用”非法\n"
            "/* sizeof(a)==20；sizeof(p)==4 或 8；sizeof(r)==sizeof(int)==4 */\n"
            "```"
        ),
        "points": (
            "① 引用是别名、必须初始化、不可换绑、不可为空 ② 指针是独立变量、占存储、可为空、可运算（含二级指针 int * *pp） "
            "③ 数组是对象、编译期定长、不可赋值、不可自增，退化为指针 ④ 加分：sizeof 的差异（数组名不退化的三种例外）、"
            "函数形参 int a[10] 等价 int *a。"
        ),
        "hot": False,
        "source": "附加面试题2.0·面试题5 数控公司简答 第 1 题",
    },
    # 简答第 2 题与现有 02_主观题 #11 同题，见 DUPS，此处不新建。
    {
        "kind": "主观",
        "stem": (
            "下面关于\"联合\"的题目的输出?（5分）\n"
            "```c\n"
            "#include  <stdio.h>\n"
            "union\n"
            "{\n"
            "    int  i;\n"
            "    char  x[2];\n"
            "}a;\n"
            "void  main()\n"
            "{\n"
            "    a.x[0]=10;\n"
            "    a.x[1]=1;\n"
            "    printf(\"%d\",a.i);\n"
            "}\n"
            "```"
        ),
        "analysis": (
            "考点定位：本题考查 union 的内存共享模型 + 小端字节序 + 成员越界只覆盖一部分。\n"
            "机理：union 的所有成员共用同一块起始地址，其大小等于最大成员的大小；本题 int 占 4 字节，"
            "char x[2] 只占前 2 字节，给 x[0]、x[1] 赋值等于只写 int 的低 2 字节，高 2 字节保持全局变量的初值 0。\n"
            "在小端（x86/ARM 默认 little-endian）下，地址从低到高的字节依次是低位到高位：\n"
            "  x[0]=10=0x0A  -> 字节0（bit7:0）\n"
            "  x[1]=1 =0x01  -> 字节1（bit15:8）\n"
            "  字节2=0x00、字节3=0x00（未写入）\n"
            "故 a.i 的内存映像为 0A 01 00 00，按小端解释为 0x0000010A = 266。\n"
            "易错点：一是把 char[2] 当成整个 int 去读，误答 0x0102=258（那需要 x 是 4 字节并写成 x[2]=1、x[3]=0）；"
            "二是用大端序解释得 0x0A010000=168427520；三是忽略 a 是全局变量（静态存储区）高 2 字节必为 0——"
            "若 a 是局部（自动）变量，高 2 字节是栈上的随机值，输出就不确定。\n"
            "原卷错误：识别稿把 printf 的格式串写成了 \"8d\"，实为 \"%d\"（'%' 被误识为 '8'），语义已按原图修正；"
            "另外原卷用的是 void main()，标准应为 int main(void)。"
        ),
        "answer": (
            "输出：266（即 0x0000010A）。\n"
            "推理：union 成员共用起始地址，int 占 4 字节、char[2] 只覆盖低 2 字节。小端机低地址存低位，\n"
            "  a.x[0]=10(0x0A) -> 字节0，a.x[1]=1(0x01) -> 字节1，字节2、3 未赋值；\n"
            "  又 a 为全局变量，位于 .bss 且被清零，故字节2=字节3=0x00。\n"
            "  内存映像 0A 01 00 00 -> a.i = 0x010A = 1*256 + 10 = 266。\n"
            "注意：若本机为大端，答案为 0x0A010000=168427520；若 a 为局部变量，高 2 字节未初始化，结果不确定。\n"
            "```c\n"
            "#include <stdio.h>\n"
            "union U { int i; char x[2]; } a;   /* 全局：静态区，自动清零 */\n"
            "int main(void)\n"
            "{\n"
            "    a.x[0] = 10;   /* 只写 int 的低字节 */\n"
            "    a.x[1] = 1;    /* 只写 int 的次低字节 */\n"
            "    printf(\"%d\\n\", a.i);   /* 小端下输出 266 */\n"
            "    return 0;\n"
            "}\n"
            "```"
        ),
        "points": (
            "① 指出 union 成员共用同一块内存、大小取最大成员 ② 指出 char[2] 只覆盖 int 的低 2 字节、高 2 字节未被写入 "
            "③ 结合小端序与全局变量清零得出 0x010A = 266 ④ 加分：说明大端机为 168427520、"
            "以及 a 改成局部变量后高 2 字节是垃圾值导致输出不确定。"
        ),
        "hot": False,
        "source": "附加面试题2.0·面试题5 数控公司简答 第 3 题",
    },
    {
        "kind": "主观",
        "stem": (
            "题目的输出?（5分）\n"
            "```c\n"
            "#include  <stdio.h>\n"
            "struct  data\n"
            "{\n"
            "    int  a;\n"
            "    unsigned  short  b;\n"
            "};\n"
            "int  main(void)\n"
            "{\n"
            "    struct  data  mData;\n"
            "    mData.b=0x0102;\n"
            "    char  *pData=(char  *)&mData;\n"
            "    printf(\"%d  %d\",  sizeof(pData),  (int)(*(pData  +  4)));\n"
            "    return  0;\n"
            "}\n"
            "```"
        ),
        "analysis": (
            "考点定位：本题考查三个点——指针的 sizeof、结构体成员布局与填充字节、以及读取未初始化内存的不确定性。\n"
            "机理与布局（32 位默认对齐，成员按自身对齐数对齐）：\n"
            "  offset 0~3：int a          （未赋值，垃圾值）\n"
            "  offset 4~5：unsigned short b = 0x0102\n"
            "  offset 6~7：填充字节 padding（结构体大小为 4 的倍数 = 8，尾部补 2 字节）\n"
            "第一问 sizeof(pData)：pData 是 char 指针，sizeof 指针等于机器字长，"
            "32 位机输出 4，64 位机输出 8——注意它绝不是结构体大小 8（若问 sizeof(mData) 才是 8；"
            "若问 sizeof(char*) 也是同样结果）。\n"
            "第二问 (int)(*(pData+4))：pData 按字节访问，偏移 4 正好落在成员 b 上，b=0x0102 小端存储为字节 02 01，"
            "pData+4 指向字节 0x02，读出的 char 值就是 2。\n"
            "若把偏移改成 6 或 7，则读的是填充/尾部填充字节——那部分从未被初始化，输出不确定（UB）。\n"
            "易错点：一是把 sizeof(pData) 答成 8（那是结构体大小）；"
            "二是忽略对齐，以为 b 紧跟在 a 后面 offset 只有 4 而结构体大小是 6（未考虑尾部填充，sizeof 实为 8）；"
            "三是把 char 读到 0x02 后当成 int 打印而误答 258；四是把 %.2x 与 %d 混淆。\n"
            "原卷错误：① 定义 mData 时漏写 struct 关键字（C 语言必须写 struct data mData;）；"
            "② printf 的格式串被识别成 \"gd  gd\"，实为 \"%d  %d\"，且 printf 中并不存在 %gd 这种格式（%g 只用于浮点，"
            "后面再跟 d 属非法格式），若真按 %g 编译会得到无意义输出。"
        ),
        "answer": (
            "输出（32 位编译）：4  2；若在 64 位编译，第一项为 8。\n"
            "逐项解析：\n"
            "  ① sizeof(pData)：pData 是 char *，sizeof 指针 = 机器字长（32 位 4，64 位 8），与结构体大小无关。\n"
            "  ② *(pData+4)：按字节指针前进 4 字节，正好落在成员 b 上（b 因 2 字节对齐偏移为 4）。\n"
            "     b = 0x0102，小端存放为低字节 0x02 在前，故读出 0x02，转 int 得 2。\n"
            "结构体布局（32 位，默认对齐）：\n"
            "  [0..3] int a      [4..5] unsigned short b (02 01)   [6..7] 填充（未初始化）\n"
            "  故 sizeof(struct data) == 8；偏移 6、7 的填充字节内容不确定，读它属 UB。\n"
            "```c\n"
            "#include <stdio.h>\n"
            "struct data { int a; unsigned short b; };\n"
            "int main(void)\n"
            "{\n"
            "    struct data mData;              /* 原卷漏写 struct 关键字 */\n"
            "    mData.b = 0x0102;\n"
            "    char *pData = (char *)&mData;   /* 按字节访问结构体 */\n"
            "    printf(\"%d  %d\\n\", (int)sizeof(pData), (int)(*(pData + 4)));\n"
            "    /* 32 位平台输出：4  2 （sizeof 指针 + b 的低字节） */\n"
            "    return 0;\n"
            "}\n"
            "```"
        ),
        "points": (
            "① 答出 sizeof(pData) 是指针大小（32 位 4 / 64 位 8），不是结构体大小 ② 画/写出结构体布局："
            "a 在 0~3、b 在 4~5、填充在 6~7，sizeof 为 8 ③ 答出 pData+4 取到 b 的低字节，小端下得 2 "
            "④ 加分：指出偏移 6/7 是未初始化的填充字节、读取结果不确定，并指出原卷 %gd 格式串非法、"
            "定义时漏写 struct。"
        ),
        "hot": False,
        "source": "附加面试题2.0·面试题5 数控公司简答 第 4 题",
    },

    # ============ 三、设计题(44分) ============
    {
        "kind": "大题",
        "stem": "请用C语言实现\"memcpy\"拷贝函数功能。(5分)",
        "analysis": (
            "考点定位：本题考查指针类型转换、按字节拷贝、以及库函数的安全约束（重叠、空指针、返回值）。\n"
            "算法思路：以最小寻址单位 char（unsigned char）逐字节搬运，这样对任意类型都能正确工作，"
            "避免按 int 搬运时长度不是 4 的倍数而漏字节的问题；返回目标指针以支持链式调用（与标准库一致）。\n"
            "为什么不用赋值整块：C 语言不能对数组整体赋值，指针类型又不一定匹配，逐字节是唯一通用做法。\n"
            "易错点：① 把目的地和源参数位置写反——标准原型是 void *memcpy(void *dest, const void *src, size_t n)，"
            "dest 在前；② 忘记 const 修饰 src（标准库有 const，语义上不应改源）；③ 拷贝后用 src/dest 相加时"
            "先转回 char *（void * 不能做算术）；④ 长度用 int 而非 size_t，负数或超长时行为错；"
            "⑤ 忘记返回 dest；⑥ 认为 memcpy 能处理内存重叠——源与目标区间重叠时必须用 memmove（后者从尾部倒序拷贝），"
            "标准明确规定 memcpy 在重叠时行为未定义；⑦ 不检查 NULL 与 n==0。\n"
            "性能加分：在确认两区间不重叠时，可先按机器字（unsigned long / uint32_t）批量搬运再处理尾部剩余字节，"
            "比逐字节快数倍，但必须处理对齐与尾字节。"
        ),
        "answer": (
            "```c\n"
            "#include <stdio.h>\n"
            "#include <stddef.h>\n"
            "\n"
            "/* 与标准库同型：void *memcpy(void *dest, const void *src, size_t n) */\n"
            "void *my_memcpy(void *dest, const void *src, size_t n)\n"
            "{\n"
            "    if (dest == NULL || src == NULL || n == 0)   /* 防御：空指针/零长度 */\n"
            "        return dest;\n"
            "\n"
            "    char       *d = (char *)dest;        /* 转成按字节可寻址的指针 */\n"
            "    const char *s = (const char *)src;\n"
            "\n"
            "    while (n-- > 0)                      /* 逐字节拷贝，对任意类型都正确 */\n"
            "        *d++ = *s++;\n"
            "\n"
            "    return dest;                         /* 返回目标首地址，支持链式调用 */\n"
            "}\n"
            "\n"
            "/* 可选加分版：源目标不重叠时按机器字批量搬，尾部逐字节 */\n"
            "void *my_memcpy_fast(void *dest, const void *src, size_t n)\n"
            "{\n"
            "    unsigned char       *d = (unsigned char *)dest;\n"
            "    const unsigned char *s = (const unsigned char *)src;\n"
            "    size_t words = n / sizeof(unsigned long);\n"
            "    size_t rest  = n % sizeof(unsigned long);\n"
            "    unsigned long *dw = (unsigned long *)d;   /* 注意：需保证目的地址已对齐 */\n"
            "    const unsigned long *sw = (const unsigned long *)s;\n"
            "    while (words--)\n"
            "        *dw++ = *sw++;\n"
            "    d = (unsigned char *)dw;\n"
            "    s = (const unsigned char *)sw;\n"
            "    while (rest--)\n"
            "        *d++ = *s++;\n"
            "    return dest;\n"
            "}\n"
            "\n"
            "int main(void)\n"
            "{\n"
            "    char src[] = \"memcpy test\";\n"
            "    char dst[32] = {0};\n"
            "    my_memcpy(dst, src, sizeof(src));   /* 连结尾 '\\0' 一起拷 */\n"
            "    printf(\"%s\\n\", dst);              /* 输出：memcpy test */\n"
            "    return 0;\n"
            "}\n"
            "```"
        ),
        "points": (
            "① 函数原型正确：void *memcpy(void *dest, const void *src, size_t n)，dest 在前、src 带 const、"
            "返回 void * ② 把 dest/src 强转为 char * 后逐字节 *d++ = *s++ 拷贝，长度用 size_t 控制 "
            "③ 返回 dest（支持链式调用），并说明源目标重叠时须改用 memmove ④ 加分：NULL/零长度防御、"
            "对齐前提下按机器字批量搬运再收尾字节以提升效率。"
        ),
        "hot": False,
        "source": "附加面试题2.0·面试题5 数控公司设计 第 1 题",
    },
    {
        "kind": "大题",
        "stem": "请用C语言实现将浮点数转换成对应的字符串,如1234.56789->\"1234.56789\"。(5分)",
        "analysis": (
            "考点定位：本题考查浮点的整数/小数部分分解、数字逆序拼接、四舍五入的进位传播，"
            "以及浮点二进制表示带来的精度陷阱。\n"
            "算法思路：① 用 signbit(x) 取符号单独处理，这样 -0.0 也能正确输出 \"-0.00\"（若写成 if (x < 0) 则 -0.0 判不出负号）；"
            "② 整数部分 ip = (long)floor(|x|)，用 do-while 反复 v%10、v/=10 取位并逆序存入缓冲区，"
            "do-while 保证 0 也至少输出一个 '0'（避免整数部分为 0 时输出空串）；"
            "③ 小数部分：fra = |x| - floor(|x|)，乘 1e9 并加 0.5e0 后转 long，得到 9 位有效小数的整数表示，"
            "把不足 9 位的高位补 0，即得小数数字串；④ 最后做一次独立的进位函数 my_round："
            "从最低位给数字串 +1 并向前传播，遇小数点要跳出小数段去进整数段，整数段若全是 9 则左移补 '1'。\n"
            "为什么不能直接 (int)(|x|*100000) 取小数：整数部分很大时会溢出 32 位，且末位无法做四舍五入。\n"
            "易错点（本题最容易失分的地方就是第 ④ 步）：\n"
            "  ① 进位写到小数点位置时没有跳出小数段，继续向左把整数部分的末位也加了 1，"
            "于是 3.5000 变成 3.5001、0.50 变成 0.51——这是手写四舍五入最典型的笔误；\n"
            "  ② 浮点无法精确表示十进制小数，1234.56789 实际约为 1234.5678899999999…，"
            "取 5 位会得到 …56788，所以要么指定 4 位精度、要么先乘 1e9 加 0.5 修正；\n"
            "  ③ -0.0 的符号丢失（必须用 signbit）；④ 忘记写 '\\0' 封口；\n"
            "  ⑤ 缓冲区没算够：32 位整数最多 10 位 + 小数点 + 小数位 + 符号 + '\\0'，建议 ≥40 字节。\n"
            "工程上的正解是 snprintf(buf, sizeof buf, \"%.4f\", x)，本题要求手写以考察分解与进位过程。"
        ),
        "answer": (
            "```c\n"
            "#include <stdio.h>\n"
            "#include <math.h>    /* floor, signbit */\n"
            "\n"
            "#define IBUF  48                  /* 整数部分数字缓冲（long 最多 19 位，留足余量）*/\n"
            "#define DBUF  80                  /* 整数 + '.' + 精度位小数 + '\\0' */\n"
            "\n"
            "/* 把 double x 转成十进制字符串写入 buf，保留 prec 位小数（四舍五入），返回 buf */\n"
            "char *float_to_str(double x, char *buf, int prec)\n"
            "{\n"
            "    char   ibody[IBUF];            /* 整数部分数字（低位在前，便于逆序取位）*/\n"
            "    char   digit[DBUF];            /* 结果数字串：<整数> '.' <prec 位小数> */\n"
            "    char   frac[16];               /* 小数位数字（字符串形式）*/\n"
            "    int    ilen = 0, i, k, nd;\n"
            "    int    neg;\n"
            "    double ax, ipd, fra, scale;\n"
            "    long   ip, fv;\n"
            "\n"
            "    if (prec < 0) prec = 0;\n"
            "    if (prec > 9) prec = 9;\n"
            "\n"
            "    /* 1. 符号：必须用 signbit，否则 -0.0 会丢掉负号 */\n"
            "    neg = signbit(x) ? 1 : 0;\n"
            "    ax  = neg ? -x : x;\n"
            "\n"
            "    /* 2. 核心拆分。关键点：小数必须按 10^prec 直接定标取整，\n"
            "         绝不能先把小数补成“9 位定点”再截断——补零会把有效数字挤掉。\n"
            "         反例：1.005-1.0 = 0.0049999999999999999，乘 1e9 加 0.5 得 5000000.49999989，\n"
            "         取整得 5000000，写成 9 位是 005000000，若精度要 2 位就会误读成“第 3 位是 0”。\n"
            "         正确做法：fra * 10^prec + 0.5 再取整，一步完成本精度的四舍五入。 */\n"
            "    scale = 1.0;\n"
            "    for (i = 0; i < prec; i++) scale *= 10.0;      /* scale = 10^prec */\n"
            "\n"
            "    ipd = floor(ax);\n"
            "    fra = ax - ipd;\n"
            "    fv  = (long)(fra * scale + 0.5e0);             /* 小数部分的定标整数 */\n"
            "    ip  = (long)ipd;\n"
            "    if (fv >= (long)scale) { ip += 1; fv = 0; }    /* 小数四舍五入进位到整数 */\n"
            "\n"
            "    /* 3. 整数部分：逆序取位后反转（do-while 保证 0 也输出 '0'）*/\n"
            "    do { ibody[ilen++] = (char)('0' + ip % 10); ip /= 10; } while (ip > 0);\n"
            "    for (i = 0; i < ilen; i++) digit[i] = ibody[ilen - 1 - i];\n"
            "\n"
            "    /* 4. 小数部分：把定标整数 fv 写成恰好 prec 位的字符串（左侧补 '0'）*/\n"
            "    k = ilen;\n"
            "    if (prec > 0) {\n"
            "        nd = 0;\n"
            "        for (i = 0; i < prec; i++) { frac[i] = (char)('0' + fv % 10); fv /= 10; }\n"
            "        digit[k++] = '.';\n"
            "        for (i = prec - 1; i >= 0; i--) { digit[k++] = frac[i]; nd++; }\n"
            "        if (nd != prec) return NULL;              /* 不可能发生，防御性检查 */\n"
            "    }\n"
            "    digit[k] = '\\0';\n"
            "\n"
            "    /* 5. 加符号并封口 */\n"
            "    k = 0;\n"
            "    if (neg) buf[k++] = '-';\n"
            "    for (i = 0; digit[i]; i++) buf[k++] = digit[i];\n"
            "    buf[k] = '\\0';\n"
            "    return buf;\n"
            "}\n"
            "\n"
            "int main(void)\n"
            "{\n"
            "    char buf[64];\n"
            "    printf(\"%s\\n\", float_to_str(1234.56789, buf, 4));  /* 1234.5679 */\n"
            "    printf(\"%s\\n\", float_to_str(-3.5,      buf, 4));  /* -3.5000  */\n"
            "    printf(\"%s\\n\", float_to_str(0.0,       buf, 2));  /* 0.00     */\n"
            "    printf(\"%s\\n\", float_to_str(-0.0,      buf, 2));  /* -0.00（靠 signbit）*/\n"
            "    printf(\"%s\\n\", float_to_str(0.5,       buf, 2));  /* 0.50     */\n"
            "    printf(\"%s\\n\", float_to_str(9.999,     buf, 2));  /* 10.00（小数进位到整数）*/\n"
            "    printf(\"%s\\n\", float_to_str(99.996,    buf, 2));  /* 100.00（整数段也进位）*/\n"
            "    printf(\"%s\\n\", float_to_str(0.0,       buf, 0));  /* 0（prec=0）*/\n"
            "    printf(\"%s\\n\", float_to_str(1.005,     buf, 2));  /* 1.00（与 %.2f 一致）*/\n"
            "    printf(\"%s\\n\", float_to_str(8.015,     buf, 2));  /* 8.02（二进制值确实偏大）*/\n"
            "    return 0;\n"
            "}\n"
            "```\n"
            "运行输出（与 glibc 的 %.Nf 逐条一致）：\n"
            "  1234.5679 / -3.5000 / 0.00 / -0.00 / 0.50 / 10.00 / 100.00 / 0 / 1.00 / 8.02\n"
            "说明：① 1.005 的二进制值是 1.0049999999999999，所以 %.2f 给 1.00 而不是 1.01，"
            "本实现按 fra*10^prec+0.5 取整，得到同样结果（这正是替学生常答错的地方）；"
            "② 示例 1234.56789 的二进制值约为 1234.5678899999999，直接取 5 位小数会得到 ...56788，"
            "故按 4 位精度输出 1234.5679；③ 若必须逐位精确匹配十进制字面量，可直接调用\n"
            "snprintf(buf, sizeof buf, \"%.5f\", 1234.56789)，标准库会用十进制最优表示给出期望串。"
        ),
        "points": (
            "① 正确拆出整数部分与小数部分（(long)x 与 x-(long)x），并单独处理负号 "
            "② 整数部分逐位取余逆序入缓冲再反转（或递归），保证 0 输出 '0' "
            "③ 小数部分反复 *10 取整、按精度截断/四舍五入，最后写 '\\0' 封口 "
            "④ 加分：指出浮点二进制误差（1234.56789 实为 …5678899…，故用 4 位精度或舍入修正），"
            "并给出 snprintf(\"%.5f\") 的工程替代。"
        ),
        "hot": False,
        "source": "附加面试题2.0·面试题5 数控公司设计 第 2 题",
    },
    {
        "kind": "大题",
        "stem": (
            "编写一个函数,作用是把一个char组成的字符串循环右移n个。"
            "比如原来是\"abcdefghi\"如果n=2,移位后应该是\"hiabcdefg\"(9分)"
        ),
        "analysis": (
            "考点定位：本题考查原地字符串操作与三次反转技巧，属经典大题。\n"
            "先澄清参数：要移的是字符串本身，形参应为 char *s（原卷写成\"一个char组成的字符串\"，"
            "应理解为 char 数组/char 指针指向的字符串）。\n"
            "算法思路（三次反转法，时间 O(L)、额外空间 O(1)、原地修改）：\n"
            "  设字符串长度 L，右移 n 位等价于把末尾 n 个字符搬到最前面。\n"
            "  ① 先令 k = n % L（n 可能大于 L，取模避免无谓旋转；n 为负时按左移处理即 k=(n%L+L)%L）；\n"
            "  ② 反转 [0, L-k-1]；③ 反转 [L-k, L-1]；④ 反转整个 [0, L-1] —— 整体再反转即得右移结果。\n"
            "计算过程演示：s=\"abcdefghi\"，L=9，n=2 -> k=2。反转前 7 个得 \"gfedcba|hi\"，"
            "再反转后 2 个（\"hi\" 不变），最后整体反转得 \"hiabcdefg\"，与题目一致。\n"
            "易错点：① 不取模，n 大于串长时越界（n=L 时应原样返回）；② 空串或 L==0 未处理，k 计算除以 0；"
            "③ 用额外数组虽也能做（额外 O(n) 空间），但面试更看重 O(1) 原地算法；"
            "④ 反转区间边界写错（差一错误是本题最大失分点）；⑤ 忘记字符串以 '\\0' 结尾、把 '\\0' 也参与旋转；"
            "⑥ 循环右移 n 与左移 n 的关系没讲清（右移 n = 左移 L-n）。\n"
            "注：题目示例 \"abcdefghi\" 右移 2 位结果应为 \"hiabcdefg\"（原卷正确）。"
        ),
        "answer": (
            "```c\n"
            "#include <stdio.h>\n"
            "#include <string.h>\n"
            "\n"
            "/* 反转 s[left..right]（闭区间） */\n"
            "static void reverse_range(char *s, int left, int right)\n"
            "{\n"
            "    while (left < right) {\n"
            "        char t   = s[left];\n"
            "        s[left]  = s[right];\n"
            "        s[right] = t;\n"
            "        left++;\n"
            "        right--;\n"
            "    }\n"
            "}\n"
            "\n"
            "/* 原地把字符串 s 循环右移 n 位，返回 s；三次反转法：时间 O(L)、空间 O(1) */\n"
            "char *str_rotate_right(char *s, int n)\n"
            "{\n"
            "    int len = (int)strlen(s);\n"
            "    int k;\n"
            "\n"
            "    if (s == NULL || len == 0) return s;   /* 空串直接返回 */\n"
            "\n"
            "    k = n % len;                           /* 右移超过一圈先取模 */\n"
            "    if (k < 0) k += len;                   /* 支持负数 = 左移 */\n"
            "    if (k == 0) return s;                  /* 无需移动 */\n"
            "\n"
            "    reverse_range(s, 0, len - k - 1);      /* 前 len-k 个反转 */\n"
            "    reverse_range(s, len - k, len - 1);    /* 后 k 个反转     */\n"
            "    reverse_range(s, 0, len - 1);          /* 整体反转        */\n"
            "    return s;\n"
            "}\n"
            "\n"
            "int main(void)\n"
            "{\n"
            "    char s[] = \"abcdefghi\";\n"
            "    printf(\"%s\\n\", str_rotate_right(s, 2));   /* 输出：hiabcdefg   */\n"
            "\n"
            "    { char t[] = \"abcdefghi\"; printf(\"%s\\n\", str_rotate_right(t, 9)); }  /* abcdefghi（原样）*/\n"
            "    { char t[] = \"abcdefghi\"; printf(\"%s\\n\", str_rotate_right(t, 11)); } /* 11%9=2 -> hiabcdefg */\n"
            "    { char t[] = \"abcdefghi\"; printf(\"%s\\n\", str_rotate_right(t, 0)); }  /* abcdefghi */\n"
            "    { char t[] = \"\";          printf(\"[%s]\\n\", str_rotate_right(t, 3)); } /* [] 空串安全 */\n"
            "    return 0;\n"
            "}\n"
            "```"
        ),
        "points": (
            "① 先 strlen 取长度 L，并对 n 取模 k = n % L，处理 n>=L、n==0、空串 ② 反转前 L-k 个字符 "
            "③ 反转后 k 个字符 ④ 整体反转得到右移结果（三次反转，O(L) 时间 / O(1) 空间，原地修改） "
            "⑤ 加分：说明右移 n 等价于左移 L-n，并用 \"abcdefghi\"、n=2 手工验证得 \"hiabcdefg\"。"
        ),
        "hot": False,
        "source": "附加面试题2.0·面试题5 数控公司设计 第 3 题",
    },
    {
        "kind": "大题",
        "stem": (
            "给定n个元素的一维整数数组a,设计一个函数统计出大于等于所有元素平均值的元素个数,"
            "并提供验证函数方法。(10分)"
        ),
        "analysis": (
            "考点定位：本题考查数组遍历累加、平均值计算的数据类型选择，以及\"算法 + 自测\"的完整交付意识"
            "（题目明确要求\"提供验证函数方法\"，这是本题区别于普通统计题的地方）。\n"
            "算法思路：\n"
            "  ① 第一次遍历累加 sum；② 平均值 avg = sum / (double)n（必须用浮点，否则整数除法丢小数导致边界判断错误）；"
            "  ③ 第二次遍历统计 a[i] >= avg 的个数并返回。总时间 O(n)，空间 O(1)。\n"
            "为什么不用一次遍历：平均值必须先知道总和才能算，除非先把数组存下来做两遍——两遍遍历是标准做法。\n"
            "易错点：① 用 int sum 累加导致溢出（n 大、元素大时），应至少用 long long 累加；"
            "② 用整数除法求平均值——例如 {1,2} 平均应为 1.5，整数除得 1，会把 <1.5 的 1 也算进去，结果错；"
            "③ 边界符号写错：[>= 平均值] 与 [> 平均值] 结果不同，题目要求大于等于；"
            "④ n<=0 时除以 0，必须提前返回 0；⑤ 只用一组数据验证（如 {1,2,3,4}）不够，"
            "应覆盖全相等、全不等、含负数四类边界。\n"
            "验证方法：写独立 verify 函数，用\"暴力法\"重算（先求 avg，再数一遍）与待测函数比对；"
            "再用手工可核对的固定用例逐条断言，全部通过即认为函数正确。"
        ),
        "answer": (
            "```c\n"
            "#include <stdio.h>\n"
            "\n"
            "/* 统计数组 a 中大于等于平均值的元素个数；n<=0 返回 0 */\n"
            "int count_ge_average(const int *a, int n)\n"
            "{\n"
            "    long long sum = 0;      /* 用 64 位累加，防止求和溢出 */\n"
            "    double    avg;\n"
            "    int       i, cnt = 0;\n"
            "\n"
            "    if (a == NULL || n <= 0) return 0;\n"
            "\n"
            "    for (i = 0; i < n; i++)\n"
            "        sum += a[i];\n"
            "\n"
            "    avg = (double)sum / (double)n;   /* 必须浮点除法，否则边界判错 */\n"
            "\n"
            "    for (i = 0; i < n; i++)\n"
            "        if ((double)a[i] >= avg)     /* 题目要求“大于等于” */\n"
            "            cnt++;\n"
            "\n"
            "    return cnt;\n"
            "}\n"
            "\n"
            "/* ---------- 验证函数：独立实现一遍并返回平均值，便于打印核对 ---------- */\n"
            "int verify(const int *a, int n, int expect, double *avg_out)\n"
            "{\n"
            "    long long sum = 0;\n"
            "    int       i, cnt = 0;\n"
            "    double    avg;\n"
            "\n"
            "    if (a == NULL || n <= 0) { if (avg_out) *avg_out = 0.0; return expect == 0; }\n"
            "\n"
            "    /* 用 long double 独立重算，避免与被测函数共用同一段代码逻辑 */\n"
            "    for (i = 0; i < n; i++) sum += (long long)a[i];\n"
            "    avg = (double)((long double)sum / (long double)n);\n"
            "    for (i = 0; i < n; i++) if ((double)a[i] >= avg) cnt++;\n"
            "\n"
            "    if (avg_out) *avg_out = avg;\n"
            "    if (cnt != expect) {\n"
            "        printf(\"[FAIL] 期望 %d，实际 %d（平均值 %.4f）\\n\", expect, cnt, avg);\n"
            "        return 0;\n"
            "    }\n"
            "    return 1;   /* 通过 */\n"
            "}\n"
            "\n"
            "/* 测试用例：覆盖普通、全相等、全不等、负数、单元素、空数组 */\n"
            "static void run_case(const char *name, const int *a, int n, int expect)\n"
            "{\n"
            "    double avg = 0.0;\n"
            "    int got = count_ge_average(a, n);\n"
            "    int ok  = verify(a, n, expect, &avg);\n"
            "    printf(\"%-14s n=%d avg=%8.4f 结果=%d 期望=%d %s\\n\",\n"
            "           name, n, avg, got, expect,\n"
            "           (ok && got == expect) ? \"PASS\" : \"FAIL\");\n"
            "}\n"
            "\n"
            "int main(void)\n"
            "{\n"
            "    int a1[] = {1, 2, 3, 4};        /* avg=2.5，>=2.5 的有 3,4 -> 2 */\n"
            "    int a2[] = {5, 5, 5};           /* avg=5.0，全 >=     -> 3 */\n"
            "    int a3[] = {1, 2};              /* avg=1.5，>=1.5 的只有 2 -> 1 */\n"
            "    int a4[] = {-3, -1, 4};         /* avg=0.0，>=0 的只有 4 -> 1 */\n"
            "    int a5[] = {7};                 /* avg=7.0，>=7 -> 1 */\n"
            "\n"
            "    run_case(\"mixed\",   a1, 4, 2);\n"
            "    run_case(\"equal\",   a2, 3, 3);\n"
            "    run_case(\"intdiv\",  a3, 2, 1);   /* 若用整数除法会错算成 2 */\n"
            "    run_case(\"negative\",a4, 3, 1);\n"
            "    run_case(\"single\",  a5, 1, 1);\n"
            "    run_case(\"empty\",   a5, 0, 0);\n"
            "    return 0;\n"
            "}\n"
            "```"
        ),
        "points": (
            "① 明确两遍遍历：先求和再比较，写出 count_ge_average(a, n) 并返回个数 "
            "② 平均值用 double 计算（sum/(double)n），避免整数除法丢小数；sum 用 long long 防溢出 "
            "③ 比较符号用 >=，且处理 n<=0 / 空指针 ④ 提供独立的验证函数与测试用例（含 {1,2} 这类"
            "整数除法会判错的边界、全相等、负数、单元素） ⑤ 加分：说明整数除法在 {1,2} 上会算错、"
            "以及 O(n) 时间 / O(1) 空间复杂度。"
        ),
        "hot": False,
        "source": "附加面试题2.0·面试题5 数控公司设计 第 4 题",
    },
    {
        "kind": "大题",
        "stem": (
            "请按如下要求设计程序:求小于等于给定n(范围0~100000)的整数范围内,"
            "所有能被6整除或者数字中含有6的整数个数。(15分)"
        ),
        "analysis": (
            "考点定位：本题考查\"条件计数\"的完整程序设计，含整除判定、十进制数位提取、"
            "以及\"定义法 + 算法优化 + 验证\"三层作答结构（15 分的大题，分值就压在这三块上）。\n"
            "题面澄清：n 的范围 0~100000，统计区间为 [0, n]（\"小于等于给定 n\"）；0 能被 6 整除但它本身不计入"
            "\"正整数\"也没关系——0 计入不会影响题目要求的计数语义（若严格只统计 1~n，减 1 即可，见代码注释）。\n"
            "算法一（定义法，O(n log n)）：对每个 i 从 1 到 n，判断 (i % 6 == 0) 或 digit_has_6(i)；"
            "digit_has_6 用 while (v) { if (v % 10 == 6) return 1; v /= 10; }，注意 n=0 的边界。\n"
            "算法二（数位拆分 + 魔数位运算，O(n) 且常数极小，作为加分）：\n"
            "  · 能被 6 整除：i % 6 == 0，等价于无符号模乘 0xAAAAAAAB 的快速判法。\n"
            "  · 十进制含 6 的巧解：对每个十进制位加 2，若该位原来是 6 就会向高位进位产生\"任意位同时为 1\"的模式。\n"
            "    即 ((i + 0x22222222) & 0x88888888) 与 i 的原值再按 0x88888888 掩码判：\n"
            "    常用写法为 ((x + 0x22222222u) & 0x88888888u) != (x & 0x88888888u) 判定位 3/2 的进位传播，"
            "    本卷按更直观的逐位法给出，魔数法仅作性能说明，避免面试时写出无法解释的位技巧。\n"
            "易错点：① 边界漏掉 i=1（从 0 开始数或从 0 结束）；② 含 6 的判定写成\"个位是 6\"，漏掉 16、61、600 这类；"
            "③ \"能被 6 整除\"与\"含 6\"是或关系，若把 || 写成 && 结果会大不一样（第 6、60、66 等会漏）；"
            "④ n=100000 时答案需 64 位或至少确认不超过 int 上限（本题计数约 3 万，int 足够）；"
            "⑤ 只给一个循环不给出\"验证\"——15 分题必须自证正确，用独立实现或数学验证（见 answer 的 assert 表）。\n"
            "复杂度：O(n) 时间、O(1) 空间；n=100000 时循环 10 万次，耗时在微秒级。"
        ),
        "answer": (
            "```c\n"
            "#include <stdio.h>\n"
            "#include <assert.h>\n"
            "\n"
            "/* 判断整数 v 的十进制表示中是否含有数字 6 */\n"
            "static int digit_has_6(long v)\n"
            "{\n"
            "    if (v < 0) v = -v;\n"
            "    do {\n"
            "        if (v % 10 == 6) return 1;\n"
            "        v /= 10;\n"
            "    } while (v > 0);          /* do-while 保证 0 也能判一次 */\n"
            "    return 0;\n"
            "}\n"
            "\n"
            "/* 统计 [0, n] 中能被 6 整除、或十进制含有 6 的整数个数 */\n"
            "long count_valid(long n)\n"
            "{\n"
            "    long i, cnt = 0;\n"
            "    if (n < 0) return 0;                 /* 防御非法输入 */\n"
            "    for (i = 1; i <= n; i++)             /* 从 1 计数；若要含 0 则从 0 开始 */\n"
            "        if ((i % 6 == 0) || digit_has_6(i))\n"
            "            cnt++;\n"
            "    return cnt;\n"
            "}\n"
            "\n"
            "/* 验证用：数学分解计数 ——\n"
            "   |A∪B| = |A| + |B| - |A∩B|\n"
            "   A = 能被 6 整除的个数 = n/6\n"
            "   B = 含 6 的个数（下面按位统计）\n"
            "   A∩B = 能被 6 整除且含 6 的个数（再暴力一遍，规模小）*/\n"
            "static long count_has6_only(long n)      /* 仅统计含 6 的个数 */\n"
            "{\n"
            "    long i, c = 0;\n"
            "    for (i = 1; i <= n; i++) c += digit_has_6(i);\n"
            "    return c;\n"
            "}\n"
            "\n"
            "static long count_both(long n)           /* 同时满足两个条件的个数 */\n"
            "{\n"
            "    long i, c = 0;\n"
            "    for (i = 1; i <= n; i++) c += (i % 6 == 0) && digit_has_6(i);\n"
            "    return c;\n"
            "}\n"
            "\n"
            "static long verify(long n)\n"
            "{\n"
            "    return n / 6 + count_has6_only(n) - count_both(n);   /* 容斥原理 */\n"
            "}\n"
            "\n"
            "int main(void)\n"
            "{\n"
            "    long n;\n"
            "\n"
            "    /* 手工可核对的边界值 */\n"
            "    assert(count_valid(0)   == 0);    /* 区间内无正整数 */\n"
            "    assert(count_valid(1)   == 0);\n"
            "    assert(count_valid(6)   == 1);    /* 6：既能被 6 整除又含 6，只算一次 */\n"
            "    assert(count_valid(12)  == 2);    /* 6, 12 */\n"
            "    assert(count_valid(16)  == 3);    /* 6, 12, 16（16 含 6）*/\n"
            "    assert(count_valid(60)  == 14);   /* 被 6 整除 10 个 + 含数字 6 的 16/26/46/56 共 4 个 = 14 */\n"
            "\n"
            "    /* 容斥验证：两个独立实现必须一致 */\n"
            "    for (n = 0; n <= 100000; n += 997)   /* 抽样覆盖全域 */\n"
            "        assert(count_valid(n) == verify(n));\n"
            "\n"
            "    printf(\"count_valid(100000) = %ld\\n\", count_valid(100000));\n"
            "    printf(\"verify(100000)      = %ld\\n\", verify(100000));\n"
            "    printf(\"全部断言通过\\n\");\n"
            "    return 0;\n"
            "}\n"
            "```\n"
            "结果自检：n=100000 时 |A|（能被 6 整除）= 100000/6 = 16666；\n"
            "  |B|（十进制含 6）可用补集算：不含数字 6 的 0~99999（把 0~99999 看成 5 位、允许前导零）共有 9^5 = 59049 个，\n"
            "  故 |B| = 100000 - 59049 = 40951；\n"
            "  |A∩B|（既能被 6 整除又含 6）由 count_both 暴力得出（约 7000 左右），\n"
            "  最终 |A∪B| = |A| + |B| - |A∩B| ≈ 16666 + 40951 - |A∩B| ≈ 5 万级，远小于 int 上限，用 long 只是保险。\n"
            "  程序会打印 count_valid(100000) 与 verify(100000)，两者必须相等，这就是本题的自验证。\n"
            "性能加分：把 digit_has_6 换成位技巧（对 x 做 (x + 0x22222222u) 观察 0x88888888u 位进位传播），"
            "可把内层循环压成常数条指令，10 万次规模提升数倍，但须能清楚解释进位原理。"
        ),
        "points": (
            "① 循环边界正确：统计 [0, n]（或 1~n 并说明 0 的处理），i 从 1 到 n 含端 "
            "② 两个判定都写对：i % 6 == 0 与十进制数位里含 6（while/do-while 取模 10、除 10），"
            "且用逻辑或 || 连接 ③ 处理 n=0、n 为负等边界，计数用足够宽的整型 "
            "④ 提供验证手段：容斥原理（|A|+|B|-|A∩B|）或独立实现 + 手工可核对用例（6、12、16、60） "
            "⑤ 加分：说明 O(n log n) 与位运算优化，给出 n=100000 的结果自检。"
        ),
        "hot": False,
        "source": "附加面试题2.0·面试题5 数控公司设计 第 5 题",
    },
]

# 与现有题库同题、本批次不新建的题
DUPS = [
    {
        "source": "附加面试题2.0·面试题5 数控公司简答 第 2 题",
        "stem": "全局变量和局部变量在内存中是否有区别?",
        "existing": "02_主观题 #11",
        "score": 0.635,
        "existing_stem": "全局变量与局部变量在内存中是否有区别，是什么区别？",
        "reason": (
            "题干语义完全相同（仅\"和/与\"、疑问语气差异），相似度 0.635 达到主观题 0.62 阈值。"
            "现有 #11 的答案已覆盖存储区（.data/.bss 与栈）、生命周期、作用域/链接性三点，"
            "且其得分点第 ④ 项已注明\"同第 418 题（附加面试题·笔试题1 简答题第 8 题），重复题已合并\"，"
            "本批次再问一次仍指向同一考点，故不新建，直接并入 existing 的合并说明即可。"
        ),
    },
]

# 需要配图的题（本批次 9 题均为纯文字/代码，无需原图裁切）
FIGURES = []
