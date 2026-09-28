#ifndef __FWLIB_SYS_I2C_H
#define __FWLIB_SYS_I2C_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_i2c.h —— 【系统】I2C 总线模块  头文件
 * ================================================================
 *  设计定位 : 硬件 I2C 的"薄封装"—— 面向"寄存器式"器件
 *             （写器件地址 → 写寄存器号 → 读/写数据）
 *             每次调用都自带超时和错误码，失败能说清"卡在哪一步"
 *  标准库关键词 : I2C_Init / I2C_Cmd / I2C_DeInit（初始化）;GPIO_SetBits/ResetBits（总线恢复）;
 *                 读写时序为寄存器直写（见 .c——标准库 I2C_xxx 的标志等待没有内置超时）
 *
 *  本板接线（普中-天马 F407开发板原理图；换板子改"区块 1"）:
 *      I2C1 : SCL = PB8  SDA = PB9
 *              —— 板载 24C02(EEPROM，地址 0x50) / MPU6050(六轴，0x68)
 *                 都挂在这条总线上，且已有 4.7k 上拉
 *              —— 外部 I2C 温湿度模块(如 AHT10 / SHT30)接排针同样可用
 *      I2C2 : SCL = PB10 SDA = PB11 (与 USART3 引脚复用，二选一)
 *      I2C3 : SCL = PA8  SDA = PC9
 *      ⚠ MPU6050 的中断脚 MPU_INT = PC0（需自行用 sys_exti 接管）
 *  ⚠ 本板引脚占用提醒（用之前先看，否则会跟已有外设打架）:
 *      · I2C2 的 PB10/PB11 = USART3 的 TX/RX（ESP8266 接口）→ 二选一；
 *      · I2C3 的 PA8 = 板载红外接收头（ext_io 的 EXT_IR）→ 二选一；
 *        PC9 = SDIO_D1（TF 卡数据线）→ 用了 TF 卡就不能用 I2C3。
 *      结论：本板推荐只用 I2C1（PB8/PB9，板载 24C02 + MPU6050）；
 *      外接 I2C 模块（OLED / AHT10 / SHT30）也接到 I2C1 排针上即可。
 *
 *  "无应答(ADDR NACK)"排查思路（本模块的 ErrStr 会提示到哪一步）:
 *      ① 器件地址写错——注意区分"7 位地址"和"8 位地址"：
 *         数据手册给的 0x68、0x50 等是 7 位地址，直接传；
 *         若手册给的是"写地址 0xD0 / 读地址 0xD1"（8 位），
 *         要右移一位变成 7 位（0xD0>>1 = 0x68）再传！
 *      ② 总线无上拉——SDA/SCL 必须各有一个 4.7k 左右上拉到 3.3V；
 *         本板总线已有上拉，接线到外部模块时注意模块是否自带
 *      ③ 器件没供电 / 没接好 / 地址脚接法不同（A0~A2 决定低位）
 *      ④ 总线被从机拉死（SCL 或 SDA 一直低）→ 调用 SYS_I2C_BusReset
 *
 *  使用方式 :
 *      SYS_I2C_Init(SYS_I2C_1, 100000);           // ① 100kHz 初始化
 *      if (SYS_I2C_IsDeviceReady(SYS_I2C_1, SYS_I2C_ADDR_AHT10) == 0) {
 *          uint8_t raw[6];
 *          SYS_I2C_ReadBytes(SYS_I2C_1, SYS_I2C_ADDR_AHT10, 0x00, raw, 6);
 *      } else {
 *          printf("%s\r\n", SYS_I2C_ErrStr(err)); // ② 无应答时看提示
 *      }
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换板子只改这里）
 * ================================================================ */
/* -------------------- 引脚定义 -------------------- */
#define SYS_I2C1_SCL_PORT   GPIOB
#define SYS_I2C1_SCL_PIN    GPIO_Pin_8
#define SYS_I2C1_SDA_PORT   GPIOB
#define SYS_I2C1_SDA_PIN    GPIO_Pin_9

#define SYS_I2C2_SCL_PORT   GPIOB
#define SYS_I2C2_SCL_PIN    GPIO_Pin_10
#define SYS_I2C2_SDA_PORT   GPIOB
#define SYS_I2C2_SDA_PIN    GPIO_Pin_11

#define SYS_I2C3_SCL_PORT   GPIOA
#define SYS_I2C3_SCL_PIN    GPIO_Pin_8
#define SYS_I2C3_SDA_PORT   GPIOC
#define SYS_I2C3_SDA_PIN    GPIO_Pin_9

/* -------------------- 常用器件 7 位地址 -------------------- */
/* 提醒 : 都是"7 位地址"。若数据手册给的是 8 位(如 0xD0)，右移 1 位 */
#define SYS_I2C_ADDR_24C02      0x50    /* 板载 EEPROM   */
#define SYS_I2C_ADDR_MPU6050    0x68    /* 板载六轴(AD0=0) */
#define SYS_I2C_ADDR_AHT10      0x38    /* 外接 AHT10 温湿度 */
#define SYS_I2C_ADDR_SHT30      0x44    /* 外接 SHT30 温湿度 */
#define SYS_I2C_ADDR_SSD1306    0x3C    /* 外接 OLED 屏(I2C 版) */

/* -------------------- 超时与总线恢复 -------------------- */
/* 等待标志位的最大循环次数（跑满即报"超时"，防止死等卡死程序） */
#define SYS_I2C_TIMEOUT         2000000UL
/* 总线恢复时手动拨时钟的每拍延时（空循环次数） */
#define SYS_I2C_BUS_DELAY       200UL


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* I2C 编号（与内部配置表一一对应） */
typedef enum {
    SYS_I2C_1 = 0,          /* I2C1 → PB8 / PB9   */
    SYS_I2C_2 = 1,          /* I2C2 → PB10 / PB11 */
    SYS_I2C_3 = 2,          /* I2C3 → PA8 / PC9   */
    SYS_I2C_COUNT = 3
} SysI2cId_t;

/* 错误码（函数返回 int；非 0 即失败，用 SYS_I2C_ErrStr 转文字）
 * 说明 : 把"卡在哪一步"编码出来，正是排查 I2C 问题的钥匙 */
typedef enum {
    SYS_I2C_OK          =  0,   /* 成功 */
    SYS_I2C_ERR_PARAM   = -1,   /* 参数非法（id 越界 / 空指针） */
    SYS_I2C_ERR_START   = -2,   /* 起始条件失败（总线忙、上拉缺失） */
    SYS_I2C_ERR_ADDR    = -3,   /* 器件地址无应答（见文件头排查思路） */
    SYS_I2C_ERR_DATA    = -4,   /* 数据阶段无应答（寄存器/写法不对） */
    SYS_I2C_ERR_TIMEOUT = -5,   /* 等标志超时（总线被拉死，试 BusReset） */
    SYS_I2C_ERR_BUS     = -6    /* 总线错误（BERR，时序被干扰） */
} SysI2cErr_t;

/* 初始化 I2C：时钟 / 引脚(复用开漏) / 速率
 * 标准库 : RCC_APB1PeriphClockCmd + GPIO_PinAFConfig/GPIO_Init
 *          (GPIO_Mode_AF + GPIO_OType_OD + GPIO_PuPd_UP)
 *          + I2C_DeInit + I2C_StructInit + I2C_Init + I2C_Cmd
 * 参数 : id —— 总线编号，三选一: SYS_I2C_1 / SYS_I2C_2 / SYS_I2C_3
 *              （三者引脚对照见文件头"本板接线"）
 *        speed —— 速率 Hz（0 = 默认 100000）;常用:100000(标准) / 400000(快速)
 * 示例 : SYS_I2C_Init(SYS_I2C_1, 100000);   // 板载 24C02/MPU6050 都在 I2C1 */
void SYS_I2C_Init(SysI2cId_t id, uint32_t speed);

/* 探测器件是否在线：发一次"写地址"，收到应答即在线
 * 返回 : SYS_I2C_OK = 在线；SYS_I2C_ERR_ADDR = 无应答（不在线）
 * 用途 : 排查第一步——先确认"器件理不理我"，再谈读写
 * 标准库 : 无——START/地址/STOP 时序为寄存器直写（自带超时防死等;
 *          以下 Read/Write 系列同一套时序,见 .c 的 i2c_start 等）
 * 示例 : if (SYS_I2C_IsDeviceReady(SYS_I2C_1, SYS_I2C_ADDR_24C02) == 0) {
 *            ...24C02 在线,可以继续读写了...
 *        } */
int SYS_I2C_IsDeviceReady(SysI2cId_t id, uint8_t addr7);

/* 写一个寄存器（单字节）
 * 时序 : START → 地址+W → 寄存器号 → 数据 → STOP
 * 示例 : SYS_I2C_WriteByte(SYS_I2C_1, SYS_I2C_ADDR_24C02, 0x00, 0x5A);   // 往 0x00 写 0x5A */
int SYS_I2C_WriteByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t data);

/* 读一个寄存器（单字节）
 * 时序 : START → 地址+W → 寄存器号 → 重复START → 地址+R → 读 → NACK → STOP
 * 示例 : uint8_t who;
 *        SYS_I2C_ReadByte(SYS_I2C_1, SYS_I2C_ADDR_MPU6050, 0x75, &who);  // 读 WHO_AM_I
 * 扩展提示 : 寄存器地址自增的器件批量读 → ReadBytes（先例）;
 *            非"寄存器式"器件 → 参照 .c 的 start/addr/write_raw 自行组合 */
int SYS_I2C_ReadByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t *out);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 连写多个寄存器（如初始化 MPU6050 时批量写配置表）
 * 时序 : START → 地址+W → 寄存器号 → 数据0 → 数据1 → … → STOP
 * 示例 : SYS_I2C_WriteBytes(SYS_I2C_1, SYS_I2C_ADDR_MPU6050, 0x6B, cfg, 3);
 *        // 从 0x6B 起连续写 cfg[] 的 3 个字节 */
int SYS_I2C_WriteBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                       const uint8_t *buf, uint16_t len);

/* 连读多个寄存器（如读 AHT10 的 6 字节测量数据、MPU6050 的 14 字节）
 * 示例 : uint8_t raw[6];
 *        SYS_I2C_ReadBytes(SYS_I2C_1, SYS_I2C_ADDR_AHT10, 0x00, raw, 6); */
int SYS_I2C_ReadBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                      uint8_t *buf, uint16_t len);

/* 错误码 → 中文解释（排查"无应答"时的第一手信息）
 * 示例 : int err = SYS_I2C_IsDeviceReady(SYS_I2C_1, 0x50);
 *        if (err != SYS_I2C_OK) printf("%s\r\n", SYS_I2C_ErrStr(err)); */
const char *SYS_I2C_ErrStr(int err);

/* 总线恢复：当 SDA/SCL 被从机拉死（读一直超时）时调用
 * 原理 : 关掉 I2C 外设 → 用 GPIO 手动拨 9 个时钟让从机复位
 *         → 补发 STOP → 重新初始化 I2C（沿用上次速率）
 * 示例 : if (SYS_I2C_IsDeviceReady(SYS_I2C_1, 0x50) == SYS_I2C_ERR_TIMEOUT) {
 *            SYS_I2C_BusReset(SYS_I2C_1);   // 拉死后先救总线
 *        } */
void SYS_I2C_BusReset(SysI2cId_t id);


/* ================================================================
 *  附:标准库结构体速查 —— I2C_TypeDef（定义在 stm32f4xx.h）
 * ================================================================
 *  成员一览（含库中用法）:
 *    CR1     控制 1:I2C_CR1_PE 使能 / _START 起始 / _ACK 应答 / _SWRST 软复位
 *            （库的寄存器直写时序就在写它——start/stop 步骤）
 *    CR2     控制 2:中断/DMA 相关,库未用
 *    OAR1/2  自身地址:主模式不用（做从机时才需要）
 *    DR      数据:写入 = 发送、读出 = 接收（i2c_write_raw 等读写它）
 *    SR1     状态 1:I2C_SR1_SB（起始已发出）/ _ADDR（地址已应答）/
 *            _AF（无应答）/ _BTF（字节传输完成）——"等标志+超时"循环轮询它
 *            （"无应答(ADDR NACK)"错误码就是从 I2C_SR1_AF 位来的）
 *    SR2     状态 2:I2C_SR2_BUSY（总线忙）/ _MSL（主模式）——发 START 前等等它
 *    CCR     时钟:速率分频（I2C_Init 按 speed 算好写它）
 *    TRISE   上升时间:限制信号上升沿（I2C_Init 配置）
 *    FLTR    滤波:库保持默认
 * ================================================================ */

#endif /* __FWLIB_SYS_I2C_H */
