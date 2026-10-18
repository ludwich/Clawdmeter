#pragma once
#include <stdint.h>

// The data the generated scene headers (scene_office.h, scene_outdoor.h) are
// made of. tools/scenes/scenekit.py writes them from the scene scripts in
// tools/scenes/; scene.cpp draws them. Everything is const, so it lives in
// flash and costs no RAM.
//
// A scene is a list of flat rectangles drawn in order (later on top) on a
// 320x172 canvas (scaled for other panels). Each names a palette index; the
// palette has a day, dusk and night variant. A rectangle shows at the times
// in its `tod` mask and belongs to one group: 0 is static, others drift,
// cycle as a frame of an animation, or stand for something the engine draws
// live in their place: a clock's hands, an LED countdown, the burndown chart.

#define SCENE_W        320
#define SCENE_H        172
#define SCENE_FLOOR_Y  162      // Clawd's feet line (stage.cpp feet_y at 172 px)

#define SCENE_TOD_DAY   1
#define SCENE_TOD_DUSK  2
#define SCENE_TOD_NIGHT 4

enum SceneGroupType : uint8_t {
    SG_STATIC = 0,
    SG_DRIFT  = 1,   // p = speed (px per 10 s, signed), wrap x0, wrap x1 for its left edge
    SG_FRAME  = 2,   // p = period ms, phase ms; frame k of n
    SG_CLOCK  = 3,   // p = centre x, centre y, radius; its one rect is the hands' slot
    SG_DIGITS = 4,   // p = digit w, digit h, stroke; box = "H:MM"; its one rect (lit colour) is the slot
    SG_CHART  = 5,   // p = cell px | (no-data colour + 1) << 8, area colour, now colour;
                     //     box = the plot; its one rect
                     //     (the line colour) is the slot
};

struct SceneRect {
    int16_t  x, y;
    uint16_t w, h;
    uint8_t  c;      // palette index
    uint8_t  tod;    // SCENE_TOD_* mask
    uint8_t  g;      // group index (0 = static)
};

struct SceneGroup {
    uint8_t  type;   // SceneGroupType
    uint8_t  n, k;   // SG_FRAME: frame k of n
    int16_t  p[3];   // per type, above
    int16_t  box[4]; // x, y, w, h of its rects as authored
};

enum SceneAnchorId { SCENE_ANCHOR_DESK, SCENE_ANCHOR_SIGN, SCENE_ANCHOR_COUNT };

struct SceneDef {
    const char*      name;
    const uint32_t*  palette[3];   // day, dusk, night: 0xRRGGBB per colour index
    uint8_t          colors;
    const SceneRect* rects;
    uint16_t         rect_count;
    const SceneGroup* groups;
    uint8_t          group_count;
    int16_t          anchors[SCENE_ANCHOR_COUNT][4];   // x, y, w, h; x = -1 when unset
};
