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
#include <zmk/events/position_state_changed.h>
#include <lvgl.h>

#define MODS_REFRESH_MS       25
#define MOD_HOLD_TIMEOUT_MS   200
#define MOD_SHIFT_MASK        (1u << 0)
#define MOD_CTRL_MASK         (1u << 1)
#define MOD_ALT_MASK          (1u << 2)
#define MOD_GUI_MASK          (1u << 3)
#define MODS_TEXT_LEN         5
#define LEFT_POS_LGUI_A       13
#define LEFT_POS_LALT_S       14
#define LEFT_POS_LCTRL_D      15
#define LEFT_POS_LSHIFT_F     16
#define RIGHT_POS_RSHIFT_J    19
#define RIGHT_POS_RCTRL_K     20
#define RIGHT_POS_RALT_L      21
#define RIGHT_POS_RGUI_SQT    22

struct tracked_mod_key {
    uint32_t position;
    uint8_t mask;
    const char *name;
    bool pressed;
    bool active;
    int64_t pressed_at;
};

static lv_obj_t *mods_label;
static uint8_t current_mods;
static uint8_t last_rendered_mods = 0xFF;
static struct tracked_mod_key tracked_mod_keys[] = {
    {.position = LEFT_POS_LSHIFT_F, .mask = MOD_SHIFT_MASK, .name = "SHIFT_L"},
    {.position = LEFT_POS_LCTRL_D, .mask = MOD_CTRL_MASK, .name = "CTRL_L"},
    {.position = LEFT_POS_LALT_S, .mask = MOD_ALT_MASK, .name = "ALT_L"},
    {.position = LEFT_POS_LGUI_A, .mask = MOD_GUI_MASK, .name = "GUI_L"},
    {.position = RIGHT_POS_RSHIFT_J, .mask = MOD_SHIFT_MASK, .name = "SHIFT_R"},
    {.position = RIGHT_POS_RCTRL_K, .mask = MOD_CTRL_MASK, .name = "CTRL_R"},
    {.position = RIGHT_POS_RALT_L, .mask = MOD_ALT_MASK, .name = "ALT_R"},
    {.position = RIGHT_POS_RGUI_SQT, .mask = MOD_GUI_MASK, .name = "GUI_R"},
};

static void recompute_current_mods_from_keys(void) {
    uint8_t rebuilt_mods = 0;
    for (size_t i = 0; i < ARRAY_SIZE(tracked_mod_keys); i++) {
        if (tracked_mod_keys[i].active) {
            rebuilt_mods |= tracked_mod_keys[i].mask;
        }
    }

    current_mods = rebuilt_mods;
}

static void format_mods_text(uint8_t mods, char out[MODS_TEXT_LEN]) {
    uint8_t idx = 0;
    if (mods & MOD_SHIFT_MASK) {
        out[idx++] = 'S';
    }
    if (mods & MOD_CTRL_MASK) {
        out[idx++] = 'C';
    }
    if (mods & MOD_ALT_MASK) {
        out[idx++] = 'A';
    }
    if (mods & MOD_GUI_MASK) {
        out[idx++] = 'G';
    }
    out[idx] = '\0';
}

static struct tracked_mod_key *find_tracked_mod_key(uint32_t position) {
    for (size_t i = 0; i < ARRAY_SIZE(tracked_mod_keys); i++) {
        if (tracked_mod_keys[i].position == position) {
            return &tracked_mod_keys[i];
        }
    }

    return NULL;
}

static void activate_interrupted_mods(uint32_t triggering_position) {
    bool any_activated = false;

    for (size_t i = 0; i < ARRAY_SIZE(tracked_mod_keys); i++) {
        struct tracked_mod_key *mod = &tracked_mod_keys[i];
        if (!mod->pressed || mod->active || mod->position == triggering_position) {
            continue;
        }

        mod->active = true;
        any_activated = true;
        LOG_DBG("Right display mod interrupted -> active: %s trigger_position=%u", mod->name,
                (unsigned int)triggering_position);
    }

    if (any_activated) {
        recompute_current_mods_from_keys();
    }
}

static void update_mods_from_hold_timeout(void) {
    bool any_activated = false;
    int64_t now = k_uptime_get();

    for (size_t i = 0; i < ARRAY_SIZE(tracked_mod_keys); i++) {
        struct tracked_mod_key *mod = &tracked_mod_keys[i];
        if (!mod->pressed || mod->active) {
            continue;
        }

        if ((now - mod->pressed_at) >= MOD_HOLD_TIMEOUT_MS) {
            mod->active = true;
            any_activated = true;
            LOG_DBG("Right display mod timeout -> active: %s hold_ms=%lld", mod->name,
                    (long long)(now - mod->pressed_at));
        }
    }

    if (any_activated) {
        recompute_current_mods_from_keys();
    }
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
    update_mods_from_hold_timeout();
    update_mods_label(false);
}

static int corne_right_mods_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    struct tracked_mod_key *mod = find_tracked_mod_key(ev->position);
    if (ev->state) {
        activate_interrupted_mods(ev->position);
    }

    if (mod == NULL) {
        LOG_DBG("Right display non-mod position event: source=%u position=%u state=%d current=0x%02x",
                (unsigned int)ev->source, (unsigned int)ev->position, ev->state,
                (unsigned int)current_mods);
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (ev->state) {
        mod->pressed = true;
        mod->pressed_at = ev->timestamp;
        LOG_DBG("Right display mod pressed: %s position=%u source=%u ts=%lld", mod->name,
                (unsigned int)mod->position, (unsigned int)ev->source, (long long)ev->timestamp);
    } else {
        mod->pressed = false;
        mod->pressed_at = 0;
        if (mod->active) {
            mod->active = false;
            recompute_current_mods_from_keys();
            LOG_DBG("Right display mod released: %s current=0x%02x", mod->name,
                    (unsigned int)current_mods);
        } else {
            LOG_DBG("Right display mod tapped: %s", mod->name);
        }
    }

    LOG_DBG(
        "Right display mod position event: source=%u position=%u state=%d pressed=%d active=%d current=0x%02x",
        (unsigned int)ev->source, (unsigned int)ev->position, ev->state, mod->pressed, mod->active,
        (unsigned int)current_mods);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(corne_right_mods_listener, corne_right_mods_listener);
ZMK_SUBSCRIPTION(corne_right_mods_listener, zmk_position_state_changed);

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
    LOG_INF("Initializing Corne right custom display with position modifiers widget");
    current_mods = 0;
    for (size_t i = 0; i < ARRAY_SIZE(tracked_mod_keys); i++) {
        tracked_mod_keys[i].pressed = false;
        tracked_mod_keys[i].active = false;
        tracked_mod_keys[i].pressed_at = 0;
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
