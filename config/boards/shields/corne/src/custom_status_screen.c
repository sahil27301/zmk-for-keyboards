/*
 * Custom Corne peripheral display
 * Live modifiers widget + ZMK built-in widgets
 */

#include <stdbool.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/display/widgets/peripheral_status.h>
#include <zmk/hid.h>
#include <lvgl.h>

#define MODS_REFRESH_MS 100
#define MOD_CTRL_MASK   ((1u << 0) | (1u << 4))
#define MOD_SHIFT_MASK  ((1u << 1) | (1u << 5))
#define MOD_ALT_MASK    ((1u << 2) | (1u << 6))
#define MOD_GUI_MASK    ((1u << 3) | (1u << 7))
#define MODS_TEXT_LEN   8

static lv_obj_t *mods_label;
static zmk_mod_flags_t last_mods = (zmk_mod_flags_t)0xFF;

static void format_mods_text(zmk_mod_flags_t mods, char out[MODS_TEXT_LEN]) {
    out[0] = (mods & MOD_CTRL_MASK) ? 'C' : '-';
    out[1] = ' ';
    out[2] = (mods & MOD_SHIFT_MASK) ? 'S' : '-';
    out[3] = ' ';
    out[4] = (mods & MOD_ALT_MASK) ? 'A' : '-';
    out[5] = ' ';
    out[6] = (mods & MOD_GUI_MASK) ? 'G' : '-';
    out[7] = '\0';
}

static void update_mods_label(bool force) {
    zmk_mod_flags_t mods = zmk_hid_get_explicit_mods();
    if (!force && mods == last_mods) {
        return;
    }

    char mods_text[MODS_TEXT_LEN];
    format_mods_text(mods, mods_text);
    lv_label_set_text(mods_label, mods_text);
    lv_obj_invalidate(mods_label);

    LOG_DBG("Right display mods update: mods=0x%02x text=%s", (unsigned int)mods, mods_text);
    last_mods = mods;
}

static void mods_timer_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    update_mods_label(false);
}

/* ── Screen layout (128x32) ──
 *
 * +----------+---------+---------+
 * | BT status|         | Battery |
 * +----------+ [MODS]  +---------+
 * |          |         |         |
 * +----------+---------+---------+
 */

static struct zmk_widget_battery_status battery_widget;
static struct zmk_widget_peripheral_status peripheral_widget;

lv_obj_t *zmk_display_status_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    LOG_INF("Initializing Corne right custom display with modifiers widget");

    /* Modifiers state — center */
    mods_label = lv_label_create(screen);
    lv_obj_align(mods_label, LV_ALIGN_CENTER, 0, 0);
    update_mods_label(true);
    lv_timer_create(mods_timer_cb, MODS_REFRESH_MS, NULL);

    /* Built-in peripheral status — top left */
    zmk_widget_peripheral_status_init(&peripheral_widget, screen);
    lv_obj_align(zmk_widget_peripheral_status_obj(&peripheral_widget),
                 LV_ALIGN_TOP_LEFT, 0, 0);

    /* Built-in battery status — top right */
    zmk_widget_battery_status_init(&battery_widget, screen);
    lv_obj_align(zmk_widget_battery_status_obj(&battery_widget),
                 LV_ALIGN_TOP_RIGHT, 0, 0);

    return screen;
}
