#ifndef __FWLIB_SYS_FLASH_H
#define __FWLIB_SYS_FLASH_H

#include "stm32f4xx.h"
#include <stddef.h>     /* NULL（可省参数判断，sys_flash.c 使用） */

/* ================================================================
 *  sys_flash.h —— 【系统】内部 Flash 擦写与参数保存模块  头文件
 * ================================================================
 *  设计定位 : 把"参数 / 设置掉电不丢"这件事变简单——
 *             ① 底层：按扇区擦除、任意长度写入（自动回读校验）；
 *             ② 上层（区块 3）：参数区一键保存 / 读回，
 *                带魔术字与校验和，第一次上电读取会返回"未保存过"。
 *
 *  依赖 : Keil RTE 组件 Flash（已勾选，SPL 的 stm32f4xx_flash.c）
 *  标准库关键词 : FLASH_Unlock / FLASH_Lock / FLASH_ClearFlag /
 *                 FLASH_EraseSector / FLASH_ProgramWord（读回校验为直接内存读）
 *
 *  使用方式 :
 *      typedef struct { ... } Cfg_t;
 *      Cfg_t cfg;
 *      if (SYS_FLASH_LoadParams(&cfg, sizeof(cfg), 0) != SYS_FLASH_OK) {
 *          // 首次上电或数据损坏：保持默认值即可
 *      }
 *      ... 修改 cfg ...
 *      SYS_FLASH_SaveParams(&cfg, sizeof(cfg));   // 保存后立即生效
 *
 *  重要注意事项 :
 *   ① 参数区默认取 1MB 型号的最后 128KB 扇区首地址（0x080E0000），
 *      必须确认程序代码 / Bootloader 没有用到该扇区，否则互相破坏；
 *   ② 擦除的最小单位是"扇区"（16/64/128KB），保存参数会整扇区擦掉；
 *   ③ 擦写期间 CPU 取指会被硬件暂挂（大扇区擦除可达秒级）——
 *      避免在喂狗临界时刻 / 高频中断密集处做擦写；与 sys_wdg 同用时
 *      要先评估看门狗超时（或擦写前喂一次、适当加大超时）；
 *   ④ Flash 写入寿命约 1 万次：不要在高频循环里反复保存。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* 参数区起始地址：默认 = 1MB 型号的 Sector 11 起始（最后 128KB）
 * ⚠ 程序代码不要越过 0x080E0000（约 896KB 处）——否则编译进
 *   Sector 11 的代码会被"保存参数"的整扇区擦除破坏！
 * 修改指引：选一个"程序绝不会用到"的扇区，取其首地址填这里
 * （F407ZG：Sector 11 = 0x080E0000 ~ 0x080FFFFF；换型号先看手册） */
#define SYS_FLASH_PARAM_ADDR    0x080E0000UL

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
/* 擦除 addr 所在扇区（自动定位扇区号；addr 必须在芯片 Flash 内）
 * 返回 : SYS_FLASH_OK 或错误码
 * 标准库 : FLASH_Unlock + FLASH_ClearFlag + FLASH_EraseSector(VoltageRange_3)
 *          + FLASH_Lock
 * 示例 : SYS_FLASH_EraseSector(0x080E0000);   // 擦参数区所在扇区 */
uint8_t SYS_FLASH_EraseSector(uint32_t addr);

/* 从 addr 写入 len 字节（addr 必须 4 字节对齐；写前先自行擦除）
 * 说明 : 内部按"字"编程，每写一个字立即回读校验；
 *        长度不是 4 的倍数时，尾部那一字的未用字节以 0xFF 填充
 * 返回 : SYS_FLASH_OK / SYS_FLASH_ERR_PARAM / SYS_FLASH_ERR_WRITE …
 * 标准库 : FLASH_Unlock + FLASH_ClearFlag + FLASH_ProgramWord + FLASH_Lock
 * 示例 : SYS_FLASH_EraseSector(0x080E0000);              // ① 先擦扇区
 *        SYS_FLASH_Write(0x080E0000, &cfg, sizeof(cfg));  // ② 再写入 */
uint8_t SYS_FLASH_Write(uint32_t addr, const void *data, uint32_t len);

/* 读取 Flash（Flash 可直接像内存一样读；buf 不能为 NULL）
 * 标准库 : 无——Flash 映射在地址空间,memcpy 直读即可
 * 示例 : uint8_t head[8];
 *        SYS_FLASH_Read(0x080E0000, head, 8);   // 读回 8 字节 */
void SYS_FLASH_Read(uint32_t addr, void *buf, uint32_t len);


/* ================================================================
 *                    区块 3：扩展功能（参数区一键保存 / 读回）
 * ================================================================ */
/* 保存参数到参数区（先整扇区擦除，再写入）
 *   数据布局：[魔术字 4B][长度 4B][数据(按字补齐)][校验和 4B]
 * 参数 : data —— 参数结构体指针；len —— 字节数（≤ SYS_FLASH_PARAM_MAX）
 * 返回 : SYS_FLASH_OK 或错误码
 * 示例 : Cfg_t cfg;  ...修改 cfg...
 *        SYS_FLASH_SaveParams(&cfg, sizeof(cfg));   // 一键保存（掉电不丢）*/
uint8_t SYS_FLASH_SaveParams(const void *data, uint16_t len);

/* 从参数区读回并校验
 * 参数 : data —— 接收缓冲；max_len —— 缓冲大小；
 *        out_len —— 实际读回字节数（不关心就传 0）
 * 返回 : SYS_FLASH_OK 成功；
 *        SYS_FLASH_ERR_EMPTY —— 没保存过（首次上电，属正常）；
 *        SYS_FLASH_ERR_SUM / ERR_PARAM —— 数据损坏 / 缓冲太小
 * 示例 : Cfg_t cfg;
 *        if (SYS_FLASH_LoadParams(&cfg, sizeof(cfg), 0) != SYS_FLASH_OK) {
 *            ...首次上电:用默认值...
 *        } */
uint8_t SYS_FLASH_LoadParams(void *data, uint16_t max_len, uint16_t *out_len);

#endif /* __FWLIB_SYS_FLASH_H */
