#ifndef __FWLIB_SYS_I2C_H
#define __FWLIB_SYS_I2C_H

#include "stm32f4xx.h"

/* ================================================================
 *  sys_i2c.h — I2C 总线模块
 * ----------------------------------------------------------------
 *  功能  : 硬件 I2C 薄封装，面向寄存器式器件（写器件地址 → 写寄存器号 →
 *          读写数据）;每次调用自带超时与错误码
 *  标准库: I2C_Init / I2C_Cmd / I2C_DeInit;GPIO_SetBits/ResetBits（总线恢复）;
 *          读写时序为寄存器直写，见 sys_i2c.c（标志等待无内置超时）
 *  接线  : GEC-M4 原理图;I2C1 SCL=PB8 SDA=PB9（板载 24C02 / MPU6050）;
 *          I2C2 SCL=PB10 SDA=PB11（与 USART3 复用）;I2C3 SCL=PA8 SDA=PC9
 *  地址  : 8 位写地址右移 1 位得 7 位（0xD0>>1 = 0x68）;SDA/SCL 各需 4.7k
 *          上拉到 3.3V（板载已有）
 *  约束  : 全操作超时封顶 SYS_I2C_TIMEOUT;错误码区分 START/地址/数据/
 *          超时/总线;总线恢复 SYS_I2C_BusReset（9 时钟 + STOP + 重初始化）;
 *          开漏 + 板级 4.7k 外部上拉;未内建 SMBus PEC、速率自动降级;
 *          长线或强干扰环境降到 100kHz
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
/* 以下均为 7 位地址;手册若给 8 位地址（如 0xD0），右移 1 位后再传 */
#define SYS_I2C_ADDR_24C02      0x50    /* 板载 EEPROM   */
#define SYS_I2C_ADDR_MPU6050    0x68    /* 板载六轴(AD0=0) */
#define SYS_I2C_ADDR_AHT10      0x38    /* 外接 AHT10 温湿度 */
#define SYS_I2C_ADDR_SHT30      0x44    /* 外接 SHT30 温湿度 */
#define SYS_I2C_ADDR_SSD1306    0x3C    /* 外接 OLED 屏(I2C 版) */

/* -------------------- 超时与总线恢复 -------------------- */
/* 等待标志位的最大循环次数，跑满即返回超时错误码 */
#define SYS_I2C_TIMEOUT         200000UL
/* 总线恢复时手动拨时钟的每拍空循环次数 */
#define SYS_I2C_BUS_DELAY       200UL


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */
/* I2C 编号，与内部配置表按序对应 */
typedef enum {
    SYS_I2C_1 = 0,          /* I2C1 → PB8 / PB9   */
    SYS_I2C_2 = 1,          /* I2C2 → PB10 / PB11 */
    SYS_I2C_3 = 2,          /* I2C3 → PA8 / PC9   */
    SYS_I2C_COUNT = 3
} SysI2cId_t;

/* 错误码;函数返回 int，非 0 即失败，用 SYS_I2C_ErrStr 转文字 */
typedef enum {
    SYS_I2C_OK          =  0,   /* 成功 */
    SYS_I2C_ERR_PARAM   = -1,   /* 参数非法，id 越界或空指针 */
    SYS_I2C_ERR_START   = -2,   /* 起始条件失败，总线忙或上拉缺失 */
    SYS_I2C_ERR_ADDR    = -3,   /* 器件地址无应答，见文件头排查 */
    SYS_I2C_ERR_DATA    = -4,   /* 数据阶段无应答，寄存器或写法不对 */
    SYS_I2C_ERR_TIMEOUT = -5,   /* 等标志超时，总线被拉死，试 BusReset */
    SYS_I2C_ERR_BUS     = -6    /* 总线错误 BERR，时序受干扰 */
} SysI2cErr_t;

/* 初始化 I2C：时钟 / 引脚复用开漏 GPIO_OType_OD / 速率
 * 标准库 : RCC_APB1PeriphClockCmd + GPIO_PinAFConfig/GPIO_Init(复用开漏 GPIO_OType_OD)
 *          + I2C_DeInit + I2C_StructInit + I2C_Init + I2C_Cmd
 * 参数 : id — 总线编号;引脚见文件头接线
 *        speed — 速率 Hz，0 = 默认 100000;常用 100000(标准) / 400000(快速) */
void SYS_I2C_Init(SysI2cId_t id, uint32_t speed);

/* ================================================================
 *          区块 2b：多任务共用总线的显式加锁（SYS_I2C_Lock / Unlock）
 * ================================================================
 *  用途 : 公共事务入口内部已自动加锁;仅当连续多次事务不能被别的器件插入
 *         时（如 OLED 整帧刷新）才由调用方在外层调用。
 *  用法 : SYS_I2C_Lock(SYS_I2C_1); ... 多条 SYS_I2C_xxx ... SYS_I2C_Unlock(SYS_I2C_1);
 *  约束 : 锁可重入，同一任务重复 Lock 计数 +1，与内部自动加锁可安全嵌套;
 *         每条总线一把锁，首次使用时创建，不要求先 Init;中断上下文加锁
 *         被跳过，I2C 访问应交给任务;持锁期间不可做长延时（刷一屏 20~25ms）;
 *         bus ≥ SYS_I2C_COUNT 时空操作，建锁失败退化为不加锁
 * ================================================================ */
void SYS_I2C_Lock(SysI2cId_t bus);
void SYS_I2C_Unlock(SysI2cId_t bus);

/* 探测器件是否在线：发一次写地址，收到应答即在线
 * 返回 : SYS_I2C_OK = 在线；SYS_I2C_ERR_ADDR = 无应答
 * 标准库 : 无;START/地址/STOP 时序为寄存器直写，自带超时;
 *          Read/Write 系列同一套时序，见 sys_i2c.c */
int SYS_I2C_IsDeviceReady(SysI2cId_t id, uint8_t addr7);

/* 写一个寄存器（单字节）
 * 时序 : START → 地址+W → 寄存器号 → 数据 → STOP
 * 示例 : SYS_I2C_WriteByte(SYS_I2C_1, SYS_I2C_ADDR_24C02, 0x00, 0x5A); */
int SYS_I2C_WriteByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t data);

/* 读一个寄存器（单字节）
 * 时序 : START → 地址+W → 寄存器号 → 重复START → 地址+R → 读 → NACK → STOP
 * 示例 : uint8_t who;
 *        SYS_I2C_ReadByte(SYS_I2C_1, SYS_I2C_ADDR_MPU6050, 0x75, &who); */
int SYS_I2C_ReadByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t *out);


/* ================================================================
 *                    区块 3：扩展功能
 * ================================================================ */
/* 连写多个寄存器（初始化 MPU6050 等批量写配置表）
 * 时序 : START → 地址+W → 寄存器号 → 数据0 → 数据1 → … → STOP */
int SYS_I2C_WriteBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                       const uint8_t *buf, uint16_t len);

/* 连读多个寄存器（如读 AHT10 的 6 字节测量数据、MPU6050 的 14 字节） */
int SYS_I2C_ReadBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                      uint8_t *buf, uint16_t len);

/* 无寄存器号读：不发寄存器号或命令字节，直接 repeated-start 读 len 个字节
 * 用途 : 写进去的字节本身即命令、没有寄存器号概念的器件，如 BH1750
 * 时序 : START → 地址+R → 读 len 个字节 → STOP
 * 约束 : 调用前器件须已进入可输出数据的状态。BH1750：先 WriteCmd(0x23, 0x01)
 *        上电，再 WriteCmd(0x23, 0x10) 连续 H 分辨率，delay_ms(180) 等转换，
 *        之后本函数读 2 字节，lux = (raw[0]<<8 | raw[1]) / 1.2。
 *        只发命令的写须用 SYS_I2C_WriteCmd();改用 WriteBytes 会把命令当
 *        寄存器号发出，且 NULL/0 参数返回 SYS_I2C_ERR_PARAM */
int SYS_I2C_ReadRaw(SysI2cId_t id, uint8_t addr7, uint8_t *buf, uint16_t len);

/* 只发一个字节：命令或只写寄存器号，后面不带数据
 * 用途 : 写进去的字节本身即命令的器件，如 BH1750（0x01 上电 /
 *        0x10 连续 H 分辨率 / 0x20 单次测量）、CCS811（0xF4）
 * 时序 : START → 地址+W → cmd → 等 BTF → STOP
 * 参数 : cmd — 命令字或寄存器号（语义由器件决定）
 * 返回 : SYS_I2C_OK(0) 或 SYS_I2C_ERR_* 负值，见上方错误码 */
int SYS_I2C_WriteCmd(SysI2cId_t id, uint8_t addr7, uint8_t cmd);

/* 错误码 → 中文解释，排查无应答用 */
const char *SYS_I2C_ErrStr(int err);

/* 总线恢复：SDA/SCL 被从机拉死（读一直超时）时调用
 * 原理 : 关掉 I2C 外设 → 用 GPIO 手动拨 9 个时钟让从机复位
 *         → 补发 STOP → 重新初始化 I2C（沿用上次速率） */
void SYS_I2C_BusReset(SysI2cId_t id);

/* 扫描总线上的所有器件（0x08 ~ 0x77 逐个发地址探测）
 * 返回 : 找到的器件个数;found[] 写入 7 位地址，最多 max 个且不回绕 */
uint8_t SYS_I2C_Scan(SysI2cId_t id, uint8_t *found, uint8_t max);

/* 16 位寄存器地址版连写 / 连读，外部 EEPROM 与大寄存器图器件常用
 * 时序 : START → 地址W → 寄存器号高字节 → 低字节 → 数据…（读:重复START换向）
 * 说明 : 8 位寄存器版见 WriteBytes/ReadBytes，超时与错误码策略相同 */
int SYS_I2C_WriteReg16(SysI2cId_t id, uint8_t addr7, uint16_t reg,
                       const uint8_t *buf, uint16_t len);
int SYS_I2C_ReadReg16 (SysI2cId_t id, uint8_t addr7, uint16_t reg,
                       uint8_t *buf, uint16_t len);


/* ================================================================
 *  附:标准库结构体 I2C_TypeDef 速查（定义在 stm32f4xx.h）
 * ================================================================
 *    CR1  控制 1:PE 使能(I2C_CR1_PE) / START 起始(I2C_CR1_START) /
 *         ACK 应答(I2C_CR1_ACK) / 软复位(I2C_CR1_SWRST)
 *    CR2  控制 2:中断与 DMA，库未用;OAR1/2 自身地址仅从机模式使用
 *    DR   数据:写入 = 发送、读出 = 接收
 *    SR1  状态 1:SB(I2C_SR1_SB) / ADDR(I2C_SR1_ADDR) / BTF(I2C_SR1_BTF);
 *         ADDR NACK 错误码来自 AF 位
 *    SR2  状态 2:BUSY(I2C_SR2_BUSY) / MSL(I2C_SR2_MSL)
 *    CCR  时钟:速率分频，I2C_Init 按 speed 计算
 *    TRISE 上升时间:限制信号上升沿，I2C_Init 配置;FLTR 滤波保持默认
 * ================================================================ */

#endif /* __FWLIB_SYS_I2C_H */
