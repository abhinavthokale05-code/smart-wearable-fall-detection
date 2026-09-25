#include "sample.h"
#include <stdio.h>
#include <Arduino.h>


extern String emergencyType;
extern bool bleAlreadySent;
void startEmergencyBLETrigger();
void startSosAudioAlert(bool continuous);
void stopEmergencySOS(const char *reason);

#ifdef ENABLE_APP_SAMPLE

REGISTER_APP("SOS", &ui_img_sample_png, sample_screen_main, sample_screen_init);

static bool autoMode = false;
static int countdown = 5;
static bool manualDecisionTaken = false;

static lv_obj_t *labelTitle = NULL;
static lv_obj_t *labelCount = NULL;
static lv_timer_t *timer = NULL;

static lv_obj_t *audioPromptMask = NULL;
static lv_obj_t *audioPromptPanel = NULL;

/* -------------------------------------------------- */
static void hide_audio_prompt(void)
{
    if (audioPromptMask != NULL)
    {
        lv_obj_add_flag(audioPromptMask, LV_OBJ_FLAG_HIDDEN);
    }
}

/* -------------------------------------------------- */
static void show_audio_prompt(void)
{
    if (audioPromptMask != NULL)
    {
        lv_obj_remove_flag(audioPromptMask, LV_OBJ_FLAG_HIDDEN);
    }
}

/* -------------------------------------------------- */
static void trigger_sos()
{
    Serial.println("SENDING THE LOCATION");

    startEmergencyBLETrigger();

    if (labelCount != NULL)
    {
        lv_label_set_text(labelCount, "HELP SENT!");
    }
}

/* -------------------------------------------------- */
static void send_manual_sos(bool withAudio)
{
    if (manualDecisionTaken)
    {
        return;
    }

    manualDecisionTaken = true;
    bleAlreadySent = false;
    emergencyType = withAudio ? "MANUAL SOS + AUDIO" : "MANUAL SOS";

    if (withAudio)
    {
        startSosAudioAlert(true);
        Serial.println("[SOS] MANUAL SOS WITH AUDIO ALERT");
    }
    else
    {
        Serial.println("[SOS] MANUAL SOS WITHOUT AUDIO ALERT");
    }

    trigger_sos();
    hide_audio_prompt();
    ui_app_exit();
}

/* -------------------------------------------------- */
static void audio_yes_event(lv_event_t *e)
{
    (void)e;
    send_manual_sos(true);
}

/* -------------------------------------------------- */
static void audio_no_event(lv_event_t *e)
{
    (void)e;
    send_manual_sos(false);
}

/* -------------------------------------------------- */
static void cancel_event(lv_event_t *e)
{
    (void)e;
    stopEmergencySOS("SOS canceled from app");
}

/* -------------------------------------------------- */
static void send_event(lv_event_t *e)
{
    (void)e;

    if (manualDecisionTaken)
    {
        return;
    }

    show_audio_prompt();
}

/* -------------------------------------------------- */
static void timer_cb(lv_timer_t *t)
{
    (void)t;
    countdown--;

    char buf[32];
    snprintf(buf, sizeof(buf), "Sending in %d...", countdown);

    if (labelCount != NULL)
    {
        lv_label_set_text(labelCount, buf);
    }

    if (countdown <= 0)
    {
        if (timer != NULL)
        {
            lv_timer_del(timer);
            timer = NULL;
        }

        if (labelCount != NULL)
        {
            lv_label_set_text(labelCount, "Sending SOS...");
        }

        Serial.println("SENDING THE LOCATION");
        trigger_sos();

        lv_timer_handler();
        lv_tick_inc(100);
        delay(300);

        if (sample_screen_main != NULL)
        {
            ui_app_exit();
        }

        autoMode = false;
        countdown = 5;
    }
}

/* -------------------------------------------------- */
void sample_screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SCREEN_UNLOADED)
    {
        if (timer != NULL)
        {
            lv_timer_del(timer);
            timer = NULL;
        }

        autoMode = false;
        countdown = 5;
        manualDecisionTaken = false;
        labelTitle = NULL;
        labelCount = NULL;
        audioPromptMask = NULL;
        audioPromptPanel = NULL;
        sample_screen_main = NULL;
    }

    if (code == LV_EVENT_GESTURE &&
        lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_RIGHT)
    {
        if (!autoMode && audioPromptMask != NULL &&
            !lv_obj_has_flag(audioPromptMask, LV_OBJ_FLAG_HIDDEN))
        {
            hide_audio_prompt();
            return;
        }

        if (autoMode)
        {
            stopEmergencySOS("SOS dismissed by gesture");
        }
        else
        {
            ui_app_exit();
        }
    }
}

/* -------------------------------------------------- */
void sample_screen_init(void)
{
    sample_screen_main = lv_obj_create(NULL);
    lv_obj_remove_flag(sample_screen_main, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(sample_screen_main, lv_color_hex(0x000000), 0);

    labelTitle = lv_label_create(sample_screen_main);
    lv_obj_align(labelTitle, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_set_style_text_font(labelTitle, &lv_font_montserrat_20, 0);

    labelCount = lv_label_create(sample_screen_main);
    lv_obj_align(labelCount, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_text_font(labelCount, &lv_font_montserrat_18, 0);

    lv_obj_t *btn = lv_btn_create(sample_screen_main);
    lv_obj_set_size(btn, 140, 45);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -25);

    lv_obj_t *txt = lv_label_create(btn);
    lv_obj_center(txt);

    if (autoMode)
    {
        lv_label_set_text(labelTitle, "FALL DETECTED");
        lv_label_set_text(labelCount, "Sending in 5...");
        lv_label_set_text(txt, "STOP SOS");

        lv_obj_add_event_cb(btn, cancel_event, LV_EVENT_CLICKED, NULL);

        countdown = 5;
        timer = lv_timer_create(timer_cb, 1000, NULL);
    }
    else
    {
        lv_label_set_text(labelTitle, "EMERGENCY SOS");
        lv_label_set_text(labelCount, "Need help right now?");
        lv_label_set_text(txt, "SEND HELP");

        lv_obj_add_event_cb(btn, send_event, LV_EVENT_CLICKED, NULL);
    }

    audioPromptMask = lv_obj_create(sample_screen_main);
    lv_obj_set_size(audioPromptMask, lv_pct(100), lv_pct(100));
    lv_obj_center(audioPromptMask);
    lv_obj_remove_flag(audioPromptMask, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(audioPromptMask, 0, 0);
    lv_obj_set_style_bg_color(audioPromptMask, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(audioPromptMask, 170, 0);
    lv_obj_set_style_border_width(audioPromptMask, 0, 0);
    lv_obj_set_style_pad_all(audioPromptMask, 0, 0);
    lv_obj_add_flag(audioPromptMask, LV_OBJ_FLAG_HIDDEN);

        audioPromptPanel = lv_obj_create(audioPromptMask);
    lv_obj_set_size(audioPromptPanel, 290, 210);
    lv_obj_center(audioPromptPanel);
    lv_obj_remove_flag(audioPromptPanel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_radius(audioPromptPanel, 18, 0);
    lv_obj_set_style_bg_color(audioPromptPanel, lv_color_hex(0x111111), 0);
    lv_obj_set_style_bg_opa(audioPromptPanel, 255, 0);
    lv_obj_set_style_border_color(audioPromptPanel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(audioPromptPanel, 2, 0);
    lv_obj_set_style_pad_all(audioPromptPanel, 0, 0);

    lv_obj_t *popupQuestion = lv_label_create(audioPromptPanel);
    lv_obj_set_width(popupQuestion, 230);
    lv_obj_set_height(popupQuestion, LV_SIZE_CONTENT);
    lv_obj_align(popupQuestion, LV_ALIGN_CENTER, 0, -38);
    lv_label_set_long_mode(popupQuestion, LV_LABEL_LONG_WRAP);
    lv_label_set_text(popupQuestion, "AUDIO ALERT!!?");
    lv_obj_set_style_text_align(popupQuestion, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(popupQuestion, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(popupQuestion, &lv_font_montserrat_20, 0);

    lv_obj_t *yesBtn = lv_btn_create(audioPromptPanel);
    lv_obj_set_size(yesBtn, 104, 48);
    lv_obj_align(yesBtn, LV_ALIGN_BOTTOM_LEFT, 24, -18);
    lv_obj_set_style_radius(yesBtn, 22, 0);
    lv_obj_set_style_bg_color(yesBtn, lv_color_hex(0xD62828), 0);
    lv_obj_set_style_bg_opa(yesBtn, 255, 0);
    lv_obj_add_event_cb(yesBtn, audio_yes_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *yesTxt = lv_label_create(yesBtn);
    lv_label_set_text(yesTxt, "YES");
    lv_obj_set_style_text_font(yesTxt, &lv_font_montserrat_18, 0);
    lv_obj_center(yesTxt);

    lv_obj_t *noBtn = lv_btn_create(audioPromptPanel);
    lv_obj_set_size(noBtn, 104, 48);
    lv_obj_align(noBtn, LV_ALIGN_BOTTOM_RIGHT, -24, -18);
    lv_obj_set_style_radius(noBtn, 22, 0);
    lv_obj_set_style_bg_color(noBtn, lv_color_hex(0x3A3A3A), 0);
    lv_obj_set_style_bg_opa(noBtn, 255, 0);
    lv_obj_add_event_cb(noBtn, audio_no_event, LV_EVENT_CLICKED, NULL);

    lv_obj_t *noTxt = lv_label_create(noBtn);
    lv_label_set_text(noTxt, "NO");
    lv_obj_set_style_text_font(noTxt, &lv_font_montserrat_18, 0);
    lv_obj_center(noTxt);

    lv_obj_add_event_cb(sample_screen_main, sample_screen_event_cb, LV_EVENT_ALL, NULL);
}

/* -------------------------------------------------- */
void sample_force_close(void)
{
    if (sample_screen_main == NULL)
    {
        return;
    }

    lv_disp_t *display = lv_display_get_default();
    lv_obj_t *actScr = lv_display_get_screen_active(display);

    if (actScr == sample_screen_main)
    {
        ui_app_exit();
    }
}

/* -------------------------------------------------- */
void sample_open_auto(void)
{
    autoMode = true;
    manualDecisionTaken = false;

    if (sample_screen_main == NULL)
    {
        sample_screen_init();
    }

    lv_scr_load(sample_screen_main);
}

/* -------------------------------------------------- */
void sample_open_manual(void)
{
    autoMode = false;
    manualDecisionTaken = false;

    if (sample_screen_main == NULL)
    {
        sample_screen_init();
    }

    lv_scr_load(sample_screen_main);
}

#endif
