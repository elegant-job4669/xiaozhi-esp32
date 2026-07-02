#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "lvgl_display.h"
#include "gif/lvgl_gif.h"

#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_timer.h>

#include <memory>
#include <string>


class OledDisplay : public LvglDisplay {
private:
    esp_lcd_panel_io_handle_t panel_io_ = nullptr;
    esp_lcd_panel_handle_t panel_ = nullptr;

    lv_obj_t* top_bar_ = nullptr;
    lv_obj_t* status_bar_ = nullptr;
    lv_obj_t* content_ = nullptr;
    lv_obj_t* content_left_ = nullptr;
    lv_obj_t* content_right_ = nullptr;
    lv_obj_t* container_ = nullptr;
    lv_obj_t* side_bar_ = nullptr;
    lv_obj_t *emotion_label_ = nullptr;
    lv_obj_t* chat_message_label_ = nullptr;

#if CONFIG_OLED_ASCII_FACE_128X64
    lv_obj_t* face_image_ = nullptr;
    lv_obj_t* face_icon_label_ = nullptr;
    std::unique_ptr<LvglGif> face_gif_controller_ = nullptr;
    std::string current_emotion_ = "neutral";
    int face_anim_frame_ = 0;
    esp_timer_handle_t face_anim_timer_ = nullptr;

    void SetupUI_128x64_AsciiFace();
    void RenderEmotionFace();
    void RenderEmotionFaceUnlocked();
    void ShowEmotionImage(const LvglImage* image);
    void ShowEmotionIcon(const char* emotion);
    const char* ResolveEmotionToShow() const;
    void StartFaceAnimation();
    void StopFaceAnimation();
    static void FaceAnimTimerCallback(void* arg);
#endif

    virtual bool Lock(int timeout_ms = 0) override;
    virtual void Unlock() override;

    void SetupUI_128x64();
    void SetupUI_128x32();

public:
    OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width, int height, bool mirror_x, bool mirror_y);
    ~OledDisplay();

    virtual void SetupUI() override;
#if CONFIG_OLED_ASCII_FACE_128X64
    virtual void SetStatus(const char* status) override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void UpdateStatusBar(bool update_all = false) override;
#endif
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
};

#endif // OLED_DISPLAY_H
