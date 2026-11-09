#ifndef CE_CONTROL_ITEMS_H
#define CE_CONTROL_ITEMS_H
#include "ce_controls.h"
typedef struct {
    uint32_t mask;
    CE_HANDLE item;
    uint32_t state, state_mask;
    CE_WCHAR *text;
    int32_t text_max, image, selected_image, children;
    CE_LPARAM parameter;
} CE_TVITEM;
typedef struct {
    CE_HANDLE parent, after;
    CE_TVITEM item;
} CE_TVINSERT;
typedef struct {
    uint32_t mask;
    int32_t width;
    CE_WCHAR *text;
    void *bitmap;
    int32_t text_max, format;
    CE_LPARAM parameter;
    int32_t image, order;
} CE_HDITEM;
typedef struct {
    int32_t bitmap, command;
    uint8_t state, style, reserved[2];
    CE_WPARAM data;
    CE_LPARAM string;
} CE_TBBUTTON;
#endif
