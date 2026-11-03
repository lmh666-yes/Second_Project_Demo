#ifndef BOARD_SELFTEST_H
#define BOARD_SELFTEST_H

/* 阶段自检（A4）—— 由 工具箱/检查/构建烧录/gen_selftest.py 生成，别手改。
   用法：
     1) 把 board_selftest.c / .h 加进 Keil 工程（右键分组 → Add Existing Files）
     2) main() 里串口初始化之后调一次 BOARD_SELFTEST_Run();
     3) 串口 115200 会打 STAGE<N>=PASS / FAIL，A2 的 --serial 按这个判
   下面几个开关按需要置 0/1：接不了的阶段置 0，免得一直 FAIL。 */

#define BOARD_SELFTEST_STAGE2 1
#define BOARD_SELFTEST_STAGE3 1
#define BOARD_SELFTEST_STAGE6 1
#define BOARD_SELFTEST_STAGE7 0

void BOARD_SELFTEST_Run(void);

#endif /* BOARD_SELFTEST_H */
