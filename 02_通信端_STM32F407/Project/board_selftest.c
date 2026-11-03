/* ================================================================
 *  board_selftest.c — 阶段自检（A4）
 *  板子：板2 通信端 (F407ZG)
 *  生成：2026-10-20 11:03（由 gen_selftest.py 生成，手改会被覆盖）
 *
 *  接入两步：① 本文件与 board_selftest.h 加进 Keil 工程
 *            （右键分组 → Add Existing Files）；
 *            ② main() 里串口初始化之后调一次 BOARD_SELFTEST_Run();
 *  串口 115200 会打印 STAGE<N>=PASS / FAIL。
 * ================================================================ */
#include "board_selftest.h"

#include "stm32f4xx.h"
#include "sys_usart.h"
#include "sys_clock.h"
#include "sys_tick.h"
#include "FreeRTOS.h"
#include "queue.h"
#include "sys_frame.h"
#include "modbus.h"

/* 自检输出用的串口：与 main.c 的调试串口保持一致 */
#define ST_DBG   SYS_USART_1

/* ---------- 阶段 2：时钟 168MHz + 滴答在走 ---------- */
static uint8_t st_stage2(void)
{
    uint32_t t0;

    if (SYS_CLK_GetFreq() != 168000000UL) {
        return 0U;                  /* 时钟没跑到 168MHz */
    }
    t0 = SYS_TICK_GetTick();
    SYS_TICK_Delay_ms(10U);
    if ((uint32_t)(SYS_TICK_GetTick() - t0) < 8U) {
        return 0U;                  /* 滴答没走（或走得远慢于 1ms） */
    }
    return 1U;
}

/* ---------- 阶段 3：队列能建、能发、能收（调度器启动前也能跑） ---------- */
static uint8_t st_stage3(void)
{
    QueueHandle_t q;
    uint32_t v = 0x12345678UL;
    uint32_t got = 0UL;

    q = xQueueCreate(2U, sizeof(uint32_t));
    if (q == NULL) {
        return 0U;                  /* 堆不够或参数非法 */
    }
    if (xQueueSend(q, &v, 0U) != pdPASS) {
        vQueueDelete(q);
        return 0U;
    }
    if (xQueueReceive(q, &got, 0U) != pdPASS) {
        vQueueDelete(q);
        return 0U;
    }
    vQueueDelete(q);
    return (uint8_t)(got == v);
}

/* ---------- 阶段 6：组帧 20 字节 + CRC + Verify 回环 ---------- */
static uint8_t st_stage6(void)
{
    uint8_t  f[32];
    uint16_t n;
    uint16_t crc;

    /* 25.60C / 60.30%RH / 1013.2hPa / 320lx / 15ppb / 812 */
    n = SYS_FRAME_BuildEnv(f, (uint16_t)sizeof(f),
                            2560, 6030, 10132, 320U, 15U, 812U);
    if (n != 20U) {
        return 0U;                  /* 整帧应是 12 + 8 = 20 字节 */
    }
    /* 帧头 / CMD / LEN */
    if (f[0] != 0xAAU || f[1] != 0x55U) return 0U;
    if (f[2] != 0x01U || f[3] != 0x0CU) return 0U;
    /* 数据段大端：0A00 178E 2794 0140 000F 032C */
    if (f[4]  != 0x0AU || f[5]  != 0x00U) return 0U;
    if (f[6]  != 0x17U || f[7]  != 0x8EU) return 0U;
    if (f[8]  != 0x27U || f[9]  != 0x94U) return 0U;
    if (f[10] != 0x01U || f[11] != 0x40U) return 0U;
    if (f[12] != 0x00U || f[13] != 0x0FU) return 0U;
    if (f[14] != 0x03U || f[15] != 0x2CU) return 0U;
    /* 帧尾 */
    if (f[18] != 0x55U || f[19] != 0xAAU) return 0U;
    /* CRC 覆盖 CMD..DATA 共 14 字节，低字节在前 */
    crc = MODBUS_CRC16(&f[2], 14U);
    if (f[16] != (uint8_t)(crc & 0xFFU)) return 0U;
    if (f[17] != (uint8_t)(crc >> 8))   return 0U;
    /* 整帧校验必须过 */
    if (SYS_FRAME_Verify(f, n) != SYS_FRAME_OK) return 0U;
    /* 负例：改一个字节，必须被 CRC 拦下（证明校验真的在起作用） */
    f[4] ^= 0x01U;
    if (SYS_FRAME_Verify(f, n) != SYS_FRAME_ERR_CRC) return 0U;
    return 1U;
}

/* 打印一条 STAGE<N>=PASS/FAIL */
static void st_report(uint8_t no, uint8_t ok)
{
    SYS_USART_Printf(ST_DBG, "STAGE%u=%s\r\n",
                     (unsigned)no, ok ? "PASS" : "FAIL");
}

void BOARD_SELFTEST_Run(void)
{
    SYS_USART_SendLine(ST_DBG, "");
    SYS_USART_SendLine(ST_DBG, "=== board selftest ===");
#if BOARD_SELFTEST_STAGE2
    st_report(2U, st_stage2());
#endif
#if BOARD_SELFTEST_STAGE3
    st_report(3U, st_stage3());
#endif
#if BOARD_SELFTEST_STAGE6
    st_report(6U, st_stage6());
#endif
    SYS_USART_SendLine(ST_DBG, "=== selftest done ===");
}
