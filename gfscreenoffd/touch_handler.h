/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef _OFF_TOUCH_HANDLER_H
#define _OFF_TOUCH_HANDLER_H

#include <linux/input.h>

#include <chrono>
#include <string>

// keyCode from kernel to detect pressed fp
#define KEY_FOD_SCRNOFF_DOWN 325
// keyCode that will be fired for client side (DeviceKeyHandler)
#define KEY_FOD_GESTURE_DOWN 745
// time delay before resetting touch state
// usually waiting for doze.pulse launch to be done
#define TOUCH_RESET_DELAY_MS 350

static constexpr const char* fodStatusPath = "/sys/devices/virtual/touch/tp_dev/fod_status";
static constexpr const char* fodTestPath = "/sys/devices/virtual/touch/tp_dev/fod_test";
static constexpr const char* fblankPath = "/sys/class/backlight/panel0-backlight/bl_power";

namespace vendor {
namespace chaldeastudio {
namespace gfscreenoffd {

class TouchHandler {
public:
    TouchHandler(const int& fd);
    void startListener();
    void releasePendingTouch(const int duration);
    void sendEvent(int fd, int type, int code, int value);

private:
    int mAreaPressed;
    int mVirtualInput;
    std::chrono::time_point<std::chrono::system_clock> mLastTouch;
    std::string mTouchEventPath;
};

} // namespace gfscreenoffd
} // namespace chaldeastudio
} // namespace vendor
#endif
