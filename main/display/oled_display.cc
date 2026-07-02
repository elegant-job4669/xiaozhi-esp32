#include "oled_display.h"
#include "assets/lang_config.h"
#include "lvgl_theme.h"
#include "lvgl_font.h"
#include "application.h"
#include "device_state.h"

#include <string>
#include <algorithm>
#include <cstring>

#include <esp_log.h>
#include <esp_err.h>
#include <esp_lvgl_port.h>
#include <font_awesome.h>

#define TAG "OledDisplay"

LV_FONT_DECLARE(BUILTIN_TEXT_FONT);
LV_FONT_DECLARE(BUILTIN_ICON_FONT);
LV_FONT_DECLARE(font_awesome_30_1);

OledDisplay::OledDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
    int width, int height, bool mirror_x, bool mirror_y)
    : panel_io_(panel_io), panel_(panel) {
    width_ = width;
    height_ = height;

    auto text_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_TEXT_FONT);
    auto icon_font = std::make_shared<LvglBuiltInFont>(&BUILTIN_ICON_FONT);
    auto large_icon_font = std::make_shared<LvglBuiltInFont>(&font_awesome_30_1);
    
    auto dark_theme = new LvglTheme("dark");
    dark_theme->set_text_font(text_font);
    dark_theme->set_icon_font(icon_font);
    dark_theme->set_large_icon_font(large_icon_font);

    auto& theme_manager = LvglThemeManager::GetInstance();
    theme_manager.RegisterTheme("dark", dark_theme);
    current_theme_ = dark_theme;

    ESP_LOGI(TAG, "Initialize LVGL");
    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_priority = 1;
    port_cfg.task_stack = 6144;
#if CONFIG_SOC_CPU_CORES_NUM > 1
    port_cfg.task_affinity = 1;
#endif
    lvgl_port_init(&port_cfg);

    ESP_LOGI(TAG, "Adding OLED display");
    const lvgl_port_display_cfg_t display_cfg = {
        .io_handle = panel_io_,
        .panel_handle = panel_,
        .control_handle = nullptr,
        .buffer_size = static_cast<uint32_t>(width_ * height_),
        .double_buffer = false,
        .trans_size = 0,
        .hres = static_cast<uint32_t>(width_),
        .vres = static_cast<uint32_t>(height_),
        .monochrome = true,
        .rotation = {
            .swap_xy = false,
            .mirror_x = mirror_x,
            .mirror_y = mirror_y,
        },
        .flags = {
            .buff_dma = 1,
            .buff_spiram = 0,
            .sw_rotate = 0,
            .full_refresh = 0,
            .direct_mode = 0,
        },
    };

    display_ = lvgl_port_add_disp(&display_cfg);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    // Note: SetupUI() should be called by Application::Initialize(), not in constructor
    // to ensure lvgl objects are created after the display is fully initialized.
}

void OledDisplay::SetupUI() {
    // Prevent duplicate calls - if already called, return early
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }
    
    Display::SetupUI();  // Mark SetupUI as called
    if (height_ == 64) {
#if CONFIG_OLED_ASCII_FACE_128X64
        SetupUI_128x64_AsciiFace();
#else
        SetupUI_128x64();
#endif
    } else {
        SetupUI_128x32();
    }
}

OledDisplay::~OledDisplay() {
#if CONFIG_OLED_ASCII_FACE_128X64
    StopFaceAnimation();
#endif

    if (content_ != nullptr) {
        lv_obj_del(content_);
    }

    bool is_128x64_layout = (top_bar_ != nullptr);
    if (status_bar_ != nullptr && is_128x64_layout) {
        status_label_ = nullptr;
        notification_label_ = nullptr;
        lv_obj_del(status_bar_);
    }
    if (top_bar_ != nullptr) {
        network_label_ = nullptr;
        mute_label_ = nullptr;
        battery_label_ = nullptr;
        lv_obj_del(top_bar_);
    }
    if (side_bar_ != nullptr) {
        if (!is_128x64_layout) {
            status_label_ = nullptr;
            notification_label_ = nullptr;
            network_label_ = nullptr;
            mute_label_ = nullptr;
            battery_label_ = nullptr;
        }
        lv_obj_del(side_bar_);
    }
    if (container_ != nullptr) {
        lv_obj_del(container_);
    }

    if (panel_ != nullptr) {
        esp_lcd_panel_del(panel_);
    }
    if (panel_io_ != nullptr) {
        esp_lcd_panel_io_del(panel_io_);
    }
    lvgl_port_deinit();
}

bool OledDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock(timeout_ms);
}

void OledDisplay::Unlock() {
    lvgl_port_unlock();
}

void OledDisplay::SetChatMessage(const char* role, const char* content) {
#if CONFIG_OLED_ASCII_FACE_128X64
    (void)role;
    (void)content;
    return;
#endif
    DisplayLockGuard lock(this);
    if (chat_message_label_ == nullptr) {
        return;
    }

    // Replace all newlines with spaces
    std::string content_str = content;
    std::replace(content_str.begin(), content_str.end(), '\n', ' ');

    if (content_right_ == nullptr) {
        lv_label_set_text(chat_message_label_, content_str.c_str());
    } else {
        if (content == nullptr || content[0] == '\0') {
            lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_label_set_text(chat_message_label_, content_str.c_str());
            lv_obj_remove_flag(content_right_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

#if CONFIG_OLED_ASCII_FACE_128X64

enum class MouthStyle {
    Flat,
    Smile,
    Frown,
    Wave,
    OpenWide,
    Heart,
    Cup,
    Star,
    Circle,
    Link,
    DotsAnim,
    Custom,
};

struct AsciiFaceParts {
    char left_eye;
    char right_eye;
    char mid_overlay;
    MouthStyle mouth;
    const char* custom_mouth;
    bool chip_eyes;
};

static int GetFaceColumnCount(const lv_font_t* font) {
    if (font == nullptr) {
        return 16;
    }

    int char_w = lv_font_get_glyph_width(font, 'O', 0);
    if (char_w <= 0) {
        char_w = 8;
    }

    int cols = static_cast<int>(LV_HOR_RES) / char_w;
    if (cols < 11) {
        cols = 11;
    } else if (cols > 20) {
        cols = 20;
    }
    return cols;
}

static std::string PadCentered(const std::string& text, int cols) {
    if (static_cast<int>(text.size()) >= cols) {
        return text.substr(0, cols);
    }
    std::string line(cols, ' ');
    int offset = (cols - static_cast<int>(text.size())) / 2;
    for (size_t i = 0; i < text.size(); ++i) {
        line[offset + static_cast<int>(i)] = text[i];
    }
    return line;
}

static std::string BuildEyeLine(char left, char right, int cols) {
    std::string line(cols, '_');
    line.front() = left;
    line.back() = right;
    return line;
}

static std::string BuildChipEyeLine(int cols) {
    std::string line(cols, '_');
    if (cols >= 7) {
        line[0] = '[';
        line[1] = '#';
        line[2] = ']';
        line[cols - 3] = '[';
        line[cols - 2] = '#';
        line[cols - 1] = ']';
    }
    return line;
}

static std::string BuildThickMidLine(int cols, char fill = '=') {
    return std::string(cols, fill);
}

static std::string BuildMidLineWithOverlay(int cols, char overlay, char fill = '=') {
    auto line = BuildThickMidLine(cols, fill);
    line[cols / 2] = overlay;
    return line;
}

static std::string BuildMouthLine(MouthStyle style, int cols, int anim_frame) {
    switch (style) {
    case MouthStyle::Flat:
        return std::string(cols, '-');
    case MouthStyle::Smile: {
        std::string line(cols, '_');
        line.front() = '\\';
        line.back() = '/';
        return line;
    }
    case MouthStyle::Frown: {
        std::string line(cols, '_');
        line.front() = '/';
        line.back() = '\\';
        return line;
    }
    case MouthStyle::Wave: {
        std::string line(cols, '_');
        line.front() = '~';
        line.back() = '~';
        return line;
    }
    case MouthStyle::OpenWide: {
        std::string line(cols, '_');
        line.front() = '(';
        line.back() = ')';
        return line;
    }
    case MouthStyle::Heart: {
        auto line = std::string(cols, '-');
        int mid = cols / 2;
        if (mid > 0) {
            line[mid - 1] = '<';
        }
        line[mid] = '3';
        return line;
    }
    case MouthStyle::Cup: {
        auto line = std::string(cols, '-');
        line[cols / 2] = 'U';
        return line;
    }
    case MouthStyle::Star: {
        auto line = std::string(cols, '-');
        line[cols / 2] = '*';
        return line;
    }
    case MouthStyle::Circle: {
        auto line = std::string(cols, '-');
        line[cols / 2] = 'O';
        return line;
    }
    case MouthStyle::Link:
        return PadCentered("--O--", cols);
    case MouthStyle::DotsAnim: {
        static const char* patterns[] = {". . . . .", ".. .. ..", "... ..."};
        return PadCentered(patterns[anim_frame % 3], cols);
    }
    case MouthStyle::Custom:
    default:
        return std::string(cols, '-');
    }
}

static std::string BuildAsciiFace(const AsciiFaceParts& parts, int cols, int anim_frame = 0) {
    std::string text;
    text.reserve(static_cast<size_t>(cols) * 3 + 4);

    if (parts.chip_eyes) {
        text += BuildChipEyeLine(cols);
    } else {
        text += BuildEyeLine(parts.left_eye, parts.right_eye, cols);
    }
    text.push_back('\n');

    if (parts.mid_overlay != '\0') {
        text += BuildMidLineWithOverlay(cols, parts.mid_overlay);
    } else {
        text += BuildThickMidLine(cols);
    }
    text.push_back('\n');

    if (parts.mouth == MouthStyle::Custom && parts.custom_mouth != nullptr) {
        text += PadCentered(parts.custom_mouth, cols);
    } else {
        text += BuildMouthLine(parts.mouth, cols, anim_frame);
    }
    return text;
}

static const AsciiFaceParts* LookupEmotionFace(const char* emotion) {
    static const struct {
        const char* name;
        AsciiFaceParts parts;
    } kEmotionFaces[] = {
        {"neutral",     {'O', 'O', '\0', MouthStyle::Flat, nullptr, false}},
        {"happy",       {'^', '^', '\0', MouthStyle::Smile, nullptr, false}},
        {"laughing",    {'*', '*', '\0', MouthStyle::Smile, nullptr, false}},
        {"funny",       {'^', '^', 'o', MouthStyle::Wave, nullptr, false}},
        {"sad",         {'>', '<', '\0', MouthStyle::Flat, nullptr, false}},
        {"angry",       {'>', '<', '\0', MouthStyle::Frown, nullptr, false}},
        {"crying",      {'T', 'T', '\0', MouthStyle::Frown, nullptr, false}},
        {"loving",      {'*', '*', '\0', MouthStyle::Heart, nullptr, false}},
        {"embarrassed", {'@', '@', '\0', MouthStyle::Flat, nullptr, false}},
        {"surprised",   {'O', 'O', 'o', MouthStyle::Circle, nullptr, false}},
        {"shocked",     {'O', 'O', 'O', MouthStyle::Circle, nullptr, false}},
        {"thinking",    {'O', '.', '\0', MouthStyle::Flat, nullptr, false}},
        {"winking",     {'^', 'O', '\0', MouthStyle::Flat, nullptr, false}},
        {"cool",        {'O', 'O', '\0', MouthStyle::Flat, nullptr, false}},
        {"relaxed",     {'-', '-', '\0', MouthStyle::Flat, nullptr, false}},
        {"delicious",   {'^', '^', '\0', MouthStyle::Cup, nullptr, false}},
        {"kissy",       {'*', '*', '\0', MouthStyle::Star, nullptr, false}},
        {"confident",   {'>', '<', '\0', MouthStyle::Smile, nullptr, false}},
        {"sleepy",      {'-', '-', 'z', MouthStyle::Flat, nullptr, false}},
        {"silly",       {'^', 'O', '\0', MouthStyle::Smile, nullptr, false}},
        {"confused",    {'?', '?', '\0', MouthStyle::Flat, nullptr, false}},
        {"microchip_ai",{'O', 'O', '\0', MouthStyle::Flat, nullptr, true}},
        {"link",        {'O', 'O', '\0', MouthStyle::Link, nullptr, false}},
        {"triangle_exclamation", {'X', 'X', '!', MouthStyle::Flat, nullptr, false}},
        {"circle_xmark",{'X', 'X', '!', MouthStyle::Flat, nullptr, false}},
        {"cloud_slash", {'>', '<', '\0', MouthStyle::Flat, nullptr, false}},
        {"cloud_arrow_down", {'O', 'O', '.', MouthStyle::DotsAnim, nullptr, false}},
    };

    if (emotion == nullptr) {
        return nullptr;
    }
    for (const auto& item : kEmotionFaces) {
        if (strcmp(emotion, item.name) == 0) {
            return &item.parts;
        }
    }
    return nullptr;
}

static const AsciiFaceParts kNeutralFace = {'O', 'O', '\0', MouthStyle::Flat, nullptr, false};
static const AsciiFaceParts kErrorFace = {'X', 'X', '!', MouthStyle::Flat, nullptr, false};

static const AsciiFaceParts* GetStateOverlayFace(DeviceState state, int anim_frame) {
    switch (state) {
        case kDeviceStateListening: {
            static const AsciiFaceParts frames[] = {
                {'^', '^', 'o', MouthStyle::Flat, nullptr, false},
                {'-', '-', 'o', MouthStyle::Flat, nullptr, false},
            };
            return &frames[anim_frame % 2];
        }
        case kDeviceStateConnecting: {
            static const AsciiFaceParts frames[] = {
                {'.', '.', 'o', MouthStyle::DotsAnim, nullptr, false},
                {'O', 'O', '-', MouthStyle::DotsAnim, nullptr, false},
            };
            return &frames[anim_frame % 2];
        }
        case kDeviceStateSpeaking: {
            static const AsciiFaceParts frames[] = {
                {'O', 'O', 'O', MouthStyle::Flat, nullptr, false},
                {'O', 'O', 'o', MouthStyle::Flat, nullptr, false},
            };
            return &frames[anim_frame % 2];
        }
        case kDeviceStateStarting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
        case kDeviceStateAudioTesting: {
            static const AsciiFaceParts frames[] = {
                {'O', 'O', '.', MouthStyle::DotsAnim, nullptr, false},
                {'O', 'O', '.', MouthStyle::DotsAnim, nullptr, false},
                {'O', 'O', '.', MouthStyle::DotsAnim, nullptr, false},
            };
            return &frames[anim_frame % 3];
        }
        case kDeviceStateFatalError:
            return &kErrorFace;
        default:
            return nullptr;
    }
}

static bool StateNeedsAnimation(DeviceState state) {
    switch (state) {
        case kDeviceStateListening:
        case kDeviceStateConnecting:
        case kDeviceStateSpeaking:
        case kDeviceStateStarting:
        case kDeviceStateActivating:
        case kDeviceStateUpgrading:
        case kDeviceStateAudioTesting:
            return true;
        default:
            return false;
    }
}

void OledDisplay::FaceAnimTimerCallback(void* arg) {
    auto* display = static_cast<OledDisplay*>(arg);
    display->face_anim_frame_++;
    if (!display->Lock(0)) {
        return;
    }
    display->RenderAsciiFaceUnlocked();
    display->Unlock();
}

void OledDisplay::StartFaceAnimation() {
    if (face_anim_timer_ == nullptr) {
        esp_timer_create_args_t timer_args = {
            .callback = FaceAnimTimerCallback,
            .arg = this,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "ascii_face_anim",
            .skip_unhandled_events = true,
        };
        ESP_ERROR_CHECK(esp_timer_create(&timer_args, &face_anim_timer_));
    }
    esp_timer_stop(face_anim_timer_);
    ESP_ERROR_CHECK(esp_timer_start_periodic(face_anim_timer_, 500000));
}

void OledDisplay::StopFaceAnimation() {
    if (face_anim_timer_ != nullptr) {
        esp_timer_stop(face_anim_timer_);
        esp_timer_delete(face_anim_timer_);
        face_anim_timer_ = nullptr;
    }
    face_anim_frame_ = 0;
}

void OledDisplay::SetupUI_128x64_AsciiFace() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_radius(container_, 0, 0);
    lv_obj_set_style_bg_opa(container_, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(container_, LV_OBJ_FLAG_SCROLLABLE);

    face_label_ = lv_label_create(container_);
    lv_obj_set_width(face_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(face_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_line_space(face_label_, 6, 0);
    lv_obj_set_style_pad_all(face_label_, 0, 0);
    lv_obj_center(face_label_);
    lv_label_set_text(face_label_, "");

    current_emotion_ = "neutral";
    RenderAsciiFaceUnlocked();
}

void OledDisplay::RenderAsciiFaceUnlocked() {
    if (face_label_ == nullptr) {
        return;
    }

    const AsciiFaceParts* face = nullptr;
    const char* emotion = current_emotion_.c_str();
    DeviceState state = Application::GetInstance().GetDeviceState();

    if (strcmp(emotion, "neutral") != 0) {
        face = LookupEmotionFace(emotion);
    }
    if (face == nullptr) {
        face = GetStateOverlayFace(state, face_anim_frame_);
    }
    if (face == nullptr) {
        face = &kNeutralFace;
    }

    const lv_font_t* font = lv_obj_get_style_text_font(face_label_, LV_PART_MAIN);
    const int cols = GetFaceColumnCount(font);
    lv_label_set_text(face_label_, BuildAsciiFace(*face, cols, face_anim_frame_).c_str());

    if (StateNeedsAnimation(state) && strcmp(emotion, "neutral") == 0) {
        if (face_anim_timer_ == nullptr) {
            StartFaceAnimation();
        }
    } else {
        StopFaceAnimation();
    }
}

void OledDisplay::RenderAsciiFace() {
    DisplayLockGuard lock(this);
    RenderAsciiFaceUnlocked();
}

void OledDisplay::SetStatus(const char* status) {
    (void)status;
    RenderAsciiFace();
}

void OledDisplay::ShowNotification(const char* notification, int duration_ms) {
    (void)notification;
    (void)duration_ms;
}

void OledDisplay::ClearChatMessages() {
}

void OledDisplay::UpdateStatusBar(bool update_all) {
    (void)update_all;
}

#endif // CONFIG_OLED_ASCII_FACE_128X64

void OledDisplay::SetupUI_128x64() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
    lv_obj_set_style_text_color(screen, lv_color_black(), 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_row(container_, 0, 0);

    /* Layer 1: Top bar - for status icons */
    top_bar_ = lv_obj_create(container_);
    lv_obj_set_size(top_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(top_bar_, 0, 0);
    lv_obj_set_style_bg_opa(top_bar_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(top_bar_, 0, 0);
    lv_obj_set_style_pad_all(top_bar_, 0, 0);
    lv_obj_set_flex_flow(top_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top_bar_, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(top_bar_, LV_SCROLLBAR_MODE_OFF);

    network_label_ = lv_label_create(top_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    lv_obj_t* right_icons = lv_obj_create(top_bar_);
    lv_obj_set_size(right_icons, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(right_icons, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_icons, 0, 0);
    lv_obj_set_style_pad_all(right_icons, 0, 0);
    lv_obj_set_flex_flow(right_icons, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(right_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    mute_label_ = lv_label_create(right_icons);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    battery_label_ = lv_label_create(right_icons);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    /* Layer 2: Status bar - for center text labels */
    status_bar_ = lv_obj_create(screen);
    lv_obj_set_size(status_bar_, LV_HOR_RES, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_style_bg_opa(status_bar_, LV_OPA_TRANSP, 0);  // Transparent background
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_scrollbar_mode(status_bar_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_layout(status_bar_, LV_LAYOUT_NONE, 0);  // Use absolute positioning
    lv_obj_align(status_bar_, LV_ALIGN_TOP_MID, 0, 0);  // Overlap with top_bar_

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(notification_label_, LV_HOR_RES);
    lv_obj_set_style_text_align(notification_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_align(notification_label_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_width(status_label_, LV_HOR_RES);
    lv_label_set_long_mode(status_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(status_label_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);
    lv_obj_align(status_label_, LV_ALIGN_CENTER, 0, 0);

    /* Content */
    content_ = lv_obj_create(container_);
    lv_obj_set_scrollbar_mode(content_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_radius(content_, 0, 0);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_width(content_, LV_HOR_RES);
    lv_obj_set_flex_grow(content_, 1);
    lv_obj_set_flex_flow(content_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_flex_main_place(content_, LV_FLEX_ALIGN_CENTER, 0);

    content_left_ = lv_obj_create(content_);
    lv_obj_set_size(content_left_, 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content_left_, 0, 0);
    lv_obj_set_style_border_width(content_left_, 0, 0);

    emotion_label_ = lv_label_create(content_left_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_MICROCHIP_AI);
    lv_obj_center(emotion_label_);
    lv_obj_set_style_pad_top(emotion_label_, 8, 0);

    content_right_ = lv_obj_create(content_);
    lv_obj_set_size(content_right_, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_all(content_right_, 0, 0);
    lv_obj_set_style_border_width(content_right_, 0, 0);
    lv_obj_set_flex_grow(content_right_, 1);
    lv_obj_add_flag(content_right_, LV_OBJ_FLAG_HIDDEN);

    chat_message_label_ = lv_label_create(content_right_);
    lv_label_set_text(chat_message_label_, "");
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(chat_message_label_, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_width(chat_message_label_, width_ - 32);
    lv_obj_set_style_pad_top(chat_message_label_, 14, 0);

    // Start scrolling subtitle after a delay
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000), LV_PART_MAIN);

    low_battery_popup_ = lv_obj_create(screen);
    lv_obj_set_scrollbar_mode(low_battery_popup_, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_size(low_battery_popup_, LV_HOR_RES * 0.9, text_font->line_height * 2);
    lv_obj_align(low_battery_popup_, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(low_battery_popup_, lv_color_black(), 0);
    lv_obj_set_style_radius(low_battery_popup_, 10, 0);
    low_battery_label_ = lv_label_create(low_battery_popup_);
    lv_label_set_text(low_battery_label_, Lang::Strings::BATTERY_NEED_CHARGE);
    lv_obj_set_style_text_color(low_battery_label_, lv_color_white(), 0);
    lv_obj_center(low_battery_label_);
    lv_obj_add_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN);
}

void OledDisplay::SetupUI_128x32() {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    auto icon_font = lvgl_theme->icon_font()->font();
    auto large_icon_font = lvgl_theme->large_icon_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);

    /* Container */
    container_ = lv_obj_create(screen);
    lv_obj_set_size(container_, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_flex_flow(container_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(container_, 0, 0);
    lv_obj_set_style_border_width(container_, 0, 0);
    lv_obj_set_style_pad_column(container_, 0, 0);

    /* Emotion label on the left side */
    content_ = lv_obj_create(container_);
    lv_obj_set_size(content_, 32, 32);
    lv_obj_set_style_pad_all(content_, 0, 0);
    lv_obj_set_style_border_width(content_, 0, 0);
    lv_obj_set_style_radius(content_, 0, 0);

    emotion_label_ = lv_label_create(content_);
    lv_obj_set_style_text_font(emotion_label_, large_icon_font, 0);
    lv_label_set_text(emotion_label_, FONT_AWESOME_MICROCHIP_AI);
    lv_obj_center(emotion_label_);

    /* Right side */
    side_bar_ = lv_obj_create(container_);
    lv_obj_set_size(side_bar_, width_ - 32, 32);
    lv_obj_set_flex_flow(side_bar_, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_all(side_bar_, 0, 0);
    lv_obj_set_style_border_width(side_bar_, 0, 0);
    lv_obj_set_style_radius(side_bar_, 0, 0);
    lv_obj_set_style_pad_row(side_bar_, 0, 0);

    /* Status bar */
    status_bar_ = lv_obj_create(side_bar_);
    lv_obj_set_size(status_bar_, width_ - 32, 16);
    lv_obj_set_style_radius(status_bar_, 0, 0);
    lv_obj_set_flex_flow(status_bar_, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_all(status_bar_, 0, 0);
    lv_obj_set_style_border_width(status_bar_, 0, 0);
    lv_obj_set_style_pad_column(status_bar_, 0, 0);

    status_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(status_label_, 1);
    lv_obj_set_style_pad_left(status_label_, 2, 0);
    lv_label_set_text(status_label_, Lang::Strings::INITIALIZING);

    notification_label_ = lv_label_create(status_bar_);
    lv_obj_set_flex_grow(notification_label_, 1);
    lv_obj_set_style_pad_left(notification_label_, 2, 0);
    lv_label_set_text(notification_label_, "");
    lv_obj_add_flag(notification_label_, LV_OBJ_FLAG_HIDDEN);

    mute_label_ = lv_label_create(status_bar_);
    lv_label_set_text(mute_label_, "");
    lv_obj_set_style_text_font(mute_label_, icon_font, 0);

    network_label_ = lv_label_create(status_bar_);
    lv_label_set_text(network_label_, "");
    lv_obj_set_style_text_font(network_label_, icon_font, 0);

    battery_label_ = lv_label_create(status_bar_);
    lv_label_set_text(battery_label_, "");
    lv_obj_set_style_text_font(battery_label_, icon_font, 0);

    chat_message_label_ = lv_label_create(side_bar_);
    lv_obj_set_size(chat_message_label_, width_ - 32, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_left(chat_message_label_, 2, 0);
    lv_label_set_long_mode(chat_message_label_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(chat_message_label_, "");

    // Start scrolling subtitle after a delay
    static lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_delay(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_obj_set_style_anim(chat_message_label_, &a, LV_PART_MAIN);
    lv_obj_set_style_anim_duration(chat_message_label_, lv_anim_speed_clamped(60, 300, 60000), LV_PART_MAIN);
}

void OledDisplay::SetEmotion(const char* emotion) {
#if CONFIG_OLED_ASCII_FACE_128X64
    if (emotion != nullptr && emotion[0] != '\0') {
        current_emotion_ = emotion;
    } else {
        current_emotion_ = "neutral";
    }
    RenderAsciiFace();
    return;
#endif
    const char* utf8 = font_awesome_get_utf8(emotion);
    DisplayLockGuard lock(this);
    if (emotion_label_ == nullptr) {
        return;
    }
    if (utf8 != nullptr) {
        lv_label_set_text(emotion_label_, utf8);
    } else {
        lv_label_set_text(emotion_label_, FONT_AWESOME_NEUTRAL);
    }
}

void OledDisplay::SetTheme(Theme* theme) {
    DisplayLockGuard lock(this);

    auto lvgl_theme = static_cast<LvglTheme*>(theme);
    auto text_font = lvgl_theme->text_font()->font();

    auto screen = lv_screen_active();
    lv_obj_set_style_text_font(screen, text_font, 0);
}
