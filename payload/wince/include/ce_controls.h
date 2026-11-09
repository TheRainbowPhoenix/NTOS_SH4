#ifndef CE_CONTROLS_H
#define CE_CONTROLS_H
#include "ce_types.h"
enum CE_CONTROL_KIND {
    CE_CC_LIST = 1,
    CE_CC_TREE,
    CE_CC_HEADER,
    CE_CC_TAB,
    CE_CC_TOOLBAR,
    CE_CC_STATUS,
    CE_CC_PROGRESS,
    CE_CC_TRACKBAR,
    CE_CC_UPDOWN,
    CE_CC_TOOLTIP,
    CE_CC_REBAR,
    CE_CC_COMMAND,
    CE_CC_COMBO,
    CE_CC_EDIT
};
typedef struct {
    uint32_t mask;
    int32_t item, subitem;
    uint32_t state, state_mask;
    CE_WCHAR *text;
    int32_t text_max, image;
    CE_LPARAM parameter;
} CE_LVITEM;
typedef struct {
    uint32_t mask;
    CE_WCHAR *text;
    int32_t text_max, image;
    CE_LPARAM parameter;
} CE_TABITEM;
typedef struct {
    uint32_t mask;
    int32_t format, width;
    CE_WCHAR *text;
    int32_t text_max, subitem, image, order;
} CE_COLUMN;
typedef struct {
    uint32_t size, classes;
} CE_INITCOMMON;
typedef struct {
    uint32_t size, flags;
    void *instance;
    const CE_WCHAR *resource;
    void *icon;
    const CE_WCHAR *title;
    void *dialog;
    CE_LPARAM parameter;
    void *callback;
    void *parent_ref;
} CE_PROPPAGE;
CE_HANDLE CeCreateControl(unsigned kind, CE_HANDLE parent, uint32_t style, int id);
CE_RESULT CeControlMessage(CE_HANDLE handle, unsigned message, CE_WPARAM wp, CE_LPARAM lp);
int CeDestroyControl(CE_HANDLE handle);
unsigned CeControlKind(CE_HANDLE handle);
int CeControlSetBounds(CE_HANDLE handle, int x, int y, int width, int height);
int CeControlGetBounds(CE_HANDLE handle, CE_RECT *rect);
CE_HANDLE CeControlParent(CE_HANDLE handle);
CE_LPARAM CeControlSetLong(CE_HANDLE handle, int index, CE_LPARAM value);
CE_LPARAM CeControlGetLong(CE_HANDLE handle, int index);
typedef void (*CE_CONTROL_PAINTER)(void *context, const CE_RECT *rect, const CE_WCHAR *text,
                                   int selected);
int CeControlPaint(CE_HANDLE handle, CE_CONTROL_PAINTER painter, void *context);
void CeControlsReset(void);
#endif
