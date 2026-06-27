/**
 * display_anim.cpp — 数码管动画引擎 实现
 */

#include "display_anim.h"
#include <Preferences.h>

// ============================================================
// 天气动画帧数据
// 4 位数码管 x 7段+h，每帧 = 4 个字节
// 共阳极：0=亮 1=灭
// ============================================================

/** Sunshine 双帧太阳（H 段在光芒帧亮） */
const uint8_t ANIM_SUNNY[2][4] = {
    {B01101100, B01100010, B00001110, B01101100},  // sun1：带光芒的太阳 + H亮
    {B11111111, B01100011, B00001111, B11111111},  // sun2：光芒熄灭 + H灭
};

/** Raining 雨滴级联（H 段在中间帧亮起模拟水花） */
const uint8_t ANIM_RAINY[6][4] = {
    {B10111111, B10111111, B10111111, B10111111},  // rain1：左上段
    {B11011110, B11011110, B11011110, B11011110},  // rain2：中间 + H亮（水花）
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 间歇
    {B11111011, B11111011, B11111011, B11111011},  // rain3：右下段
    {B11011110, B11011110, B11011110, B11011110},  // rain4：中间 + H亮
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 间歇
};

/** 爱心滚动 */
const uint8_t LOVE_FRAMES[6][4] = {
    {B11100011, B00000011, B11000111, B01100001},
    {B11111111, B11100011, B00000011, B11000111},
    {B11111111, B11111111, B11100011, B00000011},
    {B01100001, B11111111, B11111111, B11100011},
    {B11000111, B01100001, B11111111, B11111111},
    {B00000011, B11000111, B01100001, B11111111},
};

/** 笑脸 & Smile 眨眼 */
const uint8_t ANIM_SMILE[2][4] = {
    {B11000101, B00111011, B00111011, B11000101},  // 笑脸
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 闭眼
};

/** 哭脸 & Sad 眨眼 */
const uint8_t ANIM_SAD[2][4] = {
    {B11010111, B00111001, B00111001, B11010111},  // 哭脸
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 闭眼
};

/** 无表情 Nol 眨眼 */
const uint8_t ANIM_NOL[2][4] = {
    {B11111111, B01000100, B01000100, B11111111},  // 无表情
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 闭眼
};

/** 多云飘移 4 帧 */
const uint8_t ANIM_CLOUDY[4][4] = {
    {B01111110, B00111111, B11111111, B11111111},
    {B11111111, B01111110, B00111111, B11111111},
    {B11111111, B11111111, B01111110, B00111111},
    {B11111111, B11111111, B11111111, B11111111},
};

/** 雪花飘落 4 帧 */
const uint8_t ANIM_SNOW[4][4] = {
    {B11111110, B11111111, B11111111, B11111111},
    {B11111111, B11111110, B11111111, B11111111},
    {B11111111, B11111111, B11111110, B11111111},
    {B11111111, B11111111, B11111111, B11111110},
};

/** 雷阵雨闪电 4 帧 */
const uint8_t ANIM_THUNDER[4][4] = {
    {B00000000, B00000000, B00000000, B00000000},  // 全亮（闪电闪白）
    {B00000001, B00000001, B00000001, B00000001},  // A~G亮、H灭
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 全灭（黑暗）
    {ALL_OFF,   ALL_OFF,   ALL_OFF,   ALL_OFF   },  // 全灭（等待）
};

/** 默认扫描 4 帧 */
const uint8_t ANIM_DEFAULT[4][4] = {
    {B01111110, B11111111, B11111111, B11111111},
    {B11111111, B01111110, B11111111, B11111111},
    {B11111111, B11111111, B01111110, B11111111},
    {B11111111, B11111111, B11111111, B01111110},
};

// ============================================================
// 内置动画名称
// ============================================================

const char* BUILTIN_NAMES[10] = {
    "Sunshine",  // 0
    "Raining",   // 1
    "Love",      // 2
    "Smile",     // 3
    "Sad",       // 4
    "Nol",       // 5
    "Cloudy",    // 6
    "Snow",      // 7
    "Thunder",   // 8
    "Default",   // 9
};

// ============================================================
// 动画状态变量
// ============================================================

uint8_t lastDigits[4] = {0xFF, 0xFF, 0xFF, 0xFF};
bool firstRefresh = true;

bool animRunning = false;
bool animComplete = false;
uint8_t animTarget[4];
uint8_t animCurrent[4];
int animPos;
int animFrame;
unsigned long animLastStep;

uint8_t weatherAnimType = WEATHER_ANIM_NONE;
unsigned long weatherAnimStartTime = 0;
int16_t weatherDisplayTemp = 0;
uint8_t animCycleIndex = 0;
int16_t lastAnimTemp = 0;

bool wthrOvValid = false;
uint8_t wthrOvData[WTHR_OV_MAX_FRAMES][4];
uint16_t wthrOvDur[WTHR_OV_MAX_FRAMES];
uint8_t wthrOvCount = 0;
uint8_t wthrOvIdx = 0;
unsigned long wthrOvLastMs = 0;

uint8_t webAnimType = 0;
unsigned long webAnimStartTime = 0;

uint8_t userPatternData[4] = {0xFF, 0xFF, 0xFF, 0xFF};

UserAnimFrame userAnimBuffer[USER_ANIM_MAX_FRAMES];
uint8_t userAnimFrameCount = 0;
uint8_t userAnimFrameIdx = 0;
unsigned long userAnimFrameStart = 0;

// ============================================================
// 天气类型 → 内置动画索引
// ============================================================

const uint8_t weatherToBuiltinIdx[] = {
    0,  // 0: NONE
    0,  // 1: SUN → builtin 0
    6,  // 2: CLOUD → builtin 6
    1,  // 3: RAIN → builtin 1
    7,  // 4: SNOW → builtin 7
    8,  // 5: THUNDER → builtin 8
    9,  // 6: DEFAULT → builtin 9
};

// ============================================================
// 翻页动画启动
// ============================================================

void anim_start(uint8_t target[4]) {
    animRunning = true;
    animComplete = false;
    animPos = 0;
    animFrame = 0;
    animLastStep = millis();
    memcpy(animTarget, target, 4);

    for (int i = 0; i < 4; i++) {
        animCurrent[i] = (lastDigits[i] <= 9) ? lastDigits[i] : 10;
    }

    while (animPos < 4 && animCurrent[animPos] == animTarget[animPos]) {
        animPos++;
    }

    if (animPos >= 4) {
        animComplete = true;
        animRunning = false;
        memcpy(lastDigits, target, 4);
    }
}

// ============================================================
// 天气动画覆写加载
// ============================================================

bool wthr_load_override(uint8_t builtinIdx) {
    Preferences prefs;
    prefs.begin("builtin", true);
    String val = prefs.getString(("ov" + String(builtinIdx)).c_str(), "");
    prefs.end();
    if (val.length() == 0) return false;

    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, val);
    if (err != DeserializationError::Ok) return false;

    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0) return false;

    wthrOvCount = 0;
    for (JsonVariant fv : arr) {
        if (wthrOvCount >= WTHR_OV_MAX_FRAMES) break;
        JsonObject f = fv.as<JsonObject>();
        JsonArray d = f["data"].as<JsonArray>();
        if (d.size() != 4) continue;
        for (int i = 0; i < 4; i++) {
            wthrOvData[wthrOvCount][i] = d[i].as<uint8_t>();
        }
        wthrOvDur[wthrOvCount] = f["duration"].as<uint16_t>();
        if (wthrOvDur[wthrOvCount] < 50) wthrOvDur[wthrOvCount] = 50;
        wthrOvCount++;
    }
    if (wthrOvCount == 0) return false;
    wthrOvIdx = 0;
    wthrOvLastMs = millis();
    wthrOvValid = true;
    return true;
}

// ============================================================
// 帧序列导出
// ============================================================

bool frames_to_json(JsonArray &out, const uint8_t (*frames)[4],
                    uint8_t count, uint16_t ms) {
    for (uint8_t i = 0; i < count; i++) {
        JsonObject f = out.createNestedObject();
        JsonArray d = f["data"].to<JsonArray>();
        for (int j = 0; j < 4; j++) {
            d.add(frames[i][j]);
        }
        f["duration"] = ms;
    }
    return true;
}

// ============================================================
// 内置动画默认数据查询
// ============================================================

bool display_get_builtin_default_frames(uint8_t builtinIdx, JsonArray &frames) {
    switch (builtinIdx) {
        case 0: return frames_to_json(frames, ANIM_SUNNY,  2, 500);
        case 6: return frames_to_json(frames, ANIM_CLOUDY, 4, 300);
        case 1: return frames_to_json(frames, ANIM_RAINY,  6, 200);
        case 7: return frames_to_json(frames, ANIM_SNOW,   4, 300);
        case 8: return frames_to_json(frames, ANIM_THUNDER,4, 200);
        case 9: return frames_to_json(frames, ANIM_DEFAULT,4, 300);
        case 2: return frames_to_json(frames, LOVE_FRAMES, 6, 300);
        case 3: return frames_to_json(frames, ANIM_SMILE,  2, 400);
        case 4: return frames_to_json(frames, ANIM_SAD,    2, 400);
        case 5: return frames_to_json(frames, ANIM_NOL,    2, 400);
        default: return false;
    }
}
