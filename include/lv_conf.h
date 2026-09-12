// LVGL v9 project config. Only deltas from defaults; rest falls back internally.
#pragma once
#define LV_CONF_H

#define LV_COLOR_DEPTH 16
#define LV_MEM_SIZE (64 * 1024U)
#define LV_DEF_REFR_PERIOD 20

// Tick comes from Arduino millis(), no FreeRTOS timer needed.
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())

#define LV_USE_LOG 1
#define LV_LOG_LEVEL LV_LOG_LEVEL_WARN

#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1

// Widgets used by ui.cpp; everything else stays off to save flash.
#define LV_USE_LABEL 1
#define LV_USE_BUTTON 1
#define LV_USE_TABVIEW 1
#define LV_USE_LIST 1
#define LV_USE_SLIDER 1
#define LV_USE_BAR 1
#define LV_USE_IMAGE 1
#define LV_USE_ARC 1
#define LV_USE_SPINNER 1

#define LV_USE_FLEX 1
#define LV_USE_GRID 1
#define LV_USE_THEME_DEFAULT 1
#define LV_USE_ANIMATION 1
#define LV_OBJ_STYLE_CACHE 1
