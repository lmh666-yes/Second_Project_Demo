#ifndef __FWLIB_SYS_STR_H
#define __FWLIB_SYS_STR_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_str.h —— 【工具】字符串 / 命令解析工具  头文件
 * ================================================================
 *  设计定位 : 串口"命令协议"的解析三件套 + 格式化组包——
 *             查找命令 / 分解参数 / 字符串转整数 / 格式化到字符串
 *  标准库对照（本模块是 C 标准库字符串函数的手写等价版,便于对照
 *             学习;不用本模块时,换成下面右侧的 C 库函数也一样）:
 *     SYS_STR_Find   =  strstr()     —— 查找子串（命令识别）
 *     SYS_STR_Split  =  strtok()     —— 按分隔符分解（取参数）
 *     SYS_STR_ToInt  =  atoi()       —— 字符串转整数
 *     SYS_STR_Format =  sprintf()    —— 格式化组包（配合串口发送）
 *
 *  典型流水线（教材"以 # 结尾命令"的库版,收帧见 SYS_USART_ReadUntil）:
 *     收: SYS_USART_ReadUntil(..., '#', ...)   → 得到不含 '#' 的一整条命令
 *     判: SYS_STR_Find(line, "SET-DATE")       → 这是哪条命令
 *     拆: SYS_STR_Split(line, ":", arg, n)     → 命令与参数分开
 *     转: SYS_STR_ToInt(arg[1])                → 参数变数字
 *     发: SYS_USART_SendFormat(..., "YEAR:%d", y) → 组包并发出
 *
 *  使用方式 :
 *      char line[64] = "SET-DATE:2026/9/22";
 *      char *arg[4];
 *      if (SYS_STR_Find(line, "SET-DATE")) {          // ① 命令识别
 *          int n = SYS_STR_Split(line, ":", arg, 4);  // ② 分解:arg[0]/arg[1]
 *          if (n >= 2) {
 *              y = SYS_STR_ToInt(arg[1]);             // ③ 数字参数
 *          }
 *      }
 *  重要说明 :
 *      ① 全部"就地操作"——Split 会把分隔符改成 '\0' 并修改原字符串,
 *         传进来的必须是可写缓冲（不能是常量字符串）;
 *      ② 纯软件模块:无硬件依赖、无动态内存,只用你给的数组;
 *      ③ 溢出/越界全防护:ToInt 饱和、Split 受 max 限制、Format 有长度上限
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* 纯软件模块,无引脚 / 时钟 / 缓冲等板级配置——本区块无内容 */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 查找子串:在 str 里找 sub,找到返回"首次出现位置"的指针,否则返回 0
 * 对照 : C 标准库 strstr();手写实现便于理解"逐起点比较"的算法
 * 示例 : if (SYS_STR_Find(line, "SET-DATE")) { ... }   // 命令识别 */
char *SYS_STR_Find(const char *str, const char *sub);

/* 分解字符串:按"分隔符集合"就地切分,分出的每段地址存进 argv[]
 * 参数 : str    —— 待分解字符串（会被就地改写!不能传常量字符串）;
 *        delims —— 分隔符集合（其中每个字符都算分隔,如 ":" 或 "/-."）;
 *        argv   —— 输出:各段起始地址数组;max —— argv 最多存几段
 * 返回 : 分出的段数（连续分隔符不产生空段,与 strtok 行为一致）
 * 对照 : C 标准库 strtok() 循环;举例:
 *        "SET-DATE:2026/9/22" 先按 ":" 分 → "SET-DATE" / "2026/9/22"
 *        再对第二段按 "/-." 分 → "2026" / "9" / "22"
 * 示例 : char *arg[4];
 *        int n = SYS_STR_Split(line, ":", arg, 4);
 *        // n>=2 时 arg[0]="SET-DATE",arg[1]="2026/9/22" */
int SYS_STR_Split(char *str, const char *delims, char *argv[], int max);

/* 字符串转整数（十进制;自动跳过前导空格,支持 +/- 号）
 * 返回 : 解析出的数值;完全不是数字时返回 0;溢出饱和到 ±2147483647
 * 对照 : C 标准库 atoi()
 * 示例 : int y = SYS_STR_ToInt("2026");      // y = 2026
 *        int m = SYS_STR_ToInt("-9abc");    // m = -9(解析到非数字为止) */
int32_t SYS_STR_ToInt(const char *str);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 带"成功标志"的字符串转整数（排查"参数解析失败"时用）
 * 参数 : ok —— 输出:1 = 至少解析到一位数字;0 = 不是数字串（可传 0 不看）
 * 示例 : uint8_t ok; int32_t v = SYS_STR_ParseInt(arg[1], &ok);
 *        if (!ok) SYS_USART_SendLine(SYS_USART_1, "BAD ARG"); */
int32_t SYS_STR_ParseInt(const char *str, uint8_t *ok);

/* 格式化到字符串（等价 sprintf,但带长度上限、不会越界）
 * 参数 : buf/size —— 输出缓冲及总大小;format... —— 与 printf 相同
 * 返回 : 写入的字符数（超长按 size 截断,恒有结尾 '\0'）
 * 对照 : C 标准库 snprintf();配合 SYS_USART_SendString 即可发送
 * 示例 : char msg[64];
 *        SYS_STR_Format(msg, sizeof(msg), "%d-%d-%d", y, m, d);
 *        SYS_USART_SendString(SYS_USART_1, msg); */
int SYS_STR_Format(char *buf, uint16_t size, const char *format, ...);

#endif /* __FWLIB_SYS_STR_H */
