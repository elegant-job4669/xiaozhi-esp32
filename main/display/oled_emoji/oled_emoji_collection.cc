#include "oled_emoji_collection.h"

#define LV_IMAGE_DECLARE(name) extern const lv_image_dsc_t name;

LV_IMAGE_DECLARE(neutral);
LV_IMAGE_DECLARE(happy);
LV_IMAGE_DECLARE(laughing);
LV_IMAGE_DECLARE(funny);
LV_IMAGE_DECLARE(sad);
LV_IMAGE_DECLARE(angry);
LV_IMAGE_DECLARE(crying);
LV_IMAGE_DECLARE(loving);
LV_IMAGE_DECLARE(embarrassed);
LV_IMAGE_DECLARE(surprised);
LV_IMAGE_DECLARE(shocked);
LV_IMAGE_DECLARE(thinking);
LV_IMAGE_DECLARE(winking);
LV_IMAGE_DECLARE(cool);
LV_IMAGE_DECLARE(relaxed);
LV_IMAGE_DECLARE(delicious);
LV_IMAGE_DECLARE(kissy);
LV_IMAGE_DECLARE(confident);
LV_IMAGE_DECLARE(sleepy);
LV_IMAGE_DECLARE(silly);
LV_IMAGE_DECLARE(confused);

OledMonochromeEmoji64::OledMonochromeEmoji64() {
    AddEmoji("neutral", new LvglSourceImage(&neutral));
    AddEmoji("happy", new LvglSourceImage(&happy));
    AddEmoji("laughing", new LvglSourceImage(&laughing));
    AddEmoji("funny", new LvglSourceImage(&funny));
    AddEmoji("sad", new LvglSourceImage(&sad));
    AddEmoji("angry", new LvglSourceImage(&angry));
    AddEmoji("crying", new LvglSourceImage(&crying));
    AddEmoji("loving", new LvglSourceImage(&loving));
    AddEmoji("embarrassed", new LvglSourceImage(&embarrassed));
    AddEmoji("surprised", new LvglSourceImage(&surprised));
    AddEmoji("shocked", new LvglSourceImage(&shocked));
    AddEmoji("thinking", new LvglSourceImage(&thinking));
    AddEmoji("winking", new LvglSourceImage(&winking));
    AddEmoji("cool", new LvglSourceImage(&cool));
    AddEmoji("relaxed", new LvglSourceImage(&relaxed));
    AddEmoji("delicious", new LvglSourceImage(&delicious));
    AddEmoji("kissy", new LvglSourceImage(&kissy));
    AddEmoji("confident", new LvglSourceImage(&confident));
    AddEmoji("sleepy", new LvglSourceImage(&sleepy));
    AddEmoji("silly", new LvglSourceImage(&silly));
    AddEmoji("confused", new LvglSourceImage(&confused));
}
