#ifndef __FWLIB_VL53L0X_H
#define __FWLIB_VL53L0X_H

#include "stm32f4xx.h"
#include "sys_i2c.h"

/* ================================================================
 *  vl53l0x.h —— 【传感器】VL53L0X 激光测距（ToF 飞行时间，3cm ~ 200cm）
 * ================================================================
 *  设计定位 : 超声波会被"物体的形状/吸音材料"骗，红外三角测距近处不准；
 *             ToF 是**真·测光来回时间**，精度 ±3% 且不挑目标材质，
 *             暗处也能用。光电检测仪、避障小车、液位检测都吃这一套。
 *             面向【智能光电检测仪 / 智能环境监测小车】。
 *  依赖     : sys_i2c.h（寄存器时序）、gpio_core.h（DWT 毫秒计时，免初始化）
 *  标准库关键词 : 无——全走 sys_i2c 的 I2C 一层
 *
 *  【接线（模块 4 针 / 6 针都一样，认名字）】
 *      VIN  —— 3.3V（**3.3V！5V 会烧**，VL53L0X 是 2.8V 器件）
 *      GND  —— GND
 *      SCL  —— PB8（板载 I2C1 的 SCL，排针上就能找到）
 *      SDA  —— PB9（板载 I2C1 的 SDA）
 *      XSHUT —— 大多数模块座子内部已上拉，**不用接**；
 *               只有一条总线上挂两颗才需要用它做分时上电改地址
 *  ⚠ 板载 I2C1 上已经挂了 24C02(0x50) 和 MPU6050(0x68)，
 *    VL53L0X 是 0x29 —— 三者**不冲突**，可以一起挂，板载上拉也够用
 *  ⚠ 模块供电一定要稳：激光管瞬间电流不小，电源纹波大会直接读成 8190mm
 *
 *  【使用方式】
 *      SYS_I2C_Init(SYS_I2C_1, 400000);       // ① 先初始化 I2C 总线
 *
 *      uint8_t err = VL53L0X_Init();          // ② 初始化传感器（约 100ms）
 *      if (err != VL53L0X_OK) { printf("%s\r\n", VL53L0X_ErrStr(err)); }
 *
 *      uint16_t mm;
 *      if (VL53L0X_ReadMm(&mm) == VL53L0X_OK) // ③ 测一次（约 33ms）
 *          printf("[ToF] %u mm\r\n", mm);
 *
 *  【它"说谎"的时候长什么样（先知道，免得上板怀疑人生）】
 *      · 读回接近 **8190 / 8191** —— 没测到（太远 / 全黑 / 目标太斜）
 *      · 读回 **~20mm 且不随距离变** —— 前面有玻璃/亚克力（镜面反光）
 *      · 数值在 800~2000 之间反复横跳 —— 目标在量程边缘，正常
 *      · 大太阳底下量程会明显缩水（环境光把回波淹了）
 *
 *  移植指引 : 换 I2C 口改 VL53L0X_I2C_ID；换引脚改 sys_i2c.h；
 *             换地址（同总线挂两颗）用 VL53L0X_SetAddress()；
 *             要长距模式看 VL53L0X_SetVcselPulsePeriod()。
 * ================================================================ */


/* ================================================================
 *                    区块 1：定义与宏定义区（换场景只改这里）
 * ================================================================ */
#define VL53L0X_I2C_ID          SYS_I2C_1   /* 挂在板载 I2C1（PB8=SCL / PB9=SDA） */
#define VL53L0X_I2C_ADDR        0x29U       /* 7 位地址，**芯片固定**，改不了只能换 */
#define VL53L0X_I2C_SPEED       400000UL    /* VL53L0X 支持 400kHz 快速模式 */

#define VL53L0X_TIMEOUT_MS      100U        /* 等一次测量完成的超时（默认预算 33ms） */

/* 出厂 Model ID（IDENTIFICATION_MODEL_ID = 0xC0），用来判断芯片到底通没通 */
#define VL53L0X_MODEL_ID        0xEEU

/* 量程参考值：默认预算下的典型量程；长距模式另说 */
#define VL53L0X_RANGE_MIN_MM    30U         /* 近界，再近只能用短距模式 */
#define VL53L0X_RANGE_MAX_MM    2000U       /* 远界（白墙、室内正常光） */

/* 测量模式：驱动开机的默认测量预算（微秒）——越大越准越慢，最小 20000 */
#define VL53L0X_BUDGET_DEFAULT_US   33000UL

/* VCSEL 脉冲周期选择（只有要榨量程时才碰） */
#define VL53L0X_VCSEL_PRE           0U      /* 预量程段；合法值 12/14/16/18 */
#define VL53L0X_VCSEL_FINAL         1U      /* 最终量程段；合法值 8/10/12/14 */

/* 返回码（全库统一：0 成功） */
#define VL53L0X_OK              0U      /* 成功 */
#define VL53L0X_ERR_PARAM       1U      /* 空指针 / 参数非法 */
#define VL53L0X_ERR_NO_DEVICE   2U      /* 器件不在线（Model ID 读不到 0xEE） */
#define VL53L0X_ERR_I2C         3U      /* I2C 收发失败（查接线/上拉/供电） */
#define VL53L0X_ERR_TIMEOUT     4U      /* 等测量完成超时 */
#define VL53L0X_ERR_SPAD        5U      /* SPAD 信息读取失败（内部自检不过） */
#define VL53L0X_ERR_CALIB       6U      /* 参考校准失败 */
#define VL53L0X_ERR_NOT_INIT    7U      /* 忘了先 VL53L0X_Init */


/* ================================================================
 *                        区块 2：基础功能
 * ================================================================ */

/* 【初始化】上电调一次，内含 ST 官方那 80 多条调参表 + 参考校准
 * 前提 : **必须先 SYS_I2C_Init(VL53L0X_I2C_ID, VL53L0X_I2C_SPEED)**
 * 返回 : VL53L0X_OK / VL53L0X_ERR_*
 * 耗时 : 约 100ms（其中两次参考校准各占几十 ms），上电时跑没问题
 * 说明 : 这几步顺序不能乱——
 *        ① 读 0xC0 确认是 VL53L0X（不是 0xEE 直接返回错误，别硬跑）
 *        ② DataInit：切 2V8 供电模式、取 stop_variable、放开信号率限制
 *        ③ StaticInit：读 SPAD 信息 → 配参考 SPAD → 灌调参表 → 配中断
 *        ④ 参考校准：先 VHV 校准(0x40) 再相位校准(0x00)
 *        stop_variable 是 ② 里从 0x91 读出来的一个魔数，
 *        后面每次启动测量都要先写回去 —— 自己写驱动最容易漏这一步。
 * 示例 : if (VL53L0X_Init() != VL53L0X_OK) printf("VL53L0X 没起来\r\n"); */
uint8_t VL53L0X_Init(void);

/* 【测一次距离】★核心；单次模式，测完自动停
 * 参数 : mm —— 出参，单位毫米
 * 返回 : VL53L0X_OK          测到了
 *        VL53L0X_ERR_TIMEOUT 超时（前方无目标 / 太远）
 *        其它                VL53L0X_ERR_*
 * 耗时 : 约 33ms（= 测量预算），阻塞式
 * 示例 : uint16_t d; if (VL53L0X_ReadMm(&d) == VL53L0X_OK) { 用 d } */
uint8_t VL53L0X_ReadMm(uint16_t *mm);

/* 【测一次 + 拿状态码】想知道"为什么没测到"就用这个
 * 参数 : mm     —— 出参距离；status —— 出参状态字节（见 VL53L0X_StatusStr）
 * 返回 : VL53L0X_OK / VL53L0X_ERR_*
 * 示例 : uint16_t d; uint8_t st;
 *        VL53L0X_ReadMmEx(&d, &st);
 *        printf("%u mm (%s)\r\n", d, VL53L0X_StatusStr(st)); */
uint8_t VL53L0X_ReadMmEx(uint16_t *mm, uint8_t *status);

/* 【探测器件在不在】发一次 I2C，回得到 Model ID 就算在
 * 返回 : 1 = 在线；0 = 不在
 * 用途 : 排查第一步——比乱猜接线快得多
 * 示例 : if (!VL53L0X_IsPresent()) printf("先查 SDA/SCL/供电\r\n"); */
uint8_t VL53L0X_IsPresent(void);

/* 【读 Model ID】正常应为 0xEE；读回 0xFF 通常是器件没应答
 * 示例 : printf("ModelID = 0x%02X\r\n", VL53L0X_GetModelID()); */
uint8_t VL53L0X_GetModelID(void);


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

/* 【启动连续测量】之后不用每次下命令，传感器自己一直测
 * 参数 : period_ms —— 0 = 背靠背（测完立刻下一次，最快）
 *                     非 0 = 定时模式，隔 period_ms 测一次
 * 返回 : VL53L0X_OK / VL53L0X_ERR_*
 * 示例 : VL53L0X_StartContinuous(0);
 *        for (;;) { VL53L0X_ReadContinuousMm(&mm); Delay_ms(10); } */
uint8_t VL53L0X_StartContinuous(uint32_t period_ms);

/* 【读连续模式的最新一次结果】不启动测量，只等结果
 * 返回 : VL53L0X_OK / VL53L0X_ERR_*
 * 说明 : 没启动连续模式时调它会超时，属正常
 * 示例 : uint16_t d; VL53L0X_ReadContinuousMm(&d); */
uint8_t VL53L0X_ReadContinuousMm(uint16_t *mm);

/* 【停止连续测量】回单次模式 */
uint8_t VL53L0X_StopContinuous(void);

/* 【改 I2C 地址】同一总线要挂两颗 VL53L0X 时的唯一办法
 * 参数 : new_addr7 —— 新的 7 位地址（**必须 0x08~0x77**，且别撞 24C02/MPU6050）
 * 返回 : VL53L0X_OK / VL53L0X_ERR_PARAM
 * 用法 : 两颗都要接 XSHUT——先都拉低 → 放开第一颗 → Init 后 SetAddress(0x30)
 *        → 放开第二颗（此时它还是 0x29）→ Init 后 SetAddress(0x31)
 * ⚠ 改完地址掉电不保存，每次上电都得重来一遍
 * 示例 : VL53L0X_SetAddress(0x30); */
uint8_t VL53L0X_SetAddress(uint8_t new_addr7);

/* 【设测量预算】= 一次测量最多花多少微秒
 * 参数 : budget_us —— 20000 ~ 200000 之间才有意义（默认 33000）
 * 返回 : VL53L0X_OK / VL53L0X_ERR_PARAM
 * 说明 : 预算翻 N 倍，测距噪声降到 1/√N —— 要更稳就加大，代价是变慢
 * 示例 : VL53L0X_SetTimingBudget(100000);   // 100ms 一次，明显更稳 */
uint8_t VL53L0X_SetTimingBudget(uint32_t budget_us);

/* 【读当前测量预算】单位微秒 */
uint32_t VL53L0X_GetTimingBudget(void);

/* 【设回波信号率门限】单位 MCPS（兆计数/秒），默认 0.25
 * 参数 : mcps —— 0 ~ 511.99
 * 返回 : VL53L0X_OK / VL53L0X_ERR_PARAM
 * 说明 : 门限调**低** → 量程变大，但更容易被旁边墙的反光骗；
 *        门限调**高** → 只认强回波，抗干扰好但量程缩水
 * 示例 : VL53L0X_SetSignalRateLimit(0.10f);   // 要更远 */
uint8_t VL53L0X_SetSignalRateLimit(float mcps);

/* 【改 VCSEL 脉冲周期】榨量程/提精度用，改完会自动重算预算并重做相位校准
 * 参数 : type   —— VL53L0X_VCSEL_PRE（合法 12/14/16/18，默认 14）
 *                  或 VL53L0X_VCSEL_FINAL（合法 8/10/12/14，默认 10）
 *        period_pclks —— 周期值（只能取偶数）
 * 返回 : VL53L0X_OK / VL53L0X_ERR_PARAM
 * 说明 : 周期越**长** → 量程越大、精度略降；越**短** → 越准、量程越小
 * 示例 : // 长距模式：两段都拉到最大
 *        VL53L0X_SetVcselPulsePeriod(VL53L0X_VCSEL_PRE,   18);
 *        VL53L0X_SetVcselPulsePeriod(VL53L0X_VCSEL_FINAL, 14); */
uint8_t VL53L0X_SetVcselPulsePeriod(uint8_t type, uint8_t period_pclks);

/* 【读回当前 VCSEL 周期】
 * 返回 : 周期值（PCLK 数）；参数非法返回 0 */
uint8_t VL53L0X_GetVcselPulsePeriod(uint8_t type);

/* 【改超时】单位毫秒；0 = 不超时（**会死等，别这么干**） */
void VL53L0X_SetTimeout(uint16_t ms);

/* 【上次测量是不是超时了】读一次自动清零（照着 ST 原库的习惯留的） */
uint8_t VL53L0X_TimeoutOccurred(void);

/* 【状态码转中文说明】调试打印用
 * 说明 : 状态码取自 RESULT_RANGE_STATUS(0x14) 的 bit6:3
 * 示例 : printf("[ToF] %s\r\n", VL53L0X_StatusStr(st)); */
const char *VL53L0X_StatusStr(uint8_t status);

/* 【返回码转中文说明】调试打印用
 * 示例 : printf("[ToF] %s\r\n", VL53L0X_ErrStr(VL53L0X_Init())); */
const char *VL53L0X_ErrStr(uint8_t err);

#endif /* __FWLIB_VL53L0X_H */
