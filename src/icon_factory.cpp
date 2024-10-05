#include <Arduino.h>

#include <Adafruit_GFX.h>

#include <math.h>

#include "image_tools.h"

uint16_t colour_565_sun = COLOR565(255, 237, 128); // light yellow

struct IconDrawInfo_t
{
    GFXcanvas16 *canvas;
    int16_t w, h;
    int16_t cx, cy;
    uint8_t daytime;
    float t;
};

struct IconDrawFunction
{
    String code;
    void (*handler)(IconDrawInfo_t *);
};

void ClearSky(IconDrawInfo_t *info);

IconDrawFunction icon_handler_map[] = {
    {"01", &ClearSky},
    {"", NULL}};

void DrawWeatherIcon(String icon_name, GFXcanvas16 *canvas, float t) {
    String icon_code = icon_name.substring(0, 2);
    uint8_t daytime = icon_name.endsWith("d") ? 1 : 0;
    uint8_t i = 0;

    // clear the canvas
    IconDrawInfo_t info = {
        .canvas = canvas,
        .w = canvas->width(),
        .h = canvas->height(),
        .cx = (int16_t)(canvas->width() >> 1),
        .cy = (int16_t)(canvas->height() >> 1),
        .daytime = daytime,
        .t = t};
    memset(canvas->getBuffer(), 0, info.w * info.h * 2);
    while (icon_handler_map[i].handler != NULL) {
        // Serial.printf("[%s] vs [%s]", icon_handler_map[i].code.c_str(), icon_code.c_str());
        if (icon_handler_map[i].code == icon_code) {
            // Serial.printf(" -- MATCH!!\n");
            icon_handler_map[i].handler(&info);
            break;
        }
        // Serial.printf("\n");
        i++;
    }
}

// ----------------------------------------------------------------------------------------------------------------------
// 01: Clear Sky
// ----------------------------------------------------------------------------------------------------------------------
void ClearSkyDay(IconDrawInfo_t *info) {
    int16_t r_outer = min(info->w, info->h) / 2, r_inner = r_outer / 2, r_mid = r_inner + 4, l = r_outer - r_mid;
    int16_t d_mid = r_mid * M_SQRT1_2, d_outer = r_outer * M_SQRT1_2;
    // Serial.printf("cx=[%d], cy=[%d], --> r_outer=[%d], r_mid=[%d], r_inner=[%d]\n", info->cx, info->cy, r_outer, r_mid, r_inner);

    info->canvas->drawCircle(info->cx, info->cy, r_inner, colour_565_sun);
    info->canvas->drawCircle(info->cx, info->cy, r_inner - 1, colour_565_sun);
    float t = info->t * M_PI * 2.0f;
    for (uint8_t a = 0; a < 8; a++) {
        float tt = t;
        for (uint8_t v = 0; v < 2; v++) {
            float cos = cosf(tt), sin = sinf(tt);
            int16_t dx_mid = cos * r_mid, dy_mid = sin * r_mid, dx_outer = cos * r_outer, dy_outer = sin * r_outer;
            // Serial.printf(" ... dx_mid=[%d], dy_mid=[%d], dx_outer=[%d], dy_outer=[%d]\n", dx_mid, dy_mid, dx_outer, dy_outer);
            info->canvas->drawLine(info->cx + dx_mid, info->cy + dy_mid, info->cx + dx_outer, info->cy + dy_outer, colour_565_sun);
            tt += (2.0f * M_PI / 100.0f);
        }
        t += (2.0f * M_PI / 8.0f);
    }

    // info->canvas->drawFastHLine(info->cx + r_mid, info->cy, l, colour_565_sun);
    // info->canvas->drawFastHLine(info->cx - r_mid, info->cy, -l, colour_565_sun);
    // info->canvas->drawFastVLine(info->cx, info->cy + r_mid, l, colour_565_sun);
    // info->canvas->drawFastVLine(info->cx, info->cy + r_mid, -l, colour_565_sun);
    // info->canvas->drawLine(info->cx + d_mid, info->cy + d_mid, info->cx + d_outer, info->cy + d_outer, colour_565_sun);
    // info->canvas->drawLine(info->cx - d_mid, info->cy + d_mid, info->cx - d_outer, info->cy + d_outer, colour_565_sun);
    // info->canvas->drawLine(info->cx - d_mid, info->cy - d_mid, info->cx - d_outer, info->cy - d_outer, colour_565_sun);
    // info->canvas->drawLine(info->cx + d_mid, info->cy - d_mid, info->cx + d_outer, info->cy - d_outer, colour_565_sun);
}
void ClearSkyNight(IconDrawInfo_t *info) {
}
void ClearSky(IconDrawInfo_t *info) {
    info->daytime ? ClearSkyDay(info) : ClearSkyNight(info);
}