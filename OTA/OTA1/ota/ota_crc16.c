#include "ota_crc16.h"

/* CRC16-MODBUS：初值 0xFFFF，多项式 0xA001（0x8005 的反射写法），结果不异或。
   与工程 sys_frame.c 里帧 CRC 的算法完全一致（同一套参数），
   所以整镜像 CRC 与帧 CRC 用同一条自检向量就能互相印证。 */
uint16_t OTA_Crc16(const void *buf, uint32_t len, uint16_t seed)
{
    const uint8_t *p = (const uint8_t *)buf;
    uint16_t crc = seed;
    uint32_t i;
    int b;

    if (p == 0 || len == 0u)
        return seed;

    for (i = 0u; i < len; i++) {
        crc ^= (uint16_t)p[i];
        for (b = 0; b < 8; b++) {
            if (crc & 0x0001u)
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            else
                crc = (uint16_t)(crc >> 1);
        }
    }
    return crc;
}

uint16_t OTA_Crc16Block(const void *buf, uint32_t len)
{
    return OTA_Crc16(buf, len, 0xFFFFu);
}
