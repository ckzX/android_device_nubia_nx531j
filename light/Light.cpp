/*
 * Copyright (C) 2018 The LineageOS Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "LightService"

#include <log/log.h>

#include "Light.h"

#include <fstream>

#define LEDS            "/sys/class/leds/"

#define NUBIA_LED     LEDS "nubia_led/"
#define LCD_LED         LEDS "lcd-backlight/"
#define LCD_MAX_BRIGHTNESS_LED LCD_LED "max_brightness"

// Nubia LED
#define LED_BRIGHTNESS NUBIA_LED "brightness"
#define LED_BLINK_MODE NUBIA_LED "blink_mode"
#define LED_CHANNEL NUBIA_LED "outn"
#define LED_GRADE NUBIA_LED "grade_parameter"
#define LED_FADE NUBIA_LED "fade_parameter"


// Battery
#define BATTERY_CAPACITY "/sys/class/power_supply/battery/capacity"

#define BATTERY_CHARGING_STATUS "/sys/class/power_supply/battery/status"

// Blink mode
#define BLINK_MODE_ON 6
#define BLINK_MODE_OFF 1
#define BLINK_MODE_BREATH 3
#define BLINK_MODE_BREATH_ONCE 6

// Events
#define BREATH_SOURCE_NOTIFICATION 0x01
#define BREATH_SOURCE_BATTERY 0x02
#define BREATH_SOURCE_BUTTONS 0x04
#define BREATH_SOURCE_ATTENTION 0x08

// Outn channels
#define LED_CHANNEL_HOME 16
#define LED_CHANNEL_BUTTON 8

// Grade values
#define LED_GRADE_BUTTON 8
#define LED_GRADE_HOME 8
#define LED_GRADE_HOME_BATTERY_LOW 0
#define LED_GRADE_HOME_NOTIFICATION 6
#define LED_GRADE_HOME_BATTERY 6

// Max display brightness
#define MAX_LCD_BRIGHTNESS    255

static LightState g_battery;
static LightState g_notification;
static LightState g_attention;
static LightState g_buttons;

int initialized = 0;
std::mutex mLock;

namespace {
/*
 * Write value to path and close file.
 */
static void set(std::string path, std::string value) {
    std::ofstream file(path);

    if (!file.is_open()) {
        ALOGW("failed to write %s to %s", value.c_str(), path.c_str());
        return;
    }

    file << value;
}

static std::string get(std::string path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        ALOGW("failed to read %s", path.c_str());
        return "";
    }

    std::string value;
    file >> value;
    return value;
}

static int getInt(std::string path) {
    std::ifstream file(path);

    if (!file.is_open()) {
        ALOGW("failed to read %s", path.c_str());
        return 0;
    }

    int value;
    file >> value;
    return value;
}

static void set(std::string path, int value) {
    set(path, std::to_string(value));
}

static inline bool isLit(const LightState& state) {
    return state.color & 0x00ffffff;
}

static uint32_t rgbToBrightness(const LightState& state) {
    uint32_t color = state.color & 0x00ffffff;
    return ((77 * ((color >> 16) & 0xff)) + (150 * ((color >> 8) & 0xff)) +
            (29 * (color & 0xff))) >> 8;
}

static void setBreathingLightLocked(int event_source,const LightState& state){
    int brightness, blink;
    int onMS, offMS;
    
    switch (state.flashMode) {
        case Flash::TIMED:
            onMS = state.flashOnMs;
            offMS = state.flashOffMs;
            break;
        case Flash::NONE:
        default:
            onMS = 0;
            offMS = 0;
            break;
    }
    
    brightness = rgbToBrightness(state);
    blink = onMS > 0 && offMS > 0;
    
    if (blink)
    {
        char buffer[25];
        if (onMS == 1)
        { // Always
            onMS = -1;
        }
        else if (onMS > 1 && onMS <= 250)
        { // Very fast
            onMS = 0;
        }
        else if (onMS > 250 && onMS <= 500)
        { // Fast
            onMS = 1;
        }
        else if (onMS > 500 && onMS <= 1000)
        { // Normal
            onMS = 2;
        }
        else if (onMS > 1000 && onMS <= 2000)
        { // Long
            onMS = 3;
        }
        else if (onMS > 2000 && onMS <= 5000)
        { // Very long
            onMS = 4;
        }
        else if (onMS > 5000)
        {
            onMS = 5;
        }

        // We can not keep the notification button is constantly
        // illuminated. Therefore, disable it.
        if (onMS != -1)
        {
            if (offMS > 1 && offMS <= 250)
            { // Very fast
                offMS = 1;
            }
            else if (offMS > 250 && offMS <= 500)
            { // Fast
                offMS = 2;
            }
            else if (offMS > 500 && offMS <= 1000)
            { // Normal
                offMS = 3;
            }
            else if (offMS > 1000 && offMS <= 2000)
            { // Long
                offMS = 4;
            }
            else if (offMS > 2000 && offMS <= 5000)
            { // Very long
                offMS = 5;
            }
            else if (onMS > 5000)
            {
                offMS = 6;
            }
        } else {
            offMS = 0;
        }

        snprintf(buffer, sizeof(buffer), "%d %d %d\n", offMS, onMS, onMS);
        ALOGD(
            "fade_time(offMS)=%d fullon_time(onMS)=%d fulloff_time(onMS)=%d\n", offMS, onMS, onMS);
        set(LED_CHANNEL, LED_CHANNEL_HOME);
        set(LED_GRADE, LED_GRADE_HOME_NOTIFICATION);
        set(LED_FADE, buffer);
        set(LED_BLINK_MODE, BLINK_MODE_BREATH);
    } else {
        if (brightness <= 0) {
            // Disable Home LED
            set(LED_CHANNEL, LED_CHANNEL_HOME);
            set(LED_GRADE, 0);
            set(LED_FADE, "0 0 0");
            set(LED_BLINK_MODE, BLINK_MODE_OFF);
        } else {
            if (event_source == BREATH_SOURCE_BUTTONS) {
                set(LED_CHANNEL, LED_CHANNEL_HOME);
                set(LED_GRADE, LED_GRADE_BUTTON);
                set(LED_FADE, "1 0 0");
                set(LED_BLINK_MODE, BLINK_MODE_BREATH_ONCE);
            } else if (event_source == BREATH_SOURCE_BATTERY) {
                int grade;
                int blink_mode;

                // can't get battery info from state, getting it from sysfs
                int is_charging = 0;
                int capacity = 0;
                std::string charging_status = get(BATTERY_CHARGING_STATUS);
                if (charging_status=="Charging"
                    || charging_status=="Full") {
                    is_charging = 1;
                }
                capacity = getInt(BATTERY_CAPACITY);
                if (is_charging == 0) {
                    // battery low
                    grade = LED_GRADE_HOME_BATTERY_LOW;
                    blink_mode = BLINK_MODE_BREATH;
                } else {
                    grade = LED_GRADE_HOME_BATTERY;
                    if (capacity < 90) {
                        // battery chagring
                        blink_mode = BLINK_MODE_BREATH;
                    } else {
                        // battery full
                        blink_mode = BLINK_MODE_BREATH_ONCE;
                    }
                }
                set(LED_CHANNEL, LED_CHANNEL_HOME);
                set(LED_GRADE, grade);
                set(LED_FADE, "3 0 4");
                set(LED_BLINK_MODE, blink_mode);
            }
        }
    }
}

static void handleBreathingLightLocked() {
    if (isLit(g_attention)){
        setBreathingLightLocked(BREATH_SOURCE_ATTENTION, g_attention);
    } else if (isLit(g_notification)) {
        setBreathingLightLocked(BREATH_SOURCE_NOTIFICATION, g_notification);
    } else if (isLit(g_buttons)) {
        setBreathingLightLocked(BREATH_SOURCE_BUTTONS, g_buttons);
    } else {
        setBreathingLightLocked(BREATH_SOURCE_BATTERY, g_battery);
    }
}

static void handleBacklight(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    int max_brightness = getInt(LCD_MAX_BRIGHTNESS_LED);
    if (max_brightness < 0)
        max_brightness = MAX_LCD_BRIGHTNESS;
    
    uint32_t brightness = rgbToBrightness(state);
    
    if (max_brightness != MAX_LCD_BRIGHTNESS) {
        //int old_brightness = brightness;
        brightness = brightness * max_brightness / MAX_LCD_BRIGHTNESS;
    }
    
    set(LCD_LED "brightness", brightness);
}

static void handleButtons(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    
    uint32_t brightness = rgbToBrightness(state);
    
    g_buttons = state;
    
    if (brightness <= 0) {
        // Disable buttons
        set(LED_CHANNEL, LED_CHANNEL_BUTTON);
        set(LED_BLINK_MODE, BLINK_MODE_OFF);
        set(LED_BRIGHTNESS, 0);
        
        handleBreathingLightLocked();
    } else {
        if (initialized == 0) {
            // Kill buttons
            //set(LED_CHANNEL, LED_CHANNEL_BUTTON);
            set(LED_FADE, "0 0 0");
            set(LED_BLINK_MODE, BLINK_MODE_BREATH); // Disable all buttons keys (?)
            set(LED_BRIGHTNESS, 0); // Disable left key
            initialized = 1;
        }
        
        handleBreathingLightLocked();
        
        // Set buttons
        set(LED_CHANNEL, LED_CHANNEL_BUTTON);
        set(LED_BRIGHTNESS, brightness);
        set(LED_BLINK_MODE, BLINK_MODE_BREATH_ONCE);
    }
}

static void handleBattery(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    g_battery = state;
    handleBreathingLightLocked();
}

static void handleNotification(const LightState& state) {
    std::lock_guard<std::mutex> lock(mLock);
    uint32_t brightness;
    uint32_t color;
    uint32_t rgb[3];
    
    g_notification = state;
    
    brightness = (g_notification.color & 0xFF000000) >> 24;
    if (brightness > 0 && brightness < 0xFF)
    {
        // Retrieve each of the RGB colors
        color = g_notification.color & 0x00FFFFFF;
        rgb[0] = (color >> 16) & 0xFF;
        rgb[1] = (color >> 8) & 0xFF;
        rgb[2] = color & 0xFF;

        // Apply the brightness level
        if (rgb[0] > 0)
            rgb[0] = (rgb[0] * brightness) / 0xFF;
        if (rgb[1] > 0)
            rgb[1] = (rgb[1] * brightness) / 0xFF;
        if (rgb[2] > 0)
            rgb[2] = (rgb[2] * brightness) / 0xFF;

        // Update with the new color
        g_notification.color = (rgb[0] << 16) + (rgb[1] << 8) + rgb[2];
    }
    
    handleBreathingLightLocked();
}

static void handleAttention(const LightState& state) {
    g_attention = state;
    handleBreathingLightLocked();
}

/* Keep sorted in the order of importance. */
static std::vector<LightBackend> backends = {
    { Type::ATTENTION, handleAttention },
    { Type::NOTIFICATIONS, handleNotification },
    { Type::BATTERY, handleBattery },
    { Type::BACKLIGHT, handleBacklight },
    { Type::BUTTONS, handleButtons },
};

}  // anonymous namespace

namespace android {
namespace hardware {
namespace light {
namespace V2_0 {
namespace implementation {

Return<Status> Light::setLight(Type type, const LightState& state) {
    LightStateHandler handler;
    bool handled = false;

    /* Lock global mutex until light state is updated. */
    //std::lock_guard<std::mutex> lock(globalLock);

    /* Update the cached state value for the current type. */
    for (LightBackend& backend : backends) {
        if (backend.type == type) {
            backend.state = state;
            handler = backend.handler;
        }
    }

    /* If no handler has been found, then the type is not supported. */
    if (!handler) {
        return Status::LIGHT_NOT_SUPPORTED;
    }

    /* Light up the type with the highest priority that matches the current handler. */
    for (LightBackend& backend : backends) {
        if (handler == backend.handler && isLit(backend.state)) {
            handler(backend.state);
            handled = true;
            break;
        }
    }

    /* If no type has been lit up, then turn off the hardware. */
    if (!handled) {
        handler(state);
    }

    return Status::SUCCESS;
}

Return<void> Light::getSupportedTypes(getSupportedTypes_cb _hidl_cb) {
    std::vector<Type> types;

    for (const LightBackend& backend : backends) {
        types.push_back(backend.type);
    }

    _hidl_cb(types);

    return Void();
}

}  // namespace implementation
}  // namespace V2_0
}  // namespace light
}  // namespace hardware
}  // namespace android
