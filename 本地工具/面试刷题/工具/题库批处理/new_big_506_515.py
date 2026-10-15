# -*- coding: utf-8 -*-
"""506~515 查漏补缺补充题数据（题干/解析/代码/要点/得分点）。
代码须为完整可编译程序（含 main 演示）；全部内容严禁出现双星序列（解析器会剔除）。
"""
QUESTIONS = [
{
 "num": 506,
 "title": "请写出二分查找（折半查找）的完整函数（循环版与递归版），说明边界处理与时间复杂度。",
 "analysis": "二分查找的前提是序列有序（本题按升序）。原理：每次取区间中间元素与 key 比较，相等即命中；key 大于中间元素则到右半区继续，小于则到左半区继续——每比较一次区间缩小一半，最多约 log2(n)+1 次比较，时间复杂度 O(log n)。两个关键边界：①中点必须写成 int mid = lo + (hi - lo) / 2，不能写 (lo + hi) / 2——当 lo、hi 都接近 int 上限时相加会溢出（有符号溢出是未定义行为，Java 数组二分曾用同款修法修复）；②区间语义必须与更新方式配套：本题用闭区间 [lo, hi]，循环条件 lo <= hi，排除中点后写 lo = mid + 1 或 hi = mid - 1；若改用左闭右开 [lo, hi) 则循环条件 lo < hi、hi 更新为 mid，两种写法不能混用，否则漏元素或死循环。循环版用 while 迭代（额外空间 O(1)）；递归版把区间缩小表达为自调用，出口是 lo > hi（区间为空表示未找到），递归深度 O(log n)、栈空间 O(log n)。易错点：忘记数组必须有序；mid 相加溢出；把更新写成 lo = mid 导致死循环；递归版区间与循环版不一致；返回值语义（下标还是布尔）混淆。应用：有序表查找、库函数 bsearch、以及「二分答案」（对答案空间二分的算法题）。",
 "code": '''#include <stdio.h>

/* 循环版：在升序数组 a[0..n-1] 中查找 key；找到返回下标，未找到返回 -1 */
int binary_search(const int a[], int n, int key)
{
    int lo = 0, hi = n - 1;                     /* 区间语义：闭区间 [lo, hi] */
    while (lo <= hi) {                          /* 区间非空则继续 */
        int mid = lo + (hi - lo) / 2;           /* 防 (lo + hi) 相加溢出 */
        if (a[mid] == key)      return mid;
        else if (a[mid] < key)  lo = mid + 1;   /* 右半区 [mid+1, hi] */
        else                    hi = mid - 1;   /* 左半区 [lo, mid-1] */
    }
    return -1;
}

/* 递归版：与循环版同语义（闭区间 [lo, hi]），出口是区间为空 */
int binary_search_rec(const int a[], int lo, int hi, int key)
{
    int mid;
    if (lo > hi) return -1;                     /* 未找到：区间为空 */
    mid = lo + (hi - lo) / 2;
    if (a[mid] == key)      return mid;
    else if (a[mid] < key)  return binary_search_rec(a, mid + 1, hi, key);
    else                    return binary_search_rec(a, lo, mid - 1, key);
}

int main(void)
{
    int a[] = {1, 3, 5, 7, 9, 11, 13};
    int n = sizeof(a) / sizeof(a[0]);

    printf("循环版查 7  -> 下标 %d\\n", binary_search(a, n, 7));               /* 3 */
    printf("循环版查 8  -> 下标 %d\\n", binary_search(a, n, 8));               /* -1 */
    printf("递归版查 13 -> 下标 %d\\n", binary_search_rec(a, 0, n - 1, 13));   /* 6 */
    printf("递归版查 0  -> 下标 %d\\n", binary_search_rec(a, 0, n - 1, 0));    /* -1 */
    return 0;
}''',
 "yaodian": "必须有序为前提；mid 用 lo+(hi-lo)/2 防溢出；闭区间配套 lo<=hi 与 mid±1 跳过；循环版迭代、递归版同语义；时间 O(log n)。",
 "defen": "①循环版与递归版都正确（区间边界与返回语义一致）②说明 mid 防溢出的原因、写明 O(log n) 复杂度 ③加分：指出必须有序的前提、lo=mid 死循环风险、或 bsearch/二分答案等应用。",
},
{
 "num": 507,
 "title": "请写出希尔排序的完整函数（增量序列用 n/2 折半），说明复杂度与适用性。",
 "analysis": "希尔排序（缩小增量排序）是插入排序的改进：先按增量 gap 把序列分成若干子序列（下标相隔 gap 的元素同组），组内做插入排序——这样元素一次可跨 gap 个位置移动，逆序对快速减少；随后增量折半缩小，当 gap = 1 时做最后一轮普通插入排序，而此时序列已基本有序，插入排序接近 O(n)。实现要点：gap 从 n/2 开始，每轮 gap /= 2 直到 0；内层对每个 i（从 gap 到 n-1）把 a[i] 按步长 gap 插入到组内正确位置——结构与直接插入排序完全相同，只是步长 1 换成 gap。性能：复杂度与增量序列的选取有关，n/2 折半序列最坏约 O(n²)，但平均表现明显优于直接插入排序与冒泡；取 Hibbard 增量（2^k-1）约 O(n^1.5)，Sedgewick 序列可到 O(n^1.3) 量级。稳定性：不稳定——相同元素可能被分到不同组、跨越彼此；空间 O(1)，原地排序。适用场景：中等规模数据、内存受限场景、希望代码简单且平均性能优于 O(n²) 类算法时；工程中大数组一般仍选快排/归并/堆排。",
 "code": '''#include <stdio.h>

/* 希尔排序：增量 gap 依次取 n/2, n/4, ..., 1；每个 gap 对相隔 gap 的组做插入排序 */
void shell_sort(int a[], int n)
{
    int gap, i, j, key;
    for (gap = n / 2; gap > 0; gap /= 2) {       /* 增量折半，直到 1 */
        for (i = gap; i < n; i++) {              /* 组内插入排序（步长 gap） */
            key = a[i];
            j = i;
            while (j >= gap && a[j - gap] > key) {
                a[j] = a[j - gap];               /* 比 key 大的元素后移 gap 位 */
                j -= gap;
            }
            a[j] = key;
        }
    }
}

int main(void)
{
    int a[] = {9, 1, 5, 8, 3, 7, 4, 6, 2};
    int n = sizeof(a) / sizeof(a[0]), i;

    shell_sort(a, n);
    for (i = 0; i < n; i++) printf("%d ", a[i]);   /* 1 2 3 4 5 6 7 8 9 */
    printf("\\n");
    return 0;
}''',
 "yaodian": "增量 gap 从 n/2 折半到 1；组内按步长 gap 做插入排序；最坏约 O(n²)、平均优于插入排序且与增量序列有关；O(1) 空间、不稳定。",
 "defen": "①写出正确的希尔排序（gap 折半循环 + 组内插入排序）②说明增量序列与复杂度、稳定性 ③加分：Hibbard/Sedgewick 等增量改进、或适用场景（中等规模、原地、嵌入式）。",
},
{
 "num": 508,
 "title": "请写出快速排序的完整函数（partition + 递归；建议取首元素或三数取中），说明最坏/平均复杂度与优化。",
 "analysis": "快速排序是分治算法，三步：①选基准（pivot）；②分区（partition）——把小于基准的元素放到左边、大于基准的放到右边，基准归位；③对左右两个子区间分别递归。本题分区用挖坑法：基准取首元素保存到 key（原位置成为第一个坑），从右往左找比 key 小的填入左坑、再从左往右找比 key 大的填入右坑，交替直到两指针相遇，最后把 key 放回相遇点——一趟分区 O(n)，基准的最终位置从此确定、不再参与后续排序。性能：平均与最好 O(n log n)（每次划分接近均分，共 log n 层、每层 O(n)）；最坏 O(n²)，发生在序列基本有序或逆序而基准又固定取首元素时（每次划分一个子区间为空、递归深度 n），大量重复元素时同样会退化。优化：①随机基准或三数取中（首、中、尾取中位数）降低踩最坏的概率；②小区间（长度低于阈值，如 16）改用插入排序；③三路划分（小于、等于、大于三段）处理重复键；④尾递归优化或手工栈控制递归深度。空间：递归栈平均 O(log n)、最坏 O(n)。稳定性：不稳定（分区交换会打乱相等元素相对次序）。快排是实践中最快的通用内排序之一，C 标准库 qsort、C++ std::sort（内省排序：快排 + 堆排 + 插入排序）都基于它。",
 "code": '''#include <stdio.h>

/* 分区（挖坑法）：以 a[lo] 为基准，小的放左、大的放右；返回基准最终位置 */
int partition(int a[], int lo, int hi)
{
    int key = a[lo];                            /* 基准取首元素（可选三数取中优化） */
    while (lo < hi) {
        while (lo < hi && a[hi] >= key) hi--;   /* 从右往左找比基准小的 */
        a[lo] = a[hi];                          /* 填入左坑 */
        while (lo < hi && a[lo] <= key) lo++;   /* 从左往右找比基准大的 */
        a[hi] = a[lo];                          /* 填入右坑 */
    }
    a[lo] = key;                                /* 两指针相遇处即基准的最终位置 */
    return lo;
}

/* 递归快排：对 [lo, hi] 区间排序 */
void quick_sort(int a[], int lo, int hi)
{
    int p;
    if (lo >= hi) return;                       /* 0 或 1 个元素：已有序 */
    p = partition(a, lo, hi);
    quick_sort(a, lo, p - 1);                   /* 左半区 */
    quick_sort(a, p + 1, hi);                   /* 右半区 */
}

int main(void)
{
    int a[] = {6, 2, 9, 1, 8, 3, 7, 5, 4};
    int n = sizeof(a) / sizeof(a[0]), i;

    quick_sort(a, 0, n - 1);
    for (i = 0; i < n; i++) printf("%d ", a[i]);   /* 1 2 3 4 5 6 7 8 9 */
    printf("\\n");
    return 0;
}''',
 "yaodian": "挖坑法分区一趟 O(n)、基准归位后递归左右；平均/最好 O(n log n)、最坏 O(n²)（有序加固定基准）；优化有随机基准/三数取中、小区间插入、三路划分；不稳定。",
 "defen": "①partition 与递归框架正确（基准归位、左右区间对）②说明最坏/平均复杂度及成因 ③加分：给出至少一种优化（随机基准/三数取中/三路划分/尾递归）或指出不稳定。",
},
{
 "num": 509,
 "title": "请写出选择排序的完整函数，说明其不稳定性与 O(n²) 复杂度。",
 "analysis": "选择排序把数组分为已排序区（前部）与未排序区（后部）：每轮在未排序区扫描一遍，记下最小元素下标，与未排序区第一个元素交换——一趟确定一个元素的最终位置，共 n-1 趟。特点：①比较次数固定为 n(n-1)/2，与初始序列无关，无论什么数据都要完整扫描；②移动次数少，最多 n-1 次交换（每趟至多一次），在对「交换代价大」的场合仍有价值；③空间 O(1)、原地；④不稳定：交换是跨距离进行的，可能把相等元素越过——例如 [5a, 5b, 2]，第一趟最小值是 2，与 5a 交换得到 [2, 5b, 5a]，两个 5 的相对次序被破坏。复杂度：最好、最坏、平均均为 O(n²)，只适合小规模数据或对交换次数敏感的场景；大规模数据工程上用快排/归并/堆排，选择排序更多出现在教学与小数据场合。易错点：把最小值与 a[j] 随手交换会破坏已排序区（应只与区首交换）；比较次数与移动次数说反；稳定性回答错误（选择排序不稳定）。",
 "code": '''#include <stdio.h>

/* 选择排序：每趟从未排序区选出最小元素，与未排序区第一个元素交换 */
void selection_sort(int a[], int n)
{
    int i, j, min_idx, t;
    for (i = 0; i < n - 1; i++) {
        min_idx = i;                            /* 假定区首最小 */
        for (j = i + 1; j < n; j++)
            if (a[j] < a[min_idx])
                min_idx = j;                    /* 记录更小元素的下标 */
        if (min_idx != i) {                     /* 最小值不在区首才交换 */
            t = a[i]; a[i] = a[min_idx]; a[min_idx] = t;
        }
    }
}

int main(void)
{
    int a[] = {5, 2, 8, 1, 9, 3, 7, 4, 6};
    int n = sizeof(a) / sizeof(a[0]), i;

    selection_sort(a, n);
    for (i = 0; i < n; i++) printf("%d ", a[i]);   /* 1 2 3 4 5 6 7 8 9 */
    printf("\\n");
    return 0;
}''',
 "yaodian": "每趟从未排序区选最小放区首；比较次数固定 n(n-1)/2、移动至多 n-1 次；O(n²)、O(1)、不稳定。",
 "defen": "①实现正确（只与未排序区首交换、范围正确）②说明 O(n²) 与比较/移动次数特点 ③加分：举出不稳定的具体例子或适用场景（小规模、交换代价敏感）。",
},
{
 "num": 510,
 "title": "请写出归并排序的完整函数（merge + 递归），说明其稳定性与 O(n) 辅助空间。",
 "analysis": "归并排序是典型分治法：①分解——把区间 [lo, hi] 二分成 [lo, mid] 与 [mid+1, hi]；②递归——对两个子区间分别归并排序；③合并（merge）——把两段已有序子序列并成一段有序序列：双指针从两段头部开始，每次取较小者放入辅助数组，取完后再拷回原数组。两个核心点：①合并结果先暂存辅助数组再拷回，因此需要 O(n) 的额外空间——这是归并相对快排/堆排的最大代价；②比较写成「等值时取左段」（a[i] <= a[j] 时先取 a[i]）即可保证稳定。性能：最好、最坏、平均均为 O(n log n)——每层合并总量 O(n)、共 log n 层，这也是它相对快排的稳健优势；空间 O(n)（辅助数组加 O(log n) 递归栈）；稳定。适用：要求稳定排序、数据量大、或数据在链表上（链表归并可做到 O(1) 额外空间，天然适合）；也是外部排序（大文件分块归并）的基础。易错点：忘记把辅助数组拷回原数组；merge 的右段起点写成 mid（应为 mid+1）；稳定性靠等值取左段这个细节；递归出口 lo >= hi；每次合并都 malloc/free 辅助数组会拖慢性能（应一次分配、反复使用）。",
 "code": '''#include <stdio.h>

/* 合并：把有序区间 a[lo..mid] 与 a[mid+1..hi] 并成一段有序（借用 tmp） */
static void merge(int a[], int tmp[], int lo, int mid, int hi)
{
    int i = lo, j = mid + 1, k = lo;
    while (i <= mid && j <= hi)
        tmp[k++] = (a[i] <= a[j]) ? a[i++] : a[j++];   /* 等值取左段：保证稳定 */
    while (i <= mid) tmp[k++] = a[i++];                /* 左段剩余 */
    while (j <= hi)  tmp[k++] = a[j++];                /* 右段剩余 */
    for (k = lo; k <= hi; k++)
        a[k] = tmp[k];                                 /* 结果拷回原数组 */
}

/* 归并排序：左右两半分别递归，再合并 */
void merge_sort(int a[], int tmp[], int lo, int hi)
{
    int mid;
    if (lo >= hi) return;                       /* 0 或 1 个元素：递归出口 */
    mid = lo + (hi - lo) / 2;
    merge_sort(a, tmp, lo, mid);                /* 左半区 */
    merge_sort(a, tmp, mid + 1, hi);            /* 右半区 */
    merge(a, tmp, lo, mid, hi);                 /* 合并两段有序区 */
}

int main(void)
{
    int a[] = {9, 3, 7, 1, 8, 2, 6, 4, 5};
    int tmp[sizeof(a) / sizeof(a[0])];          /* 辅助空间 O(n) */
    int n = sizeof(a) / sizeof(a[0]), i;

    merge_sort(a, tmp, 0, n - 1);
    for (i = 0; i < n; i++) printf("%d ", a[i]);   /* 1 2 3 4 5 6 7 8 9 */
    printf("\\n");
    return 0;
}''',
 "yaodian": "分治递归 + merge 双指针；等值取左段保证稳定；辅助数组 O(n)；任何输入的复杂度都是 O(n log n)。",
 "defen": "①merge 与递归框架正确（双指针合并、拷回原数组）②说明稳定与 O(n) 辅助空间 ③加分：说明任意输入均 O(n log n)、链表归并 O(1) 额外空间、或外部排序应用。",
},
{
 "num": 511,
 "title": "请写出堆排序的完整函数（建堆 + 下滤调整），说明 O(n log n) / O(1) / 不稳定。",
 "analysis": "堆排序利用大顶堆（顺序存储的完全二叉树：下标 i 的左右孩子为 2i+1、2i+2，下标从 0 起）。分两步：①建堆——从最后一个非叶节点（下标 n/2-1）开始向前逐个下滤（sift down）：节点与较大的孩子比较，小于孩子则把大孩子上移、继续向下，直至父不小于子；②排序——重复 n-1 次：把堆顶（当前最大）与堆尾元素交换，堆大小减 1，再对堆顶下滤——最大元素依次归位到数组尾部，最终整体升序。要点：下滤中必须先选较大的孩子再比较（小顶堆则选小的）；节点不小于孩子即可提前停止；建堆自下而上保证每个子树先成为堆。性能：建堆 O(n)（逐层求和推导的结论，不是 O(n log n)）；每次交换加下滤 O(log n)，共 n-1 次，总时间 O(n log n)，且最好/最坏/平均一致；空间 O(1)（原地，无递归无额外数组）；不稳定（堆顶与堆尾是远距离交换）。适用：要求最坏情况也有 O(n log n) 且不接受归并的 O(n) 额外空间时（实时/嵌入式系统关注最坏上限）；也常用于 Top-K（维护大小 K 的小顶堆）与优先队列场景。易错点：下滤孩子下标越界判断；把堆顶与堆尾交换后忘记缩小堆范围；建堆复杂度说成 O(n log n)；把堆排误答为稳定排序。",
 "code": '''#include <stdio.h>

/* 下滤：在大小为 n 的堆中把下标 i 的节点向下调整，维持大顶堆性质 */
static void sift_down(int a[], int i, int n)
{
    int child, tmp = a[i];                      /* tmp 保存待调整元素（挖坑法） */
    for (; 2 * i + 1 < n; i = child) {
        child = 2 * i + 1;                      /* 左孩子 */
        if (child + 1 < n && a[child + 1] > a[child])
            child++;                            /* 右孩子更大则选右 */
        if (tmp >= a[child]) break;             /* 已满足堆序：父不小于子 */
        a[i] = a[child];                        /* 大孩子上移 */
    }
    a[i] = tmp;                                 /* 放到最终位置 */
}

/* 堆排序：先建大顶堆，再反复把堆顶（最大）换到末尾并缩小堆 */
void heap_sort(int a[], int n)
{
    int i, t;
    for (i = n / 2 - 1; i >= 0; i--)            /* 从最后一个非叶节点起建堆，O(n) */
        sift_down(a, i, n);
    for (i = n - 1; i > 0; i--) {
        t = a[0]; a[0] = a[i]; a[i] = t;        /* 堆顶（最大）换到当前末尾 */
        sift_down(a, 0, i);                     /* 堆大小减 1，对堆顶重新下滤 */
    }
}

int main(void)
{
    int a[] = {4, 6, 8, 5, 9, 1, 3, 2, 7};
    int n = sizeof(a) / sizeof(a[0]), i;

    heap_sort(a, n);
    for (i = 0; i < n; i++) printf("%d ", a[i]);   /* 1 2 3 4 5 6 7 8 9 */
    printf("\\n");
    return 0;
}''',
 "yaodian": "从 n/2-1 起到 0 建大顶堆；排序阶段交换堆顶与堆尾后缩小堆再下滤；O(n log n)、O(1)、不稳定；建堆 O(n)。",
 "defen": "①建堆 + 下滤 + 排序循环三步正确 ②说明 O(n log n)/O(1) 与不稳定 ③加分：指出建堆 O(n) 或最坏有保证 / Top-K / 优先队列等适用场景。",
},
{
 "num": 512,
 "title": "请实现顺序表（顺序存储的线性表）的四个基本操作：初始化、按位插入、按位删除、按值查找，并说明平均移动次数与 O(n) 复杂度。",
 "analysis": "顺序表 = 一块连续内存 + 当前长度，逻辑相邻即物理相邻：可以随机访问（下标 O(1)），但插入/删除需移动元素以保持连续。①初始化：length 置 0；②插入：先检查表满与位置合法（1 <= i <= length+1），再从最后一个元素开始向后挪一位腾出第 i 位，写入新值、长度加 1；③删除：检查位置合法（1 <= i <= length），把被删值带回后把第 i 位之后的元素逐个前移、长度减 1；④按值查找：从头顺序比较，返回第一个相等元素的位置（1 起）或 0。平均移动次数（等概率）：插入有 n+1 个可选位置，平均移动 n/2 个元素；删除有 n 个位置，平均移动 (n-1)/2 个；按值查找平均比较 (n+1)/2 次——三者都是 O(n)。与链表对比：顺序表随机访问快、无指针开销、缓存友好，但插删代价大、容量固定（静态数组）；链表插删只改指针，但不能随机访问、每节点有指针开销。易错点：插入挪动方向必须从后往前、删除必须从前往后；i 的边界（插入可取 length+1、删除不能）；忘记维护 length；平均移动次数背错。工程改进：动态数组（容量不足时 realloc 倍增）缓解固定容量问题。",
 "code": '''#include <stdio.h>

#define MAXSIZE 100                     /* 顺序表最大容量 */

typedef struct {
    int data[MAXSIZE];                  /* 连续存储数据区（下标 0 起存数据） */
    int length;                         /* 当前长度（元素个数） */
} seqlist_t;

/* 初始化：清空表 */
void seq_init(seqlist_t *L)
{
    L->length = 0;
}

/* 插入：在第 i 个位置（1 <= i <= length+1）插入 x；成功 0，失败 -1 */
int seq_insert(seqlist_t *L, int i, int x)
{
    int j;
    if (L->length >= MAXSIZE)        return -1;   /* 表满 */
    if (i < 1 || i > L->length + 1)  return -1;   /* 位置非法 */
    for (j = L->length; j >= i; j--)              /* 从后往前挪：腾出第 i 位 */
        L->data[j] = L->data[j - 1];
    L->data[i - 1] = x;
    L->length++;
    return 0;
}

/* 删除：删除第 i 个位置（1 <= i <= length）元素，值带回 *x；成功 0，失败 -1 */
int seq_delete(seqlist_t *L, int i, int *x)
{
    int j;
    if (i < 1 || i > L->length)      return -1;   /* 空表或位置非法 */
    if (x) *x = L->data[i - 1];                   /* 把被删值带出 */
    for (j = i; j < L->length; j++)               /* 从前往后挪：填补空位 */
        L->data[j - 1] = L->data[j];
    L->length--;
    return 0;
}

/* 按值查找：返回第一个等于 x 的位置（1 起），找不到返回 0 */
int seq_find(const seqlist_t *L, int x)
{
    int i;
    for (i = 0; i < L->length; i++)
        if (L->data[i] == x)
            return i + 1;
    return 0;
}

int main(void)
{
    seqlist_t L;
    int i, v;

    seq_init(&L);
    seq_insert(&L, 1, 10);              /* 10 */
    seq_insert(&L, 2, 20);              /* 10 20 */
    seq_insert(&L, 2, 15);              /* 10 15 20 */
    seq_delete(&L, 1, &v);              /* 删 10 -> 15 20 */

    printf("删除的值 = %d, 15 的位置 = %d\\n", v, seq_find(&L, 15));  /* 10, 1 */
    for (i = 0; i < L.length; i++) printf("%d ", L.data[i]);          /* 15 20 */
    printf("\\n");
    return 0;
}''',
 "yaodian": "连续存储 + 长度字段；插入从后往前挪、删除从前往后挪；边界插入 1~length+1、删除 1~length；平均移动 n/2 与 (n-1)/2；O(n)。",
 "defen": "①四个函数正确（边界检查与长度维护）②给出平均移动次数与 O(n) 结论 ③加分：与链表对比、或动态扩容改进。",
},
{
 "num": 513,
 "title": "请实现带头结点单链表的四个基本操作：尾插法建表、在第 i 位插入、按值查找、删除第一个等于 x 的节点；说明指针操作要点。",
 "analysis": "带头结点的单链表：头结点不存数据，只作统一哨兵，使首元节点的插入/删除与其它位置代码一致（不必把「改头指针」当特例处理）。①尾插法建表：初始 head->next = NULL、tail = head；每来一个元素让 tail->next 指向新节点、tail 后移——有尾指针后整体建表 O(n)（每次都从头找尾会退化为 O(n²)），且建出的链表顺序与输入一致（头插法会逆序）；②第 i 位插入：从头结点走 i-1 步找到前驱（链表只能顺链定位，没有下标随机访问），并检查 1 <= i <= 表长+1；挂链顺序「新节点先接后继、前驱再接新节点」不能反，否则丢失后继；③按值查找：从首元节点顺链比较，返回首个命中的节点指针或 NULL；④删除第一个等于 x 的节点：同样要找前驱（从头结点出发判断 p->next->data == x），摘链（前驱跳过 q）后再 free，顺序反了会断链。要点：插入与删除都要「先连后断」；找到即停（第一个）；查找/删除 O(n)，已知位置插入本身 O(1)（定位仍 O(n)）；头删、尾删、空表都要能走通。易错点：插入顺序写反丢链；删除忘记前驱；不用头结点时空表/头删特判遗漏；建表忘记置新节点 next = NULL；删除节点不 free 造成泄漏。",
 "code": '''#include <stdio.h>
#include <stdlib.h>

typedef struct node {
    int          data;
    struct node *next;
} node_t;

/* 尾插法建表（带头结点）：把数组 a 的 n 个元素依次接到表尾，返回头结点 */
node_t *list_create_tail(const int a[], int n)
{
    node_t *head = (node_t *)malloc(sizeof(node_t));   /* 头结点：不存数据 */
    node_t *tail = head;                               /* 尾指针：让尾插 O(1) */
    int i;
    if (!head) return NULL;
    head->next = NULL;
    for (i = 0; i < n; i++) {
        node_t *p = (node_t *)malloc(sizeof(node_t));
        if (!p) return head;                           /* 分配失败：返回已建部分 */
        p->data = a[i];
        p->next = NULL;
        tail->next = p;                                /* 新节点接到表尾 */
        tail = p;                                      /* 尾指针后移 */
    }
    return head;
}

/* 在第 i 个位置（1 <= i <= 表长+1）插入 x；成功 0，失败 -1 */
int list_insert(node_t *head, int i, int x)
{
    node_t *p = head;                                  /* 走到第 i-1 个节点 */
    node_t *s;
    int j = 0;
    if (i < 1) return -1;
    while (p && j < i - 1) { p = p->next; j++; }
    if (!p) return -1;                                 /* i 超过表长+1 */
    s = (node_t *)malloc(sizeof(node_t));
    if (!s) return -1;
    s->data = x;
    s->next = p->next;                                 /* 先连新节点到后继 */
    p->next = s;                                       /* 再让前驱指向新节点 */
    return 0;
}

/* 按值查找：返回第一个值为 x 的节点指针，未找到返回 NULL */
node_t *list_find(node_t *head, int x)
{
    node_t *p = head->next;                            /* 从首元节点开始 */
    while (p && p->data != x) p = p->next;
    return p;
}

/* 删除第一个值等于 x 的节点；成功 0，未找到 -1 */
int list_delete(node_t *head, int x)
{
    node_t *p = head, *q;
    while (p->next && p->next->data != x)              /* 停下时 p 是待删节点的前驱 */
        p = p->next;
    if (!p->next) return -1;                           /* 到链尾仍没找到 */
    q = p->next;                                       /* q = 待删节点 */
    p->next = q->next;                                 /* 摘链：前驱跳过 q */
    free(q);
    return 0;
}

/* 打印整表（辅助函数） */
void list_print(node_t *head)
{
    node_t *p = head->next;
    while (p) { printf("%d ", p->data); p = p->next; }
    printf("\\n");
}

int main(void)
{
    int a[] = {1, 2, 3, 4};
    node_t *head = list_create_tail(a, 4);             /* 1 2 3 4 */
    node_t *p;

    list_insert(head, 1, 0);                           /* 0 1 2 3 4 */
    list_insert(head, 6, 9);                           /* 0 1 2 3 4 9 */
    list_delete(head, 3);                              /* 0 1 2 4 9 */
    p = list_find(head, 4);
    printf("找到 4 吗：%s\\n", p ? "是" : "否");
    list_print(head);                                  /* 0 1 2 4 9 */

    p = head;                                          /* 释放整表 */
    while (p) { node_t *t = p->next; free(p); p = t; }
    return 0;
}''',
 "yaodian": "头结点不作特例；尾指针使建表 O(n)；插入先连后断、删除先找前驱；查找/删除 O(n)、定位后插入 O(1)。",
 "defen": "①四个函数正确（尾插建表、第 i 位插入、查找、删除）②说明头结点作用与指针操作顺序要点 ③加分：说明尾指针建表 O(n)、头删/尾删/空表边界或 free 释放。",
},
{
 "num": 514,
 "title": "请用除留余数法 + 链地址法实现哈希表的初始化、插入与查找，说明装填因子与冲突处理。",
 "analysis": "哈希表用哈希函数把关键字映射到桶，实现平均 O(1) 的插入与查找。本题用除留余数法 H(key) = key % m（m 为桶数）加链地址法（拉链）：每个桶挂一条同义词链，冲突关键字头插进对应链（头插 O(1)，与链长无关）。①初始化：所有桶指针置空、计数清零；②插入：算桶号、头插新节点（要求键不重复时先查找再插入）；③查找：算桶号后只遍历该桶的一条链，命中返回成功、到链尾未找到返回失败。关键参数：装填因子 α = 元素数 n / 桶数 m——α 越大链越长、查找越慢；拉链法允许 α 大于 1（比开放地址法宽松），但通常把 α 控制到 0.75~1 左右，超过阈值就扩容（桶数约翻倍、把全部元素重新散列，摊还代价仍可接受）。哈希函数设计：除留余数法的模数 m 一般取质数（或不含 20 以内质因子的数）使分布更均匀；还有直接定址、平方取中、折叠、乘法散列等。冲突处理对比：链地址法无堆积、删除简单、适合冲突多的场合；开放地址法（线性探测、二次探测、双散列）省指针但易聚集、删除要打标记。易错点：桶数取偶数/合数导致分布变差；装填因子算反（应为 n/m）；查找失败也要遍历整条链；扩容时忘记重新散列；删除节点要摘链并释放内存。",
 "code": '''#include <stdio.h>
#include <stdlib.h>

#define HASH_SIZE 13                    /* 桶数：除留余数法的模数，一般取质数 */

typedef struct hashnode {
    int              key;
    struct hashnode *next;              /* 拉链：同义词挂同一条链 */
} hashnode_t;

typedef struct {
    hashnode_t *buckets[HASH_SIZE];     /* 桶数组：每个元素是一条链的头指针 */
    int         count;                  /* 已存元素个数（用于算装填因子 n/m） */
} hashtable_t;

/* 初始化：全部桶置空、计数清零 */
void hash_init(hashtable_t *h)
{
    int i;
    for (i = 0; i < HASH_SIZE; i++)
        h->buckets[i] = NULL;
    h->count = 0;
}

/* 哈希函数：除留余数法 */
static int hash_func(int key)
{
    return key % HASH_SIZE;
}

/* 插入：计算桶号后头插；成功 0，内存分配失败 -1 */
int hash_insert(hashtable_t *h, int key)
{
    int idx = hash_func(key);
    hashnode_t *p = (hashnode_t *)malloc(sizeof(hashnode_t));
    if (!p) return -1;
    p->key = key;
    p->next = h->buckets[idx];          /* 头插：O(1)，与链长无关 */
    h->buckets[idx] = p;
    h->count++;
    return 0;
}

/* 查找：先算桶号，再遍历该桶的链；找到 0，未找到 -1 */
int hash_find(const hashtable_t *h, int key)
{
    hashnode_t *p = h->buckets[hash_func(key)];
    while (p) {
        if (p->key == key) return 0;
        p = p->next;
    }
    return -1;
}

int main(void)
{
    hashtable_t h;
    int keys[] = {18, 23, 5, 31, 44, 10, 7, 26};
    int n = sizeof(keys) / sizeof(keys[0]), i;

    hash_init(&h);
    for (i = 0; i < n; i++)
        hash_insert(&h, keys[i]);

    printf("查 31：%s\\n", hash_find(&h, 31) == 0 ? "找到" : "未找到");   /* 找到 */
    printf("查 99：%s\\n", hash_find(&h, 99) == 0 ? "找到" : "未找到");   /* 未找到 */
    printf("元素数 %d / 桶数 %d\\n", h.count, HASH_SIZE);                /* 8 / 13 */
    return 0;
}''',
 "yaodian": "除留余数 + 链地址；插入头插 O(1)；查找算桶号后只遍历一条链；装填因子 α=n/m 控制在 1 附近，超限扩容。",
 "defen": "①初始化/插入/查找正确（桶号计算与链表操作）②说明装填因子与拉链冲突处理 ③加分：扩容重散列、桶数取质数、或与开放地址法对比。",
},
{
 "num": 515,
 "title": "请实现顺序栈（数组栈）的初始化、入栈 push、出栈 pop、判空/判满（含溢出检查），说明与链栈的取舍。",
 "analysis": "顺序栈用数组 + 栈顶下标实现后进先出：top 指向栈顶元素（本题 -1 表示空栈），入栈先 ++top 再存值、出栈先取值再 --top。四个操作：①初始化 top = -1；②判空 top == -1；③判满 top == STACK_MAX - 1；④入栈前查满、出栈前查空——两个溢出检查是栈实现的核心安全点：栈满再 push 会越界写坏相邻内存，空栈再 pop 会取到垃圾值；本题失败统一返回 -1（成功 0）。与链栈的取舍：顺序栈实现简单、无 malloc/free 开销、内存连续缓存友好，但要预先固定容量（支持任意深度需动态扩容或预估上限），适合栈深可预测、追求确定性与效率的场景；链栈用单链表头插头删，入栈/出栈 O(1)、不受固定容量限制（仅受堆限制），但每节点多一个指针开销、动态分配有碎片与失败的不确定性，适合栈深变化大、无法预估上限的场合。扩展：两栈共享空间（数组两端向中间生长）可提升利用率；若采用 top 指向「栈顶之上的空位」的变体，判空/判满条件要配套修改（判满 top == STACK_MAX）。易错点：先存值再 ++top/先 --top 再取值弄反；只判空不判满；top 语义与判据不配套；出栈忘记把值带出。",
 "code": '''#include <stdio.h>

#define STACK_MAX 100                   /* 栈的最大容量 */

typedef struct {
    int data[STACK_MAX];                /* 连续存储区 */
    int top;                            /* 栈顶下标：-1 表示空栈 */
} seqstack_t;

/* 初始化：置空栈 */
void stack_init(seqstack_t *s)
{
    s->top = -1;
}

/* 判空：空返回 1 */
int stack_empty(const seqstack_t *s)
{
    return s->top == -1;
}

/* 判满：满返回 1 */
int stack_full(const seqstack_t *s)
{
    return s->top == STACK_MAX - 1;
}

/* 入栈：成功 0；栈满（上溢）返回 -1 */
int stack_push(seqstack_t *s, int x)
{
    if (stack_full(s)) return -1;       /* 溢出检查：不查会越界写 */
    s->data[++s->top] = x;              /* 先增栈顶下标，再存入 */
    return 0;
}

/* 出栈：成功 0 且值带回 *x；空栈（下溢）返回 -1 */
int stack_pop(seqstack_t *s, int *x)
{
    if (stack_empty(s)) return -1;      /* 下溢检查：空栈取到的是垃圾值 */
    if (x) *x = s->data[s->top--];      /* 先取值，再减栈顶下标 */
    return 0;
}

int main(void)
{
    seqstack_t s;
    int v, i;

    stack_init(&s);
    for (i = 1; i <= 5; i++) stack_push(&s, i * 10);   /* 10 20 30 40 50 */
    while (!stack_empty(&s)) {                          /* 出栈：后进先出 */
        if (stack_pop(&s, &v) == 0) printf("%d ", v);   /* 50 40 30 20 10 */
    }
    printf("\\n");
    printf("空栈出栈返回 %d\\n", stack_pop(&s, &v));     /* -1（下溢保护） */
    return 0;
}''',
 "yaodian": "数组 + top 下标；push 先判满、pop 先判空，失败返回 -1；顺序栈操作 O(1)、容量固定；链栈动态扩展但有指针与分配开销。",
 "defen": "①四个功能正确（含溢出检查与返回值约定）②说明与链栈的取舍（固定容量 vs 动态分配）③加分：两栈共享空间、动态扩容、或 top 语义变体说明。",
},
]
