/* settings_icons.h — MDL2-style icon set for the SystemSettings port.
 *
 * Same integer-only stroke model as files/app/files_icons.c (helpers are
 * shared from there): every glyph reduces to fill() calls, drawn in a
 * virtual 100x100 box. Glyph shapes follow the real-device captures in
 * refs/settings-screens (VLM-read: phone, keyboard+mouse, globe, brush,
 * person, clock+speech bubble...) plus platform knowledge for the
 * off-screen categories (accessibility person, padlock, update arrows,
 * puzzle).
 */
#ifndef SETTINGS_ICONS_H
#define SETTINGS_ICONS_H

#include "files_icons.h" /* IconFillFn + shared stroke helpers */

enum {
    SI_NONE = 0,
    SI_GEAR,       /* header cog (E713)                 */
    SI_PHONE,      /* System: phone outline             */
    SI_KEYBOARD,   /* Devices: keyboard + mouse         */
    SI_GLOBE,      /* Network & wireless: globe (E774)  */
    SI_BRUSH,      /* Personalization: brush + canvas   */
    SI_PERSON,     /* Accounts: person (E77B-ish)       */
    SI_CLOCKCHAT,  /* Time & language: clock + bubble   */
    SI_ACCESS,     /* Ease of Access: person in circle  */
    SI_PADLOCK,    /* Privacy: lock (E719)              */
    SI_UPDATE,     /* Update & security: circular arrows*/
    SI_PUZZLE,     /* Extras: puzzle piece              */
    SI_TILES,      /* Start: live-tile grid             */
    SI_PALETTE,    /* Colors: paint palette             */
    SI_NOTE,       /* Sounds: music note                */
    SI_SCREENLOCK, /* Lock screen: screen + padlock     */
    SI_NAVBAR,     /* Navigation bar: rounded rect      */
    SI_MONITOR,    /* Display: monitor (E7F5-ish)       */
    SI_BATTERY,    /* Battery saver: battery + leaf     */
    SI_STORAGE,    /* Storage: drive stack              */
    SI_INFO,       /* About: info circle (E946)         */
    SI_PLAY,       /* ringtone preview: play triangle   */
    SI_USB,        /* USB connector                     */
    SI_COUNT
};

/* name -> id (0 when unknown) */
int si_find(const char *name);

/* Draw icon `id` centered in the size x size box at (x, y). */
void si_draw(IconFillFn fill, void *dc, int x, int y, int size,
             unsigned color, int id);

#endif
