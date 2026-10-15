#ifndef __FWLIB_SYS_FLASH_H
#define __FWLIB_SYS_FLASH_H

#include "stm32f4xx.h"
#include <stddef.h>     /* NULL（可省参数判断，sys_flash.c 使用） */

/* sys_flash.h — 内部 Flash 擦写与参数保存
 * 底层按扇区擦除、任意长度写入并回读校验；上层为参数区保存 / 读回，带魔术字与
 * 校验和，首次上电返回 SYS_FLASH_ERR_EMPTY。
 * 约束：参数区扇区不得被程序代码 / Bootloader 占用；擦除最小单位为扇区（整扇区被擦）；
 * 擦写期间 CPU 取指被硬件暂挂，大扇区擦除可达秒级，与 sys_wdg 同用需先评估看门狗超时；
 * 写入寿命约 1 万次。
 * 调用：SYS_FLASH_LoadParams(&cfg, sizeof(cfg), 0) / SYS_FLASH_SaveParams(&cfg, sizeof(cfg)) */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 参数区起始地址：F407ZE 为 512KB 型号，Flash 到 0x0807FFFF，可用 Sector 0~7；
 * A 块取最后 128KB 扇区（Sector 7）的后半段 0x08070000。
 * 约束 : 程序代码不得越过 0x08060000，否则代码落入 Sector 7，保存参数时被整扇区擦除。
 * 换型号（ZG 1MB）改用 1MB 型号的地址。 */
#define SYS_FLASH_PARAM_ADDR    0x08070000UL

/* 参数区第二块（A/B 轮换用）：0x08060000 = 512KB 型号 Sector 7 起始
 * 约束 : 单块保存时，擦除后写入完成前掉电则旧参数已擦、新参数不完整，无权回滚；
 *        A/B 轮换改为写另一块，校验通过才成为最新，掉电时读回旧块。
 *        两块不得落在程序代码占用的扇区，且必须是不同扇区：
 *        Sector 7 = 0x08060000~0x0807FFFF，A 块后半段、B 块前半段。 */
#define SYS_FLASH_PARAM_ADDR2   0x08060000UL

/* 参数区最大字节数（含头部与校验和；不要超过所在扇区大小） */
#define SYS_FLASH_PARAM_MAX     2048U

/* 参数区魔术字：判断"是否保存过"的标记（改动它会使旧数据失效） */
#define SYS_FLASH_MAGIC         0x50415241UL    /* 'PARA' */

/* -------------------- 返回值 -------------------- */
#define SYS_FLASH_OK            0       /* 成功 */
#define SYS_FLASH_ERR_PARAM     1       /* 参数非法（长度 / 地址 / 对齐） */
#define SYS_FLASH_ERR_TIMEOUT   2       /* Flash 忙 / 超时（预留，一般不会出现） */
#define SYS_FLASH_ERR_WRITE     3       /* 擦除 / 写入失败（回读校验不符） */
#define SYS_FLASH_ERR_EMPTY     4       /* 未保存过（魔术字不符，首次正常） */
#define SYS_FLASH_ERR_SUM       5       /* 校验和不符（数据损坏） */


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* 擦除 addr 所在扇区（自动定位扇区号；addr 须在芯片 Flash 内）
 * 返回 : SYS_FLASH_OK 或错误码
 * 标准库 : FLASH_Unlock / FLASH_ClearFlag / FLASH_EraseSector(VoltageRange_3) / FLASH_Lock
 * 示例 : SYS_FLASH_EraseSector(0x08070000); */
uint8_t SYS_FLASH_EraseSector(uint32_t addr);

/* 从 addr 写入 len 字节（addr 须 4 字节对齐；写前须先擦除）
 * 说明 : 按字编程，每字写完立即回读校验；len 非 4 的倍数时，尾部未用字节填 0xFF
 * 返回 : SYS_FLASH_OK / SYS_FLASH_ERR_PARAM / SYS_FLASH_ERR_WRITE …
 * 标准库 : FLASH_Unlock / FLASH_ClearFlag / FLASH_ProgramWord / FLASH_Lock */
uint8_t SYS_FLASH_Write(uint32_t addr, const void *data, uint32_t len);

/* 读取 Flash（Flash 映射在地址空间，可直接读；buf 不得为 NULL）
 * 标准库 : 无（memcpy 直读） */
void SYS_FLASH_Read(uint32_t addr, void *buf, uint32_t len);


/* ================================================================
 *                    区块 3：扩展功能（参数区一键保存 / 读回）
 * ================================================================ */
/* 保存参数到参数区（A/B 两块轮换写入）
 *   数据布局：[魔术字 4B][序号 4B][长度 4B][数据(按字补齐)][校验和 4B]
 * 说明 : 写另一块，校验通过才算最新；擦除后掉电时旧块仍完整，读回自动退回旧块
 * 参数 : data — 参数结构体指针；len — 字节数（≤ SYS_FLASH_PARAM_MAX）
 * 返回 : SYS_FLASH_OK 或错误码 */
uint8_t SYS_FLASH_SaveParams(const void *data, uint16_t len);

/* 从参数区读回并校验（先校验后拷贝，校验失败不改动缓冲）
 * 参数 : data — 接收缓冲；max_len — 缓冲大小；out_len — 实际读回字节数（不关心传 0）
 * 返回 : SYS_FLASH_OK 成功；
 *        SYS_FLASH_ERR_EMPTY — 未保存过（首次上电，属正常）；
 *        SYS_FLASH_ERR_SUM / ERR_PARAM — 数据损坏 / 缓冲太小 */
uint8_t SYS_FLASH_LoadParams(void *data, uint16_t max_len, uint16_t *out_len);

#endif /* __FWLIB_SYS_FLASH_H */
