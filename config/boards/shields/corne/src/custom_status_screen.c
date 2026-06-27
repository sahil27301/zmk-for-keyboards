/*
 * Custom Corne peripheral display
 * Live modifiers widget + ZMK built-in widgets
 */

#include <stdbool.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/display/widgets/battery_status.h>
#include <zmk/display/widgets/peripheral_status.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <lvgl.h>

#define MODS_REFRESH_MS          25
#define MOD_SHIFT_ACTIVE_MASK    (1u << 0)
#define MOD_CTRL_ACTIVE_MASK     (1u << 1)
#define MOD_ALT_ACTIVE_MASK      (1u << 2)
#define MOD_GUI_ACTIVE_MASK      (1u << 3)
#define MOD_CTRL_EXPLICIT_MASK   ((1u << 0) | (1u << 4))
#define MOD_SHIFT_EXPLICIT_MASK  ((1u << 1) | (1u << 5))
#define MOD_ALT_EXPLICIT_MASK    ((1u << 2) | (1u << 6))
#define MOD_GUI_EXPLICIT_MASK    ((1u << 3) | (1u << 7))
#define MODS_TEXT_LEN            5
#define MOD_GROUP_COUNT          4

enum mod_group_index {
    MOD_GROUP_SHIFT = 0,
    MOD_GROUP_CTRL = 1,
    MOD_GROUP_ALT = 2,
    MOD_GROUP_GUI = 3,
};

static lv_obj_t *mods_label;
static uint8_t current_mods;
static uint8_t last_rendered_mods = 0xFF;
static uint8_t active_mod_counts[MOD_GROUP_COUNT];

static void recompute_current_mods(void) {
    uint8_t rebuilt_mods = 0;
    rebuilt_mods |= active_mod_counts[MOD_GROUP_SHIFT] > 0 ? MOD_SHIFT_ACTIVE_MASK : 0;
    rebuilt_mods |= active_mod_counts[MOD_GROUP_CTRL] > 0 ? MOD_CTRL_ACTIVE_MASK : 0;
    rebuilt_mods |= active_mod_counts[MOD_GROUP_ALT] > 0 ? MOD_ALT_ACTIVE_MASK : 0;
    rebuilt_mods |= active_mod_counts[MOD_GROUP_GUI] > 0 ? MOD_GUI_ACTIVE_MASK : 0;
    current_mods = rebuilt_mods;
}

static void update_mod_count(uint8_t group_index, bool pressed, const char *name) {
    if (pressed) {
        active_mod_counts[group_index]++;
    } else if (active_mod_counts[group_index] > 0) {
        active_mod_counts[group_index]--;
    }

    LOG_DBG("Right display mod count: %s pressed=%d count=%u", name, pressed,
            (unsigned int)active_mod_counts[group_index]);
}

static void apply_explicit_mods(uint8_t explicit_mods, bool pressed) {
    if (explicit_mods & MOD_SHIFT_EXPLICIT_MASK) {
        update_mod_count(MOD_GROUP_SHIFT, pressed, "SHIFT");
    }
    if (explicit_mods & MOD_CTRL_EXPLICIT_MASK) {
        update_mod_count(MOD_GROUP_CTRL, pressed, "CTRL");
    }
    if (explicit_mods & MOD_ALT_EXPLICIT_MASK) {
        update_mod_count(MOD_GROUP_ALT, pressed, "ALT");
    }
    if (explicit_mods & MOD_GUI_EXPLICIT_MASK) {
        update_mod_count(MOD_GROUP_GUI, pressed, "GUI");
    }

    recompute_current_mods();
}

static void format_mods_text(uint8_t mods, char out[MODS_TEXT_LEN]) {
    uint8_t idx = 0;
    if (mods & MOD_SHIFT_ACTIVE_MASK) {
        out[idx++] = 'S';
    }
    if (mods & MOD_CTRL_ACTIVE_MASK) {
        out[idx++] = 'C';
    }
    if (mods & MOD_ALT_ACTIVE_MASK) {
        out[idx++] = 'A';
    }
    if (mods & MOD_GUI_ACTIVE_MASK) {
        out[idx++] = 'G';
    }
    out[idx] = '\0';
}

static void update_mods_label(bool force) {
    if (!force && current_mods == last_rendered_mods) {
        return;
    }

    char mods_text[MODS_TEXT_LEN];
    format_mods_text(current_mods, mods_text);
    lv_label_set_text(mods_label, mods_text);
    lv_obj_invalidate(mods_label);

    LOG_DBG("Right display mods render: mods=0x%02x text=%s", (unsigned int)current_mods,
            current_mods == 0 ? "<hidden>" : mods_text);
    last_rendered_mods = current_mods;
}

static void mods_timer_cb(lv_timer_t *timer) {
    ARG_UNUSED(timer);
    update_mods_label(false);
}

static int corne_right_mods_listener(const zmk_event_t *eh) {
    const struct zmk_keycode_state_changed *ev = as_zmk_keycode_state_changed(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->explicit_modifiers == 0) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    apply_explicit_mods(ev->explicit_modifiers, ev->state);
    LOG_DBG(
        "Right display keycode event: state=%d keycode=0x%lx usage_page=0x%x explicit=0x%02x current=0x%02x",
        ev->state, (unsigned long)ev->keycode, (unsigned int)ev->usage_page,
        (unsigned int)ev->explicit_modifiers, (unsigned int)current_mods);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(corne_right_mods_listener, corne_right_mods_listener);
ZMK_SUBSCRIPTION(corne_right_mods_listener, zmk_keycode_state_changed);

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
    LOG_INF("Initializing Corne right custom display with keycode modifiers widget");
    current_mods = 0;
    for (size_t i = 0; i < MOD_GROUP_COUNT; i++) {
        active_mod_counts[i] = 0;
    }

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
