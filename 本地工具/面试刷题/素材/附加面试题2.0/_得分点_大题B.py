# -*- coding: utf-8 -*-
POINTS = {
    443: "① 签名 int countonenumber(BYTE b[], int n)，BYTE 用 typedef 定义成 unsigned char ② 核心语句 v &= (v - 1) 写对，每执行一次消掉最低位的 1，循环次数即该字节中 1 的个数 ③ 必须在局部副本 v 上操作，直接改调用者数组算错 ④加分：这是 Kernighan 法，最坏 O(n)、空间 O(1)，比逐位移动快。",
    444: "① 任务函数原型 void Task1(void *p)，函数体内 while(1) 死循环 ② 打印语句 Task 1 is running 与 Task 2 is running 各在对应任务中 ③ 周期用 vTaskDelay 配 pdMS_TO_TICKS 换算 1000ms 与 2000ms，阻塞让出 CPU 而非忙等 ④ 建任务后启动调度器 ⑤加分：无阻塞点同级任务得不到调度。",
    446: "① 前提：I2C 用 SCL、SDA 两根开漏线且需上拉，空闲时两线均为高 ② 起始 START：SCL 高电平期间 SDA 由高变低，跳变画在 SCL 高电平平台内 ③ 停止 STOP：SCL 高电平期间 SDA 由低变高 ④ 数据位规则：SDA 只在 SCL 低电平期间变化，高电平期间保持稳定 ⑤ 画图 SCL 在上、SDA 在下 ⑥加分：重复起始 Sr 与起始的区别。",
    447: "① 周期 T = 1/f = 1/10Hz = 100ms 算对，写成 10ms 是把频率当周期 ② 占空比算对：平均电压 = 占空比×满幅电压，2.5V / 5V = 50% ③ 波形正确：每周期前 50ms 出 5V、后 50ms 出 0V，跳变陡直并重复 2~3 个周期 ④ 带坐标系并标数值：横轴标 0/50/100/150/200ms ⑤加分：画出 2.5V 平均电平虚线。",
    448: "① 默认目标 all: target 写在最前面，make 默认执行第一个目标 ② 依赖写对：lib.so 依赖 a.c，target 依赖 lib.so 和 b.c，先编库再链接 ③ 命令必须 Tab 缩进，空格会报 missing separator ④ 共享库命令带 -fPIC -shared，链接用 -l:lib.so ⑤加分：clean 应声明 .PHONY。",
    449: "① 签名 char *my_strstr(const char *hay, const char *needle)，返回首次出现位置，未找到返回 NULL ② 空子串 if (!*needle) return (char *)hay ③ 外层 for 枚举起点，内层用副本 h、n 同步比较 ④ 命中判定看 if (!*n)，不能看 !*h ⑤加分：最坏 O(n×m)，KMP 可到 O(n+m)。",
    453: "① 签名 char *str_cat(char *dst, const char *src)，返回 dst 便于链式调用 ② 先找尾 while (*p) p++ 停在 dst 的 '\\0' 处作追加起点，不能从头覆盖 ③ 追加用 while ((*p++ = *src++)); 须把 '\\0' 一并拷入 ④ 要有测试例程 ⑤加分：O(len(dst)+len(src))、空间 O(1)。",
    455: "① 从后往前合并 k = n+m-1 指向 A 末尾空位，i、j 指向两数组当前最大值，较大者填入 A[k--] ② 核心语句 A[k--] = i >= 0 && A[i] > B[j] ? A[i--] : B[j--] ③ 必须有 i >= 0 保护，否则 i 变负后越界 ④ 终止条件 j >= 0，B 放完即结束 ⑤加分：从后写不覆盖未处理元素，O(n+m)、O(1)。",
    456: "① 节点定义 typedef struct Node { int data; struct Node *left, *right; } Node ② 递归出口 if (!root) return 处理空节点 ③ 顺序正确：先 inorder 左子树，再打印根 printf(\"%d \")，最后 inorder 右子树，即左→根→右 ④ BST 中序为升序 ⑤加分：O(n) 时间，空间为树高 h。",
    457: "① 签名 int my_strcmp(char *source, char *dest)，相等返回 0、不等返回 -1，是二值语义 ② 循环 while (*source && *source == *dest) 两指针同步后移 ③ 退出后若 *source == *dest 返回 0，否则返回 -1 ④ 退出只有字符不等或 source 到 '\\0' 两种原因 ⑤加分：按标准库差值语义返回扣分。",
    458: "① 参数 a = 6378137.0 m、e² = 0.00669437999014 ② 卯酉圈曲率半径 N = a / sqrt(1 - e²·sin²B) ③ X = (N + H)·cosB·cosL，Y = (N + H)·cosB·sinL ④ Z = (N·(1 - e²) + H)·sinB，必须带该修正 ⑤ 输入 B、L 用弧度，易错点 ⑥加分：H=0 时满足椭球方程可自检。",
}
