/* data_build_b1.js —— 板1 项目引导 阶段 4~9（手写、非生成物）
 *
 *  加载顺序：data_build.js（定义 STD_BUILD_GUIDE = 板1 阶段 0~3）之后引入本文件，
 *            本文件只 push，绝不重新定义 STD_BUILD_GUIDE。
 *
 *  三条硬规矩（与 data_build.js 相同）：
 *    1. 只引用 Project_1_检测端\Project\FWLIB 下真实存在的 API（名字、参数个数与顺序、引脚都对得上源码）。
 *    2. UI 不由本手册写：板1 OLED 的界面与排版由 AI 完成，本手册只写“怎么把数据交给显示任务、怎么按键、怎么验收”。
 *    3. 每条都要能验收：必须写出“看到什么现象才算过”。
 *
 *  维护：改本文件后跑 node --check data_build_b1.js（语法自检）。
 * ========================================================================== */
const STD_BUILD_B1 = [
  /* ======================= 板1 · 阶段 4 ======================= */
  {
    n: "🧭 板1 · 阶段 4｜传感器采集：把 6 路数据读准",
    m: "板1 项目引导",
    _b: "板1",
    d: "把 6 路环境量读上来：先扫 I2C 认器件，再逐个打通 BMP280 / BH1750 / CCS811 / DHT11 / MQ-135 / 光敏，并且做到“读不到就不填数”。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：任务框架（Sensor/Process/Comm/LoraRx/Monitor 五个空壳）+ SensorMsg_t 占位。\n本阶段产出：6 路采集填满 SensorMsg_t，并送进 s_q_sensor。\n下一阶段怎么用：阶段 5 在 vTaskProcess 里消费 s_q_sensor、产出 ProcResult_t；阶段 4 的采集函数不动。\n\n本阶段的产物：每一路都有一个“读一次、返回成功与否”的函数，成功给值、失败给无效标记，绝不用上一次的旧值或 0 冒充真值。6 路分别是：BMP280（气压 + 温度，I2C1 0x76）、BH1750（光照 lx，I2C1 0x23）、CCS811（eCO2/TVOC，I2C1 0x5A）、DHT11（温度 + 湿度，PG9 单总线）、MQ-135（空气质量原始 ADC）、光敏（ADC3_IN5 = PF7）。做完这一阶段，你手里应有一个“采集任务每 2 秒打完 6 路、串口能逐路看到成功/失败与数值”的可验证状态。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。",
    k: "板1 传感器采集 I2C 扫描 BMP280 BH1750 CCS811 DHT11 MQ-135 光敏 PF7 ADC3_IN5 0x76 0x23 0x5A 0x3C 单位换算 无效值 重试",
    f: [
      [
        "先用 I2C 扫描把器件认出来",
        "接错地址、没上拉、器件没供电这几种问题，扫描一眼就能分开；先扫再写驱动，能省掉一大半瞎调时间。",
        "I2C1 的 SCL=PB8、SDA=PB9（`SYS_I2C1_SCL_PIN` / `SYS_I2C1_SDA_PIN`），SDA/SCL 各需 4.7k 上拉。先用 `SYS_I2C_Init(SYS_I2C_1, 400000)` 起总线，再调 `SYS_I2C_Scan(SYS_I2C_1, found, 8)` 把应答的 7 位地址收进数组，逐个打印。本板预期能看到：0x76（BMP280，SDO 接 GND）、0x23（BH1750，ADDR 接 GND）、0x5A（CCS811，ADDR 接 GND）、0x68（板载 MPU6050）、0x50（板载 24C02）、0x3C（OLED，见阶段 8）。\n\n→ 放在：#include 进 ①；found/n/i 是局部变量，放 ④ vTaskSensor 内（临时验证才放 ⑤）。",
        "/* 放在：#include 进 ① */\n#include \"sys_i2c.h\"\n\nuint8_t found[8];\nuint8_t n, i;\n\nSYS_I2C_Init(SYS_I2C_1, 400000);\nn = SYS_I2C_Scan(SYS_I2C_1, found, 8);\nSYS_USART_Printf(SYS_USART_1, \"I2C1 scan: %u dev\\r\\n\", (unsigned)n);\nfor (i = 0; i < n; i++) {\n    SYS_USART_Printf(SYS_USART_1, \"  addr=0x%02X\\r\\n\", (unsigned)found[i]);\n}",
        "串口打印出器件地址列表。看到 0x76 / 0x23 / 0x5A 三条即三颗外接器件都在线；某个地址缺席，就对号查那颗器件的接线与 ADDR 脚。",
        "扫描只证明“有器件在总线上应答”，不证明型号对（0x50 是 24C02 不是 BH1750）。另一个常见坑：I2C 从机不应答时驱动会等到总线超时，如果把扫描放进任务里、每轮都扫，会把采集任务拖慢；扫描只在初始化时做一次。SDA/SCL 接反或没上拉时读回恒 0xFF 且扫描结果为空。",
        "SYS_I2C_Init / SYS_I2C_Scan / SYS_I2C_IsDeviceReady"
      ],
      [
        "BMP280：气压 + 温度，先读 ID 再读值",
        "气压是这套系统里最容易“悄悄读错”的一路：ID 不对说明型号或接线错，ID 对了但 CTRL_MEAS 写错则读数会一直不刷新。",
        "`SYS_BMP280_GetID(SYS_I2C_1)` 应返回 0x58（BMP280）。ID 对了才 `SYS_BMP280_Init(SYS_I2C_1)`：它写 0xF5(CONFIG)/0xF4(CTRL_MEAS)，过采样取 `SYS_BMP280_OSRS_T`（2 = ×2）、`SYS_BMP280_OSRS_P`（3 = ×4），默认 `SYS_BMP280_USE_NORMAL_MODE = 0` 即 forced 单次，读值走自包含事务：`SYS_BMP280_Read` / `SYS_BMP280_ReadRaw` 内部会写 0xF4(forced) 触发一次、等 `SYS_BMP280_FORCED_DELAY_MS`（15 ms）、再轮询状态位后连读 6 字节，所以**不必**自己调 `SYS_BMP280_Trigger()`（头文件写明「内部已含触发，不必再调」）。CTRL_MEAS 字节的位布局是 `(osrs_t << 5) | (osrs_p << 2) | mode`：默认配置算出 0x4D（forced），把 mode 改成 3 就是 0x4F（normal 连续）。`SYS_BMP280_ReadRaw` 出 0.01℃ 与 Pa，`SYS_BMP280_Read` 直接给 `temp_c` / `press_hpa`。\n\n→ 放在：#include 进 ①；SYS_BMP280_Data_t d 与 id 是局部，放 ④ vTaskSensor 内；类型来自库头，别自己再 typedef。",
        "/* 放在：#include 进 ① */\n#include \"sys_bmp280.h\"\n\nSYS_BMP280_Data_t d;\nint id;\n\nid = SYS_BMP280_GetID(SYS_I2C_1);\nif (id < 0) {\n    SYS_USART_Printf(SYS_USART_1, \"BMP280 ID 读取失败 err=%d\\r\\n\", id);\n} else {\n    SYS_USART_Printf(SYS_USART_1, \"BMP280 ID=0x%02X (want 0x58)\\r\\n\", (unsigned)id);\n}\nif (SYS_BMP280_Init(SYS_I2C_1) == 0U) {\n    if (SYS_BMP280_Read(SYS_I2C_1, &d) == 0) {\n        SYS_USART_Printf(SYS_USART_1, \"T=%.2f C  P=%.2f hPa\\r\\n\",\n                         d.temp_c, d.press_hpa);\n    }\n}\n/* 直接改 CTRL_MEAS 时的两种合法值（对照芯片手册的位布局） */\n/* SYS_I2C_WriteByte(SYS_I2C_1, SYS_BMP280_ADDR, 0xF4, 0x4D);  强制单次 */\n/* SYS_I2C_WriteByte(SYS_I2C_1, SYS_BMP280_ADDR, 0xF4, 0x4F);  连续转换 */",
        "串口打印 T 与 P：室温下 T 与 DHT11 差 1℃ 以内；P 在 1000 hPa 上下（海拔每升高 100m 降约 12 hPa）。正常模式（0x4F）下芯片连续转换，Read 只等状态位后取最新值；强制模式（0x4D）下 Read 每次都会先触发再等 15 ms，拿到的同样是当次新值——「读到上次结果」只在绕过驱动、自己直接读 0xF7 时才可能出现。",
        "两个真坑：① 用 normal（0x4F）芯片会连续转换、自加热，温度补偿偏高，气压跟着偏（手册未给具体偏差数值，属工程经验值，需上板对比 forced/normal），要趋势就留 forced；② 气压 int16 存不下 1013.25×100 = 101325，必须 ×10，见阶段 6。另外 forced 模式每次 Read 都要等满 15 ms 再读，读一次的耗时下限就是 15 ms，放高优先级任务里要留意。",
        "SYS_BMP280_GetID / SYS_BMP280_Init / SYS_BMP280_Trigger / SYS_BMP280_Read / SYS_BMP280_ReadRaw / SYS_BMP280_AltitudeM"
      ],
      [
        "BH1750：光照 lux = raw/1.2，过饱和要认出来",
        "光照是唯一一个“值本身合法但含义已经失真”的量：过饱和时读回 0xFFFF，换算出来 54612 lx，直接发出去就是假数据。",
        "`SYS_BH1750_Init(SYS_I2C_1)` 默认按 `SYS_BH1750_ADDR_DEFAULT` = 0x23 起连续 H 分辨率（`SYS_BH1750_CMD_CONT_H_RES`，1 lx、约 120ms 一次），所以初始化后要等一次转换。换算关系：H 分辨率下 `lux = raw / 1.2`，量程上限 65535 / 1.2 ≈ 54612 lx。`SYS_BH1750_ReadLux` 会自己换算；`SYS_BH1750_ReadRaw` 只给原始 16 位值（读到 0xFFFF 也照原样返回、返回码仍是 0），饱和码 3（`SYS_BH1750_ERR_SATURATED`）是 `SYS_BH1750_ReadLux` 的返回码，此时 `*lux` 不被修改。\n\n→ 放在：同第2步：include 进 ①，raw/lux/r 局部放 ④ vTaskSensor。",
        "/* 放在：同第2步：include 进 ①，raw/lux/r 局部放 ④ vTaskSensor */\n#include \"sys_bh1750.h\"\n\nuint16_t raw;\nfloat    lux = 0.0f;\nint      r;\n\nif (SYS_BH1750_Init(SYS_I2C_1) == SYS_BH1750_OK) {\n    vTaskDelay(pdMS_TO_TICKS(200));      /* 等第一次转换完（120ms 量级） */\n    r = SYS_BH1750_ReadRaw(SYS_I2C_1, &raw);\n    if (r == SYS_BH1750_OK) {\n        if (raw == 0xFFFFU) {       /* ReadRaw 不报饱和，自己判 */\n            SYS_USART_Printf(SYS_USART_1, \"BH1750 过饱和(0xFFFF)，本路放弃\\r\\n\");\n        } else {\n            SYS_USART_Printf(SYS_USART_1, \"BH1750 raw=%u lux=%.1f\\r\\n\",\n                             (unsigned)raw, (float)raw / 1.2f);\n        }\n    } else {\n        SYS_USART_Printf(SYS_USART_1, \"BH1750 err=%d\\r\\n\", r);\n    }\n    (void)SYS_BH1750_ReadLux(SYS_I2C_1, &lux);   /* 也可直接让它换算 */\n}",
        "手机手电筒照上去：raw 变大、lux 跟着变大；遮住器件 lux 掉到个位数或 0。若 raw 打到 0xFFFF（过饱和），用一张纸挡一层光，值应立刻回落——说明饱和判定生效，而不是器件坏了（读原始值时饱和不会以返回码 3 出现，那个码只有 ReadLux 会给）。",
        "只写 lux 是 54612 这种“漂亮整数”的时候先怀疑过饱和。初始化后立刻读会拿到 0 或上一次的旧值，必须留一次转换时间。另外 BH1750 的 0x23 与 24C02 的 0x50、MPU6050 的 0x68 同在 I2C1 上，谁把总线拉到长低电平（比如 MPU6050 在中断里狂读）就会让 BH1750 读超时。",
        "SYS_BH1750_Init / SYS_BH1750_InitAddr / SYS_BH1750_ReadRaw / SYS_BH1750_ReadLux / SYS_BH1750_SendCmd / SYS_BH1750_PowerDown"
      ],
      [
        "CCS811：eCO2/TVOC 要等 DRDY，建议周期性喂环境数据",
        "CCS811 是“算法型”器件：内部等效值要靠温湿度补偿，不喂环境数据（或喂错）时，算法会退回芯片默认的 50 %RH / 25 ℃ 去补偿，实测温湿度偏离大时 TVOC 会长时间漂在错的值上。",
        "`SYS_CCS811_Init(SYS_I2C_1)` 会验证 HW_ID = 0x81（`SYS_CCS811_HW_ID_VALUE`）并等 STATUS.APP_VALID。读值前用 `SYS_CCS811_DataReady(SYS_I2C_1)` 判数据就绪，再 `SYS_CCS811_Read(SYS_I2C_1, &d)` 取 `d.eco2_ppm` / `d.tvoc_ppb`。把 DHT11/BMP280 的温湿度通过 `SYS_CCS811_SetEnvData(SYS_I2C_1, temp_c, humi_rh)` 定期喂进去（每组值变化后喂一次即可）。\n\n→ 放在：同第2步：include 进 ①，d/s 局部放 ④ vTaskSensor；SetEnvData 每 60s 喂一次。",
        "/* 放在：同第2步：include 进 ①，d/s 局部放 ④ vTaskSensor */\n#include \"sys_ccs811.h\"\n\nSYS_CCS811_Data_t d;\nint s;\n\nSYS_CCS811_Init(SYS_I2C_1);\nSYS_CCS811_SetEnvData(SYS_I2C_1, 25.0f, 55.0f);   /* 换成实测温湿度 */\nif (SYS_CCS811_DataReady(SYS_I2C_1)) {\n    s = SYS_CCS811_Read(SYS_I2C_1, &d);\n    SYS_USART_Printf(SYS_USART_1, \"CCS811 eCO2=%u ppm TVOC=%u ppb (valid=%u)\\r\\n\",\n                     (unsigned)d.eco2_ppm, (unsigned)d.tvoc_ppb,\n                     (unsigned)d.valid);\n}",
        "串口每隔几秒打印一行 eCO2/TVOC：对着器件哈气或喷一点酒精（远离电路板），TVOC 应在十几秒内明显上升、之后缓慢回落。数据没就绪时 `SYS_CCS811_DataReady` 返回 0，不会打出新行。",
        "三个真实坑：① 器件出厂没跑过老化，手册要求连续通电预热至少 24 小时（前 24~48 小时电阻变化最大），这段时间基线不稳、数值漂是正常的，别当驱动错；② 不喂 `SYS_CCS811_SetEnvData` 时算法用默认 50 %RH / 25 ℃ 补偿，实测温湿度偏离大时 TVOC 会持续偏高；③ `d.valid = 0` 时 `eco2_ppm`/`tvoc_ppb` 虽然被填了但不采信——这种值不许进帧，按无效处理。",
        "SYS_CCS811_Init / SYS_CCS811_DataReady / SYS_CCS811_Read / SYS_CCS811_SetEnvData / SYS_CCS811_GetHWID / SYS_CCS811_GetBaseline"
      ],
      [
        "DHT11：单总线，锁定中断 + 1Hz 节流 + 重试",
        "DHT11 的时序全靠微秒级延时，RTOS 下任何一次中断抢占都会把位相位打断，读回校验必然失败；而它的采样率只有 1Hz，读太勤只会拿到旧值。",
        "`SYS_DHT11_Init(SYS_DHT11_PORT, SYS_DHT11_PIN)`（宏已固定 PG9）后至少等 1s 让器件稳定。读用 `SYS_DHT11_ReadRetry(&t, &h, 3)` 一次搞定重试，或自己循环 `SYS_DHT11_Read`。失败时用 `SYS_DHT11_GetRaw(raw)` 看 5 个裸字节：全 FF = 器件没应答，全 00 = 被拉死，个别位不同 = 时序被打断。两次读之间必须间隔 2200ms 以上。\n\n→ 放在：#include 进 ①；SYS_DHT11_Init 进 ⑤ 初始化段（单总线只初始化一次）；t/h/raw/r 局部放 ④ vTaskSensor。",
        "/* 放在：#include 进 ① */\n#include \"sys_dht11.h\"\n\nfloat   t = 0.0f, h = 0.0f;\nuint8_t raw[5];\nint     r;\n\nSYS_DHT11_Init(SYS_DHT11_PORT, SYS_DHT11_PIN);\nvTaskDelay(pdMS_TO_TICKS(1500));\n\nr = SYS_DHT11_ReadRetry(&t, &h, 3);      /* 内部重试 3 次 */\nSYS_DHT11_GetRaw(raw);\nSYS_USART_Printf(SYS_USART_1, \"DHT11 r=%d T=%.1f H=%.1f raw=%02X %02X %02X %02X %02X\\r\\n\",\n                 r, t, h, raw[0], raw[1], raw[2], raw[3], raw[4]);\n\nvTaskDelay(pdMS_TO_TICKS(2200));         /* 采样率 1Hz，间隔不足读到旧值 */",
        "串口每轮打印一行：r = 0 且 T 随环境变化（手捂上去升一两度）；把 raw 五个字节打出来，读失败时能一眼分出“没应答”还是“时序被打断”。连续 20 轮成功率应在 95% 以上。",
        "`SYS_DHT11_LOCK_IRQ`（sys_dht11.h 里默认 = 1）必须在读出期间关中断——别为了“让别的中断快点响应”把它改成 0，否则读数会随机变成 -2 校验失败。另外 DHT11 的小数位本来就是 0，看到 25.0 不是精度丢失。最后：RTOS 里不要为了等它去调 `SYS_TICK_Init()`。",
        "SYS_DHT11_Init / SYS_DHT11_Read / SYS_DHT11_ReadRetry / SYS_DHT11_GetRaw"
      ],
      [
        "ADC 两路：光敏 PF7(ADC3_IN5) 与 MQ-135 原始值",
        "这两路都不是数字器件，读的是原始 ADC，必须按“读数是否有效”判断而不是“值是不是 0”；而且原始值是 0~4095 的分辨率，不能直接当物理量。",
        "光敏的引脚与通道在库里已经定死：`SYS_ADC_LIGHT_ADC` = ADC3、`SYS_ADC_LIGHT_CH` = ADC_Channel_5、`SYS_ADC_LIGHT_PORT` = GPIOF、`SYS_ADC_LIGHT_PIN` = GPIO_Pin_7（PF7 = ADC3_IN5），用 `SYS_ADC_Init(...)` 初始化后 `SYS_ADC_ReadAvg` 平均 8 次。MQ-135 在库里也是既定宏：`SYS_ADC_MQ135_ADC` = ADC3、`SYS_ADC_MQ135_CH` = ADC_Channel_5、`SYS_ADC_MQ135_PORT` = GPIOF、`SYS_ADC_MQ135_PIN` = GPIO_Pin_7（PF7 = ADC3_IN5）——它与光敏那一套指向同一根引脚（设计定案：光照改走 BH1750 后这根脚空出来接 MQ-135 的 AO），接哪个器件就用哪套宏名，不要同时按两种用途初始化。两路都要用 `SYS_ADC_ReadOK` 判有效，无效时 `SYS_ADC_RAW_INVALID`（0xFFFF）不许进帧。\n\n→ 放在：#include 进 ①；SYS_ADC_Init 进 ⑤ 初始化段；light/mq135 局部放 ④ vTaskSensor。",
        "/* 放在：#include 进 ① */\n#include \"sys_adc.h\"\n\nuint16_t light, mq135;\n\n/* 光敏：库内已定 PF7 = ADC3_IN5 */\nSYS_ADC_Init(SYS_ADC_LIGHT_ADC, SYS_ADC_LIGHT_CH,\n             SYS_ADC_LIGHT_PORT, SYS_ADC_LIGHT_PIN);\nlight = SYS_ADC_ReadAvg(SYS_ADC_LIGHT_ADC, SYS_ADC_LIGHT_CH, 8);\n\n/* MQ-135：库内宏 = PF7 = ADC3_IN5（与光敏同一根脚，二选一） */\nSYS_ADC_Init(SYS_ADC_MQ135_ADC, SYS_ADC_MQ135_CH,\n             SYS_ADC_MQ135_PORT, SYS_ADC_MQ135_PIN);\nmq135 = SYS_ADC_ReadAvg(SYS_ADC_MQ135_ADC, SYS_ADC_MQ135_CH, 8);\n\nif (SYS_ADC_ReadOK(light) == 0U) {\n    SYS_USART_SendLine(SYS_USART_1, \"light ADC invalid\");\n}\nSYS_USART_Printf(SYS_USART_1, \"light=%u  mq135=%u  (%u mV)\\r\\n\",\n                 (unsigned)light, (unsigned)mq135,\n                 (unsigned)SYS_ADC_ToMilliVolt(mq135));",
        "串口打印原始值与毫伏值：遮住光敏原件，light 明显变化；对着 MQ-135 探头哈气（或靠近酒精棉），mq135 的毫伏值在几十秒内上升、离开后缓慢回落。用万用表量同一脚，读数与 `SYS_ADC_ToMilliVolt` 打印值应在 ±50mV 内。",
        "四个坑：① channel 与 port/pin 不匹配（比如写 ADC_Channel_10 却给 PF7）会读出别的通道甚至恒 0/满量程；② 该 ADC 若正在 DMA/扫描/连续采集，`SYS_ADC_Read` 直接返回 0xFFFF 并累加 `SYS_ADC_ConflictCount`，单次读前先 `SYS_ADC_DmaStop`；③ MQ-135 要预热（本工程 sys_adc.h 注明需连续通电预热 ≥48 小时、R0 需自测标定，头几分钟的读数缓升只是开始），不等预热就下阈值结论一定会误报；④ 别把原始 0~4095 当 ppm 用。",
        "SYS_ADC_Init / SYS_ADC_Read / SYS_ADC_ReadAvg / SYS_ADC_ReadOK / SYS_ADC_ToMilliVolt / SYS_ADC_ConflictCount / SYS_ADC_DmaStop"
      ],
      [
        "统一“无效值不打进帧”的采集接口",
        "6 路里只要有 1 路偶发失败就整帧丢弃，会把数据率压得很低；反过来用旧值或 0 冒充，上位机曲线就永远看不到断点。",
        "给 6 路定义一个统一的结果结构：`valid` 位图 + 各路的定点值。每轮采集：成功就置位并填值，失败就清位、值填 0。这个结构体整块进队列（`xQueueSend` 是**按值拷贝**，队列项大小用 `sizeof(结构体)`），下游只看 `valid` 位决定哪些字段能进帧。串口打印成一行便于观察。\n\n→ 放在：SensorMsg_t 进 ③（队列和下游都要用，必须在任务函数之前）；填 msg 的采集函数写成 static 辅助函数放 ④，置于 vTaskSensor 之前。",
        "/* 放在：SensorMsg_t 进 ③（队列和下游都要用，必须在任务函数之前） */\ntypedef struct {\n    uint16_t valid;          /* bit0 T bit1 H bit2 P bit3 lux bit4 tvoc bit5 mq135 */\n    int16_t  temp_x100;      /* 温度，0.01 ℃ */\n    int16_t  humi_x100;      /* 湿度，0.01 %RH */\n    int16_t  press_x10;      /* 气压，0.1 hPa（×10，不是 ×100） */\n    uint16_t light_lx;       /* 光照，lx（整数即可） */\n    uint16_t tvoc_ppb;       /* TVOC，ppb */\n    uint16_t mq135_raw;      /* MQ-135 原始 ADC 0~4095 */\n    uint32_t tick;\n} SensorMsg_t;\n\nSensorMsg_t m;\n\nmemset(&m, 0, sizeof(m));\nif (dht11_ok) { m.valid |= (1U << 0); m.temp_x100  = (int16_t)(t * 100.0f); }\nif (dht11_ok) { m.valid |= (1U << 1); m.humi_x100  = (int16_t)(h * 100.0f); }\nif (bmp_ok)   { m.valid |= (1U << 2); m.press_x10  = (int16_t)(p * 10.0f); }\nif (light_ok) { m.valid |= (1U << 3); m.light_lx   = light; }\nif (ccs_ok)   { m.valid |= (1U << 4); m.tvoc_ppb   = d.tvoc_ppb; }\nif (mq_ok)    { m.valid |= (1U << 5); m.mq135_raw  = mq135; }\nm.tick = SYS_TICK_GetTick();\n\nSYS_USART_Printf(SYS_USART_1, \"valid=0x%02X T=%d.%02d H=%d.%02d P=%d.%01d\\r\\n\",\n                 (unsigned)m.valid, m.temp_x100 / 100, m.temp_x100 % 100,\n                 m.humi_x100 / 100, m.humi_x100 % 100,\n                 m.press_x10 / 10, m.press_x10 % 10);\n\n(void)xQueueSend(s_q_sensor, &m, 0);",
        "拔掉任意一颗器件（或拔 DHT11 数据线）后看打印：`valid` 位图对应位变 0、该路不再出现在后续组帧里，其余 5 路照常出数；插回去该位自动恢复。据此判定“无效值不打进帧”确实生效。",
        "把 `m` 这样的结构体整块 `xQueueSend` 传值是对的，但别传指针（局部变量出函数即失效）；队列项大小必须是 `sizeof(SensorMsg_t)`，写成固定 20 或 4 会让后面字段被截断。另一个坑：`(int16_t)(t * 100.0f)` 对负温度是合法补码（-5.00℃ → -500），但若把 int16 硬转 uint16 再发出去就成了 65036，见阶段 6。",
        "xQueueSend / xQueueCreate / SYS_TICK_GetTick / memset"
      ]
    ]
  },

  /* ======================= 板1 · 阶段 5 ======================= */
  {
    n: "🧭 板1 · 阶段 5｜数据处理与阈值：从原始值到能报警的干净数",
    m: "板1 项目引导",
    _b: "板1",
    d: "把采集来的原始值做滤波、去野值、判阈值、打包告警位，并把量纲换算成整数（温度湿度 ×100、气压 ×10）。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：SensorMsg_t + s_q_sensor + vTaskProcess 空壳。\n本阶段产出：滤波 / 野值 / 阈值与回差 / 告警位 + ProcResult_t + s_q_result。\n下一阶段怎么用：阶段 6 在 vTaskComm 里把 ProcResult_t 组成 20 字节帧，处理逻辑不再改。\n\n本阶段的产物：一个纯计算的“处理任务”——输入是阶段 4 的 SensorMsg_t，输出是 6 个定点整数 + 1 个告警位字节。它必须做到：不阻塞、不访问硬件、读不到的数不进结果；超限置位、回到阈值以下（带回差）才清位，避免在阈值上下抖动时告警疯狂翻转。做完这一阶段，你可以在不动任何硬件的情况下，用“注入一个假值”把告警路径完整走一遍。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。",
    k: "板1 数据处理 滑动平均 中值滤波 野值剔除 阈值 回差 迟滞 告警位 打包 量纲 倍率 ×100 ×10 定点",
    f: [
      [
        "滤波：滑动平均 + 中值，抑制单点跳变",
        "传感器偶发跳变（DHT11 位相位偏一下、ADC 被电机干扰）如果直接进帧，上位机曲线上就是一根根毛刺，阈值判据会被噪声反复触发。",
        "为每一路准备一个小窗口。滑动平均用环形缓冲、窗口 8，算总和而不是每次重算（`sum -= old; sum += new;`）；中值用于抗单点野值，窗口取 3 就够：三个值排序取中间那个，把一次跳变直接吃掉。滤波只对**成功读到**的样本做，失败轮次不要塞 0 进窗口（会把平均值拉低）。\n\n→ 放在：FILT_N 宏进 ②；Filt_t 进 ③；滤波函数进 ④；滤波器实例只有 Process 用，就做成 ④ 的 static 变量，别做全局。",
        "/* 放在：FILT_N 宏进 ② */\n#define FILT_N 8\n\ntypedef struct {\n    int32_t buf[FILT_N];\n    int32_t sum;\n    uint8_t idx;\n    uint8_t cnt;\n} Filt_t;\n\nstatic int32_t filt_push(Filt_t *f, int32_t v)\n{\n    if (f->cnt < FILT_N) {\n        f->cnt++;\n    } else {\n        f->sum -= f->buf[f->idx];      /* 挤掉最老的 */\n    }\n    f->buf[f->idx] = v;\n    f->sum += v;\n    f->idx = (uint8_t)((f->idx + 1U) % FILT_N);\n    return f->sum / (int32_t)f->cnt;\n}\n\nstatic int32_t med3(int32_t a, int32_t b, int32_t c)\n{\n    if (a > b) { int32_t t = a; a = b; b = t; }\n    if (b > c) { int32_t t = b; b = c; c = t; }\n    if (a > b) { int32_t t = a; a = b; b = t; }\n    return b;                              /* 中值 */\n}",
        "串口同时打印“原始值 / 平均后 / 中值后”三列：正常时三者接近；用手指快速弹一下 DHT11 或让光敏闪一下手电，原始值出现一根大毛刺，中值列基本不动、平均列只轻微抬一下。",
        "两个坑：① 窗口越大越平滑，但对真实突变的响应越慢——测烟雾/气体这类要抓快速上升的量，窗口别超过 8；② `sum` 用 int32 累加定点值（最大 10132×8），别用 int16，会溢出。",
        "SYS_TICK_GetTick / xQueueReceive / xQueueSend"
      ],
      [
        "野值剔除：和中值/上次值比，太离谱就丢",
        "光有平均挡不住连续几拍的野值（比如 DHT11 连续两次位相位错），中值窗口 3 也吃不掉；这类值必须按“物理上不可能”直接判死。",
        "两条判据二选一或同时用：① 与同一路最近一次**已接受**值的差超过变化率上限（如气压 1 秒内变化超过 5 hPa、温度超过 3℃/次）即判野值；② 与中值之差超过容忍带即判野值。判野值后**不更新滤波器、也不更新上一次接受值**，并给该路计一个 `reject_cnt` 计数。野值不算“读失败”，所以 `valid` 位保持，但值沿用滤波结果（或本轮标无效，按你的验收口径定，定完就别改）。\n\n→ 放在：跳变宏进 ②；is_outlier 写成 static 函数放 ④；上次值状态用 ④ 的 static 变量。",
        "/* 放在：跳变宏进 ② */\n#define TEMP_JUMP_X100   300    /* 单次允许最大跳变 3.00 ℃ */\n#define PRESS_JUMP_X10    50    /* 单次允许最大跳变 5.0 hPa */\n\nstatic uint8_t is_outlier(int32_t now, int32_t last, int32_t jump_lim)\n{\n    int32_t d = now - last;\n    if (d < 0) d = -d;\n    return (d > jump_lim) ? 1U : 0U;\n}\n\nstatic uint16_t s_reject_t, s_reject_p;\n\nif (is_outlier(t_raw, s_t_last, TEMP_JUMP_X100)) {\n    s_reject_t++;                          /* 丢弃，不改 s_t_last */\n    SYS_USART_Printf(SYS_USART_1, \"temp outlier %d, reject=%u\\r\\n\",\n                     t_raw, (unsigned)s_reject_t);\n} else {\n    s_t_last = t_raw;\n}",
        "串口打印 `reject` 计数：正常运行时它长时间为 0；用打火机/电烙铁靠近温度探头再迅速移开，或在 DHT11 中途拔线再插，`reject` 计数增加而曲线不断点。",
        "最大的坑是**用野值去更新上一次接受值**：一旦把野值当成基准，后面所有正常值都会相对它判成野值，曲线会永久跑偏。另一坑是变化率限值定太严：真正的开窗通风会让温度 1 分钟内变化几度，限值按“单次采样间隔”算，不要按分钟算。",
        "SYS_USART_Printf / SYS_TICK_GetTick"
      ],
      [
        "量纲与倍率：温度湿度 ×100、气压 ×10",
        "协议里全是整数（线上不传 float：结构体对齐与字节序随编译器变），换算错一次，上位机看到的数就整体差 10 倍或 100 倍，而且看起来“还挺像”。",
        "统一在**处理任务里**换算，采集任务只给物理量（float 或原始 ADC）。规则：温度 `int16_t  temp_x100  = (int16_t)(t_c * 100.0f);`（可为负，-5.00℃ → -500，补码 0xFE0C）；湿度 `int16_t humi_x100 = (int16_t)(h_rh * 100.0f);`；气压 `int16_t press_x10 = (int16_t)(p_hpa * 10.0f);`（**必须 ×10**：1013.25×100 = 101325 超出 uint16 上限 65535，×10 得 10132 正好）。光照/TVOC/MQ-135 是 uint16 原值，不乘。\n\n→ 放在：不新增声明：直接在 ④ vTaskProcess 里写结果结构体的字段（x100/x10 是约定，不是变量）。",
        "/* 放在：不新增声明：直接在 ④ vTaskProcess 里写结果结构体的字段（x100/x10 是约定，不是变量） */\n/* 物理量 → 线上定点整数 */\nint16_t  temp_x100  = (int16_t)(25.60f * 100.0f);    /* 2560  */\nint16_t  humi_x100  = (int16_t)(60.30f * 100.0f);    /* 6030  */\nint16_t  press_x10  = (int16_t)(1013.25f * 10.0f);   /* 10132 */\n\n/* 反算回物理量（串口/OLED 自检时用） */\nSYS_USART_Printf(SYS_USART_1, \"T=%d.%02d C  H=%d.%02d %%RH  P=%d.%01d hPa\\r\\n\",\n                 temp_x100 / 100,  temp_x100 % 100,\n                 humi_x100 / 100,  humi_x100 % 100,\n                 press_x10 / 10,   press_x10 % 10);\n\n/* 负数取余要小心：-500 % 100 = 0，-560 % 100 = -60 */\nSYS_USART_Printf(SYS_USART_1, \"neg case: T=%d.%02d\\r\\n\",\n                 (int)(-560 / 100), (int)((-560 % 100 < 0) ? -(-560 % 100) : (-560 % 100)));",
        "拿已知值验证：串口打印 `T=25.60`、`H=60.30`、`P=1013.2`（用计算器核对换算）；把温度故意取负值 -5.00，打印仍为 `-5.00` 而不是 `650.36` 或 `-5.-0`。",
        "三个真坑：① 气压用 ×100 会溢出：1013.25×100 = 101325 > 65535，压成 uint16 后显示成 101.3 hPa 这种“永远偏低”的怪值；② 负数取模在 C 里是截断向零，`-560 % 100 = -60`，直接拼字符串会打出 `-5.-60`，显示前要取绝对值；③ 不要在串口打印之前就把定点值转回 float 打印（`printf` 的 `%f` 在部分工程不开 microLIB 时也不可用，用整数打印最稳）。",
        "SYS_FRAME_BuildEnv / SYS_FRAME_UnpackEnv / SYS_USART_Printf"
      ],
      [
        "阈值判定 + 回差（迟滞），让告警不抖",
        "用单一阈值判超限，值在阈值附近抖一下（比如温度 29.98/30.02）就会让告警位每秒翻转几十次：上位机报警刷屏、蜂鸣器吱吱乱叫。",
        "每个量配两个数：`LIMIT`（触发上限）与 `HYST`（回差，如 1℃）。状态机：未报警时只在 `value > LIMIT` 时置报警；已报警时只在 `value < LIMIT - HYST` 时清报警。中间这段（LIMIT-HYST ~ LIMIT）保持原状态不变。状态保存成全局/静态变量，跨轮次保持。\n\n→ 放在：阈值宏进 ②；s_alarm_t 状态进 ②（跨周期保持）；判定函数进 ④。",
        "/* 放在：阈值宏进 ② */\n#define T_LIMIT_X100   3000    /* 30.00 ℃ 触发 */\n#define T_HYST_X100     100    /* 回差 1.00 ℃ */\n\nstatic uint8_t s_alarm_t;\n\nstatic uint8_t alarm_step(uint8_t cur, int32_t v, int32_t limit, int32_t hyst)\n{\n    if (cur == 0U) {\n        if (v > limit) return 1U;          /* 超上限才置 */\n    } else {\n        if (v < (limit - hyst)) return 0U; /* 回到回差以下才清 */\n    }\n    return cur;                            /* 中间带保持 */\n}\n\ns_alarm_t = alarm_step(s_alarm_t, s_t_last, T_LIMIT_X100, T_HYST_X100);\nSYS_USART_Printf(SYS_USART_1, \"T=%d.%02d alarm=%u\\r\\n\",\n                 s_t_last / 100, s_t_last % 100, (unsigned)s_alarm_t);",
        "验收要能“卡在边界上”：把阈值临时改成 `T_LIMIT_X100 = 2600`（26.00℃）并用手捂探头，看着打印在 25.9x~26.0x 之间来回时 `alarm` 只翻转一次；松手降到 25.00 以下才变回 0。若来回乱翻，说明回差没生效或用了 `>=`/`<=` 写成两次比较。",
        "坑：① 回差小于噪声幅度等于没做（温度噪声约 ±0.1℃，回差至少 0.5℃）；② 报警状态变量别放在任务局部变量里，会每轮归零；③ 别用 `>=` 判超限又用 `>` 判恢复，边界上两边同时成立会锁死。",
        "SYS_USART_Printf / SYS_TICK_GetTick"
      ],
      [
        "告警位打包：一个字节带 6 路状态",
        "协议里没有单独给 6 个告警留 6 个字节的位置（数据段只有 12 字节，6 个物理量已经占满），把告警压进一个 bit 字节最省。",
        "约定一个字节的位含义并写进你自己的协议笔记：bit0 温度超限、bit1 湿度超限、bit2 气压超限、bit3 光照超限、bit4 TVOC 超限、bit5 MQ-135 超限、bit6 bit7 保留。用 `|=` 置位、`&= ~` 清位，每轮从零开始重算（不要跨轮累加）。打包结果既进帧（作为 TVOC 字段之外的诊断量：用 `SYS_FRAME_Send` 另发一帧 CMD=0x02 的短帧）也直接给 OLED。\n\n→ 放在：AL_ 前缀宏全进 ②；用法在 ④ vTaskProcess。",
        "/* 放在：AL_ 前缀宏全进 ② */\n#define AL_TEMP   (1U << 0)\n#define AL_HUMI   (1U << 1)\n#define AL_PRESS  (1U << 2)\n#define AL_LIGHT  (1U << 3)\n#define AL_TVOC   (1U << 4)\n#define AL_MQ135  (1U << 5)\n\nuint8_t alarm = 0U;\n\nif (s_alarm_t) alarm |= AL_TEMP;\nif (s_alarm_h) alarm |= AL_HUMI;\nif (s_alarm_p) alarm |= AL_PRESS;\nif (s_alarm_l) alarm |= AL_LIGHT;\nif (s_alarm_v) alarm |= AL_TVOC;\nif (s_alarm_m) alarm |= AL_MQ135;\n\nSYS_USART_Printf(SYS_USART_1, \"alarm=0x%02X\\r\\n\", (unsigned)alarm);\n/* 只走本地显示/蜂鸣器也行，进帧用 SendShort 另开一帧命令字 */\nif (alarm != 0U) {\n    SYS_FRAME_SendShort(SYS_USART_1, 0x02U);   /* 示例：CMD=0x02 表示“有告警” */\n}",
        "人为造每一个位：手捂温度 → 打印里 alarm 的 0x01 位出现；用手电照光敏 → 0x08 位出现；两路同时超限时打印出 0x09。挪开后逐位消失。",
        "坑：① 位定义只写代码不写文档，过两天必忘，且板2/Qt 侧要跟着同一套位定义；② 用 `alarm += AL_TEMP` 而不是 `|=`，同一路连续超限会累加成别的位的值；③ 别把告警位混进环境帧的 TVOC 字段——板2 会把它当 ppb 解析成好几万的怪值。",
        "SYS_FRAME_SendShort / SYS_FRAME_Build / SYS_USART_Printf"
      ],
      [
        "处理任务：纯计算 + 不阻塞 + 统计计数",
        "处理是唯一一个“不碰硬件”的环节，把它独立成任务，才能在不动接线的情况下反复回放同一组数据、把算法调稳。",
        "处理任务从 `s_q_sensor` 收 `SensorMsg_t`（`portMAX_DELAY` 阻塞等），做完滤波/野值/换算/阈值后，把结果填进一个下发给组帧与显示的结构——注意不要直接调用 LoRa 发送（那是阶段 7 的发送任务的活，职责分开才好查问题）。同时维护统计：收到多少轮、拒绝多少野值、当前告警位。每轮末尾 `SYS_WDG_Heartbeat(1U)` 报到。\n\n→ 放在：s_q_sensor/s_q_result 进 ②；vTaskProcess 进 ④（改已有函数体）；两条 xQueueCreate 进 ⑤ 建队列段，先于建任务。",
        "/* 放在：s_q_sensor/s_q_result 进 ② */\nstatic QueueHandle_t s_q_sensor;    /* 采集 → 处理 */\nstatic QueueHandle_t s_q_result;    /* 处理 → 组帧（结构体，按值拷贝） */\n\nvoid vTaskProcess(void *pv)\n{\n    SensorMsg_t m;\n    ProcResult_t r;\n\n    (void)pv;\n    for (;;) {\n        if (xQueueReceive(s_q_sensor, &m, portMAX_DELAY) == pdPASS) {\n            memset(&r, 0, sizeof(r));\n            r.alarm = process_one(&m, &r);          /* 滤波+野值+换算+阈值 */\n            r.tick  = SYS_TICK_GetTick();\n\n            SYS_USART_Printf(SYS_USART_1, \"[处理] valid=0x%02X alarm=0x%02X T=%d.%02d\\r\\n\",\n                             (unsigned)m.valid, (unsigned)r.alarm,\n                             r.temp_x100 / 100, r.temp_x100 % 100);\n\n            (void)xQueueSend(s_q_result, &r, 0);    /* 满就丢，不阻塞 */\n            SYS_WDG_Heartbeat(1U);\n        }\n    }\n}",
        "给处理任务单独看一条证据：串口每轮一行 `[处理] valid=... alarm=... T=...`，且这一行出现的频率与采集任务一致（每 2 秒一行）。拔掉某颗器件后，这一行照常出现但对应 valid 位为 0——说明处理任务不会因为某一路挂了就停摆。",
        "坑：① 在 `portMAX_DELAY` 的收队列里做耗时计算（比如等 I2C）会把队列撑满导致采集任务丢数据，处理任务里不要有任何 `delay` 与硬件访问；② 忘记 `SYS_WDG_Heartbeat(1)` 会让看门狗在阶段 9 反复复位；③ 处理结果结构体如果用指针传给发送任务，出函数即失效。",
        "xQueueReceive / xQueueSend / SYS_WDG_Heartbeat / SYS_TICK_GetTick"
      ]
    ]
  },

  /* ======================= 板1 · 阶段 6 ======================= */
  {
    n: "🧭 板1 · 阶段 6｜协议组帧与 CRC：20 字节定长帧",
    m: "板1 项目引导",
    _b: "板1",
    d: "把处理好的 6 个整数按固定格式拼成 20 字节帧，算出 CRC16-MODBUS，并学会用库里的组帧/校验/拆包接口自检。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：ProcResult_t + s_q_result + vTaskComm 空壳。\n本阶段产出：20 字节帧 + CRC16 + Verify，并在 LoraRx 里自环验证能原样取回。\n下一阶段怎么用：阶段 7 把这套帧交给 E22 发出去；组帧函数之后一直复用。\n\n帧格式（《检测数据端设计.md》§5）：`AA 55 | CMD | LEN | DATA(12) | CRC16-MODBUS(2) | 55 AA`，整帧 20 字节。CMD = `SYS_FRAME_CMD_ENV` 0x01，LEN = `SYS_FRAME_ENV_LEN` 0x0C。数据段全部大端，段内偏移 `SYS_FRAME_ENV_TEMP_OFF` 0 / `HUMI_OFF` 2 / `PRESS_OFF` 4 / `LIGHT_OFF` 6 / `TVOC_OFF` 8 / `MQ135_OFF` 10，对应整帧 4/6/8/10/12/14；CRC 在整帧 16/17（**低字节在前**），帧尾 18/19。本阶段的产物：一个 `SYS_FRAME_BuildEnv` 组出来的帧，以及一段能证明“它自洽、能被重新拆开”的自我验收代码。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。",
    k: "板1 协议 组帧 20 字节 AA55 55AA CRC16 MODBUS CMD_ENV 0x01 ENV_LEN 0x0C 偏移 TEMP HUMI PRESS LIGHT TVOC MQ135 大端 低字节在前 状态机 重同步 SYS_FRAME",
    f: [
      [
        "认清 20 字节帧：偏移表与三个“别搞混”",
        "协议错一个字节，板2 那边就是全丢帧，而串口上看到的还是“发出去成功了”——最费时间的排查。",
        "把偏移表抄进你的协议笔记并与头文件逐项核对：帧头 `SYS_FRAME_HEAD_HI/LO`（0xAA 0x55 线上先发 AA）、CMD `SYS_FRAME_CMD_ENV`、LEN `SYS_FRAME_ENV_LEN`、数据段 6 个 `SYS_FRAME_ENV_*_OFF`、`SYS_FRAME_OVERHEAD` = 8（帧头 2 + CMD 1 + LEN 1 + CRC 2 + 帧尾 2），所以整帧 = LEN + 8 = 20。三个容易搞混的点：① 数据段偏移 0/2/4/… 是**相对数据段**，整帧里要加 4；② PRESS 是 hPa **×10**；③ CRC 低字节在前。\n\n→ 放在：#include 进 ①；本步是认知，不新增代码。",
        "#include \"sys_frame.h\"\n\n/* 协议速查（对照 sys_frame.h 的宏，不要凭记忆写） */\n/*  [0][1]  = 0xAA 0x55                    帧头 */\n/*  [2]     = SYS_FRAME_CMD_ENV      0x01  命令 */\n/*  [3]     = SYS_FRAME_ENV_LEN      0x0C  数据段 12 字节 */\n/*  [4..5]  = TEMP  int16  0.01 ℃          段内 0  */\n/*  [6..7]  = HUMI  int16  0.01 %RH        段内 2  */\n/*  [8..9]  = PRESS int16  0.1 hPa         段内 4  */\n/*  [10..11]= LIGHT uint16 lx              段内 6  */\n/*  [12..13]= TVOC  uint16 ppb             段内 8  */\n/*  [14..15]= MQ135 uint16 原始 ADC        段内 10 */\n/*  [16][17]= CRC16-MODBUS 低字节在前 */\n/*  [18][19]= 0x55 0xAA                    帧尾 */",
        "你能不看代码把偏移表默画出来（含 ×10、大端、CRC 低字节在前三点），并与 `sys_frame.h` 第 35~78 行逐条对上。",
        "最常见的错：把 `SYS_FRAME_ENV_PRESS_OFF`（4）当成整帧偏移 4 去写字段——整帧 4 是 TEMP 的起点。另一个错是把 CRC 写成高字节在前，板2 会报 `SYS_FRAME_ERR_CRC`。",
        "SYS_FRAME_CMD_ENV / SYS_FRAME_ENV_LEN / SYS_FRAME_OVERHEAD / SYS_FRAME_ENV_TEMP_OFF / SYS_FRAME_ENV_PRESS_OFF / SYS_FRAME_ENV_MQ135_OFF"
      ],
      [
        "用 SYS_FRAME_BuildEnv 组一帧（首选）",
        "库里已经把 12 字节大端拼装与套帧写好了，自己手拼只能多一处出错的机会；先用它把“正确帧长什么样”跑出来。",
        "`SYS_FRAME_BuildEnv(out, cap, temp_x100, humi_x100, press_x10, light, tvoc, mq135)` 返回整帧字节数（= 12 + 8 = 20），cap 至少 20，否则返回 0。参数全是定点整数（25.60℃ → 2560、60.30 %RH → 6030、1013.2 hPa → 10132）。组帧后**不要急着发**，先逐字节打印出来。\n\n→ 放在：临时验证放 ⑤；正式用法在 ④ vTaskComm 的 for 里。",
        "/* 放在：临时验证放 ⑤ */\nuint8_t  f[32];\nuint16_t n;\n\nn = SYS_FRAME_BuildEnv(f, sizeof(f), 2560, 6030, 10132, 320, 15, 812);\nSYS_USART_Printf(SYS_USART_1, \"frame n=%u\\r\\n\", (unsigned)n);\nfor (i = 0; i < n; i++) {\n    SYS_USART_Printf(SYS_USART_1, \"%02X \", (unsigned)f[i]);\n}\nSYS_USART_SendLine(SYS_USART_1, \"\");",
        "串口打出的 20 个字节应符合：`AA 55 01 0C 0A 00 17 8E 27 94 01 40 00 0F 03 2C xx xx 55 AA`（其中 0A00 = 2560、178E = 6030、2794 = 10132、0140 = 320、000F = 15、032C = 812），CRC 两个字节由库算出。n 必须是 20。",
        "`cap` 传小了（比如 `sizeof(f)` 用了 12）函数返回 0 而不发任何东西，容易误判成“函数没实现”；另一个坑：参数顺序是 temp/humi/press/light/tvoc/mq135，别把 light 和 tvoc 位置调换（上位机曲线会变成光照跑到 TVOC 曲线上）。",
        "SYS_FRAME_BuildEnv / SYS_FRAME_Build / SYS_USART_Printf"
      ],
      [
        "自己拼一遍 + 算 CRC，确认你懂每个字节",
        "只会调库函数，出问题时无法判断是“数据错”还是“CRC 错”；自己拼一次是最快的理解方式，也是板2/Qt 侧对照实现的基准。",
        "手拼：按偏移表把每个字段大端写入，然后对 `[2..15]`（CMD + LEN + 数据段，共 14 字节）算 CRC16-MODBUS（多项式 0xA001 右移版），低字节写 `[16]`、高字节写 `[17]`，最后收帧尾。板1 的 CRC 例程在 `sys_modbus.h`：`uint16_t SYS_MODBUS_Crc16(const uint8_t *buf, uint16_t len)`。\n\n→ 放在：put_be16 这类 static 辅助函数放 ④，必须在 vTaskComm 之前。",
        "/* 放在：put_be16 这类 static 辅助函数放 ④，必须在 vTaskComm 之前 */\n#include \"sys_modbus.h\"\n\nstatic void put_be16(uint8_t *p, uint16_t v)\n{\n    p[0] = (uint8_t)(v >> 8);        /* 大端：高字节在前 */\n    p[1] = (uint8_t)(v & 0xFFU);\n}\n\nuint8_t  f[20];\nuint16_t crc;\n\nmemset(f, 0, sizeof(f));\nf[0] = 0xAAU;  f[1] = 0x55U;\nf[2] = SYS_FRAME_CMD_ENV;\nf[3] = SYS_FRAME_ENV_LEN;\nput_be16(&f[4],  (uint16_t)temp_x100);     /* int16 补码按位写即可 */\nput_be16(&f[6],  (uint16_t)humi_x100);\nput_be16(&f[8],  (uint16_t)press_x10);\nput_be16(&f[10], light_lx);\nput_be16(&f[12], tvoc_ppb);\nput_be16(&f[14], mq135_raw);\n\ncrc = SYS_MODBUS_Crc16(&f[2], 14U);        /* 范围：CMD+LEN+数据段 */\nf[16] = (uint8_t)(crc & 0xFFU);            /* 低字节在前 */\nf[17] = (uint8_t)(crc >> 8);\nf[18] = 0x55U;  f[19] = 0xAAU;",
        "手拼的 20 字节与上一步 `SYS_FRAME_BuildEnv` 的输出应**逐字节相同**（同一组输入）。不一致就先比前 18 个字节找数据错，前 18 个相同而 CRC 不同就是 CRC 范围或字节序错。",
        "负温度：`temp_x100` 是 int16（-500），按位写进 uint16 是 0xFE0C，`put_be16(&f[4], (uint16_t)(-500))` 是对的；但如果写成 `(uint16_t)(-500.0f)` 先转 float 再转无符号，行为依编译器而定，必须在整数域完成转换。另一个坑：CRC 范围若误算成 `[0..15]`（含帧头），板2 永远校验不过。",
        "SYS_MODBUS_Crc16 / SYS_FRAME_BuildEnv"
      ],
      [
        "上板自检：SYS_FRAME_Verify 必须返回 0",
        "发送端自己算的 CRC，最容易“自证清白”的方式就是再用库的校验函数把整帧验一遍；这一步能在发送前拦掉绝大多数协议错。",
        "`SYS_FRAME_Verify(buf, len)` 返回 `SYS_FRAME_OK`（0）才算过，其他值含义：1 帧头错、2 帧尾错、3 长度字段与实际帧长不符（典型是两帧粘接）、4 CRC 错、5 参数非法。发送任务在 `LORA_E22_Send` 之前先 Verify 一次，失败就计数并丢弃，别发出去。\n\n→ 放在：放 ④ vTaskComm 内，组完帧立刻验。",
        "/* 放在：放 ④ vTaskComm 内，组完帧立刻验 */\nuint8_t r;\n\nif (SYS_FRAME_Verify(f, n) == SYS_FRAME_OK) {\n    SYS_USART_SendLine(SYS_USART_1, \"frame verify OK\");\n} else {\n    r = SYS_FRAME_Verify(f, n);\n    SYS_USART_Printf(SYS_USART_1, \"frame verify FAIL rc=%u\\r\\n\", (unsigned)r);\n}\n\n/* 也可用“整段 CRC = 0”当第二重判据：覆盖 CMD..CRC 自己再算一遍应为 0 */\nif (SYS_MODBUS_Crc16(&f[2], 16U) == 0U) {\n    SYS_USART_SendLine(SYS_USART_1, \"crc check OK\");\n}",
        "串口每帧一行 `frame verify OK`；故意把 `f[17]` 异或 0x01 再 Verify，应打印 `FAIL rc=4`（CRC 错）；故意把帧长传 19，应打印 `rc=3`（长度不符）。两种错都能被区分出来。",
        "把 `SYS_FRAME_Verify` 的返回值当布尔用（`if (SYS_FRAME_Verify(...))` 在成功时是 0 = 假）会写成“成功就报错”；要写 `== SYS_FRAME_OK`。另一个坑：`SYS_FRAME_Verify` 校验的是**整帧**，别把数据段指针传进去。",
        "SYS_FRAME_Verify / SYS_FRAME_OK / SYS_FRAME_ERR_CRC / SYS_FRAME_ERR_LEN / SYS_MODBUS_Crc16"
      ],
      [
        "发送前那一刻的组帧位置：处理结果 → 帧缓冲",
        "把组帧放在发送任务里、用处理任务给的结构体当输入，能让“处理结果”与“线上字节”一一对应；反过来在采集任务里组帧，会让排查时分不清是采集错还是组帧错。",
        "发送任务从 `s_q_result` 收 `ProcResult_t`，用 `SYS_FRAME_BuildEnv` 组帧（定点值直接来自结构体），Verify 后 `LORA_E22_Send(f, n)`。注意帧缓冲必须是**任务局部数组**或专用缓冲，不要用多个任务共享的全局缓冲（发送任务正在发，别的任务改写它 = 发出两帧拼接的怪帧）。\n\n→ 放在：直接改 ④ 已有的 vTaskComm 函数体，不要再写第二个 main 或新任务。",
        "/* 放在：直接改 ④ 已有的 vTaskComm 函数体，不要再写第二个 main 或新任务 */\nvoid vTaskComm(void *pv)\n{\n    ProcResult_t r;\n    uint8_t  f[32];\n    uint16_t n;\n\n    (void)pv;\n    for (;;) {\n        if (xQueueReceive(s_q_result, &r, portMAX_DELAY) == pdPASS) {\n            n = SYS_FRAME_BuildEnv(f, sizeof(f), r.temp_x100, r.humi_x100,\n                                   r.press_x10, r.light_lx, r.tvoc_ppb, r.mq135_raw);\n            if ((n == 20U) && (SYS_FRAME_Verify(f, n) == SYS_FRAME_OK)) {\n                /* 下一阶段在这里发出去 */\n                SYS_USART_Printf(SYS_USART_1, \"[组帧] 20B ok, tick=%u\\r\\n\",\n                                 (unsigned)SYS_TICK_GetTick());\n            } else {\n                SYS_USART_Printf(SYS_USART_1, \"[组帧] 失败 n=%u\\r\\n\", (unsigned)n);\n            }\n            SYS_WDG_Heartbeat(2U);\n        }\n    }\n}",
        "串口稳定的 `[组帧] 20B ok` 每 2 秒一行。把某路的值手工改成越界数（如 light 传 70000 被截断成 4464），Verify 仍然 OK 但数值变了——说明校验只能证明“字节自洽”，不能替代量程检查。",
        "`n` 用 uint16 接返回值，别用 uint8；`SYS_FRAME_BuildEnv` 在 cap < 20 时返回 0，检查 `n == 20U` 能顺带把这类错挡住。发送任务里不要调 `SYS_TICK_Init()`。",
        "SYS_FRAME_BuildEnv / SYS_FRAME_Verify / xQueueReceive / SYS_WDG_Heartbeat"
      ],
      [
        "收侧状态机与重同步：Feed/Poll/Get 怎么配",
        "帧尾 0x55AA 与下一帧帧头 0xAA55 连在一起是 `55 AA AA 55`——中间藏着一个裸的 `AA 55`。只查帧头两字节的解析器会在这里“找到”一个假帧头，然后整条流永久错位。",
        "库给了两条喂字节路径，**同一路串口只喂一边**：中断已把字节放进环形缓冲时用 `SYS_FRAME_Poll(uart)`（它在中断缓冲里找帧、喂状态机），或在你自己的 ISR 里逐字节 `SYS_FRAME_Feed(uart, byte)`。收完后用 `SYS_FRAME_Available(uart)` 判有无帧，再 `SYS_FRAME_Get(uart, &cmd, payload, cap, &len)` 取走数据段（**不含 CMD/LEN/CRC**）。错帧计数看 `SYS_FRAME_ErrCount(uart)`；链路重连或怀疑错位时 `SYS_FRAME_Reset(uart)` 丢掉半帧、重新找帧头。\n\n→ 放在：放 ④ vTaskLoraRx 内；cmd/payload/plen 都是函数内局部。",
        "/* 放在：放 ④ vTaskLoraRx 内 */\n/* 板1 用同一条路自环验证解析：把刚组好的帧 Feed 进去，应该能原样取回 */\nuint8_t  cmd;\nuint8_t  payload[SYS_FRAME_MAX_PAYLOAD];\nuint16_t plen;\nint16_t  t100, h100, p10;\nuint16_t lx, tv, mq;\n\nfor (i = 0; i < n; i++) {\n    (void)SYS_FRAME_Feed(SYS_USART_1, f[i]);\n}\nif (SYS_FRAME_Available(SYS_USART_1) != 0U) {\n    if (SYS_FRAME_Get(SYS_USART_1, &cmd, payload, sizeof(payload), &plen) == 0U) {\n        SYS_FRAME_UnpackEnv(payload, plen, &t100, &h100, &p10, &lx, &tv, &mq);\n        SYS_USART_Printf(SYS_USART_1, \"loopback T=%d.%02d P=%d.%01d light=%u\\r\\n\",\n                         t100 / 100, t100 % 100, p10 / 10, p10 % 10, (unsigned)lx);\n    }\n}\nSYS_USART_Printf(SYS_USART_1, \"err=%u\\r\\n\", (unsigned)SYS_FRAME_ErrCount(SYS_USART_1));",
        "自环测试应打印出与发送端一致的 T/P/light，`err` 计数不增长。再做一次“两帧连发”：把两帧首尾相接 Feed 进去，应能取出两帧、`err` 不变；把帧中间少喂一个字节（模拟丢字节），应取不出帧且 `err` 增长——说明重同步在干活。",
        "四个坑：① 同时用 Poll 和 Feed 喂同一路会各自维护游标，帧会丢；② `SYS_FRAME_Get` 的 `cap` 必填，传 0 时会一直返回“缓冲不够”并保留帧；③ `Get` 取出的 payload 是**数据段**，长度是帧内 LEN（12），不要拿它当整帧传给 `SYS_FRAME_Verify`；④ 别用 `SYS_USART_Available` 自己拼一个解析器绕过状态机——重同步逻辑就白写了。",
        "SYS_FRAME_Feed / SYS_FRAME_Poll / SYS_FRAME_Available / SYS_FRAME_Get / SYS_FRAME_UnpackEnv / SYS_FRAME_Reset / SYS_FRAME_ErrCount"
      ]
    ]
  },

  /* ======================= 板1 · 阶段 7 ======================= */
  {
    n: "🧭 板1 · 阶段 7｜LoRa 发送：把帧交给 E22",
    m: "板1 项目引导",
    _b: "板1",
    d: "把组好的 20 字节帧通过 E22 模块发出去：认准 UART、控制脚与模式，按节奏发、出错要能重试、别把模块灌爆。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：能组帧、能验帧。\n本阶段产出：E22 初始化、发送（AUX 判忙）、发送节奏、链路掉线判据。\n下一阶段怎么用：阶段 8 的显示任务直接读结果快照；阶段 9 靠本阶段的 tx_ok/tx_fail 做联调对账。\n\n本阶段的产物：发送任务里一句 `LORA_E22_Send(f, 20)` 能把帧发出去，并且你有一套“看得见”的验收方式（对着板2 或示波器/逻辑分析仪）。关键约束：LoRa 走 `SYS_LORA_UART` = `SYS_USART_3`（PB10/PB11），控制脚 M0/M1/AUX = PB0/PB1/PB2，波特率 `SYS_LORA_BAUD` = 9600 8N1；业务收发前模块必须处于 `LORA_E22_MODE_TRANSPARENT`。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。",
    k: "板1 LoRa E22 UART3 PB10 PB11 M0 PB0 M1 PB1 AUX PB2 9600 8N1 透传 配置模式 AT 发送节奏 帧间隔 丢包 超时 重试",
    f: [
      [
        "初始化与模式：先认模块，再切透传",
        "E22 的控制脚决定它当前是“透传收发的管子”还是“等你发 AT 的配置口”。模式不对，你发出去的 20 字节会被当 AT 指令字符串处理，板2 永远收不到帧。",
        "`LORA_E22_Init()` 会配置三根控制脚、按 `SYS_LORA_BAUD` 起 UART、并自检 AUX，返回 `LORA_E22_OK`(0) / `LORA_E22_ERR_AUX`(1) / `LORA_E22_ERR_NO_MODULE`(2)。初始化后可用 `LORA_E22_SetMode(LORA_E22_MODE_CONFIG)` 进配置模式、`LORA_E22_SendAT(\"AT\")` 试连通、`LORA_E22_ReadConfig` 核对地址/网络ID/信道，然后**必须** `LORA_E22_SetMode(LORA_E22_MODE_TRANSPARENT)` 切回透传，业务才能真正收发。\n\n→ 放在：#include 进 ①；LORA_E22_Init() 进 ⑤ 初始化段（只调一次）；cfg 这类缓冲区是临时局部，需要就放 ④。",
        "/* 放在：#include 进 ① */\n#include \"lora_e22.h\"\n\nchar    cfg[128];\nuint8_t r;\n\nr = LORA_E22_Init();\nSYS_USART_Printf(SYS_USART_1, \"E22 init=%u\\r\\n\", (unsigned)r);\n\nr = LORA_E22_SetMode(LORA_E22_MODE_CONFIG);\nSYS_USART_Printf(SYS_USART_1, \"to CONFIG=%u\\r\\n\", (unsigned)r);\n\nif (LORA_E22_SendAT(\"AT\") == LORA_E22_OK) {\n    SYS_USART_SendLine(SYS_USART_1, \"AT ok\");\n}\nmemset(cfg, 0, sizeof(cfg));\n(void)LORA_E22_ReadConfig(cfg, sizeof(cfg));\nSYS_USART_Printf(SYS_USART_1, \"cfg=%s\\r\\n\", cfg);\n\nr = LORA_E22_SetMode(LORA_E22_MODE_TRANSPARENT);\nSYS_USART_Printf(SYS_USART_1, \"back to TRANSPARENT=%u (0=ok)\\r\\n\", (unsigned)r);",
        "串口应依次看到 `E22 init=0`、`to CONFIG=0`、`AT ok`、一行配置字符串（含地址/信道/网络ID）、`back to TRANSPARENT=0`。两端互通要求地址、信道、网络ID 三项完全相同——把两边的 cfg 打印放一起对比。",
        "四个真实坑：① **不接天线不要发射**，会烧功放；② M0/M1 悬空会随机进配置模式，表现为“有时能发有时发不出去”，一定要焊好；③ 忘了切回透传是最常见的一次性问题；④ 两端波特率都要 9600（`LORA_E22_SetBaud` 改的是模块侧，改完本端 `SYS_USART_Init` 的波特率也要跟着改）。",
        "LORA_E22_Init / LORA_E22_SetMode / LORA_E22_SendAT / LORA_E22_ReadConfig / LORA_E22_SetBaud / LORA_E22_MODE_TRANSPARENT"
      ],
      [
        "发送：AUX 忙就别灌数据",
        "E22 内部有收发缓冲，AUX 高电平表示“模块正忙”。不等 AUX 就连续灌帧，超过缓冲后模块会丢字节，表现是“偶尔整帧错”或“板2 收到半帧”。",
        "发送前用 `LORA_E22_IsBusy()` 判断（1 = 忙），或用 `LORA_E22_WaitReady(timeout_ms)` 等就绪（内部按 `SYS_LORA_AUX_TIMEOUT_MS` = 1000ms 兜底）。就绪后 `LORA_E22_Send(f, 20)`：它是阻塞式逐字节写出，返回 `LORA_E22_OK`(0) 才算成功，`LORA_E22_ERR_PARAM`(4) 是空指针/零长度。发完别立刻再发第二帧——留帧间隔。\n\n→ 放在：放 ④ vTaskComm 内（WaitReady 判断 + Send）。",
        "/* 放在：放 ④ vTaskComm 内（WaitReady 判断 + Send） */\nuint8_t  r;\n\nif (LORA_E22_WaitReady(SYS_LORA_AUX_TIMEOUT_MS) == LORA_E22_OK) {\n    r = LORA_E22_Send(f, n);\n    if (r == LORA_E22_OK) {\n        s_tx_ok++;\n        SYS_USART_Printf(SYS_USART_1, \"[发送] 第 %u 帧已发出\\r\\n\", (unsigned)s_tx_ok);\n    } else {\n        s_tx_fail++;\n        SYS_USART_Printf(SYS_USART_1, \"[发送] 失败 r=%u 累计 %u\\r\\n\",\n                         (unsigned)r, (unsigned)s_tx_fail);\n    }\n} else {\n    s_tx_fail++;\n    SYS_USART_SendLine(SYS_USART_1, \"[发送] AUX 一直忙，本帧放弃\");\n}",
        "串口每 2 秒一行 `[发送] 第 N 帧已发出`，N 单调递增且与 LED 闪烁节奏一致。拔掉天线座旁的 AUX 线（或把 AUX 拉高）后，应看到 `AUX 一直忙，本帧放弃`，`s_tx_fail` 增长——说明忙判定真的在起作用。",
        "坑：① 拿 `LORA_E22_Send` 的返回值当“对方收到了”——它只表示本端串口把字节交给模块了；② 在 AUX 忙时硬发（忽略返回值循环重试）会把失败计数刷成天文数字却一帧没出去；③ 发送是阻塞的，20 字节 @9600 约 21ms，别在 1ms 软定时器里调用。",
        "LORA_E22_Send / LORA_E22_IsBusy / LORA_E22_WaitReady / SYS_LORA_AUX_TIMEOUT_MS"
      ],
      [
        "发送节奏：定周期 + 帧间隔，别把空中灌满",
        "板1 每 2 秒一帧已经远超“环境量变化速度”，发太密既没有信息量，又会把 LoRa 的空口占满、增加丢包与功耗。",
        "节奏用 RTOS 的 `vTaskDelay` 建立，不要用忙等；发送任务每轮：收队列 → 组帧 → Verify → 等 AUX → Send → `vTaskDelay(pdMS_TO_TICKS(SEND_PERIOD_MS))`。帧与帧之间至少留 `SYS_LORA_FRAME_GAP_MS`（10ms）量级的间隔（20 字节 @9600 本身约 21ms，加上空中时间，实际间隔会更大）。发送周期做成宏，方便后面调。\n\n→ 放在：SEND_PERIOD_MS 宏进 ②；周期在 ④ vTaskComm 里改。",
        "/* 放在：SEND_PERIOD_MS 宏进 ② */\n#define SEND_PERIOD_MS   2000U\n\nvoid vTaskComm(void *pv)\n{\n    ProcResult_t r;\n    uint8_t  f[32];\n    uint16_t n;\n\n    (void)pv;\n    for (;;) {\n        if (xQueueReceive(s_q_result, &r, portMAX_DELAY) == pdPASS) {\n            n = SYS_FRAME_BuildEnv(f, sizeof(f), r.temp_x100, r.humi_x100,\n                                   r.press_x10, r.light_lx, r.tvoc_ppb, r.mq135_raw);\n            if ((n == 20U) && (SYS_FRAME_Verify(f, n) == SYS_FRAME_OK)\n                && (LORA_E22_WaitReady(SYS_LORA_AUX_TIMEOUT_MS) == LORA_E22_OK)) {\n                if (LORA_E22_Send(f, n) == LORA_E22_OK) { s_tx_ok++; } else { s_tx_fail++; }\n            } else {\n                s_tx_fail++;\n            }\n            SYS_WDG_Heartbeat(2U);\n        }\n        vTaskDelay(pdMS_TO_TICKS(SEND_PERIOD_MS));   /* 2s 一帧，节奏靠它 */\n    }\n}",
        "串口/上位机统计：一分钟内 `s_tx_ok` 增加约 30 次（2s 一帧），不是几百次。把 `SEND_PERIOD_MS` 改成 100 试一下，能看到发送计数暴涨、板2 侧坏帧计数同时上升——这正说明过密会丢帧。",
        "坑：① 在 RTOS 里用 `SYS_TICK_Delay_ms` 忙等代替 `vTaskDelay` 会占着 CPU，低优先级任务饿死（库在 RTOS 模式下虽然不再死等，但仍然不如 vTaskDelay）；② 忘了在任务里 `SYS_WDG_Heartbeat(2U)`，阶段 9 开狗后会被复位；③ 不要为了“更实时”把周期设成 50ms —— LoRa 的空中速率与信道占用不支持这种密度。",
        "LORA_E22_Send / LORA_E22_WaitReady / vTaskDelay / SYS_WDG_Heartbeat / SYS_LORA_FRAME_GAP_MS"
      ],
      [
        "丢包与超时：自己造一个“没发成功”的判据",
        "LoRa 是单向广播式透明传输：本端发成功 ≠ 对端收到。必须有一条自己的“对端还活着”的判据，否则网断了你这边还在 happily 打印 tx_ok +1。",
        "三个手段，按需组合：① 计数：`s_tx_ok` / `s_tx_fail` 在监控任务里定期打印，失败率持续 > 5% 就要查天线/信道/距离；② 心跳对拍：让板2 收到帧后回一个短帧（如 `SYS_FRAME_SendShort(SYS_LORA_UART, 0x0F)`），板1 用 `SYS_FRAME_Poll(SYS_LORA_UART)` + `SYS_FRAME_Get` 收它，超过 N 秒没收到就点亮告警 LED；③ 超时：用 `SYS_TICK_Timeout` 或 `SYS_TICK_Elapsed` 记录“上次收到对端帧的时刻”。\n\n→ 放在：s_last_rx_tick/s_link_down 进 ②（跨任务跨周期保持）；轮询放 ④ vTaskLoraRx。",
        "/* 放在：s_last_rx_tick/s_link_down 进 ②（跨任务跨周期保持） */\nstatic uint32_t s_last_rx_tick;\nstatic uint8_t  s_link_down;\n\n/* 收对端心跳（放在发送任务里或单独的接收轮询里） */\nif (SYS_FRAME_Poll(SYS_LORA_UART) > 0U) {\n    uint8_t  cmd;\n    uint8_t  pl[SYS_FRAME_MAX_PAYLOAD];\n    uint16_t plen;\n    if (SYS_FRAME_Get(SYS_LORA_UART, &cmd, pl, sizeof(pl), &plen) == 0U) {\n        s_last_rx_tick = SYS_TICK_GetTick();\n        s_link_down = 0U;\n    }\n}\n\n/* 10 秒没听到对端 = 链路断 */\nif (SYS_TICK_Timeout(s_last_rx_tick, 10000U)) {\n    if (s_link_down == 0U) {\n        s_link_down = 1U;\n        LED_On(3);\n        SYS_USART_SendLine(SYS_USART_1, \"[链路] 10s 未收到对端，LED3 告警\");\n    }\n}\n\nSYS_USART_Printf(SYS_USART_1, \"tx_ok=%u tx_fail=%u err=%u\\r\\n\",\n                 (unsigned)s_tx_ok, (unsigned)s_tx_fail,\n                 (unsigned)SYS_FRAME_ErrCount(SYS_LORA_UART));",
        "人为把板2 断电（或拔掉板2 天线），10 秒内板1 的 LED3 点亮、串口打出 `[链路] 10s 未收到对端`；板2 上电后 LED3 熄灭、不再打印。这段时间 `s_tx_ok` 仍在涨——正好证明“发成功 ≠ 对端收到”。",
        "坑：① 把 `LORA_E22_Send` 的成功当“对方收到”是这类系统最典型的错觉；② 用 `SYS_FRAME_Poll` 收对端帧前，该串口必须已 `SYS_USART_InitRxIT`（字节靠中断进环形缓冲），否则 Poll 永远拿不到字节；③ 别在同一个任务里既阻塞发送又轮询接收，发送的 20ms 阻塞期间对端的回包靠中断缓冲兜住，缓冲不够就会丢——回包要短。",
        "SYS_FRAME_Poll / SYS_FRAME_Get / SYS_FRAME_ErrCount / SYS_FRAME_SendShort / SYS_TICK_Timeout / SYS_USART_InitRxIT / LED_On"
      ],
      [
        "调试出口：把“线上字节”与“本地数值”并排打印",
        "LoRa 出问题时，你分不清是“本地数据错”“组帧错”还是“模块没发出去”。把三者并排打出来，一次就能定位到层。",
        "发送任务里每帧打印三样：① 处理后的定点值（本地数值）；② 帧的 20 个字节（十六进制，线上字节）；③ 发送返回码与统计（模块结果）。再配一条“对端回执”打印（如果实现了心跳）。这四行是联调阶段最值钱的东西，别嫌刷屏——联调完再关。\n\n→ 放在：打印语句放 ④ vTaskComm，跟发送同一处，别另开任务。",
        "/* 放在：打印语句放 ④ vTaskComm，跟发送同一处，别另开任务 */\nSYS_USART_Printf(SYS_USART_1, \"[值]  T=%d.%02d H=%d.%02d P=%d.%01d L=%u V=%u M=%u\\r\\n\",\n                 r.temp_x100 / 100, r.temp_x100 % 100,\n                 r.humi_x100 / 100, r.humi_x100 % 100,\n                 r.press_x10 / 10,  r.press_x10 % 10,\n                 (unsigned)r.light_lx, (unsigned)r.tvoc_ppb,\n                 (unsigned)r.mq135_raw);\n\nSYS_USART_SendString(SYS_USART_1, \"[帧]  \");\nfor (i = 0; i < n; i++) {\n    SYS_USART_Printf(SYS_USART_1, \"%02X \", (unsigned)f[i]);\n}\nSYS_USART_SendLine(SYS_USART_1, \"\");\n\nSYS_USART_Printf(SYS_USART_1, \"[发]  n=%u tx_ok=%u tx_fail=%u\\r\\n\",\n                 (unsigned)n, (unsigned)s_tx_ok, (unsigned)s_tx_fail);",
        "串口每次发送前三行成组出现，且第三行 `n=20`。把 `[帧]` 那行的第 8、9 字节与 `[值]` 行的 P 对照（1013.2 hPa → 2794 十进制 → 0x0AEA），能用计算器验证一致。",
        "坑：① 逐字节 `SYS_USART_Printf` 打印 20 个字节会占用串口几十毫秒，联调时别把这一手留在 2 秒周期的正式版本里（改成打摘要或按需开关）；② `SYS_USART_Printf` 的 `%02X` 参数要用 `(unsigned)` 转换，`uint8_t` 会被提升成 int，多数实现没问题但显式转换更安全。",
        "SYS_USART_Printf / SYS_USART_SendString / SYS_USART_SendLine / LORA_E22_Send"
      ]
    ]
  },

  /* ======================= 板1 · 阶段 8 ======================= */
  {
    n: "🧭 板1 · 阶段 8｜本地显示与交互：OLED、按键、蜂鸣器",
    m: "板1 项目引导",
    _b: "板1",
    d: "把处理结果交给 OLED 显示任务、用 4 个按键翻页、超限时用蜂鸣器报警；界面排版由 AI 完成，你负责数据通路、按键事件与验收。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：帧能发、结果结构体齐全。\n本阶段产出：显示任务（读快照 + 脏标志）、按键翻页、蜂鸣器告警、显示任务的优先级与周期。\n下一阶段怎么用：阶段 9 长跑时看这些任务的水位与栈；不再新增业务任务，只加统计与验收。\n\n本阶段的产物：一个独立的显示任务——它从共享快照里取最新一行数据、按页显示、按键翻页、告警时响蜂鸣器。硬件事实（照库里的宏）：OLED 走 I2C1，SCL = PB8、SDA = PB9，地址 `SYS_OLED_I2C_ADDR` = 0x3C，初始化 `SYS_OLED_Init(SYS_I2C_1, 0x3C)`；按键 `KEY1` = PA0、`KEY2` = PE2、`KEY3` = PE3、`KEY4` = PE4（`KEY_Scan` 返回 id 0~3，无按键返回 `KEY_NONE`）；蜂鸣器 `BEEP_PORT` = GPIOF、`BEEP_PIN` = GPIO_Pin_8。**注意：I2C2 的 PB10/PB11 与 LoRa 的 USART3 同脚，OLED 只能用 I2C1。**\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。",
    k: "板1 OLED SSD1306 I2C1 PB8 PB9 0x3C SYS_OLED_Init ShowString ShowNum RefreshDirty 按键 KEY1 PA0 KEY2 PE2 KEY3 PE3 KEY4 PE4 KEY_Scan 蜂鸣器 PF8 BEEP 显示任务 翻页 验收",
    f: [
      [
        "OLED 初始化与“屏亮了”的最小验收",
        "屏不亮是显示环节的第一道坎，而且原因极多（地址、上拉、供电、器件被占）。先用最小代码把“屏能亮、能写一个字”确认下来，再谈排版。",
        "确认接线：VCC→3.3V、GND→GND、SCL→PB8、SDA→PB9，SDA/SCL 各 4.7k 上拉。`SYS_OLED_Init(SYS_I2C_1, 0x3C)` 会自己 `SYS_I2C_Init(SYS_I2C_1, 400000)` 并下发 SSD1306 命令表、清屏，返回 0 = 成功、1 = 器件无应答。注意 `SYS_OLED_Init` 的第二参数是 7 位地址，模块焊成 0x3D 时要传 0x3D（或改 `SYS_OLED_I2C_ADDR`）。写字符后必须调刷新函数才上屏。\n\n→ 放在：#include 进 ①；SYS_OLED_Init 进 ⑤ 初始化段。",
        "/* 放在：#include 进 ① */\n#include \"sys_oled.h\"\n\nuint8_t r;\n\nr = SYS_OLED_Init(SYS_I2C_1, 0x3C);\nSYS_USART_Printf(SYS_USART_1, \"OLED init=%u\\r\\n\", (unsigned)r);\nif (r == 0U) {\n    SYS_OLED_Clear();\n    SYS_OLED_ShowString(0, 0, \"BOARD1 SENSOR\");\n    SYS_OLED_ShowString(2, 0, \"T=\");\n    SYS_OLED_ShowFloat(2, 24, 25.60f, 2);\n    SYS_OLED_Refresh();          /* 改显存后必须上屏 */\n}\nSYS_USART_Printf(SYS_USART_1, \"oled err_count=%u last=%u\\r\\n\",\n                 (unsigned)SYS_OLED_ErrCount(), (unsigned)SYS_OLED_LastErr());",
        "OLED 第一行出现 `BOARD1 SENSOR`，第三行出现 `T=25.60`。串口 `OLED init=0`。若屏不亮但 init 返回 0，看 `SYS_OLED_ErrCount()`：它在涨 = 写入失败（查上拉/地址/总线被占），恒 0 = 写成功但屏侧问题（查供电与对比度/接线）。",
        "坑：① 只调 `ShowString` 不调 `SYS_OLED_Refresh`/`RefreshDirty`，屏上永远没反应（显示函数只改内存缓冲）；② 把 OLED 挂到 I2C2（PB10/PB11）会与 LoRa 的 USART3 抢脚，表现为 LoRa 或屏有一个不工作；③ 整帧 `SYS_OLED_Refresh` 是 1024 字节、@400kHz 约 23~25ms，放进 1ms 定时器或高优先级任务会拖垮别的东西。",
        "SYS_OLED_Init / SYS_OLED_Clear / SYS_OLED_ShowString / SYS_OLED_ShowFloat / SYS_OLED_Refresh / SYS_OLED_ErrCount / SYS_OLED_LastErr"
      ],
      [
        "数据交给显示任务：共享快照 + 脏标志",
        "显示任务不能直接去读传感器（一个任务一份硬件访问会把 I2C 与 DHT11 的时序搅在一起），也不能每次都全屏重画（23ms × 每秒一次，白占总线）。",
        "用一个全局快照结构 + 一个“有新数据”的脏标志把处理结果交给显示任务：处理任务写完快照后置标志，显示任务看到标志才重绘，重绘完清标志。快照的读写用简单的进入/退出临界区保护（`taskENTER_CRITICAL()` / `taskEXIT_CRITICAL()`），保证显示任务不会读到“温度写了一半”的中间态。显示任务优先级**低于**采集/处理/发送，仅在 CPU 空闲时刷新。\n\n→ 放在：s_disp_dirty/s_snap 进 ②（跨任务共享，加 volatile）；写快照在 ④ vTaskProcess，读在 ④ 显示任务。",
        "/* 放在：s_disp_dirty/s_snap 进 ②（跨任务共享，加 volatile） */\nstatic volatile uint8_t   s_disp_dirty;\nstatic ProcResult_t        s_snap;      /* 处理结果快照 */\n\n/* 处理任务侧：写完快照置脏 */\ntaskENTER_CRITICAL();\ns_snap = r;\ns_disp_dirty = 1U;\ntaskEXIT_CRITICAL();\n\n/* 显示任务侧：只在有新数据时重绘 */\nvoid vTaskDisplay(void *pv)\n{\n    ProcResult_t s;\n    (void)pv;\n    for (;;) {\n        if (s_disp_dirty != 0U) {\n            taskENTER_CRITICAL();\n            s = s_snap;\n            s_disp_dirty = 0U;\n            taskEXIT_CRITICAL();\n\n            SYS_OLED_ShowNum(2, 24, s.temp_x100 / 100);   /* 界面排版由 AI 给 */\n            (void)SYS_OLED_RefreshDirty();                /* 只推脏页，快得多 */\n        }\n        vTaskDelay(pdMS_TO_TICKS(200));\n    }\n}",
        "改动任意一路数据（比如手捂温度探头），OLED 上的数值在 1 秒内跟着变；把传感器全部拔掉，显示任务照旧刷屏（显示的还是上一帧/无效标记），不会卡死。用逻辑分析仪看 I2C1：只有脏页被写，整屏刷新不会每次都出现。",
        "坑：① 两个任务同时访问 I2C1 必须靠 `sys_i2c` 的每总线互斥锁（OLED 内部已用），你在外面再套一层 `SYS_I2C_Lock(SYS_I2C_1)` 只在“多条 I2C 操作必须原子”时才需要，套错会死锁；② 脏标志用 `volatile`，必须用临界区而不是靠“它看起来原子”；③ 显示任务里不要调 `SYS_OLED_Refresh()` 全屏刷新 + 高优先级，会挤掉采集任务。",
        "SYS_OLED_ShowNum / SYS_OLED_RefreshDirty / SYS_OLED_MarkAllDirty / SYS_I2C_Lock / SYS_I2C_Unlock / taskENTER_CRITICAL"
      ],
      [
        "按键翻页：KEY_Scan 取事件、只在事件时换页",
        "用 `KEY_Read` 每轮轮询电平会把“按住”当成连续事件，翻页停不下来；而长按与抖动又需要消抖。库的 `KEY_Scan` 正好是“按下瞬间只报一次”。",
        "先 `KEY_Init()`（内部按 `KEY_PULL` 配上拉、按 `KEY_ACTIVE_LOW` 适配极性），再在显示任务或一个专门的按键轮询里调 `KEY_Scan()`：返回本次新按下的 id（0~3），无事件返回 `KEY_NONE`（0xFF）。按 id 切页：KEY1 下一页、KEY2 上一页、KEY3 回到首页、KEY4 手动清告警。长按可以用 `KEY_LongPress(id, hold_ms)`（前提是先用 `KEY_Scan` 拿到按下事件）。\n\n→ 放在：PAGE_NUM 宏进 ②；page/k 放 ④ 按键任务内局部，不建议塞回 main。",
        "/* 放在：PAGE_NUM 宏进 ② */\n#include \"key.h\"\n\n#define PAGE_NUM  3U\n\nuint8_t page = 0U;\nuint8_t k;\n\nKEY_Init();\n\nfor (;;) {\n    k = KEY_Scan();                 /* 有消抖，只在按下瞬间返回一次 */\n    if (k != KEY_NONE) {\n        BEEP_KeySound();            /* 按键提示音，50ms */\n        if (k == 0U) {              /* KEY1 = PA0 */\n            page = (uint8_t)((page + 1U) % PAGE_NUM);\n        } else if (k == 1U) {       /* KEY2 = PE2 */\n            page = (uint8_t)((page + PAGE_NUM - 1U) % PAGE_NUM);\n        } else if (k == 2U) {       /* KEY3 = PE3，回首页 */\n            page = 0U;\n        }\n        SYS_USART_Printf(SYS_USART_1, \"key id=%u page=%u\\r\\n\",\n                         (unsigned)k, (unsigned)page);\n        s_disp_dirty = 1U;          /* 让显示任务重绘 */\n    }\n    vTaskDelay(pdMS_TO_TICKS(20));\n}",
        "按 KEY1：串口打印 `key id=0 page=1`，OLED 换到第 2 页；连按 3 次回到第 1 页（0→1→2→0）。按 KEY2 反向。手指按住不放：只打印一次（不连发）。同时蜂鸣器每按一下短响一声。",
        "坑：① 一次按键被识别成多次 = 用了 `KEY_Read` 或没消抖，`KEY_Scan` 内部有 `KEY_DEBOUNCE_MS`(10ms) 且命中时阻塞 10ms，别在 1ms 定时器里调；② PA0 同时是 ADC1_IN0，如果你按阶段 4 的示例把 MQ-135 挂在 PA0 上，按键会失效/读数乱跳——这也是 MQ-135 必须自己选一个**不冲突**通道的原因；③ PE2/PE3/PE4 与 FSMC/LCD 等复用功能无关，但别把它们再配成别的外设。",
        "KEY_Init / KEY_Scan / KEY_Read / KEY_LongPress / KEY_NONE / BEEP_KeySound"
      ],
      [
        "蜂鸣器告警：告警位驱动，带确认与静音",
        "报警声必须是“可解释”的：什么算告警由阶段 5 的告警位决定，蜂鸣器只是执行器；没有静音手段的报警在现场会被人直接拔线。",
        "`BEEP_Init()` 后有两种用法：阻塞式 `BEEP_Beep(times)` / `BEEP_BeepEx(times, on_ms, off_ms)`（内部按 `BEEP_ON_MS_DEFAULT`/`BEEP_OFF_MS_DEFAULT` 各 100ms 鸣叫），以及**非阻塞**的 `BEEP_AsyncStart(times, on_ms, off_ms)` + `BEEP_Update()`（必须每 1ms 调一次）+ `BEEP_AsyncStop()`。告警场景用非阻塞那套，另配一个 `s_buzzer_mute` 标志（KEY4 切换）。注意 `BEEP_ACTIVE_LOW` 决定了有效电平，库里自动适配，你只管调函数。\n\n→ 放在：s_mute 进 ②；BEEP_Update() 必须周期调，放 ④ 对应任务循环里（1ms 或最小周期任务）。",
        "/* 放在：s_mute 进 ② */\n#include \"beep.h\"\n\nstatic uint8_t s_mute;\n\nBEEP_Init();\n\n/* 每 1ms 调一次（软定时器任务或最小周期任务） */\nBEEP_Update();\n\nif ((s_snap.alarm != 0U) && (s_mute == 0U)) {\n    BEEP_AsyncStart(3U, 100U, 100U);      /* 响 3 声，非阻塞 */\n} else {\n    BEEP_AsyncStop();                     /* 立即停 + 静音 */\n}\n\n/* KEY4（PE4）切换静音并在屏上标出来 */\nif (k == 3U) {\n    s_mute = (uint8_t)(s_mute ^ 1U);\n    SYS_OLED_ShowString(7, 0, s_mute ? \"MUTE\" : \"    \");\n    s_disp_dirty = 1U;\n}",
        "造一个真实告警（把温度阈值临时调到当前温度以下），蜂鸣器按 100ms 响/停循环；按 KEY4 后蜂鸣器立刻停、OLED 上出现 `MUTE`；再按一下恢复。移走告警源后告警位清掉，声音也跟着停。",
        "坑：① `BEEP_AsyncStart` 不调 `BEEP_Update()` 就永远不响（很多人以为它自己会响）；② `BEEP_Update` 在任务里用 `vTaskDelay(pdMS_TO_TICKS(1))` 驱动在 `configTICK_RATE_HZ=1000` 时才准，频率不够会有节奏误差，可接受；③ 别在中断里调 `BEEP_Beep` 这种阻塞函数；④ 无源蜂鸣器用这套函数不响——这套是给**有源**蜂鸣器的通断控制（要出频率得靠定时器 PWM）。",
        "BEEP_Init / BEEP_Update / BEEP_AsyncStart / BEEP_AsyncStop / BEEP_BeepEx / BEEP_ACTIVE_LOW"
      ],
      [
        "显示任务的验收清单（AI 的界面怎么算接对了）",
        "界面排版是 AI 给的，你要能判断“接没接对”——判据必须与你写的数据通路一一对应，否则出问题无法归因。",
        "把验收拆成四条可观察项：① **数据一致**：OLED 上 6 个值都能在串口同一轮打印里找到同一个数（用同一份快照）；② **刷新节流**：数值变化后 1 秒内上屏，且 I2C 上不是每秒都在整屏刷新；③ **无效可见**：某路读失败时，屏幕上显示占位（如 `--`）而不是 0 或旧值；④ **告警可见**：告警位非零时屏幕上有明确标记（颜色反显/`!`/`ALM`）并与蜂鸣器同步。四条都能说清“我怎么看到的”。\n\n→ 放在：放 ④ 显示任务里（按 valid 位决定显示数值还是占位符）。",
        "/* 放在：放 ④ 显示任务里（按 valid 位决定显示数值还是占位符） */\n/* 无效值不打成 0 的显示写法：占位符 */\nif ((s.valid & (1U << 0)) != 0U) {\n    SYS_OLED_ShowNum(2, 24, s.temp_x100 / 100);\n} else {\n    SYS_OLED_ShowString(2, 24, \"--\");\n}\n\n/* 告警标记：和蜂鸣器同源（都取 s.alarm） */\nif (s.alarm != 0U) {\n    SYS_OLED_ShowString(7, 96, \"ALM\");\n}",
        "拔掉 DHT11 后 OLED 对应位置显示 `--`（不是 0、也不是上一次的 25.6）；造告警后同屏出现 `ALM` 且蜂鸣器响；连续观察 1 分钟，I2C1 上不出现“每秒整屏刷新”的规律波形。",
        "坑：① 显示用的数与串口打印的数取自**不同**来源（一个读快照、一个读局部变量）时，会看到两边数值偶尔不同，验收时无法归因——统一用快照；② 无效值用 0 占位，现场会被当成“温度 0℃”误判；③ 告警标记不要把蜂鸣器的 `s_mute` 当数据源（静音了但仍在告警，标记必须还在）。",
        "SYS_OLED_ShowString / SYS_OLED_ShowNum / SYS_OLED_RefreshDirty / SYS_OLED_ErrCount"
      ],
      [
        "显示任务的调度位置：优先级、周期与看门狗",
        "显示是全系统里最“耗时且不重要”的活：I2C 整屏 23ms、局部刷新每页 3ms。它的优先级放错，会直接拖慢采集与 LoRa 发送。",
        "建任务时给显示任务**最低优先级**（比采集 3、处理 3、发送 4、监控 5 都低，比如 2），栈 512 字起（`SYS_STR_Format` 与 OLED 帧缓冲都要栈）。周期用 `vTaskDelay(pdMS_TO_TICKS(200))` 轮询脏标志，不要忙等；它**不参与**看门狗心跳汇总（心跳数只算采集/处理/发送 3 个），所以在 `SYS_WDG_Heartbeat` 里不要给它分配 id。\n\n→ 放在：xTaskCreate 调用进 ⑤ 建任务段，跟其它任务排在一起，调度器之前。",
        "/* 放在：xTaskCreate 调用进 ⑤ 建任务段，跟其它任务排在一起，调度器之前 */\n(void)xTaskCreate(vTaskSensor,  \"Sensor\",  512, NULL, 3, NULL);\n(void)xTaskCreate(vTaskProcess, \"Process\", 512, NULL, 3, NULL);\n(void)xTaskCreate(vTaskComm,    \"Comm\",    512, NULL, 4, NULL);\n(void)xTaskCreate(vTaskDisplay, \"Display\", 512, NULL, 2, NULL);   /* 最低 */\n(void)xTaskCreate(vTaskMonitor, \"Monitor\", 256, NULL, 5, NULL);   /* 最高 */\n\n/* 心跳只报采集(0)/处理(1)/发送(2)，显示与监控不占位 */",
        "用 Keil 的 RTX/FreeRTOS 任务列表或自己打印 `uxTaskGetNumberOfTasks()`/各任务运行计数，能看到 Display 任务的运行时间占比明显低于 Sensor/Comm；把 Display 优先级临时提到 4 再观察，采集周期会开始抖动——反证优先级设置的作用。",
        "坑：① 显示任务栈给小了（256）会在 `SYS_OLED_ShowFloat` 或格式化时爆栈，现象是随机跑飞或进 HardFault（阶段 9 的 `SYS_FAULT_*` 能抓到）；② 给显示任务也调 `SYS_WDG_Heartbeat(3)` 会让心跳汇总多一格而库里 `SYS_WDG_HEARTBEAT_COUNT` 没改，报到范围对不上；③ OLED 初始化必须在调度器起来之前或一个任务里做一次，别在多个任务里都调 `SYS_OLED_Init`。",
        "xTaskCreate / uxTaskPriorityGet / SYS_WDG_HEARTBEAT_COUNT / SYS_WDG_Heartbeat"
      ]
    ]
  },

  /* ======================= 板1 · 阶段 9 ======================= */
  {
    n: "🧭 板1 · 阶段 9｜联调验收：与板2 对接、跑 30 分钟",
    m: "板1 项目引导",
    _b: "板1",
    d: "把板1 与板2 接起来跑通整条链路，开看门狗与故障现场记录，连续跑 30 分钟看水位与丢帧，最后按清单逐条签收。",
    g: "【从哪来 · 到哪去】\n上一阶段交给你的：全部业务任务已就位并各自跑通。\n本阶段产出：联调序号、统计打印、看门狗报到、故障现场、逐条验收清单。\n下一阶段怎么用：换到板2 工程做接收侧；板1 的 20 字节帧格式与队列口径保持不变，不许私改。\n\n本阶段的产物不是新功能，而是**证据**：板2 能收到板1 的每一帧并转发给上位机（Qt 曲线连续、无断点）；连续 30 分钟运行不重启、不卡死、丢帧率在可接受范围；一旦硬件层出问题能留下现场（`SYS_FAULT_*`）与复位来源（`SYS_WDG_ResetCause`）。这一阶段最忌讳“看着还行就算过了”——每条验收都要能指出“这一行打印/这一条曲线/这一个计数器”作为证据。\n\n【代码放哪 · 按你 main.c 的五个分区】\n区块：① 引用定义区（只放 #include）｜② 宏定义与全局声明区（#define + 任务间共享的变量/句柄/计数器）｜③ 结构体定义声明区（typedef struct/enum/union）｜④ 任务函数区（任务函数 + 只在本文件用的 static 辅助函数，必须写在 main 之前）｜⑤ main 函数（初始化 → 建队列/互斥量 → 建任务 → vTaskStartScheduler()，调度器永远最后）\n判断口诀：类型进③、共享变量进②、函数进④、调用进⑤、include 进①；凡在 main 之后新写的函数，xTaskCreate 都会报未定义。",
    k: "板1 联调验收 板2 对接 上位机 Qt 30 分钟 水位 丢帧 看门狗 SYS_WDG 心跳 SYS_FAULT 故障现场 复位原因 验收清单",
    f: [
      [
        "两板对接：帧能从板1 到板2 并转发上位机",
        "单板自测全过不等于链路通：两端的 LoRa 参数、协议版本、转发的串口选择都可能不一致，必须在“端到端”这一层验一次。",
        "按顺序验三层：① **空口通**：板1 每 2 秒发一帧，板2 的 `LORA_E22_Recv` 能收到（板2 侧打印收到字节数）；② **解析通**：板2 解析出 `SYS_FRAME_OK` 并打印 T/H/P 等 6 个值，与板1 串口打印的 `[值]` 行一致；③ **转发通**：板2 通过 USART2（PA2/PA3）把原始帧转发给上位机，Qt 侧曲线连续且数值与两端打印一致。三层的证据分别落在三块屏上：板1 串口、板2 串口、Qt 界面。\n\n→ 放在：s_seq 进 ②；打印放 ④ vTaskComm。",
        "/* 放在：s_seq 进 ② */\n/* 板1 侧：给每帧打一个递增序号，联调时用来数丢帧 */\nstatic uint16_t s_seq;\n\nSYS_USART_Printf(SYS_USART_1, \"[发] seq=%u T=%d.%02d P=%d.%01d\\r\\n\",\n                 (unsigned)s_seq, r.temp_x100 / 100, r.temp_x100 % 100,\n                 r.press_x10 / 10, r.press_x10 % 10);\ns_seq++;\n\n/* 板2 侧：对齐检查（板2 的代码由板2 手册负责，这里只用来看对照） */\n/* 板1 seq=100 ↔ 板2 收到的 seq 必须连续递增，缺号就是空口丢帧 */",
        "板1 `[发] seq=N` 与板2/上位机侧的序号一一对应、连续递增；Qt 曲线上能看到温度随环境变化而不是一条直线。三处同一时刻的 T 值一致（允许 1 个采样周期的滞后）。",
        "坑：① 两端 LoRa 的信道/网络ID/地址不同是最常见的“收不到”，而两端各自的自检都会通过；② 板2 转发用 USART2 时注意 PA2 与以太网 ETH_MDIO 同脚（本工程未用以太网，暂不冲突），别在板2 上同时启用；③ Qt 侧如果把带序号的自定义字节当成协议字段解析会乱——序号要么进协议、要么只在调试打印里出现，别混。",
        "LORA_E22_Send / LORA_E22_Recv / SYS_FRAME_Verify / SYS_FRAME_UnpackEnv / SYS_USART_Printf"
      ],
      [
        "连续跑 30 分钟：看水位、数丢帧、记重启",
        "偶发问题（内存泄漏、栈溢出、队列长期积压、DHT11 偶发失败累积）只在长时间运行时才暴露，跑 30 分钟是最低成本的暴露手段。",
        "跑之前先把统计做成“可读的一行”：`tick`（运行时长）、`tx_ok`/`tx_fail`、采集轮数、DHT11 失败次数、野值拒绝次数、队列水位（用 `uxQueueMessagesWaiting` 看采集队列与结果队列是否长期积压）。每 10 秒打一行。30 分钟后把首行与末行对比：失败率、水位是否随时间上升。**水位持续上升 = 处理比采集慢**，这是最常见的设计错。\n\n→ 放在：统计打印语句放 ④ Monitor，别在 main 里打。",
        "/* 放在：统计打印语句放 ④ Monitor，别在 main 里打 */\n/* FreeRTOS 队列水位 */\nSYS_USART_Printf(SYS_USART_1,\n    \"[统计] tick=%u tx_ok=%u tx_fail=%u q_sensor=%u q_result=%u dht_fail=%u reject=%u\\r\\n\",\n    (unsigned)SYS_TICK_GetTick(), (unsigned)s_tx_ok, (unsigned)s_tx_fail,\n    (unsigned)uxQueueMessagesWaiting(s_q_sensor),\n    (unsigned)uxQueueMessagesWaiting(s_q_result),\n    (unsigned)s_dht_fail, (unsigned)s_reject_t);\n\ndelay_ms_dwt(10000);   /* 仅在调度器未启动的自检阶段用；任务里改用 vTaskDelay */",
        "30 分钟后：`tick` 单调增长到约 1800000（30 分钟 = 1800 秒）；`tx_fail` 增长很慢（< 5%）；`q_sensor` / `q_result` 在 0~1 之间波动而**不是**稳定在满值 4；DHT11 失败次数占比 < 5%；全程 `SYS_WDG_ResetCause()` 里没有新的 `SYS_WDG_RST_IWDG`。",
        "坑：① 统计打印本身会占串口时间（115200 下每行几十字节约几毫秒），别每 100ms 打一次；② `uxQueueMessagesWaiting` 在中断里调用不安全，只在任务里用；③ 30 分钟里如果某个 LED 停止闪烁但数据还在流，往往是那个任务的 `SYS_WDG_Heartbeat` 漏了——等到开狗后就会复位，能提前发现就提前改。",
        "SYS_TICK_GetTick / uxQueueMessagesWaiting / SYS_USART_Printf / vTaskDelay"
      ],
      [
        "开看门狗：心跳汇总 + 报到纪律",
        "前面几个阶段都没开狗（自检要十几秒，提前开会复位），联调阶段必须开：它是“某个任务卡死”唯一能自动恢复的手段。",
        "两件事：① 启动调度器**之后**（或紧邻之前）调 `SYS_WDG_Init(APP_WDG_TIMEOUT_MS)`，本工程取的 4000ms 足够覆盖 DHT11 最长 8.4ms 关中断 + 2.2s 重试间隔；② 每个参与汇总的任务在自己的循环里调 `SYS_WDG_Heartbeat(id)`，监控任务调 `SYS_WDG_HeartbeatPoll()`。注意 `SYS_WDG_HEARTBEAT_COUNT` 在 `sys_wdg.h` 里默认是 **0**——填 0 时 `HeartbeatPoll` 退化为直接喂狗，你要的模式甲需要把该宏改成参与汇总的任务数（3），并且**改宏会影响全库语义**，改前想清楚。\n\n→ 放在：APP_WDG_TIMEOUT_MS 进 ②；SYS_WDG_Init 进 ⑤ 初始化段；各任务的心跳调用放各自 ④ 任务循环里。",
        "/* 放在：APP_WDG_TIMEOUT_MS 进 ② */\n/* 板1：三个人报到（采集=0 / 处理=1 / 发送=2），监控任务只 Poll */\n/* sys_wdg.h: #define SYS_WDG_HEARTBEAT_COUNT 3 */\n\n#define APP_WDG_TIMEOUT_MS  4000U\n\nSYS_WDG_Init(APP_WDG_TIMEOUT_MS);\n\n/* 采集任务里 */\nSYS_WDG_Heartbeat(0U);\n/* 处理任务里 */\nSYS_WDG_Heartbeat(1U);\n/* 发送任务里 */\nSYS_WDG_Heartbeat(2U);\n\n/* 监控任务（优先级最高）里，每 1s 一轮 */\nif (SYS_WDG_HeartbeatPoll() != 0U) {\n    /* 全员到齐，Poll 内部已喂狗 */\n} else {\n    SYS_USART_Printf(SYS_USART_1, \"[监控] 有任务没报到 Pending=0x%08X\\r\\n\",\n                     (unsigned)SYS_WDG_HeartbeatPending());\n}",
        "验收要能**故意造一次卡死**：在采集任务里插入 `for (;;) { }`（或用调试器暂停该任务），4 秒内板子应复位；复位后串口第一段 `SYS_WDG_ResetCause()` 解码里出现 `SYS_WDG_RST_IWDG`（独立看门狗复位）。删掉死循环后，连续跑 30 分钟不再复位。",
        "坑：① 在任务里调 `SYS_TICK_Init()` 会破坏 RTOS 的时基（这是本工程的硬约束，任何阶段都不许），时基乱了心跳节奏也跟着乱；② 三个任务报到 id 重复（比如监控也报 2）会让某个任务卡死时掩码仍能凑齐、狗喂上——卡死不被复位；③ 把 `SYS_WDG_HEARTBEAT_COUNT` 改成 3 后，`SYS_WDG_Heartbeat(id)` 传 3 以上会被忽略，别多报。",
        "SYS_WDG_Init / SYS_WDG_Heartbeat / SYS_WDG_HeartbeatPoll / SYS_WDG_HeartbeatPending / SYS_WDG_HeartbeatClear / SYS_WDG_ResetCause / SYS_WDG_RST_IWDG"
      ],
      [
        "故障现场：SYS_FAULT_* 留下 PC 与 CFSR",
        "偶发跑飞（数组越界、空指针、除零、栈溢出）在串口上往往只表现为“突然重启”，不留任何线索；故障模块能把出错指令地址与内核状态存下来。",
        "`SYS_FAULT_Init()` 在 `main` 开头调一次（它清零记录、按宏使能故障细分与除零陷阱）；`SYS_FAULT_SetCallback(cb)` 注册一个报警回调，回调里只能做最轻的事——把 `SYS_FAULT_Report(buf, size)` 生成的一行现场写进串口或置标志，**绝不能在异常上下文里调 RTOS API 或延时**。事后在 Keil Watch 里看全局 `SYS_FAULT_Record`（`pc` / `cfsr` / `hfsr` / `count`），用 `pc` 反查源码行。\n\n→ 放在：s_fault_buf 进 ②；fault_cb 是回调函数放 ④；SYS_FAULT_Init/SetCallback 进 ⑤ 初始化段。",
        "/* 放在：s_fault_buf 进 ② */\n#include \"sys_fault.h\"\n\nstatic char s_fault_buf[128];\n\nstatic void fault_cb(void)\n{\n    /* 异常上下文：只做“记录现场”这件事 */\n    (void)SYS_FAULT_Report(s_fault_buf, sizeof(s_fault_buf));\n    SYS_USART_SendLine(SYS_USART_1, \"--- FAULT ---\");\n    SYS_USART_SendLine(SYS_USART_1, s_fault_buf);\n}\n\n/* main 开头 */\nSYS_FAULT_Init();\nSYS_FAULT_SetCallback(fault_cb);\n\n/* 事后检查 */\nif (SYS_FAULT_Happened() != 0U) {\n    SYS_USART_Printf(SYS_USART_1, \"fault count=%u pc=0x%08X cfsr=0x%08X\\r\\n\",\n                     (unsigned)SYS_FAULT_Record.count,\n                     (unsigned)SYS_FAULT_Record.pc,\n                     (unsigned)SYS_FAULT_Record.cfsr);\n    SYS_FAULT_Clear();\n}",
        "故意制造一次故障（临时写 `int a = 1, b = 0; vol = a / b;`，前提是 `SYS_FAULT_DIV0_TRAP = 1`），串口应出现 `--- FAULT ---` 与一行现场报告；Keil Watch 里 `SYS_FAULT_Record.type` 非 0、`pc` 指到那一行附近。删除后 `SYS_FAULT_Happened()` 返回 0。",
        "坑：① 回调里调 `vTaskDelay`/`xQueueSend` 会二次故障或死锁，只能用寄存器级/无阻塞操作；② `SYS_FAULT_Report` 只给一行纯 ASCII，别指望它给完整调用栈；③ 默认 `SYS_FAULT_AUTO_RESET = 0`（停在死循环等调试器），现场用要自己权衡是否改成自动复位；④ 若你在调试器里用“暂停并查看”抓过故障，记得 `SYS_FAULT_Clear()`，否则 `count` 一直是旧值。",
        "SYS_FAULT_Init / SYS_FAULT_SetCallback / SYS_FAULT_Happened / SYS_FAULT_Report / SYS_FAULT_Clear / SYS_FAULT_Record / SYS_FAULT_TYPE_USAGE"
      ],
      [
        "验收清单：逐条签收，不许“差不多”",
        "没有清单的验收一定会漏项，而且漏的往往是最关键的“多久没重启”“断线后能不能自恢复”。",
        "按下面清单逐条打勾，每条都要能指出证据位置（串口行 / LED / OLED / 上位机曲线 / 计数器）：① 6 路传感器独立成功/失败可辨，无效不打进帧；② 帧长恒为 20、`SYS_FRAME_Verify` 恒返回 `SYS_FRAME_OK`；③ 板2 收到并转发，Qt 曲线连续；④ LoRa 两端参数一致、`tx_fail` < 5%；⑤ 按键翻页不连发、长按有语义；⑥ OLED 上无效值显示占位而不是 0；⑦ 告警有回差、不抖、可静音；⑧ 连续 30 分钟不重启、队列水位不随时间上升；⑨ 看门狗能复位卡死任务（已实测）；⑩ 故障现场能抓到并打印 PC。\n\n→ 放在：汇总打印放 ④ Monitor，一行签收摘要跟统计一起打。",
        "/* 放在：汇总打印放 ④ Monitor，一行签收摘要跟统计一起打 */\n/* 把清单压成一行“签收摘要”，跟着统计一起打 */\nSYS_USART_Printf(SYS_USART_1,\n    \"[签收] frame20=%u verify_ok=%u tx_fail=%u qmax=%u wdg_rst=%u fault=%u\\r\\n\",\n    (unsigned)s_frame20_ok, (unsigned)s_verify_ok, (unsigned)s_tx_fail,\n    (unsigned)s_qmax, (unsigned)SYS_WDG_ResetCause(),\n    (unsigned)SYS_FAULT_Happened());",
        "清单上 10 条全部有对应证据；`[签收]` 行里 `frame20` 与发送次数同量级、`verify_ok` 与之一致、`wdg_rst` 不再出现新的看门狗位、`fault=0`。有一条打不了勾就如实记下来，作为下一轮的工作项，而不是把阈值放宽凑过去。",
        "坑：① 把“看到数据在动”当验收 = 没有阈值、没有异常路径的验证；② 用放宽阈值让告警不响，等于把功能删掉；③ 忘记记录“哪一次是复现出来的、怎么复现的”，下一轮重新踩一遍；④ 调试期临时加的强制置位/死循环一定在签收前删干净（否则交给别人跑就是随机复位）。",
        "SYS_FRAME_Verify / SYS_WDG_ResetCause / SYS_FAULT_Happened / SYS_USART_Printf"
      ]
    ]
  }
];

/* 追加进总手册（data_build.js 已定义 STD_BUILD_GUIDE；本文件只 push，不重新定义） */
if (typeof STD_BUILD_GUIDE !== "undefined") { STD_BUILD_GUIDE.push.apply(STD_BUILD_GUIDE, STD_BUILD_B1); }
