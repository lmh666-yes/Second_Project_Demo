#ifndef OTA_APP_H
#define OTA_APP_H

/* ============================================================
 *  ota_app —— App 侧的 OTA 接收状态机（把新固件收进下载区，再交给 Boot 搬运）
 *
 *  它做的事（按收到帧的顺序）：
 *    BEGIN → 记下版本与镜像大小（>48KB 直接拒，本方案在 RAM 里攒完整镜像）
 *    DATA  → 按偏移写进 RAM 缓冲，同时累计整镜像 CRC（每帧回一条 STAT 让上位机对进度）
 *    END   → 整镜像 CRC 对了才动 Flash：
 *             ① 先回 ERASING（这一步必须先发！下面的擦除是秒级，期间 CPU 取指被挂起）
 *             ② 擦下载区（Sector 4，64KB）
 *             ③ 写镜像 → ④ **最后写固件头** → ⑤ 回 DONE → ⑥ 复位，交给 Bootloader 搬运
 *
 *  为什么先在 RAM 里攒、最后才写 Flash：
 *    · Flash 只能把 1 写成 0，同一处改写必须先擦；边收边写一旦重传/丢帧就会写出坏数据。
 *      攒在 RAM 里，CRC 不过就一个字节都不碰 Flash，"App 区永远是完整的新程序或完整的旧程序"。
 *    · 代价写在 ota_part.h 里：占 48KB RAM，且镜像不能超过运行区大小。
 * ============================================================ */

#include <stdint.h>

/* 准备：清状态、复位收帧状态机。上电调一次（在 SYS_FRAME 那路串口 InitRxIT 之后）。 */
void OTA_App_Init(void);

/* 由任务周期调用：把串口上收到的字节收成帧并处理。 */
void OTA_App_Poll(void);

/* 当前状态（OTA_ST_xxx）/ 进度百分比（0~100），给 LED 或调试打印用。 */
uint8_t OTA_App_State(void);
uint8_t OTA_App_Progress(void);

#endif /* OTA_APP_H */
