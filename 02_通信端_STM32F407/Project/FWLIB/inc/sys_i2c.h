#ifndef __FWLIB_SYS_I2C_H
#define __FWLIB_SYS_I2C_H

#include "stm32f4xx.h"

/* sys_i2c.h: 系统 I2C 总线模块头文件
 *
 *  面向寄存器式器件（写器件地址，写寄存器号，读/写数据）的硬件 I2C 封装，
 *  每次调用自带超时和错误码。标准库用 I2C_Init / I2C_Cmd / I2C_DeInit 和
 *  GPIO_SetBits/ResetBits（总线恢复）；读写时序为寄存器直写，标准库 I2C_xxx
 *  的标志等待没有内置超时。
 *
 *  本板接线（普中-天马 F407 开发板原理图，换板子改区块 1）:
 *      I2C1 : SCL = PB8  SDA = PB9，板载 24C02(EEPROM，0x50) / MPU6050(六轴，0x68)，
 *             已有 4.7k 上拉；外部 AHT10 / SHT30 等模块接排针同样可用
 *      I2C2 : SCL = PB10 SDA = PB11 (与 USART3 复用，二选一)
 *      I2C3 : SCL = PA8  SDA = PC9
 *      MPU6050 中断脚 MPU_INT = PC0（需自行用 sys_exti 接管）
 *      引脚冲突: PB10/PB11 = USART3 TX/RX（ESP8266 接口）；PA8 = 板载红外接收头
 *      （ext_io 的 EXT_IR）；PC9 = SDIO_D1（TF 卡数据线，用 TF 卡时不能用 I2C3）。
 *      本板只用 I2C1，外接 I2C 模块也接 I2C1 排针。
 *
 *  模块现状: 全操作受 SYS_I2C_TIMEOUT 封顶，任一步卡死返回错误码；错误码区分
 *  START / 地址 / 数据 / 超时 / 总线，配 ErrStr 定位到具体步骤；总线恢复
 *  SYS_I2C_BusReset（9 时钟 + STOP + 重初始化）用于从机拉死；总线为开漏形态，
 *  依赖板级 4.7k 外部上拉。未内建 SMBus PEC 校验（硬件 ENPEC 支持）和速率自动
 *  降级重试。长线或强干扰环境降到 100kHz，缩短走线，必要时用屏蔽线。
 *
 *  多任务共用一条 I2C 总线:
 *      I2C 事务是 START → 从机地址 → 数据 → STOP 的连续时序，两个任务的事务交错
 *      会破坏总线状态，从机收到错误命令或脏数据，且难以复现，所以锁的粒度是
 *      一次完整事务，不是单字节也不是单个寄存器。
 *      本模块每个公共事务入口（Init / IsDeviceReady / WriteByte / ReadByte /
 *      WriteBytes / ReadBytes / BusReset / Scan / WriteReg16 / ReadReg16）内部
 *      自动加锁，调用方无需改动。需要连续多次事务不被打断时（如 OLED 刷一整屏）
 *      由调用方用 SYS_I2C_Lock / SYS_I2C_Unlock 整段包住。锁是递归的，同一任务
 *      重复 Lock 只计数加一，外层包一段与内层公共函数各自加锁可以嵌套。
 *      每条总线一把锁（I2C1/I2C2/I2C3 互不影响），惰性创建，不要求先 Init 或
 *      调度器已启动；调度器未运行时（上电初始化阶段）加锁为空操作，此时单线程。
 *      中断里不要调用本模块: 中断上下文中加锁会被跳过（用 __get_IPSR() 判断），
 *      中断里直接调用没有互斥，需自行保证不与任务中的事务并发，正确做法是把
 *      I2C 访问交给任务。加锁不关中断，也不形成长临界区。
 *
 *  无应答(ADDR NACK)排查（ErrStr 会提示到哪一步）:
 *      器件地址写错: 数据手册给的 0x68、0x50 等是 7 位地址，直接传；若手册给的是
 *          写地址 0xD0 / 读地址 0xD1（8 位），右移一位成 7 位（0xD0>>1 = 0x68）再传。
 *      总线无上拉: SDA/SCL 必须各有一个 4.7k 左右上拉到 3.3V，本板总线已有上拉。
 *      器件没供电 / 没接好 / 地址脚 A0~A2 接法不同。
 *      总线被从机拉死（SCL 或 SDA 一直低）: 调用 SYS_I2C_BusReset。 */


/* 区块 1：定义与宏定义区（换板子只改这里） */
/* 引脚定义 */
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

/* 常用器件 7 位地址 */
/* 均为 7 位地址；数据手册给 8 位地址(如 0xD0)时右移 1 位后传入 */
#define SYS_I2C_ADDR_24C02      0x50    /* 板载 EEPROM   */
#define SYS_I2C_ADDR_MPU6050    0x68    /* 板载六轴(AD0=0) */
#define SYS_I2C_ADDR_AHT10      0x38    /* 外接 AHT10 温湿度 */
#define SYS_I2C_ADDR_SHT30      0x44    /* 外接 SHT30 温湿度 */
#define SYS_I2C_ADDR_SSD1306    0x3C    /* 外接 OLED 屏(I2C 版) */

/* 超时与总线恢复 */
/* 等待标志位的最大循环次数，跑满返回超时，防止死等 */
#define SYS_I2C_TIMEOUT         2000000UL
/* 总线恢复时手动拨时钟的每拍延时（空循环次数） */
#define SYS_I2C_BUS_DELAY       200UL


/* 区块 2：基础功能 */
/* I2C 编号（与内部配置表一一对应） */
typedef enum {
    SYS_I2C_1 = 0,          /* I2C1 → PB8 / PB9   */
    SYS_I2C_2 = 1,          /* I2C2 → PB10 / PB11 */
    SYS_I2C_3 = 2,          /* I2C3 → PA8 / PC9   */
    SYS_I2C_COUNT = 3
} SysI2cId_t;

/* 错误码：函数返回 int，非 0 即失败，用 SYS_I2C_ErrStr 转文字，错误码区分失败步骤 */
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
 * id: 总线编号 SYS_I2C_1 / SYS_I2C_2 / SYS_I2C_3（引脚见文件头接线）
 * speed: 速率 Hz，0 = 默认 100000；常用 100000(标准) / 400000(快速)
 * 标准库: RCC_APB1PeriphClockCmd + GPIO_PinAFConfig/GPIO_Init + I2C_Init/I2C_Cmd */
void SYS_I2C_Init(SysI2cId_t id, uint32_t speed);

/* 区块 2b：多任务共用总线的显式加锁（SYS_I2C_Lock / Unlock）
 * 公共事务入口内部已自动加锁，一般调用用不到；仅用于需要连续多次事务、中间不能
 * 被别的器件插入的场合（OLED 刷一整屏必须整帧写完）。bus 非法（≥ SYS_I2C_COUNT）
 * 时为空操作；Lock 因堆不足建锁失败时退化为不加锁。返回无，语义见文件头多任务一节。 */
void SYS_I2C_Lock(SysI2cId_t bus);
void SYS_I2C_Unlock(SysI2cId_t bus);

/* 探测器件是否在线：发一次写地址，收到应答即在线
 * 返回 SYS_I2C_OK 在线，SYS_I2C_ERR_ADDR 无应答（不在线）
 * START/地址/STOP 时序由寄存器直写实现，自带超时；下面 Read/Write 系列同一套时序 */
int SYS_I2C_IsDeviceReady(SysI2cId_t id, uint8_t addr7);

/* 写一个寄存器（单字节）
 * 时序: START → 地址+W → 寄存器号 → 数据 → STOP */
int SYS_I2C_WriteByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t data);

/* 读一个寄存器（单字节）
 * 时序: START → 地址+W → 寄存器号 → 重复START → 地址+R → 读 → NACK → STOP
 * 寄存器地址自增的批量读用 ReadBytes；非寄存器式器件参照 .c 的 start/addr/write_raw 组合 */
int SYS_I2C_ReadByte(SysI2cId_t id, uint8_t addr7, uint8_t reg, uint8_t *out);


/* 区块 3：扩展功能 */
/* 连写多个寄存器（如初始化 MPU6050 时批量写配置表）
 * 时序: START → 地址+W → 寄存器号 → 数据0 → 数据1 → … → STOP */
int SYS_I2C_WriteBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                       const uint8_t *buf, uint16_t len);

/* 连读多个寄存器（如读 AHT10 的 6 字节测量数据、MPU6050 的 14 字节） */
int SYS_I2C_ReadBytes(SysI2cId_t id, uint8_t addr7, uint8_t reg,
                      uint8_t *buf, uint16_t len);

/* 无寄存器号读：不发寄存器/命令字节，直接 repeated-start 读 len 个字节
 * 用于"写进去的字节即命令本身、没有寄存器号"的器件，典型是 BH1750
 * 时序: START → 地址+R → 读 len 个字节 → STOP（比 ReadBytes 少地址+W 和寄存器号）
 * 约束: 调用前器件必须已能输出数据，BH1750 先发 Power On(0x01) 和连续 H
 *       分辨率(0x10)，等一次转换后读 2 字节；光照 lux = (raw[0]<<8 | raw[1]) / 1.2
 * 用 ReadBytes 会把一个字节当寄存器号发出，这对这类器件是命令（BH1750 的 0x00
 * 是 Power Down）；只发命令用 WriteCmd()，WriteBytes(..., NULL, 0) 返回参数错误 */
int SYS_I2C_ReadRaw(SysI2cId_t id, uint8_t addr7, uint8_t *buf, uint16_t len);

/* 只发一个字节：命令，或只写寄存器号，后面不带数据
 * 用于写进去的字节本身即命令的器件: BH1750（0x01 上电 / 0x10 连续 H 分辨率 /
 * 0x20 单次测量）、CCS811（APP_START = 0xF4）；也用于只置寄存器指针
 * 时序: START → 地址+W → cmd → 等 BTF → STOP（WriteByte 发寄存器号加数据两字节）
 * cmd: 命令字或寄存器号，语义由器件决定；返回 SYS_I2C_OK(0) 或 SYS_I2C_ERR_* 负值 */
int SYS_I2C_WriteCmd(SysI2cId_t id, uint8_t addr7, uint8_t cmd);

/* 错误码转中文解释，排查无应答时用 */
const char *SYS_I2C_ErrStr(int err);

/* 总线恢复：SDA/SCL 被从机拉死（读一直超时）时调用
 * 关掉 I2C 外设，用 GPIO 手动拨 9 个时钟让从机复位，补发 STOP，再按上次速率
 * 重新初始化 I2C */
void SYS_I2C_BusReset(SysI2cId_t id);

/* 扫描总线上的所有器件（0x08 ~ 0x77 逐个发地址探测）
 * 返回找到的器件个数，found[] 写入 7 位地址，最多 max 个；found 可传 0 只要计数 */
uint8_t SYS_I2C_Scan(SysI2cId_t id, uint8_t *found, uint8_t max);

/* 16 位寄存器地址版连写 / 连读（外部 EEPROM 等常用）
 * 时序: START → 地址W → 寄存器号高字节 → 低字节 → 数据…（读时重复 START 换向）
 * 8 位寄存器版见 WriteBytes/ReadBytes，超时与错误码策略相同 */
int SYS_I2C_WriteReg16(SysI2cId_t id, uint8_t addr7, uint16_t reg,
                       const uint8_t *buf, uint16_t len);
int SYS_I2C_ReadReg16 (SysI2cId_t id, uint8_t addr7, uint16_t reg,
                       uint8_t *buf, uint16_t len);


/* 附: I2C_TypeDef 成员（定义在 stm32f4xx.h）
 *   CR1     控制 1: I2C_CR1_PE 使能 / _START 起始 / _ACK 应答 / _SWRST 软复位
 *   CR2     控制 2: 中断/DMA 相关，库未用
 *   OAR1/2  自身地址: 主模式不用
 *   DR      数据: 写入即发送，读出即接收
 *   SR1     状态 1: I2C_SR1_SB 起始已发出 / _ADDR 地址已应答 / _AF 无应答 /
 *           _BTF 字节传输完成；等标志加超时的循环轮询它（ADDR NACK 来自 _AF）
 *   SR2     状态 2: I2C_SR2_BUSY 总线忙 / _MSL 主模式，发 START 前等它
 *   CCR     时钟: 速率分频，I2C_Init 按 speed 算好写入
 *   TRISE   上升时间: 限制信号上升沿，I2C_Init 配置
 *   FLTR    滤波: 库保持默认 */

#endif /* __FWLIB_SYS_I2C_H */
