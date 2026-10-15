/* ============================================================
 *  boot_main.c —— 板1（F407ZE）OTA 升级示例：**Bootloader 侧**入口
 *
 *  本工程（OTA/OTA1_Boot）链接在 Sector 0（0x08000000，16KB），是芯片复位后的第一段程序。
 *  它只做四件事，做完就走（或停在救援态）：
 *      ① 看下载区（0x08010000）有没有"待安装的合法固件"
 *      ② 有 → 复验整镜像 CRC
 *      ③ 通过 → 搬到 App 运行区（0x08004000）并复验，成功后作废下载区头
 *      ④ 跳转到 App 运行区（无效就停在 Boot 等救援）
 *
 *  为什么要"最后才作废下载区头"：搬运到一半掉电时，头还在 → 下次上电重搬一遍，
 *  App 区永远是完整的新程序或完整的旧程序 —— 这是不掉砖的关键，别改。
 *
 *  与 App 的约定全部写在 ..\OTA1\ota\ota_part.h（分区 / 固件头 / 帧命令字），
 *  两边共用同一个头文件，不许各自复述地址。
 *
 *  日志用 ASCII：ARMCC 对字符串字面量里的中文报 #870-D（本工程要 0 警告）。
 * ============================================================ */

#include "stm32f4xx.h"

#include "boot_uart.h"
#include "ota_image.h"
#include "ota_part.h"

/* 跳转后不会再回来，所以这里把能拦的都拦住：
   关中断 → 停 SysTick → 清掉所有 NVIC 使能与挂起 → 向量表搬到 App 区 → 设栈顶 → 跳。 */
static void Boot_JumpToApp(void)
{
    const uint32_t sp = *(volatile uint32_t *)OTA_APP_ADDR;
    const uint32_t pc = *(volatile uint32_t *)(OTA_APP_ADDR + 4u);
    uint32_t i;

    __disable_irq();

    SysTick->CTRL = 0u;
    SysTick->LOAD = 0u;
    SysTick->VAL  = 0u;

    for (i = 0u; i < 8u; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;    /* 关掉所有外部中断 */
        NVIC->ICPR[i] = 0xFFFFFFFFu;    /* 清掉所有挂起 */
    }

    /* 关键：告诉内核中断向量表在 App 区。
       App 自己也会再设一次（它改自己的 VTOR），两处都设是为了不依赖"谁先跑"。 */
    SCB->VTOR = OTA_APP_ADDR;

    __set_MSP(sp);
    ((void (*)(void))pc)();             /* 跳到 App 的复位入口；正常情况下不返回 */
}

int main(void)
{
    OtaImageInfo info;

    Boot_UartInit();
    Boot_UartStr("\r\n================================\r\n"
                 " OTA Bootloader (board1 / Sector 0)\r\n"
                 "================================\r\n");

    /* ---- 1) 下载区有活干吗 ---- */
    if (OtaImage_Pending(&info)) {
        Boot_UartStr("pending firmware: size=");
        Boot_UartDec(info.size);
        Boot_UartStr(" ver=");
        Boot_UartDec(info.ver);
        Boot_UartStr(" crc=");
        Boot_UartHex32(info.crc);
        Boot_UartStr("\r\n");

        /* ---- 2) 复验整镜像 ---- */
        if (OtaImage_Verify(&info)) {
            Boot_UartStr("image CRC ok, copying to app region ...\r\n");
            if (OtaImage_Copy(&info))
                Boot_UartStr("copy done & verified; download header invalidated\r\n");
            else
                Boot_UartStr("copy/verify failed: firmware kept, retry on next boot\r\n");
        } else {
            /* 校验不过就一个字节都不动 App 区，宁可保持旧程序 */
            Boot_UartStr("image CRC mismatch: skip install (app region untouched)\r\n");
        }
    } else {
        Boot_UartStr("no pending firmware\r\n");
    }

    /* ---- 3) App 可跑吗 ---- */
    if (OtaImage_AppValid()) {
        Boot_UartStr("jump to App (0x08004000) ...\r\n");
        Boot_JumpToApp();
        /* 不该走到这里 */
        Boot_UartStr("!! jump returned, app region abnormal\r\n");
    } else {
        Boot_UartStr("no valid app (bad stack top / reset vector)\r\n");
    }

    /* ---- 4) 救援态：两个都没得跑，就停这儿，等 PC 用串口把新固件灌进下载区 ---- */
    Boot_UartStr("rescue mode: send firmware from PC via the serial link. heartbeat ...\r\n");
    for (;;) {
        volatile uint32_t d;
        Boot_UartStr(".");
        for (d = 0u; d < 3000000u; d++) {
            __NOP();
        }
    }
}
