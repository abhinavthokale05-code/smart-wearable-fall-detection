#include "piobot.h" // You can rename this header later
#include <Arduino.h>


// Link to your global heart rate variable from main.cpp
extern int currentHR;
extern const lv_image_dsc_t ui_img_sample_png;
extern lv_obj_t *ui_hrLabel;      // If not defined globally
extern lv_obj_t *ui_pulseCircle; 
void ui_app_exit(void);

REGISTER_APP("HEART RATE", &ui_img_sample_png, ui_pioScreen, ui_pioScreen_screen_init);

lv_obj_t *ui_hrLabel;
lv_obj_t *ui_pulseCircle;
lv_timer_t *hr_update_timer = NULL;

// --- Animation: Makes the circle "pulse" like a heart ---
void pulse_animation(lv_obj_t * obj) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, 180, 210); // Scale from 100% to 120%
    lv_anim_set_time(&a, 200);
    lv_anim_set_playback_time(&a, 200);
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_width); // Simplified scaling
    lv_anim_start(&a);
}

// --- Timer Callback: Updates the UI every 500ms ---
// --- Timer Callback: Updates the UI every 500ms ---
void hr_timer_cb(lv_timer_t *timer)
{
    // THE FIX: You must put a number (like 16) inside the brackets
    // This creates a "buffer" large enough to hold the heart rate text
    char buf[16]; 

    if (currentHR > 0) 
    {
        // 1. Convert the heart rate number into text inside the buffer
        snprintf(buf, sizeof(buf), "%d", currentHR);

        // 2. Update the label on the watch screen with that text
        if (ui_hrLabel != NULL) {
            lv_label_set_text(ui_hrLabel, buf);
        }

        // 3. Trigger the pulsating heart animation
        pulse_animation(ui_pulseCircle); 
    } 
    else 
    {
        // If no finger is detected, show dashes instead of 0
        if (ui_hrLabel != NULL) {
            lv_label_set_text(ui_hrLabel, "--");
        }
    }
}

void ui_event_pioScreen(lv_event_t *e)
{
    lv_event_code_t event_code = lv_event_get_code(e);
    if (event_code == LV_EVENT_SCREEN_LOADED)
    {
        // Start updating the UI
        hr_update_timer = lv_timer_create(hr_timer_cb, 500, NULL);
    }
    if (event_code == LV_EVENT_SCREEN_UNLOADED)
    {
        if (hr_update_timer != NULL) {
            lv_timer_delete(hr_update_timer);
            hr_update_timer = NULL;
        }
        lv_obj_delete(ui_pioScreen);
        ui_pioScreen = NULL;
    }
    if (event_code == LV_EVENT_GESTURE && lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT)
    {
        ui_app_exit();
    }
}

void ui_pioScreen_screen_init()
{
    ui_pioScreen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(ui_pioScreen, lv_color_hex(0x000000), 0);

    // Pulse Circle (The "Heart" Visual)
    ui_pulseCircle = lv_obj_create(ui_pioScreen);
    lv_obj_set_size(ui_pulseCircle, 200, 200);
    lv_obj_set_align(ui_pulseCircle, LV_ALIGN_CENTER);
    lv_obj_set_style_radius(ui_pulseCircle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(ui_pulseCircle, lv_color_hex(0xFF0000), 0); // Red
    lv_obj_set_style_bg_opa(ui_pulseCircle, 90, 0);

    lv_obj_set_style_shadow_width(ui_pulseCircle, 40, 0);
    lv_obj_set_style_shadow_color(ui_pulseCircle, lv_color_hex(0xFF0000), 0);
    lv_obj_set_style_shadow_opa(ui_pulseCircle, 180, 0);
    lv_obj_set_style_border_width(ui_pulseCircle, 0, 0);

    // Heart Rate Number
    ui_hrLabel = lv_label_create(ui_pioScreen);
    lv_obj_center(ui_hrLabel);
    lv_obj_set_style_text_font(ui_hrLabel, &lv_font_montserrat_48, 0); // Big font
    lv_obj_set_style_text_color(ui_hrLabel, lv_color_hex(0xFFFFFF), 0);
    lv_label_set_text(ui_hrLabel, "--");

    // "BPM" Unit Label
    lv_obj_t * unitLabel = lv_label_create(ui_pioScreen);

    lv_label_set_text(unitLabel, "BPM");

// Position
    lv_obj_align(unitLabel, LV_ALIGN_CENTER, 0, 55);

// BIGGER + BOLDER FONT
    lv_obj_set_style_text_font(unitLabel, &lv_font_montserrat_28, 0);

// Bright white color
    lv_obj_set_style_text_color(unitLabel, lv_color_hex(0xFFFFFF), 0);

// Optional letter spacing for premium look
    lv_obj_set_style_text_letter_space(unitLabel, 2, 0);

    lv_obj_add_event_cb(ui_pioScreen, ui_event_pioScreen, LV_EVENT_ALL, NULL);
}