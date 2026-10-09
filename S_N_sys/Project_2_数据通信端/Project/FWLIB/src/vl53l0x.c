#include "vl53l0x.h"
#include "gpio_core.h"      /* 引脚/位 */
#include "delay.h"          /* DWT_GetUs / DWT_ElapsedUs —— 超时计时，免初始化 */

/* ================================================================
 *  vl53l0x.c —— VL53L0X 激光测距实现（ToF / SPAD 阵列）
 * ================================================================
 *  硬件映射表
 *  ---------------------------------------------------------------
 *   功能        资源            板上位置              改这里
 *  ---------------------------------------------------------------
 *   I2C 总线    I2C1            PB8=SCL / PB9=SDA     区块1 VL53L0X_I2C_ID
 *   器件地址    0x29（固定）     ——                    区块1 VL53L0X_I2C_ADDR
 *   计时机        DWT CYCCNT      内核自带              gpio_core.h
 *  ---------------------------------------------------------------
 *  ⚠ 板载 I2C1 上已挂 24C02(0x50) 与 MPU6050(0x68)，0x29 不冲突 ✓
 *
 *  【寄存器地址是 8 位，不是 16 位】
 *  VL53L0X 的 I2C 时序就是老式"写 1 字节寄存器号，再读/写数据"，
 *  所以 sys_i2c 现成的 WriteByte / ReadBytes 直接能用，
 *  不需要为它改总线层。16 位/32 位"寄存器"只是**连续写多个数据字节**：
 *      writeReg16Bit(reg, v)  ==  往 reg 写 {v>>8, v&0xFF}
 *      writeReg32Bit(reg, v)  ==  往 reg 写 {v>>24, v>>16, v>>8, v}
 *
 *  【片内还有"寄存器页"这回事】
 *  0xFF 本身是个寄存器，写不同的值会切换到不同的页；
 *  同一个寄存器号（如 0x30）在不同页下含义完全不同。
 *  看 ST 的调参表会觉得"怎么老在写 0xFF" —— 那是在翻页，
 *  **顺序绝对不能重排**，本文件照抄官方顺序，不要"优化"。
 *
 *  【两条最容易漏的规矩】
 *  ① stop_variable：初始化时从 0x91 读出来的一个魔数，
 *     **每次启动测量前必须写回 0x91**，漏了就是读回 8190/8191 不读数。
 *  ② 每次读完必须写 SYSTEM_INTERRUPT_CLEAR(0x0B)=0x01 清中断，
 *     不然下一次一上来就"结果就绪"，你会一直读到同一条旧数据。
 *
 *  【代码出处】
 *  寄存器表与初始化流程改写自 ST 官方 API（STSW-IMG005）与
 *  Pololu vl53l0x-arduino 的移植，寄存器名与顺序保持一致，
 *  方便对着 UM2039 手册和官方 API 源码逐条核对。
 * ================================================================ */

/* ================================================================
 *                      编译期护栏（配错立刻报错）
 * ================================================================ */
typedef char vl53l0x_addr_check[((VL53L0X_I2C_ADDR >= 0x08U) && (VL53L0X_I2C_ADDR <= 0x77U)) ? 1 : -1];
typedef char vl53l0x_to_check[(VL53L0X_TIMEOUT_MS >= 20U) ? 1 : -1];
typedef char vl53l0x_budget_check[(VL53L0X_BUDGET_DEFAULT_US >= 20000UL) ? 1 : -1];


/* ================================================================
 *    区块 1（续）：寄存器地址表 —— 名字与官方 vl53l0x_device.h 一致
 * ================================================================ */
#define VL_SYSRANGE_START                       0x00U
#define VL_SYSTEM_SEQUENCE_CONFIG               0x01U
#define VL_SYSTEM_INTERMEASUREMENT_PERIOD       0x04U
#define VL_SYSTEM_INTERRUPT_CONFIG_GPIO         0x0AU
#define VL_SYSTEM_INTERRUPT_CLEAR               0x0BU
#define VL_RESULT_INTERRUPT_STATUS              0x13U
#define VL_RESULT_RANGE_STATUS                  0x14U
#define VL_MSRC_CONFIG_CONTROL                  0x60U
#define VL_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT  0x44U
#define VL_SYSTEM_HISTOGRAM_BIN                 0x81U
#define VL_GPIO_HV_MUX_ACTIVE_HIGH              0x84U
#define VL_I2C_SLAVE_DEVICE_ADDRESS             0x8AU
#define VL_MSRC_CONFIG_TIMEOUT_MACROP           0x46U
#define VL_PRE_RANGE_CONFIG_VCSEL_PERIOD        0x50U
#define VL_PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI   0x51U
#define VL_PRE_RANGE_CONFIG_VALID_PHASE_LOW     0x56U
#define VL_PRE_RANGE_CONFIG_VALID_PHASE_HIGH    0x57U
#define VL_FINAL_RANGE_CONFIG_VCSEL_PERIOD      0x70U
#define VL_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI 0x71U
#define VL_FINAL_RANGE_CONFIG_VALID_PHASE_LOW   0x47U
#define VL_FINAL_RANGE_CONFIG_VALID_PHASE_HIGH  0x48U
#define VL_GLOBAL_CONFIG_VCSEL_WIDTH            0x32U
#define VL_GLOBAL_CONFIG_SPAD_ENABLES_REF_0     0xB0U
#define VL_GLOBAL_CONFIG_REF_EN_START_SELECT    0xB6U
#define VL_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD  0x4EU
#define VL_DYNAMIC_SPAD_REF_EN_START_OFFSET     0x4FU
#define VL_ALGO_PHASECAL_LIM                    0x30U
#define VL_ALGO_PHASECAL_CONFIG_TIMEOUT         0x30U
#define VL_IDENTIFICATION_MODEL_ID              0xC0U
#define VL_OSC_CALIBRATE_VAL                    0xF8U
#define VL_VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV    0x89U

/* 调参表里有两个 0x30 — 它们在不同"页"下含义不同，别合并 */
#define VL_POWER_MANAGEMENT_GO1_POWER_FORCE     0x80U


/* ================================================================
 *                          模块内部状态
 * ================================================================ */
static uint8_t  s_addr      = VL53L0X_I2C_ADDR;     /* 当前 7 位地址 */
static uint8_t  s_inited    = 0U;                   /* 1 = 已经 Init 过 */
static uint8_t  s_stop_var  = 0x00U;                /* 0x91 里读出来的魔数 */
static uint16_t s_timeout_ms = VL53L0X_TIMEOUT_MS;
static uint32_t s_t0        = 0UL;                  /* 超时起点（DWT 微秒） */
static uint8_t  s_did_timeout = 0U;
static uint32_t s_budget_us = VL53L0X_BUDGET_DEFAULT_US;

#define VL_START_TIMEOUT()   (s_t0 = DWT_GetUs())
#define VL_CHECK_TIMEOUT()   ((s_timeout_ms != 0U) && \
                              (DWT_ElapsedUs(s_t0) > ((uint32_t)s_timeout_ms * 1000UL)))

/* 序列配置位（SYSTEM_SEQUENCE_CONFIG 的每一位 = 一个测量子步骤开不开） */
typedef struct {
    uint8_t tcc;            /* Target Centre Check 目标中心检查 */
    uint8_t dss;            /* Dynamic Spad Selection 动态 SPAD 选择 */
    uint8_t msrc;           /* Minimum Signal Rate Check 最小信号率检查 */
    uint8_t pre_range;      /* 预量程段 */
    uint8_t final_range;    /* 最终量程段 */
} VlSeqEnable_t;

typedef struct {
    uint16_t pre_range_vcsel_period_pclks;
    uint16_t final_range_vcsel_period_pclks;
    uint16_t msrc_dss_tcc_mclks;
    uint16_t pre_range_mclks;
    uint16_t final_range_mclks;
    uint32_t msrc_dss_tcc_us;
    uint32_t pre_range_us;
    uint32_t final_range_us;
} VlSeqTimeout_t;

/* 调参表一行：寄存器号 + 值 */
typedef struct {
    uint8_t reg;
    uint8_t val;
} VlRegVal_t;


/* ================================================================
 *                      区块 2（续）：I2C 收发包装
 * ================================================================ */

static void vl_write(uint8_t reg, uint8_t val)
{
    (void)SYS_I2C_WriteByte(VL53L0X_I2C_ID, s_addr, reg, val);
}

static uint8_t vl_read(uint8_t reg)
{
    uint8_t v = 0xFFU;
    (void)SYS_I2C_ReadByte(VL53L0X_I2C_ID, s_addr, reg, &v);
    return v;
}

static void vl_write16(uint8_t reg, uint16_t val)
{
    uint8_t b[2];
    b[0] = (uint8_t)(val >> 8);
    b[1] = (uint8_t)(val & 0x00FFU);
    (void)SYS_I2C_WriteBytes(VL53L0X_I2C_ID, s_addr, reg, b, 2U);
}

static uint16_t vl_read16(uint8_t reg)
{
    uint8_t b[2];
    b[0] = 0xFFU;
    b[1] = 0xFFU;
    (void)SYS_I2C_ReadBytes(VL53L0X_I2C_ID, s_addr, reg, b, 2U);
    return (uint16_t)(((uint16_t)b[0] << 8) | (uint16_t)b[1]);
}

static void vl_write32(uint8_t reg, uint32_t val)
{
    uint8_t b[4];
    b[0] = (uint8_t)(val >> 24);
    b[1] = (uint8_t)((val >> 16) & 0xFFUL);
    b[2] = (uint8_t)((val >> 8) & 0xFFUL);
    b[3] = (uint8_t)(val & 0xFFUL);
    (void)SYS_I2C_WriteBytes(VL53L0X_I2C_ID, s_addr, reg, b, 4U);
}

static void vl_write_multi(uint8_t reg, const uint8_t *src, uint8_t n)
{
    (void)SYS_I2C_WriteBytes(VL53L0X_I2C_ID, s_addr, reg, src, (uint16_t)n);
}

static void vl_read_multi(uint8_t reg, uint8_t *dst, uint8_t n)
{
    (void)SYS_I2C_ReadBytes(VL53L0X_I2C_ID, s_addr, reg, dst, (uint16_t)n);
}


/* ================================================================
 *                      区块 2（续）：时间换算
 * ================================================================ */

/* 宏周期（ns）= 2304 × VCSEL周期 × 1655ps ÷ 1000，照官方公式 */
#define VL_MACRO_PERIOD_NS(pclks) \
    ((((uint32_t)2304UL * (uint32_t)(pclks) * 1655UL) + 500UL) / 1000UL)

/* VCSEL 周期：寄存器值 ↔ PCLK 数 */
#define VL_DECODE_VCSEL(reg)     (((reg) + 1U) << 1)
#define VL_ENCODE_VCSEL(pclks)   ((((pclks) >> 1) - 1U))

/* 超时值寄存器格式是 "(低字节 × 2^高字节) + 1"，不是普通整数 */
static uint16_t vl_decode_timeout(uint16_t reg_val)
{
    return (uint16_t)((((uint32_t)(reg_val & 0x00FFU))
                       << ((uint32_t)((reg_val & 0xFF00U) >> 8))) + 1UL);
}

static uint16_t vl_encode_timeout(uint32_t timeout_mclks)
{
    uint32_t ls_byte = 0UL;
    uint16_t ms_byte = 0U;

    if (timeout_mclks > 0UL) {
        ls_byte = timeout_mclks - 1UL;
        while ((ls_byte & 0xFFFFFF00UL) > 0UL) {
            ls_byte >>= 1;
            ms_byte++;
        }
        return (uint16_t)(((uint16_t)ms_byte << 8) | (uint16_t)(ls_byte & 0xFFUL));
    }
    return 0U;
}

static uint32_t vl_mclks_to_us(uint16_t mclks, uint8_t vcsel_pclks)
{
    uint32_t macro_ns = VL_MACRO_PERIOD_NS(vcsel_pclks);
    return (uint32_t)((((uint32_t)mclks * macro_ns) + 500UL) / 1000UL);
}

static uint32_t vl_us_to_mclks(uint32_t us, uint8_t vcsel_pclks)
{
    uint32_t macro_ns = VL_MACRO_PERIOD_NS(vcsel_pclks);
    if (macro_ns == 0UL) return 0UL;
    return (uint32_t)((((uint32_t)us * 1000UL) + (macro_ns / 2UL)) / macro_ns);
}


/* ================================================================
 *                      区块 2（续）：内部原语
 * ================================================================ */
static uint8_t  vl_get_vcsel_pulse_period(uint8_t type);    /* 前置声明 */
static uint8_t  vl_get_spad_info(uint8_t *count, uint8_t *type_is_aperture);
static uint8_t  vl_perform_single_ref_cal(uint8_t vhv_init_byte);
static void     vl_get_seq_enables(VlSeqEnable_t *e);
static void     vl_get_seq_timeouts(const VlSeqEnable_t *e, VlSeqTimeout_t *t);

static void vl_get_seq_enables(VlSeqEnable_t *e)
{
    uint8_t c = vl_read(VL_SYSTEM_SEQUENCE_CONFIG);

    e->tcc         = (uint8_t)((c >> 4) & 0x01U);
    e->dss         = (uint8_t)((c >> 3) & 0x01U);
    e->msrc        = (uint8_t)((c >> 2) & 0x01U);
    e->pre_range   = (uint8_t)((c >> 6) & 0x01U);
    e->final_range = (uint8_t)((c >> 7) & 0x01U);
}

static void vl_get_seq_timeouts(const VlSeqEnable_t *e, VlSeqTimeout_t *t)
{
    t->pre_range_vcsel_period_pclks = vl_get_vcsel_pulse_period(VL53L0X_VCSEL_PRE);

    t->msrc_dss_tcc_mclks = (uint16_t)((uint16_t)vl_read(VL_MSRC_CONFIG_TIMEOUT_MACROP) + 1U);
    t->msrc_dss_tcc_us    = vl_mclks_to_us(t->msrc_dss_tcc_mclks,
                                           (uint8_t)t->pre_range_vcsel_period_pclks);

    t->pre_range_mclks = vl_decode_timeout(vl_read16(VL_PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI));
    t->pre_range_us    = vl_mclks_to_us(t->pre_range_mclks,
                                        (uint8_t)t->pre_range_vcsel_period_pclks);

    t->final_range_vcsel_period_pclks = vl_get_vcsel_pulse_period(VL53L0X_VCSEL_FINAL);
    t->final_range_mclks = vl_decode_timeout(vl_read16(VL_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI));

    if (e->pre_range != 0U) {
        /* 寄存器里存的是"预量程 + 最终量程"的和，要减掉预量程那段 */
        t->final_range_mclks = (uint16_t)(t->final_range_mclks - t->pre_range_mclks);
    }

    t->final_range_us = vl_mclks_to_us(t->final_range_mclks,
                                       (uint8_t)t->final_range_vcsel_period_pclks);
}

/* 取参考 SPAD 的个数和类型（做成"动态选择"用） */
static uint8_t vl_get_spad_info(uint8_t *count, uint8_t *type_is_aperture)
{
    uint8_t tmp;

    vl_write(0x80, 0x01);
    vl_write(0xFF, 0x01);
    vl_write(0x00, 0x00);

    vl_write(0xFF, 0x06);
    vl_write(0x83, (uint8_t)(vl_read(0x83) | 0x04U));
    vl_write(0xFF, 0x07);
    vl_write(0x81, 0x01);

    vl_write(0x80, 0x01);
    vl_write(0x94, 0x6B);
    vl_write(0x83, 0x00);

    VL_START_TIMEOUT();
    while (vl_read(0x83) == 0x00U) {
        if (VL_CHECK_TIMEOUT()) {
            s_did_timeout = 1U;
            return VL53L0X_ERR_SPAD;
        }
    }
    vl_write(0x83, 0x01);
    tmp = vl_read(0x92);

    *count            = (uint8_t)(tmp & 0x7FU);
    *type_is_aperture = (uint8_t)((tmp >> 7) & 0x01U);

    vl_write(0x81, 0x00);
    vl_write(0xFF, 0x06);
    vl_write(0x83, (uint8_t)(vl_read(0x83) & 0xFBU));   /* 清 bit2 */
    vl_write(0xFF, 0x01);
    vl_write(0x00, 0x01);

    vl_write(0xFF, 0x00);
    vl_write(0x80, 0x00);

    return VL53L0X_OK;
}

/* 单次参考校准：vhv_init_byte 传 0x40 = VHV 校准，传 0x00 = 相位校准 */
static uint8_t vl_perform_single_ref_cal(uint8_t vhv_init_byte)
{
    vl_write(VL_SYSRANGE_START, (uint8_t)(0x01U | vhv_init_byte));

    VL_START_TIMEOUT();
    while ((vl_read(VL_RESULT_INTERRUPT_STATUS) & 0x07U) == 0U) {
        if (VL_CHECK_TIMEOUT()) {
            s_did_timeout = 1U;
            return VL53L0X_ERR_CALIB;
        }
    }

    vl_write(VL_SYSTEM_INTERRUPT_CLEAR, 0x01);
    vl_write(VL_SYSRANGE_START, 0x00);

    return VL53L0X_OK;
}


/* ================================================================
 *                    区块 2：基础功能
 * ================================================================ */

uint8_t VL53L0X_GetModelID(void)
{
    return vl_read(VL_IDENTIFICATION_MODEL_ID);
}

uint8_t VL53L0X_IsPresent(void)
{
    return (VL53L0X_GetModelID() == (uint8_t)VL53L0X_MODEL_ID) ? 1U : 0U;
}

void VL53L0X_SetTimeout(uint16_t ms)
{
    s_timeout_ms = ms;
}

uint8_t VL53L0X_TimeoutOccurred(void)
{
    uint8_t t = s_did_timeout;
    s_did_timeout = 0U;
    return t;
}

uint8_t VL53L0X_Init(void)
{
    uint8_t  io_2v8 = 1U;               /* 默认按 2V8 供电模式配（绝大多数模块） */
    uint8_t  spad_count = 0U;
    uint8_t  spad_is_aperture = 0U;
    uint8_t  ref_spad_map[6];
    uint8_t  i;
    uint16_t k;
    uint8_t  r;

    s_inited   = 0U;
    s_addr     = VL53L0X_I2C_ADDR;
    s_timeout_ms = VL53L0X_TIMEOUT_MS;

    /* ---------- ① 先认人：不是 0xEE 就别往下跑了 ---------- */
    if (VL53L0X_GetModelID() != (uint8_t)VL53L0X_MODEL_ID) {
        return VL53L0X_ERR_NO_DEVICE;
    }

    /* ---------- ② DataInit ---------- */
    if (io_2v8 != 0U) {
        vl_write(VL_VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV,
                 (uint8_t)(vl_read(VL_VHV_CONFIG_PAD_SCL_SDA__EXTSUP_HV) | 0x01U));
    }

    vl_write(0x88, 0x00);               /* I2C 标准模式 */

    vl_write(0x80, 0x01);
    vl_write(0xFF, 0x01);
    vl_write(0x00, 0x00);
    s_stop_var = vl_read(0x91);         /* ★ 魔数，以后每次测量前要写回 */
    vl_write(0x00, 0x01);
    vl_write(0xFF, 0x00);
    vl_write(0x80, 0x00);

    /* 放开 MSRC(bit1) 和 预量程信号率(bit4) 两道限幅，不然远处一律报"信号弱" */
    vl_write(VL_MSRC_CONFIG_CONTROL,
             (uint8_t)(vl_read(VL_MSRC_CONFIG_CONTROL) | 0x12U));

    /* 回波信号率门限 0.25 MCPS，Q9.7 定点（×128） */
    vl_write16(VL_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT, (uint16_t)(0.25f * 128.0f));

    vl_write(VL_SYSTEM_SEQUENCE_CONFIG, 0xFF);

    /* ---------- ③ StaticInit ---------- */
    r = vl_get_spad_info(&spad_count, &spad_is_aperture);
    if (r != VL53L0X_OK) return r;

    vl_read_multi(VL_GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6U);

    /* -- 配参考 SPAD -- */
    vl_write(0xFF, 0x01);
    vl_write(VL_DYNAMIC_SPAD_REF_EN_START_OFFSET, 0x00);
    vl_write(VL_DYNAMIC_SPAD_NUM_REQUESTED_REF_SPAD, 0x2C);
    vl_write(0xFF, 0x00);
    vl_write(VL_GLOBAL_CONFIG_REF_EN_START_SELECT, 0xB4);

    {
        uint8_t first_spad = (spad_is_aperture != 0U) ? 12U : 0U;   /* 12 = 第一个孔径 SPAD */
        uint8_t enabled = 0U;

        for (i = 0U; i < 48U; i++) {
            if ((i < first_spad) || (enabled == spad_count)) {
                ref_spad_map[i / 8U] = (uint8_t)(ref_spad_map[i / 8U] & (~(1U << (i % 8U))));
            } else if (((ref_spad_map[i / 8U] >> (i % 8U)) & 0x01U) != 0U) {
                enabled++;
            }
        }
    }
    vl_write_multi(VL_GLOBAL_CONFIG_SPAD_ENABLES_REF_0, ref_spad_map, 6U);

    /* -- 灌 ST 官方调参表（顺序不能动！0xFF 是在翻页） -- */
    {
        static const VlRegVal_t tuning[] = {
            {0xFF, 0x01}, {0x00, 0x00}, {0xFF, 0x00}, {0x09, 0x00},
            {0x10, 0x00}, {0x11, 0x00}, {0x24, 0x01}, {0x25, 0xFF},
            {0x75, 0x00}, {0xFF, 0x01}, {0x4E, 0x2C}, {0x48, 0x00},
            {0x30, 0x20}, {0xFF, 0x00}, {0x30, 0x09}, {0x54, 0x00},
            {0x31, 0x04}, {0x32, 0x03}, {0x40, 0x83}, {0x46, 0x25},
            {0x60, 0x00}, {0x27, 0x00}, {0x50, 0x06}, {0x51, 0x00},
            {0x52, 0x96}, {0x56, 0x08}, {0x57, 0x30}, {0x61, 0x00},
            {0x62, 0x00}, {0x64, 0x00}, {0x65, 0x00}, {0x66, 0xA0},
            {0xFF, 0x01}, {0x22, 0x32}, {0x47, 0x14}, {0x49, 0xFF},
            {0x4A, 0x00}, {0xFF, 0x00}, {0x7A, 0x0A}, {0x7B, 0x00},
            {0x78, 0x21}, {0xFF, 0x01}, {0x23, 0x34}, {0x42, 0x00},
            {0x44, 0xFF}, {0x45, 0x26}, {0x46, 0x05}, {0x40, 0x40},
            {0x0E, 0x06}, {0x20, 0x1A}, {0x43, 0x40}, {0xFF, 0x00},
            {0x34, 0x03}, {0x35, 0x44}, {0xFF, 0x01}, {0x31, 0x04},
            {0x4B, 0x09}, {0x4C, 0x05}, {0x4D, 0x04}, {0xFF, 0x00},
            {0x44, 0x00}, {0x45, 0x20}, {0x47, 0x08}, {0x48, 0x28},
            {0x67, 0x00}, {0x70, 0x04}, {0x71, 0x01}, {0x72, 0xFE},
            {0x76, 0x00}, {0x77, 0x00}, {0xFF, 0x01}, {0x0D, 0x01},
            {0xFF, 0x00}, {0x80, 0x01}, {0x01, 0xF8}, {0xFF, 0x01},
            {0x8E, 0x01}, {0x00, 0x01}, {0xFF, 0x00}, {0x80, 0x00}
        };

        for (k = 0U; k < (uint16_t)(sizeof(tuning) / sizeof(tuning[0])); k++) {
            vl_write(tuning[k].reg, tuning[k].val);
        }
    }

    /* -- 中断改成"测量完成就拉高 GPIO"（本模块用不到 GPIO，但官方流程要设） -- */
    vl_write(VL_SYSTEM_INTERRUPT_CONFIG_GPIO, 0x04);
    vl_write(VL_GPIO_HV_MUX_ACTIVE_HIGH,
             (uint8_t)(vl_read(VL_GPIO_HV_MUX_ACTIVE_HIGH) & 0xEFU));   /* 低有效 */
    vl_write(VL_SYSTEM_INTERRUPT_CLEAR, 0x01);

    s_budget_us = VL53L0X_GetTimingBudget();

    /* 默认不要 MSRC 和 TCC 两段（省时间，精度损失可忽略） */
    vl_write(VL_SYSTEM_SEQUENCE_CONFIG, 0xE8);

    (void)VL53L0X_SetTimingBudget(s_budget_us);

    /* -- 参考校准：先 VHV(0x40) 再相位(0x00) -- */
    vl_write(VL_SYSTEM_SEQUENCE_CONFIG, 0x01);
    r = vl_perform_single_ref_cal(0x40);
    if (r != VL53L0X_OK) return r;

    vl_write(VL_SYSTEM_SEQUENCE_CONFIG, 0x02);
    r = vl_perform_single_ref_cal(0x00);
    if (r != VL53L0X_OK) return r;

    vl_write(VL_SYSTEM_SEQUENCE_CONFIG, 0xE8);

    s_inited = 1U;
    return VL53L0X_OK;
}

/* 启动测量前把 stop_variable 写回去 —— 少了这一步就是一直读 8190 */
static void vl_load_stop_variable(void)
{
    vl_write(0x80, 0x01);
    vl_write(0xFF, 0x01);
    vl_write(0x00, 0x00);
    vl_write(0x91, s_stop_var);
    vl_write(0x00, 0x01);
    vl_write(0xFF, 0x00);
    vl_write(0x80, 0x00);
}

/* 等"结果就绪"标志，然后把状态和距离取回来 */
static uint8_t vl_wait_and_read(uint16_t *mm, uint8_t *status)
{
    uint8_t st;

    VL_START_TIMEOUT();
    while ((vl_read(VL_RESULT_INTERRUPT_STATUS) & 0x07U) == 0U) {
        if (VL_CHECK_TIMEOUT()) {
            s_did_timeout = 1U;
            return VL53L0X_ERR_TIMEOUT;
        }
    }

    /* 状态在 0x14 的 bit6:3；距离在 0x1E（0x14+10），16 位、单位毫米 */
    st  = vl_read(VL_RESULT_RANGE_STATUS);
    *mm = vl_read16((uint8_t)(VL_RESULT_RANGE_STATUS + 10U));

    /* ★ 清中断，否则下一次一进来就"就绪"，读到的还是这条旧数据 */
    vl_write(VL_SYSTEM_INTERRUPT_CLEAR, 0x01);

    if (status != 0) *status = (uint8_t)((st >> 3) & 0x0FU);
    return VL53L0X_OK;
}

uint8_t VL53L0X_ReadMmEx(uint16_t *mm, uint8_t *status)
{
    uint8_t r;

    if (mm == 0) return VL53L0X_ERR_PARAM;
    if (s_inited == 0U) return VL53L0X_ERR_NOT_INIT;

    vl_load_stop_variable();
    vl_write(VL_SYSRANGE_START, 0x01);          /* 单次测量 */

    /* 等启动位自动清零 —— 清了才说明测量真的跑起来了 */
    VL_START_TIMEOUT();
    while ((vl_read(VL_SYSRANGE_START) & 0x01U) != 0U) {
        if (VL_CHECK_TIMEOUT()) {
            s_did_timeout = 1U;
            return VL53L0X_ERR_TIMEOUT;
        }
    }

    r = vl_wait_and_read(mm, status);
    return r;
}

uint8_t VL53L0X_ReadMm(uint16_t *mm)
{
    return VL53L0X_ReadMmEx(mm, 0);
}


/* ================================================================
 *                        区块 3：扩展功能
 * ================================================================ */

uint8_t VL53L0X_StartContinuous(uint32_t period_ms)
{
    if (s_inited == 0U) return VL53L0X_ERR_NOT_INIT;

    vl_load_stop_variable();

    if (period_ms != 0UL) {
        uint16_t osc = vl_read16(VL_OSC_CALIBRATE_VAL);

        /* 片内振荡器不准，官方要求乘这个校准系数换算成"芯片时钟周期" */
        if (osc != 0U) period_ms *= (uint32_t)osc;

        vl_write32(VL_SYSTEM_INTERMEASUREMENT_PERIOD, period_ms);
        vl_write(VL_SYSRANGE_START, 0x04);      /* 定时模式 */
    } else {
        vl_write(VL_SYSRANGE_START, 0x02);      /* 背靠背模式（测完立刻下一次） */
    }
    return VL53L0X_OK;
}

uint8_t VL53L0X_ReadContinuousMm(uint16_t *mm)
{
    if (mm == 0) return VL53L0X_ERR_PARAM;
    if (s_inited == 0U) return VL53L0X_ERR_NOT_INIT;

    return vl_wait_and_read(mm, 0);
}

uint8_t VL53L0X_StopContinuous(void)
{
    if (s_inited == 0U) return VL53L0X_ERR_NOT_INIT;

    vl_write(VL_SYSRANGE_START, 0x01);
    vl_write(0xFF, 0x01);
    vl_write(0x00, 0x00);
    vl_write(0x91, 0x00);
    vl_write(0x00, 0x01);
    vl_write(0xFF, 0x00);

    return VL53L0X_OK;
}

uint8_t VL53L0X_SetAddress(uint8_t new_addr7)
{
    if ((new_addr7 < 0x08U) || (new_addr7 > 0x77U)) return VL53L0X_ERR_PARAM;

    /* 用**当前**地址写"新地址"寄存器，之后就用新地址通信 */
    vl_write(VL_I2C_SLAVE_DEVICE_ADDRESS, (uint8_t)(new_addr7 & 0x7FU));
    s_addr = new_addr7;

    return VL53L0X_OK;
}

uint8_t VL53L0X_SetSignalRateLimit(float mcps)
{
    uint16_t v;

    if (mcps < 0.0f) return VL53L0X_ERR_PARAM;
    if (mcps > 511.99f) return VL53L0X_ERR_PARAM;

    /* Q9.7 定点：9 位整数 + 7 位小数 */
    v = (uint16_t)(mcps * 128.0f);
    vl_write16(VL_FINAL_RANGE_CONFIG_MIN_COUNT_RATE_RTN_LIMIT, v);

    return VL53L0X_OK;
}

uint8_t VL53L0X_SetTimingBudget(uint32_t budget_us)
{
    VlSeqEnable_t  enables;
    VlSeqTimeout_t timeouts;

    /* 官方给的各段固定开销（微秒），照抄，别算 */
    const uint32_t START_OVERHEAD      = 1910UL;
    const uint32_t END_OVERHEAD        = 960UL;
    const uint32_t MSRC_OVERHEAD       = 660UL;
    const uint32_t TCC_OVERHEAD        = 590UL;
    const uint32_t DSS_OVERHEAD        = 690UL;
    const uint32_t PRE_RANGE_OVERHEAD  = 660UL;
    const uint32_t FINAL_RANGE_OVERHEAD = 550UL;

    uint32_t used = START_OVERHEAD + END_OVERHEAD;
    uint32_t final_us;
    uint32_t final_mclks;

    if (budget_us < 20000UL) return VL53L0X_ERR_PARAM;

    vl_get_seq_enables(&enables);
    vl_get_seq_timeouts(&enables, &timeouts);

    if (enables.tcc != 0U) {
        used += (timeouts.msrc_dss_tcc_us + TCC_OVERHEAD);
    }
    if (enables.dss != 0U) {
        used += 2UL * (timeouts.msrc_dss_tcc_us + DSS_OVERHEAD);
    } else if (enables.msrc != 0U) {
        used += (timeouts.msrc_dss_tcc_us + MSRC_OVERHEAD);
    }
    if (enables.pre_range != 0U) {
        used += (timeouts.pre_range_us + PRE_RANGE_OVERHEAD);
    }

    if (enables.final_range != 0U) {
        used += FINAL_RANGE_OVERHEAD;

        /* 预算不够分给最终量程段就报错，不硬塞 */
        if (used > budget_us) return VL53L0X_ERR_PARAM;

        final_us = budget_us - used;

        final_mclks = vl_us_to_mclks(final_us, (uint8_t)timeouts.final_range_vcsel_period_pclks);

        /* 寄存器存的是"预量程 + 最终量程"的和（两段 VCSEL 周期不同，只能用 MClk 相加） */
        if (enables.pre_range != 0U) {
            final_mclks += (uint32_t)timeouts.pre_range_mclks;
        }

        vl_write16(VL_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI,
                   vl_encode_timeout(final_mclks));

        s_budget_us = budget_us;
    }

    return VL53L0X_OK;
}

uint32_t VL53L0X_GetTimingBudget(void)
{
    VlSeqEnable_t  enables;
    VlSeqTimeout_t timeouts;
    uint32_t budget;

    const uint32_t START_OVERHEAD      = 1910UL;
    const uint32_t END_OVERHEAD        = 960UL;
    const uint32_t MSRC_OVERHEAD       = 660UL;
    const uint32_t TCC_OVERHEAD        = 590UL;
    const uint32_t DSS_OVERHEAD        = 690UL;
    const uint32_t PRE_RANGE_OVERHEAD  = 660UL;
    const uint32_t FINAL_RANGE_OVERHEAD = 550UL;

    budget = START_OVERHEAD + END_OVERHEAD;

    vl_get_seq_enables(&enables);
    vl_get_seq_timeouts(&enables, &timeouts);

    if (enables.tcc != 0U) {
        budget += (timeouts.msrc_dss_tcc_us + TCC_OVERHEAD);
    }
    if (enables.dss != 0U) {
        budget += 2UL * (timeouts.msrc_dss_tcc_us + DSS_OVERHEAD);
    } else if (enables.msrc != 0U) {
        budget += (timeouts.msrc_dss_tcc_us + MSRC_OVERHEAD);
    }
    if (enables.pre_range != 0U) {
        budget += (timeouts.pre_range_us + PRE_RANGE_OVERHEAD);
    }
    if (enables.final_range != 0U) {
        budget += (timeouts.final_range_us + FINAL_RANGE_OVERHEAD);
    }

    s_budget_us = budget;
    return budget;
}

static uint8_t vl_get_vcsel_pulse_period(uint8_t type)
{
    if (type == VL53L0X_VCSEL_PRE) {
        return (uint8_t)VL_DECODE_VCSEL(vl_read(VL_PRE_RANGE_CONFIG_VCSEL_PERIOD));
    }
    if (type == VL53L0X_VCSEL_FINAL) {
        return (uint8_t)VL_DECODE_VCSEL(vl_read(VL_FINAL_RANGE_CONFIG_VCSEL_PERIOD));
    }
    return 0xFFU;
}

uint8_t VL53L0X_GetVcselPulsePeriod(uint8_t type)
{
    uint8_t p = vl_get_vcsel_pulse_period(type);
    return (p == 0xFFU) ? 0U : p;
}

uint8_t VL53L0X_SetVcselPulsePeriod(uint8_t type, uint8_t period_pclks)
{
    VlSeqEnable_t  enables;
    VlSeqTimeout_t timeouts;
    uint8_t  reg_val = (uint8_t)VL_ENCODE_VCSEL(period_pclks);

    vl_get_seq_enables(&enables);
    vl_get_seq_timeouts(&enables, &timeouts);

    if (type == VL53L0X_VCSEL_PRE) {
        /* -- 预量程段：12/14/16/18，周期越长量程越大 -- */
        switch (period_pclks) {
        case 12: vl_write(VL_PRE_RANGE_CONFIG_VALID_PHASE_HIGH, 0x18); break;
        case 14: vl_write(VL_PRE_RANGE_CONFIG_VALID_PHASE_HIGH, 0x30); break;
        case 16: vl_write(VL_PRE_RANGE_CONFIG_VALID_PHASE_HIGH, 0x40); break;
        case 18: vl_write(VL_PRE_RANGE_CONFIG_VALID_PHASE_HIGH, 0x50); break;
        default: return VL53L0X_ERR_PARAM;
        }
        vl_write(VL_PRE_RANGE_CONFIG_VALID_PHASE_LOW, 0x08);
        vl_write(VL_PRE_RANGE_CONFIG_VCSEL_PERIOD, reg_val);

        /* 周期变了，之前的超时值是按旧周期算的，必须重算重写 */
        {
            uint16_t new_pre_mclks = (uint16_t)vl_us_to_mclks(timeouts.pre_range_us, period_pclks);
            uint16_t new_msrc_mclks = (uint16_t)vl_us_to_mclks(timeouts.msrc_dss_tcc_us, period_pclks);

            vl_write16(VL_PRE_RANGE_CONFIG_TIMEOUT_MACROP_HI, vl_encode_timeout(new_pre_mclks));
            vl_write(VL_MSRC_CONFIG_TIMEOUT_MACROP,
                     (uint8_t)((new_msrc_mclks > 256U) ? 255U : (new_msrc_mclks - 1U)));
        }
    } else if (type == VL53L0X_VCSEL_FINAL) {
        /* -- 最终量程段：8/10/12/14 -- */
        switch (period_pclks) {
        case 8:
            vl_write(VL_FINAL_RANGE_CONFIG_VALID_PHASE_HIGH, 0x10);
            vl_write(VL_FINAL_RANGE_CONFIG_VALID_PHASE_LOW,  0x08);
            vl_write(VL_GLOBAL_CONFIG_VCSEL_WIDTH,  0x02);
            vl_write(VL_ALGO_PHASECAL_CONFIG_TIMEOUT, 0x0C);
            vl_write(0xFF, 0x01);
            vl_write(VL_ALGO_PHASECAL_LIM, 0x30);
            vl_write(0xFF, 0x00);
            break;
        case 10:
            vl_write(VL_FINAL_RANGE_CONFIG_VALID_PHASE_HIGH, 0x28);
            vl_write(VL_FINAL_RANGE_CONFIG_VALID_PHASE_LOW,  0x08);
            vl_write(VL_GLOBAL_CONFIG_VCSEL_WIDTH,  0x03);
            vl_write(VL_ALGO_PHASECAL_CONFIG_TIMEOUT, 0x09);
            vl_write(0xFF, 0x01);
            vl_write(VL_ALGO_PHASECAL_LIM, 0x20);
            vl_write(0xFF, 0x00);
            break;
        case 12:
            vl_write(VL_FINAL_RANGE_CONFIG_VALID_PHASE_HIGH, 0x38);
            vl_write(VL_FINAL_RANGE_CONFIG_VALID_PHASE_LOW,  0x08);
            vl_write(VL_GLOBAL_CONFIG_VCSEL_WIDTH,  0x03);
            vl_write(VL_ALGO_PHASECAL_CONFIG_TIMEOUT, 0x08);
            vl_write(0xFF, 0x01);
            vl_write(VL_ALGO_PHASECAL_LIM, 0x20);
            vl_write(0xFF, 0x00);
            break;
        case 14:
            vl_write(VL_FINAL_RANGE_CONFIG_VALID_PHASE_HIGH, 0x48);
            vl_write(VL_FINAL_RANGE_CONFIG_VALID_PHASE_LOW,  0x08);
            vl_write(VL_GLOBAL_CONFIG_VCSEL_WIDTH,  0x03);
            vl_write(VL_ALGO_PHASECAL_CONFIG_TIMEOUT, 0x07);
            vl_write(0xFF, 0x01);
            vl_write(VL_ALGO_PHASECAL_LIM, 0x20);
            vl_write(0xFF, 0x00);
            break;
        default:
            return VL53L0X_ERR_PARAM;
        }
        vl_write(VL_FINAL_RANGE_CONFIG_VCSEL_PERIOD, reg_val);

        {
            uint16_t new_fin_mclks = (uint16_t)vl_us_to_mclks(timeouts.final_range_us, period_pclks);

            if (enables.pre_range != 0U) {
                new_fin_mclks = (uint16_t)(new_fin_mclks + timeouts.pre_range_mclks);
            }
            vl_write16(VL_FINAL_RANGE_CONFIG_TIMEOUT_MACROP_HI,
                       vl_encode_timeout(new_fin_mclks));
        }
    } else {
        return VL53L0X_ERR_PARAM;
    }

    /* 改完周期必须把预算重新算一遍，再做一次相位校准，否则读数会飘 */
    (void)VL53L0X_SetTimingBudget(s_budget_us);

    {
        uint8_t seq = vl_read(VL_SYSTEM_SEQUENCE_CONFIG);
        vl_write(VL_SYSTEM_SEQUENCE_CONFIG, 0x02);
        (void)vl_perform_single_ref_cal(0x00);
        vl_write(VL_SYSTEM_SEQUENCE_CONFIG, seq);
    }

    return VL53L0X_OK;
}


/* ================================================================
 *                        错误/状态文字
 * ================================================================ */

const char *VL53L0X_StatusStr(uint8_t status)
{
    switch (status) {
    case 0U:  return "valid range";
    case 1U:  return "sigma too high (noisy)";
    case 2U:  return "signal too weak";
    case 3U:  return "target too close (min range fail)";
    case 4U:  return "phase check failed";
    case 5U:  return "hardware fail";
    case 6U:  return "range valid (no wrap check)";
    case 7U:  return "wrap target fail";
    default:  return "reserved";
    }
}

const char *VL53L0X_ErrStr(uint8_t err)
{
    switch (err) {
    case VL53L0X_OK:             return "OK";
    case VL53L0X_ERR_PARAM:      return "ERR: bad param";
    case VL53L0X_ERR_NO_DEVICE:  return "ERR: no device (model id != 0xEE)";
    case VL53L0X_ERR_I2C:        return "ERR: i2c transfer failed";
    case VL53L0X_ERR_TIMEOUT:    return "ERR: measurement timeout";
    case VL53L0X_ERR_SPAD:       return "ERR: spad info fail";
    case VL53L0X_ERR_CALIB:      return "ERR: ref calibration fail";
    case VL53L0X_ERR_NOT_INIT:   return "ERR: call VL53L0X_Init first";
    default:                     return "ERR: unknown";
    }
}

/* ==================== vl53l0x.c end ==================== */
