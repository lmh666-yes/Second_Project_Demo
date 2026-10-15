# -*- coding: utf-8 -*-
"""大题补充题 516~526 数据（排序 / 查找 / 数据结构 函数写法专项，2026-10-05）
- 每题为 dict：stem / ana（题目解析）/ ans（标准答案，含 ```c 代码）/ exp（得分点）/ hot
- 正文严禁 ** 序列（解析器全局剔除）；代码必须放 ``` 围栏
- 热点：516 否、517 是、518 是、519 是、520 是、521 否、522 否、523 是、524 否、525 是、526 否（新增 6 个热点，总热点 76/152）
"""

NEW = {}

NEW[516] = dict(
    stem="请写出折半插入排序（binary insertion sort）的完整函数，并说明它相比直接插入排序改进在哪里、为什么总体复杂度仍是 O(n^2)。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""折半插入排序 = 直接插入排序 + 用折半查找确定插入位置。序列前 i 个元素已有序，处理 a[i] 时不再从后往前逐个比较，而是对有序区 a[0..i-1] 做二分，找到「第一个大于 key 的位置」lo，再把 a[lo..i-1] 整体后移一格、把 key 放进 a[lo]。改进点：查找位置从 O(i) 次比较降为 O(log i) 次，总比较次数从 O(n^2) 降到 O(n log n)——当「比较」比「移动」昂贵时（如记录很大的结构体、比较器带复杂逻辑）收益明显。为什么总数仍是 O(n^2)：元素的移动次数没有变——每次插入平均仍要搬移 i/2 个元素，总共约 n^2/4 次移动，移动量级决定时间复杂度仍为 O(n^2)（空间 O(1)）。稳定性：填坑判据写 a[mid] <= key 时向右走（lo = mid + 1），相等元素插在其后，保持稳定；若写成 a[mid] < key 则不稳定。适用场景：数据量不大、移动代价可接受但比较代价高；教学上用它说明「减少比较次数」与「减少移动次数」是两件事。延伸：把「1 个子序列内部插入」推广为「按增量 gap 分成多组插入」就是希尔排序。易错点：二分区间与插入位置语义写错（hi 取 i 还是 i-1）；循环结束时插入位置是 lo 不是 hi；后移方向从后往前（否则覆盖）；等号方向影响稳定性。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>

/* 折半插入排序（升序）：前 i 个已有序，用二分找第一个 > key 的位置，再整体后移插入 */
void binary_insertion_sort(int a[], int n)
{
    int i, j, key, lo, hi, mid;

    for (i = 1; i < n; i++) {
        key = a[i];
        lo = 0;
        hi = i;                        /* 在 a[0..i-1] 中找第一个 > key 的位置 */
        while (lo < hi) {
            mid = (lo + hi) / 2;
            if (a[mid] <= key) lo = mid + 1;   /* <= 放右边 → 相等元素保持原序（稳定） */
            else               hi = mid;
        }
        for (j = i - 1; j >= lo; j--)  /* 腾位置：整体后移一格（只移动不比较） */
            a[j + 1] = a[j];
        a[lo] = key;                   /* 放入正确位置 */
    }
}

static void print_array(const int a[], int n)
{
    int i;
    for (i = 0; i < n; i++) printf("%d%c", a[i], i + 1 == n ? '\n' : ' ');
}

int main(void)
{
    int a[] = {49, 38, 65, 97, 76, 13, 27, 49};
    int n = sizeof(a) / sizeof(a[0]);

    binary_insertion_sort(a, n);
    printf("折半插入排序：");
    print_array(a, n);                 /* 13 27 38 49 49 65 76 97 */
    return 0;
}
```
要点：二分只降低比较次数（O(n log n) 次比较）；移动次数不变（约 n^2/4 次），时间仍是 O(n^2)；a[mid] <= key 向右走保证稳定；插入位置是循环结束时的 lo。""",
    exp="①二分找插入位置 + 后移 + 放入三步正确（边界 lo/hi 与插入位置不差一）②说清「比较 O(n log n) 但移动 O(n^2) → 总体仍 O(n^2)」③加分：稳定性判据（<= 向右）、与希尔排序的关系、或适用场景（比较昂贵、移动便宜）。",
    hot=False,
)

NEW[517] = dict(
    stem="请写出顺序查找的完整函数（含「哨兵」优化写法），说明查找成功 / 失败的平均查找长度 ASL 各是多少，并比较它与折半查找的差异与适用场景。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""顺序查找（线性查找）从表的一端逐个比较，对存储结构无要求（数组、链表都能用），也不要求有序。普通写法每轮要判断两个条件：下标是否越界、元素是否等于 key。哨兵优化：把 key 先存入 0 号位置（数据放在 a[1..n]），从后往前找，由于哨兵位必然相等，循环一定会在某个位置停下，于是可以去掉越界判断，每轮只剩一次比较，常数级提速（复杂度不变）。判据约定：返回 0 表示「只匹配到哨兵」即未找到，返回 1..n 表示找到。平均查找长度（各元素等概率）：成功的 ASL = (1+2+…+n)/n = (n+1)/2；失败的 ASL：无序表需要比较完 n 个元素，为 n；若表本身有序，失败时遇到第一个大于 key 的元素即可停止，失败 ASL 约为 n/2（可提前终止）。与折半查找对比：折半要求顺序存储且有序、比较次数 O(log n)（判定树高度 ⌊log2 n⌋+1），但插入/删除要维持有序代价高；顺序查找 O(n) 但胜在简单、支持无序、支持链式存储。选择原则：表小、频繁插入删除、无序、链式存储 → 顺序查找；表大、基本静态、有序数组 → 折半。易错点：哨兵版返回 0 与「下标 0」混淆；哨兵位置与数据区间弄错（写成 a[0..n-1] 再放哨兵会丢数据）；ASL 公式记混。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>

#define SEARCH_MAX 128

/* 普通顺序查找：在 a[0..n-1] 中找 key，找到返回下标，否则返回 -1 */
int seq_search(const int a[], int n, int key)
{
    int i;
    for (i = 0; i < n; i++)
        if (a[i] == key) return i;
    return -1;
}

/* 哨兵版：数据放在 a[1..n]，a[0] 存 key 作哨兵——循环必然停下，省掉越界判断；
   返回 0 表示未找到（只匹配到哨兵），返回 1..n 为找到的位置（1 基） */
int seq_search_sentinel(int a[], int n, int key)
{
    int i = n;
    a[0] = key;
    while (a[i] != key) i--;           /* 不用写 i >= 0：哨兵兜底 */
    return i;
}

int main(void)
{
    int data[] = {7, 3, 9, 5, 11, 3};
    int n = sizeof(data) / sizeof(data[0]);
    int a[SEARCH_MAX + 1];
    int i, r;

    for (i = 0; i < n; i++) a[i + 1] = data[i];   /* 数据搬到 a[1..n]，a[0] 留给哨兵 */

    printf("普通版查 11 -> 下标 %d\n", seq_search(data, n, 11));   /* 4 */
    printf("普通版查 4  -> %d\n", seq_search(data, n, 4));         /* -1 */
    r = seq_search_sentinel(a, n, 9);
    printf("哨兵版查 9  -> 位置 %d\n", r);                          /* 3 */
    r = seq_search_sentinel(a, n, 8);
    printf("哨兵版查 8  -> %d（0 = 未找到）\n", r);                  /* 0 */
    return 0;
}
```
要点：成功 ASL =(n+1)/2；无序表失败 ASL = n，有序表可提前终止（≈n/2）；哨兵把每轮两次判断降为一次；折半 O(log n) 但要求有序 + 顺序存储。""",
    exp="①普通版与哨兵版都正确，能说明哨兵省掉的是哪种判断（越界）②给出成功 ASL=(n+1)/2，并说明失败代价（无序 n / 有序可提前终止）③加分：折半对比与选型依据（无序或链式用顺序查找、静态有序表用折半）。",
    hot=True,
)

NEW[518] = dict(
    stem="请说明 C 标准库的排序函数 qsort 与查找函数 bsearch 的函数原型与比较函数的写法约定，并给出完整示例：整数升序、整数降序、结构体多关键字排序、字符串数组排序、以及用 bsearch 查找。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""qsort 原型：void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))，对任意类型数组原地排序，比较函数由调用者提供；bsearch 原型：void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))，前提是数组已按「同一个比较函数」排好序，命中返回指向元素的指针（未命中返回 NULL），比较键 key 也按同规则传入。比较函数约定：参数是待比较两个元素的地址（const void *），需强转为实际类型；返回值 <0 / 0 / >0 分别表示 a<b / a==b / a>b——不是布尔值，写反或只返回 0/1 会导致排序错乱。整数比较的推荐写法 (a > b) - (a < b)：显式三值且不溢出；常见坑是直接 return a - b，两数接近 int 上下限时相减溢出（UB）。降序：交换参数调用升序比较函数，或把返回值取反。字符串用 strcmp（比的是字典序，按 unsigned char）。结构体多关键字：先比主关键字，相等再比次关键字（如分数降序 + 同分按姓名升序）。qsort 不保证稳定（实现依库而定），需要稳定时用归并或自行加原始下标做次关键字。bsearch 的 size 参数是「单个元素大小」，写错会读到错位数据；对未排序数组调用 bsearch 结果未定义。易错点：a-b 溢出；比较函数与排序函数不一致（bsearch 查不到）；忘记 & 取地址或强转；把字符串数组与字符指针的层级写错（const char * const *）。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[16];
    int  score;
} stu_t;

/* 比较函数三值约定：<0 表示 a 应排在 b 前（升序），>0 反之，0 相等。
   (a > b) - (a < b) 显式给出三值，且不会像 a - b 那样溢出。 */
static int cmp_int_asc(const void *p, const void *q)
{
    int a = *(const int *)p, b = *(const int *)q;
    return (a > b) - (a < b);
}

/* 降序：交换参数借用升序比较（等价于对升序结果取反） */
static int cmp_int_desc(const void *p, const void *q)
{
    return cmp_int_asc(q, p);
}

/* 结构体多关键字：分数降序；同分按姓名升序 */
static int cmp_stu(const void *p, const void *q)
{
    const stu_t *a = (const stu_t *)p, *b = (const stu_t *)q;
    if (a->score != b->score)
        return (a->score < b->score) - (a->score > b->score);   /* 降序 */
    return strcmp(a->name, b->name);                            /* 升序 */
}

/* 字符串比较：元素类型是 char *，参数要转成 char * const * 再取一层 */
static int cmp_str(const void *p, const void *q)
{
    return strcmp(*(const char * const *)p, *(const char * const *)q);
}

int main(void)
{
    int nums[] = {42, 7, 19, 73, 7, -5, 100};
    int desc[] = {42, 7, 19, 73, 7, -5, 100};
    int n = sizeof(nums) / sizeof(nums[0]);
    int key, *hit;
    int i;

    qsort(nums, n, sizeof(nums[0]), cmp_int_asc);
    printf("升序：");
    for (i = 0; i < n; i++) printf("%d%c", nums[i], i + 1 == n ? '\n' : ' ');

    qsort(desc, n, sizeof(desc[0]), cmp_int_desc);
    printf("降序：");
    for (i = 0; i < n; i++) printf("%d%c", desc[i], i + 1 == n ? '\n' : ' ');

    /* bsearch 前提：数组已按同一比较函数排好序 */
    key = 73;
    hit = (int *)bsearch(&key, nums, n, sizeof(nums[0]), cmp_int_asc);
    printf("bsearch 查 73 -> %s\n", hit ? "命中" : "未命中");     /* 命中 */
    key = 50;
    hit = (int *)bsearch(&key, nums, n, sizeof(nums[0]), cmp_int_asc);
    printf("bsearch 查 50 -> %s\n", hit ? "命中" : "未命中");     /* 未命中 */

    stu_t all[] = {{"Lee", 90}, {"Ann", 95}, {"Bob", 90}, {"Zoe", 88}};
    int m = sizeof(all) / sizeof(all[0]);
    qsort(all, m, sizeof(all[0]), cmp_stu);
    printf("成绩榜：");
    for (i = 0; i < m; i++)
        printf("%s(%d)%c", all[i].name, all[i].score, i + 1 == m ? '\n' : ' ');
    /* 输出：Ann(95) Bob(90) Lee(90) Zoe(88) */

    const char *words[] = {"pear", "apple", "banana"};
    int w = sizeof(words) / sizeof(words[0]);
    qsort(words, w, sizeof(words[0]), cmp_str);
    printf("字符串升序：");
    for (i = 0; i < w; i++) printf("%s%c", words[i], i + 1 == w ? '\n' : ' ');
    return 0;
}
```
要点：比较函数返回三值（<0/0/>0）、参数强转、防溢出用 (a>b)-(a<b)；降序交换参数；字符串 strcmp；bsearch 必须与排序使用同一比较函数；qsort 不保证稳定。""",
    exp="①写出正确原型与三值比较函数约定 ②四个示例（升/降序、结构体多关键字、字符串、bsearch）均可用且输出正确 ③加分：a-b 溢出坑、qsort 不稳定、bsearch 前提与「比较函数必须与排序一致」、或 void* 语义解释。",
    hot=True,
)

NEW[519] = dict(
    stem="请写出对无序单链表（带头结点）进行直接插入排序的完整函数，并说明指针操作要点、复杂度与稳定性。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""链表插入排序与数组版思想相同（已排序部分 + 待插入结点），但实现方式完全不同：数组靠「后移腾位 + 放值」，链表靠「摘链 + 找位 + 重新接链」。做法：先让已排序部分只含第 1 个结点（sorted），从第二个结点开始逐个处理——每轮先用 next_node 保存当前结点的后继（摘链后原表指针已断，不先保存就丢了后续），再把它从原链上「摘下」，然后在已排序部分里找插入点：prev 从头结点开始、p 从第一个数据结点开始，用 p->data <= cur->data 向后走，直到 p 为空或 p->data 比 cur 大，把 cur 插到 prev 与 p 之间（prev->next = cur; cur->next = p）。复杂度：每轮找位置最多走 O(i) 步，总时间 O(n^2)，只改变指针、额外空间 O(1)。稳定性：用 <= 判断时，新结点会插到所有相等结点的后面，相等元素相对次序保持——稳定排序。为什么不直接搬数据：链表结点可能是大结构体，改指针比搬数据便宜得多（这正是链表排序的价值）。工程提示：链表缓存不友好，纯排序通常先把链表转数组排序再转回更快；但链表排序在「就地、无额外数组、结点很大」时仍有优势，也是链表归并排序（O(n log n)、自底向上不开递归）的基础。易错点：不先保存后继导致断链丢结点；头结点处理（用哨兵头简化「插到最前」）；比较用 < 破坏稳定性；找位循环条件写成 p->next 为空漏判。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>
#include <stdlib.h>

typedef struct node {
    int data;
    struct node *next;
} node_t;

/* 尾插建表（带头结点） */
static node_t *list_build(const int v[], int n)
{
    node_t *head = malloc(sizeof(node_t)), *tail = head;
    int i;
    head->next = NULL;
    for (i = 0; i < n; i++) {
        node_t *p = malloc(sizeof(node_t));
        p->data = v[i]; p->next = NULL;
        tail->next = p;
        tail = p;
    }
    return head;
}

static void list_print(const char *tag, const node_t *head)
{
    const node_t *p = head->next;
    printf("%s", tag);
    while (p) { printf("%d%c", p->data, p->next ? ' ' : '\n'); p = p->next; }
}

/* 直接插入排序（链表版）：逐个摘下结点，插入已排序部分的正确位置。
   O(n^2) 时间、O(1) 额外空间；用 <= 向后走 → 稳定排序。 */
static void list_insertion_sort(node_t *head)
{
    node_t *sorted = head->next;          /* 已排序部分：初始只有第 1 个结点 */
    node_t *cur;

    if (!sorted || !sorted->next) return; /* 0 或 1 个结点：已有序 */
    cur = sorted->next;
    sorted->next = NULL;                  /* 断开：已排序部分只留 1 个结点 */
    while (cur) {
        node_t *next_node = cur->next;    /* 1) 先保存后继，防止摘链丢失 */
        node_t *prev = head, *p = head->next;
        while (p && p->data <= cur->data) {        /* 2) 找第一个更大的结点 */
            prev = p;
            p = p->next;
        }
        prev->next = cur;                 /* 3) 插到 prev 与 p 之间 */
        cur->next = p;
        cur = next_node;                  /* 4) 处理原链的下一个结点 */
    }
}

int main(void)
{
    int v[] = {5, 2, 8, 1, 9, 3, 2};
    int n = sizeof(v) / sizeof(v[0]);
    node_t *head = list_build(v, n);

    list_print("排序前：", head);
    list_insertion_sort(head);
    list_print("排序后：", head);          /* 1 2 2 3 5 8 9 */
    return 0;                             /* 演示从简；工程代码应逐个 free */
}
```
要点：摘链前先存后继；哨兵头简化插到最前的边界；<= 保证稳定；O(n^2)/O(1)；与数组版区别在「改指针」而非「搬元素」。""",
    exp="①指针四步正确（存后继、摘链、找位、接链），结果有序 ②说明 O(n^2)/O(1) 与稳定性（<= 的判据）③加分：与数组版的对照（搬数据 vs 改指针）、哨兵头作用、或指出链表缓存不友好 / 链表归并排序等延伸。",
    hot=True,
)

NEW[520] = dict(
    stem="请写出合并两个升序单链表（带头结点）的完整函数，并说明复杂度、额外空间与稳定性。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""合并两个升序链表是归并（merge）思想的链表版：新建一个哨兵头结点做结果链，h1、h2 分别指向两个链表的第一个数据结点，比较两者大小——小者「摘下、尾插」进结果链、该指针前移，重复到某条链走空；最后把剩下的那条链整段接到尾部（无需逐结点搬，因为链表天然有序且连续）。复杂度：每个结点恰好被摘取一次，时间 O(m+n)；额外空间 O(1)（只重排指针、复用原结点，不算结果哨兵）；若约定「不破坏原链表」则需要复制结点，空间变 O(m+n)，面试要先问清约定。稳定性：相同时取 h1 的结点（<= 判据），结果中相等元素的相对次序与合并前一致——稳定。应用：归并排序的 merge 步骤（链表归并排序可做到 O(1) 额外空间、比数组版省内存）、多路归并、两个有序序列求并集/交集、数据库排序合并连接。易错点：忘记接剩余段（提前返回导致丢结点）；取小后忘记前移指针（死循环）；比较用 < 且在相等时取 h2 破坏稳定性；返回哨兵结点而不是 head->next。延伸：本题与第 455 题（两个升序数组从后往前合并，O(1) 空间）互为变体——数组要从后往前防覆盖，链表则没有覆盖问题。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>
#include <stdlib.h>

typedef struct node {
    int data;
    struct node *next;
} node_t;

static node_t *list_build(const int v[], int n)
{
    node_t *head = malloc(sizeof(node_t)), *tail = head;
    int i;
    head->next = NULL;
    for (i = 0; i < n; i++) {
        node_t *p = malloc(sizeof(node_t));
        p->data = v[i]; p->next = NULL;
        tail->next = p;
        tail = p;
    }
    return head;
}

static void list_print(const node_t *head)
{
    const node_t *p = head->next;
    while (p) { printf("%d%c", p->data, p->next ? ' ' : '\n'); p = p->next; }
}

/* 合并两个升序链表（都带头结点），返回结果链表的头结点（哨兵头）。
   尾插 + 接剩余：O(m+n) 时间、O(1) 额外空间（复用原结点）；相等先取 L1 → 稳定。 */
static node_t *merge_sorted(node_t *h1, node_t *h2)
{
    node_t *head = malloc(sizeof(node_t));   /* 结果链的哨兵头结点 */
    node_t *tail = head;

    head->next = NULL;
    h1 = h1->next; h2 = h2->next;             /* 跳过各自的哨兵头 */
    while (h1 && h2) {
        if (h1->data <= h2->data) { tail->next = h1; h1 = h1->next; }
        else                      { tail->next = h2; h2 = h2->next; }
        tail = tail->next;                    /* 尾指针跟上 */
    }
    tail->next = h1 ? h1 : h2;                /* 剩余整段直接接上 */
    return head;
}

int main(void)
{
    int a[] = {1, 4, 7, 9}, b[] = {2, 3, 8, 10, 11};
    node_t *h1 = list_build(a, 4), *h2 = list_build(b, 5);
    node_t *m = merge_sorted(h1, h2);

    list_print(m);                            /* 1 2 3 4 7 8 9 10 11 */
    return 0;
}
```
要点：双指针取小尾插、剩余段整段接上；O(m+n)/O(1)；<= 取 L1 保持稳定；此函数即链表归并排序的 merge 核心。""",
    exp="①合并函数正确（含剩余段处理，结果有序）②说明 O(m+n) 时间与 O(1) 额外空间（复用结点）③加分：稳定性判据、与第 455 题数组版的区别、或归并排序 / 多路归并等应用。",
    hot=True,
)

NEW[521] = dict(
    stem="请用带头结点的单链表实现链式队列，写出初始化、判空、入队、出队四组函数，并说明它与数组循环队列的取舍。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""链式队列 = 单链表 + 头尾两个指针：head 指向队头（出队端）、tail 指向队尾（入队端）。带头结点时，head 是哨兵，队头元素在 head->next；初始 head == tail（都指向哨兵）表示空队。入队：在 tail 后接新结点、更新 tail——O(1)；出队：删 head->next 结点、返回其数据——O(1)；特别注意出队的是最后一个元素时，要把 tail 复位为 head，否则 tail 悬空指向已释放结点，后续入队会写到野指针（经典坑）。与数组循环队列的取舍：链式队列没有「假溢出」问题、容量只受堆内存限制（适合队列长度变化大的场景），但每个元素多一个指针开销、malloc/free 有碎片与分配失败风险；数组循环队列内存连续、无分配开销、可预测（适合嵌入式实时系统与固定容量缓冲区），代价是容量固定、需要「牺牲一格」或计数器辅助判定空满。应用：操作系统任务/消息队列、生产者-消费者缓冲、BFS 层序遍历、打印/事件缓冲。易错点：入队忘记判空或忘更新 tail；出队最后一个元素后不复位 tail（最常见）；对空队出队未防护；把队头/队尾指针语义写反（head 出队、tail 入队要固定）。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>
#include <stdlib.h>

/* 链式队列（带头结点）：head 指向队头（哨兵），tail 指向队尾；
   入队在 tail 后接结点、出队删 head->next → 均为 O(1)，无固定容量 */
typedef struct qnode {
    int data;
    struct qnode *next;
} qnode_t;

typedef struct {
    qnode_t *head;      /* 队头哨兵 */
    qnode_t *tail;      /* 队尾（指向最后一个元素） */
} lqueue_t;

static void lq_init(lqueue_t *q)
{
    q->head = malloc(sizeof(qnode_t));      /* 哨兵头结点 */
    q->head->next = NULL;
    q->tail = q->head;                      /* 空队：head == tail */
}

static int lq_empty(const lqueue_t *q)
{
    return q->head == q->tail;
}

static int lq_push(lqueue_t *q, int v)      /* 入队：尾插，成功 0、失败 -1 */
{
    qnode_t *p = malloc(sizeof(qnode_t));
    if (!p) return -1;
    p->data = v;
    p->next = NULL;
    q->tail->next = p;                      /* 挂到队尾之后 */
    q->tail = p;                            /* 更新队尾指针 */
    return 0;
}

static int lq_pop(lqueue_t *q, int *out)     /* 出队：删头，空队返回 -1 */
{
    qnode_t *p = q->head->next;
    if (!p) return -1;
    *out = p->data;
    q->head->next = p->next;
    if (q->tail == p)                        /* 删的是最后一个元素：复位尾指针 */
        q->tail = q->head;
    free(p);
    return 0;
}

int main(void)
{
    lqueue_t q;
    int v;

    lq_init(&q);
    for (v = 1; v <= 5; v++) lq_push(&q, v * 100);   /* 入队 100 200 300 400 500 */
    printf("出队顺序：");
    while (!lq_empty(&q)) { lq_pop(&q, &v); printf("%d ", v); }
    printf("\n空队出队返回 %d\n", lq_pop(&q, &v));   /* -1 */
    return 0;
}
```
要点：head 出队端、tail 入队端；出队最后一个元素要复位 tail = head；入队/出队 O(1)；无固定容量但每结点多一个指针开销。""",
    exp="①四个函数正确（重点：出队最后一个元素后复位 tail；空队防护）②说明 O(1) 入队出队与「无假溢出」③加分：与循环队列的取舍（连续内存 / 固定容量 vs 动态 / 指针开销）、或应用场景（任务队列、BFS）。",
    hot=False,
)

NEW[522] = dict(
    stem="请写出链式栈（链栈）的入栈、出栈、取栈顶、判空四组函数，并说明它与顺序栈的取舍。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""链栈把单链表的头部当作栈顶：入栈 = 头插，出栈 = 删头——两者都只动一个指针，O(1)，不需要尾指针、不需要判满（只有 malloc 失败才算「满」，那是内存耗尽）。栈顶指针 top 就是链表头指针，空栈即 top == NULL。实现要点：push 时新结点 next 指向旧 top、再更新 top；pop 时先取数据、top 后移、再 free；取栈顶只看不删（返回副本）；判空就是 top 为空判断——空栈 pop 要返回错误码防下溢。与顺序栈的取舍：顺序栈用数组 + top 下标，内存连续缓存友好、无分配开销、容量固定（要防上溢）；链栈动态增长、没有容量上限，但每个元素多一个指针、malloc/free 有开销与碎片，实时性可预测性稍差。工程上栈深可预测且要求确定性（中断嵌套、递归深度有限）→ 顺序栈；栈深变化大或要支持任意深度 → 链栈。应用：括号匹配、表达式求值/中缀转后缀、函数调用栈原理、DFS 非递归（用栈保存待访问结点）、浏览器的前进后退。易错点：push 忘记先接 next（断链）；pop 忘记 free（内存泄漏）或先 free 后取数据（野指针）；把 top 传值导致更新丢失（要传二级指针或结构体指针）；空栈下溢未防护。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>
#include <stdlib.h>

/* 链栈：栈顶就是链表头，入栈=头插、出栈=删头，都是 O(1)、不用判满 */
typedef struct snode {
    int data;
    struct snode *next;
} snode_t;

static int stack_push(snode_t * *top, int v)    /* 入栈：头插（* *top 用于更新栈顶） */
{
    snode_t *p = malloc(sizeof(snode_t));
    if (!p) return -1;                          /* 内存耗尽才算“满” */
    p->data = v;
    p->next = *top;                             /* 新结点指向旧栈顶 */
    *top = p;                                   /* 更新栈顶 */
    return 0;
}

static int stack_pop(snode_t * *top, int *out)  /* 出栈：删头 */
{
    snode_t *p = *top;
    if (!p) return -1;                          /* 空栈下溢 */
    *out = p->data;                             /* 先取数据 */
    *top = p->next;                             /* 再移栈顶 */
    free(p);
    return 0;
}

static int stack_top(const snode_t *top, int *out)   /* 取栈顶（不弹出） */
{
    if (!top) return -1;
    *out = top->data;
    return 0;
}

static int stack_empty(const snode_t *top)
{
    return top == NULL;
}

int main(void)
{
    snode_t *s = NULL;                          /* 空栈 */
    int v, i;

    for (i = 1; i <= 5; i++) stack_push(&s, i * 11);    /* 11 22 33 44 55 */
    stack_top(s, &v);
    printf("栈顶 %d\n", v);                              /* 55 */
    printf("出栈顺序：");
    while (!stack_empty(s)) { stack_pop(&s, &v); printf("%d ", v); }
    printf("\n空栈出栈返回 %d\n", stack_pop(&s, &v));    /* -1 */
    return 0;
}
```
要点：头插删头即入栈出栈；无容量限制（仅 malloc 失败）；栈顶指针要用二级指针传递才能更新；空栈防护返回 -1。""",
    exp="①四个函数正确（头插/删头、二级指针更新、空栈防护、free）②说明链栈无「上溢」、只有内存失败 ③加分：与顺序栈取舍（固定容量/连续内存 vs 动态/指针开销）、应用（括号匹配、表达式求值、DFS 非递归）。",
    hot=False,
)

NEW[523] = dict(
    stem="请写出二叉排序树（BST）的查找与插入函数完整代码（递归写法），并说明中序遍历为何是有序的、复杂度以及最坏退化问题。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""二叉排序树（二叉搜索树）的性质：对任意结点，左子树所有键 < 根键 < 右子树所有键。查找：从根开始，key 与当前键比较——相等命中；比当前小走左、比当前大走右；走到空指针表示不存在（比较次数等于查找路径长度）。插入：沿查找路径走到空位，把新结点挂在那里（所以插入一定发生在叶子位置）；若键已存在则按约定处理（忽略、返回失败或计数），核心代码用二级指针传参（形如 bst_t * *pt），原地修改父结点的孩子指针。中序遍历有序：中序是「左→根→右」，由 BST 性质，左子树的键都小于根、根的键都小于右子树，归纳可得中序序列严格递增——这也是验证 BST 的标准方法（中序 + 检查递增）。复杂度：查找/插入都沿一条根到叶的路径，代价 O(h)（h 为树高）。最好情况（平衡）h ≈ log2 n，比较次数约 ⌊log2 n⌋+1；最坏情况：按有序序列插入（1,2,3,…）树退化成单支链，h = n，查找变 O(n)——这是 BST 最大缺陷，于是引出 AVL 树、红黑树等平衡结构（插入时旋转维持 h = O(log n)）。与折半查找对比：都 O(log n)（平衡时），但 BST 支持动态插入/删除而折半要求静态有序数组；反过来数组缓存更友好。应用：动态查找表、符号表、有序集合；工程里 stl::map/set 用红黑树、数据库索引用 B+ 树。易错点：插入写成传一级指针导致改不到父结点的孩子；重复键处理未定义；中序有序性说成「先序遍历」；忘记退化分析。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>
#include <stdlib.h>

typedef struct bst {
    int key;
    struct bst *left, *right;
} bst_t;

/* 递归查找：小往左、大往右；命中返回结点指针，走到空返回 NULL */
static bst_t *bst_search(bst_t *t, int k)
{
    if (!t || t->key == k) return t;
    return (k < t->key) ? bst_search(t->left, k) : bst_search(t->right, k);
}

/* 递归插入：沿查找路径走到空位挂上新结点；重复键不插入，返回 0 */
static int bst_insert(bst_t * *pt, int k)
{
    bst_t *t = *pt;
    if (!t) {                                /* 空位 = 插入点（必然在叶子位置） */
        bst_t *p = malloc(sizeof(bst_t));
        if (!p) return -1;
        p->key = k;
        p->left = p->right = NULL;
        *pt = p;                             /* 需要二级指针才能改父结点的孩子 */
        return 1;
    }
    if (k == t->key) return 0;               /* 已存在：按约定忽略 */
    return (k < t->key) ? bst_insert(&t->left, k) : bst_insert(&t->right, k);
}

/* 中序遍历：BST 的中序序列必为升序（验证 BST 的标准方法） */
static void inorder(const bst_t *t)
{
    if (!t) return;
    inorder(t->left);
    printf("%d ", t->key);
    inorder(t->right);
}

int main(void)
{
    int a[] = {50, 30, 70, 20, 40, 60, 80, 30};
    int n = sizeof(a) / sizeof(a[0]);
    bst_t *root = NULL;
    int i;

    for (i = 0; i < n; i++)
        printf("插入 %d -> %s\n", a[i], bst_insert(&root, a[i]) ? "成功" : "重复忽略");

    printf("中序遍历（升序）：");
    inorder(root);
    printf("\n");

    printf("查找 40 -> %s\n", bst_search(root, 40) ? "命中" : "未命中");
    printf("查找 45 -> %s\n", bst_search(root, 45) ? "命中" : "未命中");
    return 0;
}
/* 输出：插入 30 第二次时“重复忽略”；中序 20 30 40 50 60 70 80；查找 40 命中、45 未命中 */
```
要点：查找沿小左大右路径、空指针为失败；插入用二级指针挂到空位；中序升序；复杂度 O(h)，有序插入会退化成链 O(n) → 平衡树解决。""",
    exp="①查找与插入函数正确（二级指针改父孩子、重复键约定）②说明中序升序的原因与 O(h) 复杂度 ③加分：最坏退化成链及平衡树（AVL/红黑）、或与折半查找 / 动态查找表的对比。",
    hot=True,
)

NEW[524] = dict(
    stem="请写出折半查找的两个变体函数：查找第一个等于 key 的位置（lower_bound）和第一个大于 key 的位置（upper_bound），并举例说明用途与边界处理。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""普通二分命中即返回，但工程里更常用的两个变体是：lower_bound——第一个大于等于 key 的位置（全部元素都小于 key 时返回 n）；upper_bound——第一个大于 key 的位置（等于 key 的全部跳过）。两者之差 = key 在有序表中出现的次数。写法要点：统一用左闭右开区间 [lo, hi)，循环条件 lo < hi；中点仍写 lo + (hi - lo)/2 防溢出；「mid 不满足条件 → 答案一定在右半区」写 lo = mid + 1，「mid 满足条件 → 记录 hi = mid（不能写 mid-1，否则可能把答案跳过去）」；循环结束时 lo == hi 就是答案位置。两个函数只差一个等号：lower 判 a[mid] < key，upper 判 a[mid] <= key。为什么不能「命中即返回」：当 key 有多个重复元素时，普通二分可能落在任意一个上，无法确定是第一个/最后一个；变体不依赖命中、而是靠区间收敛到边界。边界：空表返回 0（即 n=0）；key 比所有元素小 → 0；比所有大 → n；全表等于 key 时 lower=0、upper=n。应用：①向有序数组插入新值且保持有序（插入位置就是 lower_bound）②统计区间内元素个数 [L,R] → lower_bound(R+1) - lower_bound(L) ③有序数组去重 / 求交集并集 ④C++ 的 std::lower_bound/upper_bound、std::equal_range 的算法原型。易错点：hi = mid - 1 越界跳过答案；区间语义与循环条件不配套（闭区间 + lo<hi 会死循环）；把 upper 的等号方向写反。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>

/* 第一个 >= key 的位置（不满足则返回 n）。区间用左闭右开 [lo, hi) */
int lower_bound_(const int a[], int n, int key)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] < key) lo = mid + 1;    /* mid 太小：答案在右边 */
        else              hi = mid;        /* mid 可以当答案：收敛左边界 */
    }
    return lo;                             /* lo == hi 即答案位置 */
}

/* 第一个 > key 的位置（upper_bound）——与上面只差一个等号 */
int upper_bound_(const int a[], int n, int key)
{
    int lo = 0, hi = n;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (a[mid] <= key) lo = mid + 1;
        else               hi = mid;
    }
    return lo;
}

int main(void)
{
    int a[] = {1, 3, 3, 3, 7, 9, 11};
    int n = sizeof(a) / sizeof(a[0]);

    printf("lower_bound(3) = %d（第 1 个 3）\n", lower_bound_(a, n, 3));   /* 1 */
    printf("upper_bound(3) = %d（跳过所有 3）\n", upper_bound_(a, n, 3)); /* 4 */
    printf("3 的个数 = %d\n",
           upper_bound_(a, n, 3) - lower_bound_(a, n, 3));                /* 3 */
    printf("lower_bound(4) = %d（4 应插入的位置）\n", lower_bound_(a, n, 4)); /* 4 */
    printf("边界：lower(0)=%d，upper(99)=%d\n",
           lower_bound_(a, n, 0), upper_bound_(a, n, 99));                /* 0, 7 */
    return 0;
}
```
要点：左闭右开 +「满足条件 hi=mid、不满足 lo=mid+1」收敛到边界；两函数只差一个等号；差即出现次数；插入位置、计数、去重等场景常用。""",
    exp="①两个函数正确（区间收敛写法、边界：全小 → n、全大 → 0）②说明 upper - lower = 出现次数、插入位置等用途 ③加分：与普通二分「命中即返回」的区别、C++ std::lower_bound / equal_range 对应、或防死循环要点（hi = mid 而非 mid-1）。",
    hot=False,
)

NEW[525] = dict(
    stem="请写出用快慢指针一趟扫描求单链表（带头结点）倒数第 k 个结点的完整函数，说明边界处理、复杂度与其典型变体。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""快慢指针（双指针同向）技巧：要找倒数第 k 个结点，让快指针先走 k 步——此时快慢指针相差 k 个身位；然后两指针同步每次前进一步，快指针走到表尾之后（NULL）时，慢指针恰好指向倒数第 k 个结点。为什么成立：设表长 n，快指针走了 k + (n - k) = n 步到 NULL，慢指针走了 n - k 步，从表头数第 n-k+1 个正是倒数第 k 个。边界处理：k <= 0 直接返回 NULL；快指针在「先走 k 步」途中就变成 NULL，说明链表不足 k 个结点，同样返回 NULL——这道检查天然内嵌在循环里，无需事先求表长。复杂度：快指针一共走 n 步、慢指针走 n-k 步，合计 O(n) 时间、只用了两个指针 O(1) 空间——对比「两趟扫描」（先求长度 n，再从头走 n-k 步）同样 O(n)，但一遍扫描在流式/长链表中更优，且面试常以此考察指针技巧。典型变体：①求中间结点——快指针每次走 2 步、慢指针走 1 步，快到头时慢即中点（求中点、判断回文、归并排序找中点都用它）②判断链表是否有环——快 2 慢 1，有环必相遇（Floyd 判圈，第 503 题）③删除倒数第 k 个结点——先找到倒数第 k+1 个（前驱），再删其后继；k 恰为表长时删的是第一个结点，用哨兵头可统一处理 ④「旋转链表」「重排链表」等组合题。易错点：先走 k 步的循环里忘记判 fast == NULL（不足 k 个时越界）；同步循环写成 while(fast->next) 导致差一位；返回慢指针时又把快指针多走一步；k=1 应是表尾结点（可带入验证）。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>
#include <stdlib.h>

typedef struct node {
    int data;
    struct node *next;
} node_t;

static node_t *list_build(const int v[], int n)
{
    node_t *head = malloc(sizeof(node_t)), *tail = head;
    int i;
    head->next = NULL;
    for (i = 0; i < n; i++) {
        node_t *p = malloc(sizeof(node_t));
        p->data = v[i]; p->next = NULL;
        tail->next = p;
        tail = p;
    }
    return head;
}

/* 一趟扫描找倒数第 k 个结点：快的先走 k 步，再同步走，快的到尾后时慢的即答案。
   k <= 0 或链表不足 k 个返回 NULL。O(n) 时间、O(1) 空间。 */
static node_t *kth_from_end(node_t *head, int k)
{
    node_t *fast = head->next;
    node_t *slow = head->next;
    int i;

    if (k <= 0) return NULL;
    for (i = 0; i < k; i++) {
        if (!fast) return NULL;            /* 不足 k 个结点 */
        fast = fast->next;                 /* 快指针先拉开 k 个身位 */
    }
    while (fast) {                         /* 同步前进 */
        fast = fast->next;
        slow = slow->next;
    }
    return slow;                           /* 快到尾后，慢即倒数第 k 个 */
}

int main(void)
{
    int v[] = {10, 20, 30, 40, 50};
    node_t *head = list_build(v, 5);
    node_t *p;

    p = kth_from_end(head, 2);
    printf("倒数第 2 个 -> %d\n", p ? p->data : -1);       /* 40 */
    p = kth_from_end(head, 5);
    printf("倒数第 5 个 -> %d\n", p ? p->data : -1);       /* 10（即第一个） */
    p = kth_from_end(head, 6);
    printf("倒数第 6 个 -> %s\n", p ? "?" : "NULL（不足 k 个）");
    p = kth_from_end(head, 0);
    printf("k=0 -> %s\n", p ? "?" : "NULL（k 非法）");
    return 0;
}
```
要点：快指针先走 k 步、再同步走；k 非法或不足 k 个返回 NULL；O(n)/O(1)；变体：中点（快 2 慢 1）、判环、删除倒数第 k 个。
（程序输出：40 / 10 / NULL（不足 k 个） / NULL（k 非法））""",
    exp="①一趟扫描实现正确（先走 k 步 + 同步走 + 边界返回 NULL）②说明 O(n)/O(1) 与 k 的边界 ③加分：中点/判环/删除倒数第 k 等变体，「两趟扫描」对照，或 k=1 即表尾的验证。",
    hot=True,
)

NEW[526] = dict(
    stem="请写出计数排序的完整函数，说明它的适用条件、时间/空间复杂度、稳定性，以及为什么它不受「比较排序 O(n log n) 下界」的限制。（查漏补缺补充题·排序/查找函数专项，2026-10-05）",
    ana=r"""计数排序（counting sort）不是比较排序：它统计每个值出现的次数，再按值的大小直接算出每个元素的最终位置——因此不受「比较排序至少要 O(n log n) 次比较」的下界约束（该下界只适用于基于比较的排序）。稳定版三步：①扫描数组统计每个值出现次数 cnt[v]++；②对 cnt 做前缀和，cnt[v] 变成「值 v 的最后一个元素应放的位置」；③从数组尾部倒序回填 out[--cnt[a[i]]] = a[i]——倒序遍历保证相同值保持原来的相对次序，即稳定。复杂度：时间 O(n + k)（k 为值域大小，如 0..VMAX 则 k = VMAX+1），空间 O(k)（计数数组）+ O(n)（输出数组）。适用条件：①待排数据是整数（或其键可映射为小范围整数）②值域 k 与 n 同量级或更小（k 太大时 O(n+k) 反而比 O(n log n) 慢，且内存爆掉）。典型场景：0~255 的 ADC 采样、百分制成绩、单字节数据直方图等嵌入式场景；也是基数排序每一位排序的内核。为什么稳定版要倒序回填：前缀和后 cnt[v]-1 是值 v 在结果中的「最后一个空位」，从后往前扫原始序列，后出现的相同元素放进更后的位置，相对次序不变；若正序回填且想保稳定，需要额外记录起点或用逆向前缀。易错点：值域写小导致 count 越界；忘记前缀和直接按计数写回（会破坏稳定性且要额外逻辑）；回填顺序写反导致不稳定；把时间复杂度答成 O(n log n) 或遗漏 k 项；对负数/浮点数直接套用（负数需加偏移、浮点不适用）。""",
    ans=r"""完整代码如下：
```c
#include <stdio.h>
#include <string.h>

#define VMAX 100                       /* 数据取值范围 0 ~ VMAX，k = VMAX + 1 */

/* 计数排序（稳定版）：只适用于小范围非负整数 —— O(n + k) 时间、O(k) 空间 */
void counting_sort(const int a[], int n, int out[])
{
    int cnt[VMAX + 1];
    int i;

    memset(cnt, 0, sizeof(cnt));                 /* ① 统计每个值的个数 */
    for (i = 0; i < n; i++) cnt[a[i]]++;

    for (i = 1; i <= VMAX; i++) cnt[i] += cnt[i - 1];   /* ② 前缀和 = 每个值的最后位置+1 */

    for (i = n - 1; i >= 0; i--)                 /* ③ 倒序回填：相同值保持原相对次序 */
        out[--cnt[a[i]]] = a[i];
}

int main(void)
{
    int a[] = {4, 2, 4, 1, 3, 2, 4, 0, 3};
    int n = sizeof(a) / sizeof(a[0]);
    int out[9];
    int i;

    counting_sort(a, n, out);
    for (i = 0; i < n; i++)
        printf("%d%c", out[i], i + 1 == n ? '\n' : ' ');   /* 0 1 2 2 3 3 4 4 4 */
    return 0;
}
```
要点：计数 → 前缀和 → 倒序回填三步；时间 O(n+k)、空间 O(k)+O(n)；非比较排序，不受 O(n log n) 下界约束；k 过大时退化不适用。""",
    exp="①三步实现正确（尤其倒序回填保稳定、前缀和语义）②说明适用条件（小范围整数、k 与 n 同量级）与 O(n+k)/O(k) ③加分：解释为何不受比较排序下界限制、k 太大时的退化、或它在基数排序 / 嵌入式直方图中的应用。",
    hot=False,
)

if __name__ == "__main__":
    assert sorted(NEW.keys()) == list(range(516, 527)), NEW.keys()
    bad = []
    for num, q in NEW.items():
        for f in ("stem", "ana", "ans", "exp"):
            if not q.get(f):
                bad.append((num, f, "缺失"))
        for f in ("ana", "ans", "exp"):
            if "**" in q[f]:
                bad.append((num, f, "含 ** 序列"))
        if q["ans"].count("```") % 2 != 0:
            bad.append((num, "ans", "代码围栏不配对"))
        if "\r" in q["ana"] + q["ans"] or "\n\n\n" in q["ans"]:
            bad.append((num, "fmt", "多余空行/CR"))
    hot = sum(1 for q in NEW.values() if q["hot"])
    print("题目数", len(NEW), "| 热点新增", hot, "| 断言", "通过" if not bad else bad)
