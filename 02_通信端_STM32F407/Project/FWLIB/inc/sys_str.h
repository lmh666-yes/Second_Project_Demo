#ifndef __FWLIB_SYS_STR_H
#define __FWLIB_SYS_STR_H

#include "stm32f4xx.h"

/* 串口命令协议解析与格式化组包工具：查找命令 / 分解参数 / 字符串转整数 / 格式化到字符串
 * 本模块为 C 标准库字符串函数的手写等价版，对应关系：
 *   SYS_STR_Find = strstr()，SYS_STR_Split = strtok()，
 *   SYS_STR_ToInt = atoi()，SYS_STR_Format = sprintf()
 * 全部就地操作：Split 把分隔符改成 '\0' 并修改原字符串，传入的必须是可写缓冲，不能是常量字符串
 * 纯软件模块，无硬件依赖，无动态内存，只使用调用方给的数组
 * 溢出防护：ToInt 饱和，Split 受 max 限制，Format 有长度上限 */


/* 纯软件模块，无引脚 / 时钟 / 缓冲等板级配置 */


/* 查找子串：在 str 里找 sub，找到返回首次出现位置的指针，否则返回 0
 * 对照 C 标准库 strstr() */
char *SYS_STR_Find(const char *str, const char *sub);

/* 分解字符串：按分隔符集合就地切分，分出的每段地址存进 argv[]
 * 参数：str 待分解字符串，会被就地改写，不能传常量字符串
 *       delims 分隔符集合，其中每个字符都算分隔符，如 ":" 或 "/-."
 *       argv 输出，各段起始地址数组
 *       max argv 最多存几段
 * 返回：分出的段数；连续分隔符不产生空段，与 strtok 行为一致 */
int SYS_STR_Split(char *str, const char *delims, char *argv[], int max);

/* 字符串转整数（十进制，跳过前导空格，支持 +/- 号）
 * 返回：解析出的数值；完全不是数字时返回 0；溢出饱和到 ±2147483647
 * 对照 C 标准库 atoi() */
int32_t SYS_STR_ToInt(const char *str);


/* 带成功标志的字符串转整数
 * 参数：ok 输出，1 = 至少解析到一位数字，0 = 不是数字串；可传 0 表示不取该标志 */
int32_t SYS_STR_ParseInt(const char *str, uint8_t *ok);

/* 格式化到字符串，带长度上限
 * 参数：buf/size 输出缓冲及总大小；format... 与 printf 相同
 * 返回：写入的字符数；超长按 size 截断，恒有结尾 '\0'
 * 对照 C 标准库 snprintf() */
int SYS_STR_Format(char *buf, uint16_t size, const char *format, ...);

#endif /* __FWLIB_SYS_STR_H */
