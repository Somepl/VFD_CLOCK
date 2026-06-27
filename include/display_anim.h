/**
 * display_anim.h — 数码管动画引擎
 *
 * 职责：
 *   - 天气/内置/用户动画帧数据
 *   - 非阻塞翻页动画状态机
 *   - 天气动画覆写加载
 */

#ifndef DISPLAY_ANIM_H
#define DISPLAY_ANIM_H

#include <Arduino.h>
#include <ArduinoJson.h>

// ============================================================
// 内置动画帧数据（extern，由 display_anim.cpp 定义）
// ============================================================

/** 全灭 = 所有段熄灭 */
#define ALL_OFF 0xFF

extern const uint8_t ANIM_SUNNY[2][4];
extern const uint8_t ANIM_RAINY[6][4];
extern const uint8_t LOVE_FRAMES[6][4];
extern const uint8_t ANIM_SMILE[2][4];
extern const uint8_t ANIM_SAD[2][4];
extern const uint8_t ANIM_NOL[2][4];
extern const uint8_t ANIM_CLOUDY[4][4];
extern const uint8_t ANIM_SNOW[4][4];
extern const uint8_t ANIM_THUNDER[4][4];
extern const uint8_t ANIM_DEFAULT[4][4];

// ============================================================
// 动画状态（全局访问）
// ============================================================

/** 最后显示的时间数字（用于检测变化触发翻页动画）*/
extern uint8_t lastDigits[4];
extern bool firstRefresh;

// 非阻塞翻页动画状态
extern bool animRunning;
extern bool animComplete;
extern uint8_t animTarget[4];
extern uint8_t animCurrent[4];
extern int animPos;
extern int animFrame;
extern unsigned long animLastStep;

// 天气动画状态
#define WEATHER_ANIM_NONE    0
#define WEATHER_ANIM_SUN     1
#define WEATHER_ANIM_CLOUD   2
#define WEATHER_ANIM_RAIN    3
#define WEATHER_ANIM_SNOW    4
#define WEATHER_ANIM_THUNDER 5
#define WEATHER_ANIM_DEFAULT 6

extern uint8_t weatherAnimType;
extern unsigned long weatherAnimStartTime;
extern int16_t weatherDisplayTemp;
extern uint8_t animCycleIndex;
extern int16_t lastAnimTemp;
#define WEATHER_ANIM_DURATION  3000

// 天气动画覆写缓冲
extern bool wthrOvValid;
#define WTHR_OV_MAX_FRAMES 30
extern uint8_t wthrOvData[WTHR_OV_MAX_FRAMES][4];
extern uint16_t wthrOvDur[WTHR_OV_MAX_FRAMES];
extern uint8_t wthrOvCount;
extern uint8_t wthrOvIdx;
extern unsigned long wthrOvLastMs;

// 网页触发的纯动画展示状态
extern uint8_t webAnimType;
extern unsigned long webAnimStartTime;
#define WEB_ANIM_DURATION  5000

// 用户自定图案/动画播放状态
extern uint8_t userPatternData[4];
#define PATTERN_DISPLAY_MS  6000

struct UserAnimFrame {
    uint8_t data[4];
    uint16_t duration;
};
#define USER_ANIM_MAX_FRAMES 30
extern UserAnimFrame userAnimBuffer[USER_ANIM_MAX_FRAMES];
extern uint8_t userAnimFrameCount;
extern uint8_t userAnimFrameIdx;
extern unsigned long userAnimFrameStart;

// ============================================================
// 动画函数
// ============================================================

/** 启动非阻塞翻页动画（对比 lastDigits 与 target，对变化的位逐帧推进）*/
void anim_start(uint8_t target[4]);

/** 检查内置动画是否有用户覆写；若有则填充 wthrOv* 缓冲 */
bool wthr_load_override(uint8_t builtinIdx);

/** 将硬编码帧数组转为 JsonArray 帧序列格式 */
bool frames_to_json(JsonArray &out, const uint8_t (*frames)[4],
                    uint8_t count, uint16_t ms);

/** 获取内置动画的硬编码默认帧数据 */
bool display_get_builtin_default_frames(uint8_t builtinIdx, JsonArray &frames);

/** 内置动画名称列表 */
extern const char* BUILTIN_NAMES[10];

/** 天气动画类型 → 内置动画索引映射 */
extern const uint8_t weatherToBuiltinIdx[];

#endif // DISPLAY_ANIM_H
