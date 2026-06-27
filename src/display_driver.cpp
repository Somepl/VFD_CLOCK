/**
 * display_driver.cpp — 数码管硬件驱动层 实现
 *
 * 74HC595 3线SPI + 硬件定时器 PWM（ISR 安全）
 */

#include "display_driver.h"
#include <ShiftRegister74HC595.h>
#include <driver/gpio.h>
#include "config.h"

// ============================================================
// 硬件实例（模块内全局）
// ============================================================

static ShiftRegister74HC595<4> sr(PIN_DATA, PIN_CLK, PIN_LATCH);

// ============================================================
// 七段数码管段码表0-9 + H段
// 段位定义：a-g + h，共阳极（0=亮 1=灭）
// 位映射: bit7=A  bit6=B  bit5=C  bit4=D  bit3=E  bit2=F  bit1=G  bit0=H
// ============================================================

const uint8_t SEGMENTS[10] = {
    B00000011,  // 0
    B10011111,  // 1
    B00100101,  // 2
    B00001101,  // 3
    B10011001,  // 4
    B01001001,  // 5
    B01000001,  // 6
    B00011111,  // 7
    B00000001,  // 8
    B00001001   // 9
};

/** 全灭（所有段 = 不亮）*/
const uint8_t SEGMENT_BLANK = B11111111;

// ============================================================
// PWM 共享缓冲区
// ============================================================

volatile uint8_t pwmSegs[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
volatile uint8_t pwmBrightness = DISPLAY_BRIGHTNESS_DEFAULT;
uint8_t lastSegments[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};

// ============================================================
// 写入接口
// ============================================================

void segs_write(const uint8_t segs[4]) {
    memcpy((void*)pwmSegs, segs, 4);
    memcpy(lastSegments, segs, 4);
}

void display_force_off() {
    uint8_t off[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
    memcpy((void*)pwmSegs, off, 4);
}

void write_all_off() {
    uint8_t off[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
    segs_write(off);
}

// ============================================================
// 字符 → 7段段码映射（共阳极）
// ============================================================

uint8_t char_to_segments(unsigned char c) {
    if (c >= '0' && c <= '9') return SEGMENTS[c - '0'];
    switch (c) {
        case 'A': return 0x11;
        case 'P': return 0x31;
        case '-': return 0xFD;   // G 段（中间横杠）
        case 0xB0: return 0x39;  // ° 符号
        case ' ': return SEGMENT_BLANK;
        default:  return SEGMENT_BLANK;
    }
}

// ============================================================
// 数字写入（不含动画）
// ============================================================

void write_digits_static(uint8_t d[4]) {
    uint8_t segs[4];
    for (int i = 0; i < 4; i++) {
        segs[i] = (d[i] <= 9) ? SEGMENTS[d[i]] : SEGMENT_BLANK;
    }
    segs_write(segs);
}

// ============================================================
// 74HC595 快速移位操作（ISR 安全，直接 GPIO 寄存器写入）
// DATA→GPIO14(低32位), CLK→GPIO33(高32位), LATCH→GPIO32(高32位)
// ============================================================

static void IRAM_ATTR isr_shift_byte(uint8_t val) {
    for (int i = 7; i >= 0; i--) {
        gpio_set_level(GPIO_NUM_14, (val >> i) & 1);  // DATA
        gpio_set_level(GPIO_NUM_33, 1);                // CLK ↑
        gpio_set_level(GPIO_NUM_33, 0);                // CLK ↓
    }
}

static void IRAM_ATTR isr_write_segments(const uint8_t segs[4]) {
    // 顺序管4→管3→管2→管1(MSBFIRST)
    isr_shift_byte(segs[3]);
    isr_shift_byte(segs[2]);
    isr_shift_byte(segs[1]);
    isr_shift_byte(segs[0]);
    gpio_set_level(GPIO_NUM_32, 1);  // LATCH ↑
    gpio_set_level(GPIO_NUM_32, 0);  // LATCH ↓
}

// ============================================================
// 硬件定时器 PWM（计数式，定时器自动重载 50µs → 20kHz）
// 100 ticks = 1 PWM 周期 (5ms = 200Hz)
//   tick 0: 输出段码（点亮）
//   tick = brightness: 输出全灭（熄灭），brightness=100 时永不熄灭
//   100 ticks 后重复
// ============================================================

static uint8_t pwm_tick = 0;
static hw_timer_t *pwmTimer = NULL;

static void IRAM_ATTR pwm_timer_isr(void) {
    pwm_tick++;
    if (pwm_tick >= 100) {
        pwm_tick = 0;
        if (pwmBrightness > 0) {
            uint8_t segs[4];
            segs[0] = pwmSegs[0];
            segs[1] = pwmSegs[1];
            segs[2] = pwmSegs[2];
            segs[3] = pwmSegs[3];
            isr_write_segments(segs);
        } else {
            uint8_t blank[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
            isr_write_segments(blank);
        }
    } else if (pwm_tick == pwmBrightness) {
        uint8_t blank[4] = {SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK, SEGMENT_BLANK};
        isr_write_segments(blank);
    }
}

// ============================================================
// 初始化
// ============================================================

void display_driver_init() {
    pwmTimer = timerBegin(0, 80, true);  // timer0, APB/80=1MHz→1µs/tick
    timerAttachInterrupt(pwmTimer, pwm_timer_isr, true);
    timerAlarmWrite(pwmTimer, 50, true);  // 50µs 自动重载 → 20kHz
    timerAlarmEnable(pwmTimer);
}
