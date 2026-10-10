#ifndef __FWLIB_SYS_STR_H
#define __FWLIB_SYS_STR_H

#include "stm32f4xx.h"

/* sys_str.h : 串口命令解析与格式化组包
 * Find=strstr,Split=strtok,ToInt=atoi,ParseInt=atoi+成功标志,Format=snprintf;
 * 收帧见 SYS_USART_ReadUntil;纯软件模块,无动态内存;
 * Split 就地改写缓冲,不可传常量字符串 */


/* ================================================================
 *                    区块 1：定义与宏定义区
 * ================================================================ */
/* 纯软件模块,无引脚 / 时钟 / 缓冲等板级配置,本区块无内容 */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 查找子串:返回 sub 在 str 中首次出现的指针,未找到返回 0 */
char *SYS_STR_Find(const char *str, const char *sub);

/* 分解字符串:按分隔符集合就地切分,各段起始地址存入 argv[]
 * 参数 : str 待分解缓冲,会写入 '\0',须可写;
 *        delims 分隔符集合,每个字符均为分隔符;
 *        argv 输出各段起始地址;max 最多存储段数
 * 返回 : 段数;连续分隔符不产生空段 */
int SYS_STR_Split(char *str, const char *delims, char *argv[], int max);

/* 字符串转整数:十进制,跳过前导空格,支持 +/- 号,遇非数字字符停止
 * 返回 : 数值;非数字串返回 0;溢出饱和到 ±2147483647 */
int32_t SYS_STR_ToInt(const char *str);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 带成功标志的字符串转整数
 * 参数 : ok 输出,解析到至少一位数字置 1,否则置 0;可传 0 不取
 * 返回 : 同 SYS_STR_ToInt */
int32_t SYS_STR_ParseInt(const char *str, uint8_t *ok);

/* 格式化到字符串,等价 snprintf
 * 参数 : buf 输出缓冲;size 缓冲总大小;format... 同 printf
 * 返回 : 写入字符数;超出 size 截断,恒有结尾 '\0' */
int SYS_STR_Format(char *buf, uint16_t size, const char *format, ...);

#endif /* __FWLIB_SYS_STR_H */
