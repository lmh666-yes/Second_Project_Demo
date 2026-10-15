#ifndef __FWLIB_SYS_FLASH_H
#define __FWLIB_SYS_FLASH_H

#include "stm32f4xx.h"
#include <stddef.h>     /* NULL：sys_flash.c 参数判断使用 */

/* sys_flash.h : 内部 Flash 扇区擦除、写入与参数保存
 * 依赖 Keil RTE 的 Flash 组件与 SPL 的 stm32f4xx_flash.c
 * 参数区数据带魔术字与校验和，首次上电读取返回未保存过
 * 擦除最小单位是扇区（16/64/128KB），保存参数会整扇区擦除
 * 擦写期间 CPU 取指被硬件暂挂，大扇区擦除可达秒级，避开喂狗临界时刻与高频中断
 * Flash 写入寿命约 1 万次，不要在高频循环里反复保存 */


/* 定义与宏定义区 */
/* 参数区起始地址：1MB 型号的 Sector 11 起始，即最后 128KB
 * 程序代码不得越过 0x080E0000（约 896KB 处），否则会被保存参数的整扇区擦除破坏
 * F407ZG：Sector 11 = 0x080E0000 ~ 0x080FFFFF；换型号需查手册的扇区划分 */
#define SYS_FLASH_PARAM_ADDR    0x080E0000UL

/* 参数区第二扇区（A/B 轮换用）：0x080C0000 = 1MB 型号的 Sector 10 起始
 * 单块保存在擦除后、写完前掉电会同时丢失新旧参数，校验和只能判坏不能回滚
 * A/B 轮换始终写另一块，写完且校验通过才生效；掉电时旧块仍完整，读回时自动退回
 * 两块都必须位于程序代码不会用到的扇区，否则擦除会破坏程序 */
#define SYS_FLASH_PARAM_ADDR2   0x080C0000UL

/* 参数区最大字节数，含头部与校验和，不得超过所在扇区大小 */
#define SYS_FLASH_PARAM_MAX     2048U

/* 参数区魔术字：判断是否保存过的标记，改动会使旧数据失效 */
#define SYS_FLASH_MAGIC         0x50415241UL    /* 'PARA' */

/* 返回值 */
#define SYS_FLASH_OK            0       /* 成功 */
#define SYS_FLASH_ERR_PARAM     1       /* 参数非法（长度 / 地址 / 对齐） */
#define SYS_FLASH_ERR_TIMEOUT   2       /* Flash 忙 / 超时（预留，一般不会出现） */
#define SYS_FLASH_ERR_WRITE     3       /* 擦除 / 写入失败（回读校验不符） */
#define SYS_FLASH_ERR_EMPTY     4       /* 未保存过（魔术字不符，首次正常） */
#define SYS_FLASH_ERR_SUM       5       /* 校验和不符（数据损坏） */


/* 基础功能 */
/* 擦除 addr 所在扇区，自动定位扇区号；addr 必须在芯片 Flash 范围内
 * 返回 : SYS_FLASH_OK 或错误码
 * 标准库 : FLASH_Unlock + FLASH_ClearFlag + FLASH_EraseSector(VoltageRange_3) + FLASH_Lock */
uint8_t SYS_FLASH_EraseSector(uint32_t addr);

/* 从 addr 写入 len 字节；addr 必须 4 字节对齐，写前先擦除该扇区
 * 说明 : 内部按字编程，每写一个字立即回读校验；len 不是 4 的倍数时尾部未用字节以 0xFF 填充
 * 返回 : SYS_FLASH_OK / SYS_FLASH_ERR_PARAM / SYS_FLASH_ERR_WRITE
 * 标准库 : FLASH_Unlock + FLASH_ClearFlag + FLASH_ProgramWord + FLASH_Lock */
uint8_t SYS_FLASH_Write(uint32_t addr, const void *data, uint32_t len);

/* 读取 Flash：Flash 映射在地址空间，memcpy 直读即可，buf 不能为 NULL */
void SYS_FLASH_Read(uint32_t addr, void *buf, uint32_t len);


/* 扩展功能：参数区保存与读回 */
/* 保存参数到参数区，A/B 两块扇区轮换写入；返回 SYS_FLASH_OK 或错误码
 * 数据布局：[魔术字 4B][序号 4B][长度 4B][数据(按字补齐)][校验和 4B]
 * 说明 : 每次写另一块，写完校验通过才生效，擦除后掉电时旧参数仍完整，读回时自动退回旧块
 * 参数 : data 为参数结构体指针；len 为字节数，不超过 SYS_FLASH_PARAM_MAX */
uint8_t SYS_FLASH_SaveParams(const void *data, uint16_t len);

/* 从参数区读回并校验，校验失败不修改接收缓冲
 * 参数 : data 为接收缓冲；max_len 为缓冲大小；out_len 为实际读回字节数，不关心传 0
 * 返回 : SYS_FLASH_OK 成功；SYS_FLASH_ERR_EMPTY 没保存过（首次上电属正常）
 *        SYS_FLASH_ERR_SUM / SYS_FLASH_ERR_PARAM 数据损坏 / 缓冲太小 */
uint8_t SYS_FLASH_LoadParams(void *data, uint16_t max_len, uint16_t *out_len);

#endif /* __FWLIB_SYS_FLASH_H */
