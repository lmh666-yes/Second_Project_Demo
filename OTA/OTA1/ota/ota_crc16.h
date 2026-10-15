#ifndef OTA_CRC16_H
#define OTA_CRC16_H

/* ============================================================
 *  ota_crc16 —— 整镜像校验用的 CRC16-MODBUS（App / Boot / PC 脚本三端必须一致）
 *
 *  为什么单独抽出来：
 *    工程 sys_frame 的帧 CRC 也是 CRC16-MODBUS（同初值 0xFFFF、同多项式 0xA001 反射），
 *    但它没有对外暴露的独立函数（帧 CRC 由 SYS_FRAME_Build 内部算）。
 *    整镜像校验要"边收边累计"，所以这里给一份可增量调用的实现，
 *    并且 Boot 与 App **共用同一个 .c**（Boot 用相对路径引用本文件），避免两份实现漂移。
 *
 *  自检向量（三端都拿它自验，改坏了立刻能发现）：
 *      CRC16("123456789") == 0x4B37
 * ============================================================ */

#include <stdint.h>

/* 增量计算：seed 传 0xFFFF 表示"从头开始"，之后把上一次的返回值再传回来即可。
   参数 buf / len 传成 (NULL, 0) 时返回 seed 本身（方便"只重置不计算"）。 */
uint16_t OTA_Crc16(const void *buf, uint32_t len, uint16_t seed);

/* 一次性算一段缓冲区：等价于 OTA_Crc16(buf, len, 0xFFFF)。 */
uint16_t OTA_Crc16Block(const void *buf, uint32_t len);

#endif /* OTA_CRC16_H */
