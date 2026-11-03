/*====================引用定义区=====================*/
/*板1（STM32F407ZET6）检测端*/

#include "stm32f4xx.h"
#include <stdio.h>
#include <string.h>


#include "gpio_core.h"
#include "led.h"
#include "beep.h"
#include "key.h"
#include "delay.h"


#include "sys_clock.h"
#include "sys_tick.h"
#include "sys_usart.h"
#include "sys_nvic.h"
#include "sys_fault.h"
#include "sys_sht30.h"
#include "sys_oled.h"
#include "sys_i2c.h"
#include "sys_frame.h"
#include "sys_status.h"
#include "sys_wdg.h"


//外设函数
#include "sys_bmp280.h"
#include "sys_bh1750.h"
#include "sys_ccs811.h"
#include "lora_e22.h"


//Free rots
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
/*=================================================*/


/*====================宏定义与全局声明区================*/
/* 调试串口：USART1（PA9/PA10）-- CH340 -- 上位机 */
#define DBG        SYS_USART_1
#define DBG_BAUD   115200U
#define DBG_VERBOSE 0U   /* 1 = print every frame + every sample */


//滤波窗口
#define FILT_N		8

//上限阈值
#define T_LIMIT_X100    2800    // 30.00 ℃
#define T_HYST_X100      100    //回差 1.00 ℃
#define H_LIMIT_X100    8800    // 80.00 %RH
#define H_HYST_X100      500
#define P_LIMIT_X10     9950    // 1020.0 hPa
#define P_HYST_X10        50
#define L_LIMIT_LX      2000    // 800 lx
#define L_HYST_LX         50
#define V_LIMIT_PPB     1000    // 1000 ppb
#define V_HYST_PPB       100
//警告位
#define AL_TEMP   (1U << 0)
#define AL_HUMI   (1U << 1)
#define AL_PRESS  (1U << 2)
#define AL_LIGHT  (1U << 3)
#define AL_TVOC   (1U << 4)
#define AL_MQ135  (1U << 5)

//下限阈值
#define T_LO_X100       1200    //5.00 ℃ 
#define T_LO_HYST_X100   100    
#define H_LO_X100       3500    
#define H_LO_HYST_X100   500
#define P_LO_X10        9700    // 970.0 hPa（重庆基线约 980，低于 970 属强低压）
#define P_LO_HYST_X10     10
#define L_LO_LX           20    // 20 lx（室内全黑）
#define L_LO_HYST_LX       5    

//下限警告位
#define AL2_TEMP   (1U << 0)
#define AL2_HUMI   (1U << 1)
#define AL2_PRESS  (1U << 2)
#define AL2_LIGHT  (1U << 3)



//全局声明
static uint32_t s_dropCnt = 0; 
static uint32_t s_sht30_fail = 0;
volatile uint8_t dbg_ccs_init = 0xFFU;
volatile uint8_t dbg_ccs_hw = 0xFFU;
volatile uint8_t dbg_ccs_rdy = 0xFFU;
volatile uint8_t dbg_ccs_rc = 0xFFU;
volatile uint8_t dbg_ccs_vld = 0xFFU;

static uint32_t s_tx_ok = 0, s_tx_fail = 0;
/*=================================================*/


/*====================结构体定义声明区(包括枚举联合)================*/
typedef struct {
	uint16_t valid;			// bit0 T bit1 H bit2 P bit3 lux bit4 tvoc bit5 mq135
	int16_t temp_x100; 		//温度,0.01℃ 
	int16_t humi_x100; 		//湿度,0.01%RH
	int16_t press_x10; 		//气压, 0.1 hpa 
	uint16_t light_lx;		//光照, lx
	uint16_t tvoc_ppb;		//TVOC, ppd
	uint16_t mq135_raw;		//MQ135 ADC 0 - 4095
	uint32_t tick;
}SensorMsg_t;

typedef struct {
    uint16_t valid;       
    uint8_t  alarm;        
    uint8_t  alarm_lo;
    int16_t  temp_x100;    
    int16_t  humi_x100;
    int16_t  press_x10;
    uint16_t light_lx;
    uint16_t tvoc_ppb;
    uint16_t mq135_raw;
    uint32_t tick;
} ProcResult_t;

typedef struct {
	int32_t buf[FILT_N];      // 环形缓冲设置，FILT_N = 8
    int32_t sum;              //累加和（int32 防溢出）
    uint8_t idx;              //下一个写入位置 
    uint8_t cnt;             
} Filt_t;


/*=================================================*/

/*====================附加函数=====================*/


//I2C恢复程序
static void I2C_BusRecover(void)
{
    GPIO_InitTypeDef gi;
    int i;
    volatile uint32_t k;

    SYS_USART_Printf(DBG, "[R]1\r\n");

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    GPIO_StructInit(&gi);
    gi.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9;
    gi.GPIO_Mode  = GPIO_Mode_OUT;
    gi.GPIO_OType = GPIO_OType_OD;
    gi.GPIO_Speed = GPIO_Speed_50MHz;
    gi.GPIO_PuPd  = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &gi);

    SYS_USART_Printf(DBG, "[R]2\r\n");

    GPIO_SetBits(GPIOB, GPIO_Pin_9);              /* SDA 释放 */

    for (i = 0; i < 9; i++) {
        GPIO_ResetBits(GPIOB, GPIO_Pin_8);
        for (k = 0; k < 500; k++) { }             /* 约 5μs @168MHz，不依赖 DWT */
        GPIO_SetBits(GPIOB, GPIO_Pin_8);
        for (k = 0; k < 500; k++) { }
    }

    SYS_USART_Printf(DBG, "[R]3\r\n");

    /* STOP：SCL 高时 SDA 由低变高 */
    GPIO_ResetBits(GPIOB, GPIO_Pin_9);
    for (k = 0; k < 500; k++) { }
    GPIO_SetBits(GPIOB, GPIO_Pin_8);
    for (k = 0; k < 500; k++) { }
    GPIO_SetBits(GPIOB, GPIO_Pin_9);
    for (k = 0; k < 500; k++) { }

    SYS_USART_Printf(DBG, "[R]4\r\n");
}




//统一集合数据
/* ==================== 传感器健康管理 ====================
 * 目的：单颗芯片坏了不要拖垮整轮采集；总线级失效时自动重新初始化。
 * 规则：连续失败 8 次判离线，离线后每 30 轮重探一次；
 *       整轮全失败连续 3 次 -> 复位总线 + 重新初始化 I2C 外设。 */
typedef struct {
    uint8_t fail;      /* 连续失败次数 */
    uint8_t offline;   /* 1 = 已隔离 */
    uint8_t skip;      /* 离线后的重探计数 */
} SensorHealth_t;

#define SENSOR_OFFLINE_FAILS   8U
#define SENSOR_RETRY_ROUNDS   30U

static SensorHealth_t s_h_sht30, s_h_bmp, s_h_bh, s_h_ccs;

static uint8_t sensor_should_try(SensorHealth_t *h)
{
    if (h->offline == 0U) { return 1U; }
    if (++h->skip >= SENSOR_RETRY_ROUNDS) {
        h->skip = 0U; h->offline = 0U; h->fail = 0U;
        return 1U;
    }
    return 0U;
}

static void sensor_ok(SensorHealth_t *h)
{
    h->fail = 0U; h->offline = 0U; h->skip = 0U;
}

static void sensor_fail(SensorHealth_t *h)
{
    if (h->fail < 255U) { h->fail++; }
    if (h->fail >= SENSOR_OFFLINE_FAILS) { h->offline = 1U; h->skip = 0U; }
}

static void sensor_health_reset_all(void)
{
    memset(&s_h_sht30, 0, sizeof(s_h_sht30));
    memset(&s_h_bmp,   0, sizeof(s_h_bmp));
    memset(&s_h_bh,    0, sizeof(s_h_bh));
    memset(&s_h_ccs,   0, sizeof(s_h_ccs));
}

void CollectAll(SensorMsg_t *m) {
	SYS_BMP280_Data_t d;
	SYS_CCS811_Data_t cs;
	float lux;
	int r;

	memset(m, 0, sizeof(*m));

	//温湿度：SHT30
	if (sensor_should_try(&s_h_sht30) != 0U) {
		r = SYS_SHT30_ReadInt(SYS_I2C_1, &m->temp_x100, &m->humi_x100);
		if (r == SYS_SHT30_OK) {
			m->valid |= (1U << 0);
			m->valid |= (1U << 1);
			sensor_ok(&s_h_sht30);
		} else {
			s_sht30_fail++;
			sensor_fail(&s_h_sht30);
			if ((s_sht30_fail == 1U) || ((s_sht30_fail % 60U) == 0U)) {
				SYS_USART_Printf(DBG, "[SHT30] fail rc=%d 累计 %u\r\n",
				                 r, (unsigned)s_sht30_fail);
			}
		}
	}

	//气压：BMP280
	if (sensor_should_try(&s_h_bmp) != 0U) {
		if (SYS_BMP280_Read(SYS_I2C_1, &d) == 0) {
			m->valid |= (1U << 2);
			m->press_x10 = (int16_t)(d.press_hpa * 10.0f);
			if ((m->valid & (1U << 0)) == 0) {
				m->valid |= (1U << 0);
				m->temp_x100 = (int16_t)(d.temp_c * 100.0f);
			}
			sensor_ok(&s_h_bmp);
		} else {
			sensor_fail(&s_h_bmp);
		}
	}

	//光照：BH1750
	if (sensor_should_try(&s_h_bh) != 0U) {
		r = SYS_BH1750_ReadLux(SYS_I2C_1, &lux);
		if (r == SYS_BH1750_OK) {
			m->valid |= (1U << 3);
			m->light_lx = (uint16_t)(lux + 0.5f);
			sensor_ok(&s_h_bh);
		} else {
			sensor_fail(&s_h_bh);
		}
	}

	//TVOC：CCS811
	if (sensor_should_try(&s_h_ccs) != 0U) {
		dbg_ccs_rdy = SYS_CCS811_DataReady(SYS_I2C_1);
		if (dbg_ccs_rdy != 0U) {
			dbg_ccs_rc = (uint8_t)SYS_CCS811_Read(SYS_I2C_1, &cs);
			dbg_ccs_vld = cs.valid;
			if (dbg_ccs_rc == 0U && cs.valid != 0U) {
				m->valid |= (1U << 4);
				m->tvoc_ppb = cs.tvoc_ppb;
				sensor_ok(&s_h_ccs);
			} else {
				sensor_fail(&s_h_ccs);
			}
		} else {
			sensor_fail(&s_h_ccs);
		}
	}

	/* MQ-135：PF7 被板载光敏占用，本板不接，第 6 字段固定 0 */
	m->tick = SYS_TICK_GetTick();
}


//数据滤波处理
static Filt_t s_f_t, s_f_h, s_f_p, s_f_l, s_f_v;
static uint8_t s_alarm_t = 0U, s_alarm_h = 0U, s_alarm_p = 0U,
               s_alarm_l = 0U, s_alarm_v = 0U;

//压入数据
static int32_t filt_push(Filt_t *f, int32_t v) {
	if(f->cnt < FILT_N) {
		f->cnt++;
	} else {
		f->sum -= f->buf[f->idx];
	}
	f->buf[f->idx] = v;
	f->sum += v;
	f->idx = (uint8_t)((f->idx + 1U) % FILT_N);
	return f->sum / (int32_t)f->cnt;
}

static uint8_t alarm_step(uint8_t cur, int32_t v, int32_t limit, int32_t hyst) 
{
    if (cur == 0U) {
        if (v > limit) return 1U;  //超上限置位           
    } else {
        if (v < (limit - hyst)) return 0U;     
    }
    return cur;                                 
}

static uint8_t s_alarm_t_lo = 0U, s_alarm_h_lo = 0U;
static uint8_t s_alarm_p_lo = 0U, s_alarm_l_lo = 0U;

//下限状态机
static uint8_t alarm_step_lo(uint8_t cur, int32_t v, int32_t limit, int32_t hyst)
{
    if (cur == 0U) {
        if (v < limit) return 1U;               //低于下限才置
    } else {
        if (v > (limit + hyst)) return 0U;      //回升过回差才清 
    }
    return cur;
}




//数据处理
static void process_one(const SensorMsg_t *m, ProcResult_t *r)
{
    uint8_t al = 0U, al_lo = 0U;
    int32_t v;

    r->valid = 0U;

    //温度 
    if ((m->valid & (1U << 0)) != 0U) {
        v = filt_push(&s_f_t, (int32_t)m->temp_x100);
        r->temp_x100 = (int16_t)v;
        r->valid |= (1U << 0);
        s_alarm_t = alarm_step(s_alarm_t, v, T_LIMIT_X100, T_HYST_X100);
        if (s_alarm_t != 0U) { al |= AL_TEMP; }
		
		s_alarm_t_lo = alarm_step_lo(s_alarm_t_lo, v, T_LO_X100, T_LO_HYST_X100);
        if (s_alarm_t_lo != 0U) { al_lo |= AL2_TEMP; }
    }

    //湿度
    if ((m->valid & (1U << 1)) != 0U) {
        v = filt_push(&s_f_h, (int32_t)m->humi_x100);
        r->humi_x100 = (int16_t)v;
        r->valid |= (1U << 1);
        s_alarm_h = alarm_step(s_alarm_h, v, H_LIMIT_X100, H_HYST_X100);
        if (s_alarm_h != 0U) { al |= AL_HUMI; }
		
		s_alarm_h_lo = alarm_step_lo(s_alarm_h_lo, v, H_LO_X100, H_LO_HYST_X100);
        if (s_alarm_h_lo != 0U) { al_lo |= AL2_HUMI; }
    }

    //气压
    if ((m->valid & (1U << 2)) != 0U) {
        v = filt_push(&s_f_p, (int32_t)m->press_x10);
        r->press_x10 = (int16_t)v;
        r->valid |= (1U << 2);
        s_alarm_p = alarm_step(s_alarm_p, v, P_LIMIT_X10, P_HYST_X10);
        if (s_alarm_p != 0U) { al |= AL_PRESS; }

        s_alarm_p_lo = alarm_step_lo(s_alarm_p_lo, v, P_LO_X10, P_LO_HYST_X10);
        if (s_alarm_p_lo != 0U) { al_lo |= AL2_PRESS; }
    }

    //光照
    if ((m->valid & (1U << 3)) != 0U) {
        v = filt_push(&s_f_l, (int32_t)m->light_lx);
        r->light_lx = (uint16_t)v;
        r->valid |= (1U << 3);
        s_alarm_l = alarm_step(s_alarm_l, v, L_LIMIT_LX, L_HYST_LX);
        if (s_alarm_l != 0U) { al |= AL_LIGHT; }

        s_alarm_l_lo = alarm_step_lo(s_alarm_l_lo, v, L_LO_LX, L_LO_HYST_LX);
        if (s_alarm_l_lo != 0U) { al_lo |= AL2_LIGHT; }
    }

    //TVOC
    if ((m->valid & (1U << 4)) != 0U) {
        v = filt_push(&s_f_v, (int32_t)m->tvoc_ppb);
        r->tvoc_ppb = (uint16_t)v;
        r->valid |= (1U << 4);
        s_alarm_v = alarm_step(s_alarm_v, v, V_LIMIT_PPB, V_HYST_PPB);
        if (s_alarm_v != 0U) { al |= AL_TVOC; }
    }



    r->alarm = al;
	r->alarm_lo = al_lo;
}


/*=================================================*/

/*====================任务系统=====================*/
//Free ROTS任务
static QueueHandle_t s_q_sensor;
static QueueHandle_t s_q_result;
static TaskHandle_t s_h_sensor, s_h_process, s_h_comm, s_h_lora, s_h_mon;
static TaskHandle_t s_h_ui;

//下行命令可改的采样周期
static volatile uint16_t s_period_ms = 2000U;
//当前是否有告警（供 UI 与命令查询）
static volatile uint8_t  s_alarm_on  = 0U;
//蜂鸣器静音开关
static volatile uint8_t  s_mute      = 0U;

//UI 显示用的最新一帧数据
static volatile int32_t  g_temp_x100, g_humi_x100, g_press_x10;
static volatile uint16_t g_light_lx, g_tvoc_ppb;
static volatile uint8_t  g_valid, g_alarm;

//超限行闪烁的半周期标志（500ms 翻转）
static volatile uint8_t  s_blink = 1U;

//下行命令处理（实现在文件后部）
static void lora_cmd_handle(uint8_t cmd, const uint8_t *pl, uint16_t len);



//从传感采集数据
void vTaskSensor(void *p) {
	
	//IIC获取外设地址
	uint8_t found[8];
	uint8_t n,i;
	
	SYS_USART_Printf(DBG, "[I2C] before: IDR=0x%04X (PB8=%u PB9=%u)\r\n",
	                 (unsigned)GPIOB->IDR,
	                 (unsigned)((GPIOB->IDR >> 8) & 1U),
	                 (unsigned)((GPIOB->IDR >> 9) & 1U));

	I2C_BusRecover();                  

	SYS_USART_Printf(DBG, "[I2C] after : IDR=0x%04X (PB8=%u PB9=%u)\r\n",
	                 (unsigned)GPIOB->IDR,
	                 (unsigned)((GPIOB->IDR >> 8) & 1U),
	                 (unsigned)((GPIOB->IDR >> 9) & 1U));

	//总线空闲应为高；有低电平说明被某颗从机拉死或上拉丢失
	if ((((GPIOB->IDR >> 8) & 1U) == 0U) || (((GPIOB->IDR >> 9) & 1U) == 0U)) {
		SYS_USART_Printf(DBG, "[I2C] 警告：空闲不是高电平，先查上拉/短路/从机拉死\r\n");
	}
	
	
	
	SYS_I2C_Init(SYS_I2C_1, 50000);   /* 100kHz：内部上拉 + 400k 会间歇 NACK */
	n = SYS_I2C_Scan(SYS_I2C_1, found, 8);
	SYS_USART_Printf(DBG, "I2C1 scan: %u dev\r\n", (unsigned)n);
	
	for(i = 0; i < n; i++) {
		SYS_USART_Printf(SYS_USART_1, "  addr=0x%02X\r\n", (unsigned)found[i]);
	}
	SYS_USART_Printf(DBG, "SHT30 init=%d proto=%u\r\n", SYS_SHT30_Init(SYS_I2C_1), (unsigned)SYS_SHT30_Proto(SYS_I2C_1));

	//BMP280部分
    int id = SYS_BMP280_GetID(SYS_I2C_1);
    SYS_USART_Printf(DBG, "BMP280 ID=0x%02X (want 0x58)\r\n", (unsigned)id);
    SYS_USART_Printf(DBG, "BMP280 init=%u\r\n", (unsigned)SYS_BMP280_Init(SYS_I2C_1));
	
	
	if(SYS_BH1750_Init(SYS_I2C_1) == SYS_BH1750_OK) {
		vTaskDelay(pdMS_TO_TICKS(200));
		SYS_USART_Printf(DBG, "BH1750 init ok\r\n");
	} else {
		SYS_USART_Printf(DBG, "BH1750 init Fail\r\n");
	}

	dbg_ccs_init = (uint8_t)SYS_CCS811_Init(SYS_I2C_1);
	dbg_ccs_hw = (uint8_t)SYS_CCS811_GetHWID(SYS_I2C_1);
	SYS_USART_Printf(DBG, "CCS811 init=%u hw=0x%02X\r\n", (unsigned)dbg_ccs_init, (unsigned)dbg_ccs_hw);

	
	//获取系统时钟节拍
	TickType_t period;

	TickType_t last __attribute__((unused)) = xTaskGetTickCount();
	static uint8_t s_all_fail = 0U;
	SensorMsg_t msg;
	
	
	for(;;) {
		TickType_t t0 = xTaskGetTickCount();

		CollectAll(&msg);

		/* 全部通道失败：先拨 9 个时钟；连续 3 轮还不行就重配 I2C 并解除隔离 */
		if (msg.valid == 0U) {
			(void)SYS_I2C_BusReset(SYS_I2C_1);
			if (++s_all_fail >= 3U) {
				s_all_fail = 0U;
				SYS_I2C_Init(SYS_I2C_1, 50000);
				sensor_health_reset_all();
				SYS_USART_Printf(DBG, "[I2C] 连续全失败，已重新初始化总线\r\n");
			}
		} else {
			s_all_fail = 0U;
		}

		if(xQueueSend(s_q_sensor, &msg, pdMS_TO_TICKS(5)) != pdPASS) {
			s_dropCnt++;
		}

		period = pdMS_TO_TICKS(s_period_ms);
		if (period == 0U) { period = pdMS_TO_TICKS(2000); }

		{
			TickType_t used = xTaskGetTickCount() - t0;
			vTaskDelay((used < period) ? (period - used) : 1U);
		}		
		
	}
}



//从队列中获取数据
void vTaskProcess(void *p) {
	SensorMsg_t m;
	ProcResult_t r;
	(void)p;
	
	
    for (;;) {
		if(xQueueReceive(s_q_sensor, &m, portMAX_DELAY) == pdPASS) {
			 memset(&r, 0, sizeof(r));
			process_one(&m, &r);

			/* 把结果交给 UI 显示 */
			g_temp_x100 = r.temp_x100;  g_humi_x100 = r.humi_x100;
			g_press_x10 = r.press_x10;  g_light_lx  = r.light_lx;
			g_tvoc_ppb  = r.tvoc_ppb;   g_valid = r.valid;  g_alarm = r.alarm;

			/* 告警：进入时响三声，恢复时停 */
			if (((r.alarm != 0U) || (r.alarm_lo != 0U)) && (s_alarm_on == 0U)) {
				s_alarm_on = 1U;
				if (s_mute == 0U) {
					BEEP_AsyncStart(3U, 150U, 150U);
				}
			} else if (((r.alarm == 0U) && (r.alarm_lo == 0U)) && (s_alarm_on != 0U)) {
				s_alarm_on = 0U;
				BEEP_AsyncStop();
			}
			
			if (DBG_VERBOSE != 0U) SYS_USART_Printf(DBG,"[处理] v=%04X hi=%02X lo=%02X T=%.2f H=%.2f P=%.1f L=%u TV=%u\r\n",
				(unsigned)r.valid, (unsigned)r.alarm, (unsigned)r.alarm_lo,
				r.temp_x100 / 100.0f, r.humi_x100 / 100.0f,
				r.press_x10 / 10.0f,
				(unsigned)r.light_lx, (unsigned)r.tvoc_ppb);
				
			if(xQueueSend(s_q_result, &r, pdMS_TO_TICKS(5)) != pdPASS) {
				
			}
		}
    }
}
//把数据发送给Lora
void vTaskComm(void *p) {
	ProcResult_t r;
	uint8_t  f[32];
	uint16_t n;
	uint8_t  i;
	uint8_t  lr;

	(void)p;

	for (;;) {
		if (xQueueReceive(s_q_result, &r, portMAX_DELAY) == pdPASS) {

			n = SYS_FRAME_BuildEnv(f, sizeof(f),
			                       r.temp_x100, r.humi_x100, r.press_x10,
			                       r.light_lx, r.tvoc_ppb, r.mq135_raw);

			if ((n == 20U) && (SYS_FRAME_Verify(f, n) == SYS_FRAME_OK)) {

				/* ---- 调试出口：本地数值 + 线上字节并排 ---- */
				if (DBG_VERBOSE != 0U) SYS_USART_Printf(DBG, "[帧] T=%.2f H=%.2f P=%.1f L=%u TV=%u\r\n",
				                 r.temp_x100 / 100.0f, r.humi_x100 / 100.0f,
				                 r.press_x10 / 10.0f,
				                 (unsigned)r.light_lx, (unsigned)r.tvoc_ppb);
				for (i = 0; i < n; i++) {
					SYS_USART_Printf(DBG, "%02X ", (unsigned)f[i]);
				}
				SYS_USART_SendLine(DBG, "");

				/* ---- 等 AUX 空闲再发 ---- */
				if (LORA_E22_WaitReady(SYS_LORA_AUX_TIMEOUT_MS) != 0U) {
					lr = LORA_E22_Send(f, n);
					if (lr == LORA_E22_OK) {
						s_tx_ok++;
						SYS_USART_Printf(DBG, "[发送] 第 %u 帧已发出\r\n",
						                 (unsigned)s_tx_ok);
					} else {
						s_tx_fail++;
						SYS_USART_Printf(DBG, "[发送] 失败 lr=%u 累计 %u\r\n",
						                 (unsigned)lr, (unsigned)s_tx_fail);
					}
				} else {
					s_tx_fail++;
					SYS_USART_SendLine(DBG, "[发送] AUX 一直忙，本帧放弃");
				}

			} else {
				SYS_USART_Printf(DBG, "[组帧] FAIL n=%u rc=%u\r\n",
				                 (unsigned)n,
				                 (unsigned)SYS_FRAME_Verify(f, n));
			}
		}
	}
}
//Lora信号处理
void vTaskLoraRx(void *p) {
    uint8_t  cmd;
    uint8_t  pl[64];
    uint16_t len;
    uint16_t n;

    (void)p;

    /* USART3 的接收中断在 LORA_E22_Init() 里已经打开了，这里负责“取帧”。
     * 板1 目前是单向上报，收到的帧先打印出来；等做下行命令(0x20~)时在这里加分支。 */
    for (;;) {
        n = SYS_FRAME_Poll(SYS_LORA_UART);
        while (n > 0U) {
            if (SYS_FRAME_Get(SYS_LORA_UART, &cmd, pl, sizeof(pl), &len) == 0U) {
                SYS_USART_Printf(DBG, "[LoRa RX] cmd=0x%02X len=%u\r\n",
                                 (unsigned)cmd, (unsigned)len);
                lora_cmd_handle(cmd, pl, len);
            }
            n--;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
//喂狗+检查栈大小
void vTaskMonitor(void *p) {
 (void)p;
    uint32_t tick10 = 0U;
    for (;;) {
        /* 每 100ms 报心跳 + 喂狗；每 5 秒打一次统计 */
        vTaskDelay(pdMS_TO_TICKS(100));
        SYS_STATUS_Heartbeat();
        SYS_WDG_Feed();
        if (++tick10 < 50U) continue;
        tick10 = 0U;
        SYS_USART_Printf(DBG, "[栈] Sensor=%u Process=%u Comm=%u LoraRx=%u Monitor=%u\r\n",
                         (unsigned)uxTaskGetStackHighWaterMark(s_h_sensor),
                         (unsigned)uxTaskGetStackHighWaterMark(s_h_process),
                         (unsigned)uxTaskGetStackHighWaterMark(s_h_comm),
                         (unsigned)uxTaskGetStackHighWaterMark(s_h_lora),
                         (unsigned)uxTaskGetStackHighWaterMark(s_h_mon));
        SYS_USART_Printf(DBG, "[统计] 丢帧=%u TX_OK=%u TX_FAIL=%u SHT30_FAIL=%u qS=%u qR=%u\r\n",
                         (unsigned)s_dropCnt, (unsigned)s_tx_ok,
                         (unsigned)s_tx_fail, (unsigned)s_sht30_fail,
                         (unsigned)uxQueueMessagesWaiting(s_q_sensor),
                         (unsigned)uxQueueMessagesWaiting(s_q_result));
    }
}

/*=================================================*/



//OLED 三页绘制
static int32_t ui_abs(int32_t v)
{
	return (v < 0) ? -v : v;
}

//一行数值：超上限写 HI、低于下限写 LO，并让该行闪烁
//OLED 是单色屏，没法变红变蓝，用闪烁 + 前缀代替颜色
static void ui_line_num(uint8_t page, const char *tag, int32_t v,
                        uint8_t frac, int32_t lo, int32_t hi, const char *unit)
{
	char b[24];
	const char *flag = "";
	uint8_t out = 0U;

	if ((hi != 0) && (v > hi)) {
		flag = "HI ";
		out  = 1U;
	} else if ((lo != 0) && (v < lo)) {
		flag = "LO ";
		out  = 1U;
	}

	if ((out != 0U) && (s_blink == 0U)) {
		return;                          //闪烁的下半周期：留空
	}

	if (frac == 2U) {
		snprintf(b, sizeof(b), "%s%s %d.%02d %s", flag, tag,
		         (int)(v / 100), (int)ui_abs(v % 100), unit);
	} else if (frac == 1U) {
		snprintf(b, sizeof(b), "%s%s %d.%d %s", flag, tag,
		         (int)(v / 10), (int)ui_abs(v % 10), unit);
	} else {
		snprintf(b, sizeof(b), "%s%s %d %s", flag, tag, (int)v, unit);
	}

	SYS_OLED_ShowString(page, 0, b);
}

static void ui_draw(uint8_t page)
{
	char b[24];

	SYS_OLED_Clear();

	if (page == 0U) {
		snprintf(b, sizeof(b), "ENV MONITOR  B1");
		SYS_OLED_ShowString(0, 0, b);
		//超上限 HI、低于下限 LO，超限行会闪烁
		ui_line_num(1, "T", g_temp_x100, 2U, T_LO_X100, T_LIMIT_X100, "C");
		ui_line_num(2, "H", g_humi_x100, 2U, H_LO_X100, H_LIMIT_X100, "%");
		ui_line_num(3, "P", g_press_x10, 1U, 0, P_LIMIT_X10, "hPa");
		ui_line_num(4, "L", (int32_t)g_light_lx, 0U, 0, L_LIMIT_LX, "lx");
		ui_line_num(5, "TVOC", (int32_t)g_tvoc_ppb, 0U, 0, V_LIMIT_PPB, "ppb");
		snprintf(b, sizeof(b), "%s", (s_alarm_on != 0U) ? "!! ALARM !!" : "state: normal");
		SYS_OLED_ShowString(6, 0, b);
		SYS_OLED_ShowString(7, 0, "K0:page K1:mute");
	} else if (page == 1U) {
		SYS_OLED_ShowString(0, 0, "STAT     PAGE 2/3");
		snprintf(b, sizeof(b), "TX OK   %u", (unsigned)s_tx_ok);
		SYS_OLED_ShowString(1, 0, b);
		snprintf(b, sizeof(b), "TX FAIL %u", (unsigned)s_tx_fail);
		SYS_OLED_ShowString(2, 0, b);
		snprintf(b, sizeof(b), "DROP    %u", (unsigned)s_dropCnt);
		SYS_OLED_ShowString(3, 0, b);
		snprintf(b, sizeof(b), "SHT ERR %u", (unsigned)s_sht30_fail);
		SYS_OLED_ShowString(4, 0, b);
		snprintf(b, sizeof(b), "UP %u s", (unsigned)(xTaskGetTickCount() / 1000U));
		SYS_OLED_ShowString(5, 0, b);
		snprintf(b, sizeof(b), "PERIOD %u ms", (unsigned)s_period_ms);
		SYS_OLED_ShowString(6, 0, b);
		SYS_OLED_ShowString(7, 0, s_mute ? "MUTE ON" : "MUTE OFF");
	} else {
		SYS_OLED_ShowString(0, 0, "ABOUT    PAGE 3/3");
		SYS_OLED_ShowString(1, 0, "Board1 F407ZE");
		SYS_OLED_ShowString(2, 0, "SHT30 BMP280");
		SYS_OLED_ShowString(3, 0, "BH1750 CCS811");
		SYS_OLED_ShowString(4, 0, "LoRa E22-400T");
		SYS_OLED_ShowString(5, 0, "20B frame 1Hz");
		snprintf(b, sizeof(b), "valid 0x%02X", (unsigned)g_valid);
		SYS_OLED_ShowString(6, 0, b);
		snprintf(b, sizeof(b), "alarm 0x%02X", (unsigned)g_alarm);
		SYS_OLED_ShowString(7, 0, b);
	}
}

//本地显示与交互：OLED + 按键 + 蜂鸣器
void vTaskUi(void *p)
{
	uint8_t  oled_ok;
	uint8_t  page = 0U;
	uint8_t  key;
	uint8_t  dirty = 1U;
	uint16_t ms = 0U;
	uint8_t  addr = SYS_OLED_I2C_ADDR;
	uint8_t  found[8];
	uint8_t  cnt;
	uint8_t  k;

	(void)p;

	/* 先扫一遍总线：能看到 0x3C/0x3D 才说明接线通了 */
	SYS_I2C_Init(SYS_I2C_1, 50000);
	cnt = SYS_I2C_Scan(SYS_I2C_1, found, 8U);
	SYS_USART_Printf(DBG, "[UI] I2C1 scan %u dev:", (unsigned)cnt);
	for (k = 0U; k < cnt; k++) {
		SYS_USART_Printf(DBG, " 0x%02X", (unsigned)found[k]);
		if ((found[k] == 0x3CU) || (found[k] == 0x3DU)) {
			addr = found[k];
		}
	}
	SYS_USART_SendString(DBG, "\r\n");

	oled_ok = (SYS_OLED_Init(SYS_I2C_1, addr) == 0U) ? 1U : 0U;
	/* SYS_OLED_Init 内部会把总线拉到 400k，这里改回 100k 与传感器一致 */
	SYS_I2C_Init(SYS_I2C_1, 50000);
	SYS_USART_Printf(DBG, "[UI] OLED addr=0x%02X %s\r\n",
	                 (unsigned)addr, (oled_ok != 0U) ? "ok" : "none");

	for (;;) {
		key = KEY_Scan();

		if (key == 0U) {                 /* KEY0：切换页面 */
			page = (uint8_t)((page + 1U) % 3U);
			dirty = 1U;
			BEEP_KeySound();
		} else if (key == 1U) {          /* KEY1：静音开关 */
			s_mute = (s_mute == 0U) ? 1U : 0U;
			if (s_mute != 0U) {
				BEEP_AsyncStop();
			}
			dirty = 1U;
			BEEP_KeySound();
		}

		if (++ms >= 25U) {               /* 20ms x 25 = 500ms 刷一次 */
			ms = 0U;
			dirty = 1U;
			s_blink = (uint8_t)(s_blink ^ 1U);   /* 超限行闪烁节奏 */
		}

		if ((dirty != 0U) && (oled_ok != 0U)) {
			dirty = 0U;
			ui_draw(page);
			SYS_OLED_RefreshDirty();
		}

		vTaskDelay(pdMS_TO_TICKS(20));
	}
}

//下行命令处理：板2 / 上位机 -> 板1
static void lora_cmd_handle(uint8_t cmd, const uint8_t *pl, uint16_t len)
{
	uint8_t  ack[8];
	uint32_t up;

	switch (cmd) {
	case 0x20U:                                  /* 设置采样周期：2 字节大端 ms */
		if (len >= 2U) {
			uint16_t ms2 = (uint16_t)(((uint16_t)pl[0] << 8) | (uint16_t)pl[1]);
			if (ms2 < 500U)   { ms2 = 500U;   }
			if (ms2 > 10000U) { ms2 = 10000U; }
			s_period_ms = ms2;
			ack[0] = 0U;
			SYS_USART_Printf(DBG, "[CMD] period -> %u ms\r\n", (unsigned)ms2);
		} else {
			ack[0] = 5U;                         /* 参数错 */
		}
		SYS_FRAME_Send(SYS_LORA_UART, (uint8_t)(cmd | 0x80U), ack, 1U);
		break;

	case 0x22U:                                  /* 查询状态 */
		up = (uint32_t)((xTaskGetTickCount() / 1000U) & 0xFFFFU);
		ack[0] = (uint8_t)((s_alarm_on != 0U) ? 1U : 0U);
		ack[1] = (uint8_t)((s_tx_ok   >> 8) & 0xFFU);
		ack[2] = (uint8_t)( s_tx_ok         & 0xFFU);
		ack[3] = (uint8_t)((s_tx_fail >> 8) & 0xFFU);
		ack[4] = (uint8_t)( s_tx_fail       & 0xFFU);
		ack[5] = (uint8_t)((s_dropCnt >> 8) & 0xFFU);
		ack[6] = (uint8_t)( s_dropCnt       & 0xFFU);
		ack[7] = (uint8_t)up;
		SYS_FRAME_Send(SYS_LORA_UART, (uint8_t)(cmd | 0x80U), ack, 8U);
		SYS_USART_SendLine(DBG, "[CMD] query status");
		break;

	case 0x24U:                                  /* 重启：先应答再复位 */
		ack[0] = 0U;
		SYS_FRAME_Send(SYS_LORA_UART, (uint8_t)(cmd | 0x80U), ack, 1U);
		vTaskDelay(pdMS_TO_TICKS(50));
		SYS_USART_SendLine(DBG, "[CMD] reboot");
		NVIC_SystemReset();
		break;

	default:
		ack[0] = 1U;                             /* 不支持的命令 */
		SYS_FRAME_Send(SYS_LORA_UART, (uint8_t)(cmd | 0x80U), ack, 1U);
		break;
	}
}

/*====================main函数=====================*/
int main(void)
{
	//时钟选择配置
    SYS_CLK_ToHighSpeed();      /* HSE 8MHz -> HCLK 168MHz */
	SYS_NVIC_Init();	/*分级初始化*/
	
	//串口初始化与基础仪器初始化
	SYS_USART_Init(DBG, DBG_BAUD);
	SYS_USART_SendString(DBG, "\r\n[board1] boot...\r\n");

	LED_Init();
    KEY_Init();
    BEEP_Init();

	//指示灯状态机：接上 IWDG 会给"上次被狗复位"亮 LED0 提示
	SYS_STATUS_Init();
	SYS_STATUS_BootStep(0);
	
	//LoRa E22D
	SYS_STATUS_BootStep(1);
	delay_us(1);
	SYS_USART_Printf(DBG, "E22 init=%u\r\n", (unsigned)LORA_E22_Init());

	
	
	/*读总线频率*/
	uint32_t hclk, pclk1, pclk2;
	SYS_CLK_GetBusFreq(&hclk, &pclk1, &pclk2);
	//格式化发送
	SYS_STATUS_BootStep(2);
	SYS_USART_Printf(DBG, "HCLK = %u PCLK1=%u PCLK2=%u\r\n",
	(unsigned)(hclk / 1000000), (unsigned)(pclk1 / 1000000), (unsigned)(pclk2 / 1000000));
	
	//打印字符串
    SYS_USART_SendString(DBG, "\r\n[board1] skeleton ready\r\n");
		
	//LED亮灯提示
	LED_ShowHex(0x0A);
	LED_BlinkUpdate();
	
	//蜂鸣器提示
	//BEEP_Beep(2);
	//BEEP_BeepEx(1, 100, 100);
	//BEEP_KeySound();
		
	//创建任务进入就绪态
	SYS_STATUS_BootStep(3);
	SYS_FAULT_Init();
	SYS_FAULT_SetCallback(SYS_STATUS_Fault);	/* 硬件异常时 LED0 常亮 */
	//建立队列
	s_q_sensor = xQueueCreate(4, sizeof(SensorMsg_t));
	s_q_result = xQueueCreate(4, sizeof(ProcResult_t));
	//两个队列任一建失败：堆不够，直接停住，避免带着空指针继续跑
	if ((s_q_sensor == NULL) || (s_q_result == NULL)) {
		SYS_USART_SendString(DBG, "[RTOS] queue create FAIL\r\n");
		for (;;) { }
	}
	
	//从传感采集数据
	xTaskCreate(vTaskSensor,  "Sensor",  640, NULL, 3, &s_h_sensor);
	//从队列中获取数据
	xTaskCreate(vTaskProcess, "Process", 512, NULL, 3, &s_h_process);
	//把数据发送给Lora
	xTaskCreate(vTaskComm,    "Comm",    512, NULL, 4, &s_h_comm);
	//Lora信号处理
	xTaskCreate(vTaskLoraRx,  "LoraRx",  512, NULL, 3, &s_h_lora);
	//喂狗+检查栈大小
	xTaskCreate(vTaskMonitor, "Monitor", 384, NULL, 5, &s_h_mon);
	//OLED 本地显示 + 按键 + 蜂鸣器
	xTaskCreate(vTaskUi,      "Ui",      512, NULL, 2, &s_h_ui);
	//句柄为 NULL = xTaskCreate 失败（堆不够）；必须停下，否则调度器起来后没人干活
	if ((s_h_sensor == NULL) || (s_h_process == NULL) || (s_h_comm == NULL) ||
		(s_h_lora == NULL) || (s_h_mon == NULL) || (s_h_ui == NULL)) {
		SYS_USART_SendString(DBG, "[RTOS] task create FAIL (heap?)\r\n");
		for (;;) { }
	}
	

	//进运行态（LED3 慢闪），并开 4 秒独立看门狗
	SYS_STATUS_EnterRun();
	SYS_WDG_Init(4000);

	//启动调度器
	vTaskStartScheduler();
	for(;;){}
}
/*=================================================*/
