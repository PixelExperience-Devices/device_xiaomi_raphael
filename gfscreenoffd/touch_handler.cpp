/* SPDX-License-Identifier: BSD-3-Clause */
#define LOG_TAG "gfscreenoffd"

#include "touch_handler.h"

#include <android-base/logging.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <unistd.h>

#include <chrono>

namespace vendor {
namespace chaldeastudio {
namespace gfscreenoffd {

static std::string FindTouchEv() {
    DIR* evd;
    int fd, eventTotal = 0;
    unsigned long evbit;

    evd = opendir("/dev/input");
    while (readdir(evd)) eventTotal++;
    closedir(evd);

    for (int i = 0; i <= eventTotal; i++) {
        std::string f("/dev/input/event" + std::to_string(i));
        if (access(f.c_str(), F_OK) != -1) {
            fd = open(f.c_str(), O_RDONLY | O_NONBLOCK);
            ioctl(fd, EVIOCGBIT(0, sizeof(evbit)), &evbit);
            if (evbit & (1 << EV_KEY) && evbit & (1 << EV_ABS)) {
                LOG(INFO) << "Found the touchscreen device at : " << f.c_str();
                close(fd);
                return f;
            }
            close(fd);
        }
    }

    return {};
}

static int UnblockFodStatus() {
    int fd, ret, status;

    fd = open(fodStatusPath, O_RDWR | O_NONBLOCK);
    ret = read(fd, &status, sizeof(status));

    if (ret < 0) {
        goto out;
    }

    if (!status) {
        LOG(INFO) << "FOD Touch listener is disabled, enabling.";
        status = 1;
        write(fd, &status, 1);
    }

out:
    close(fd);
    return ret;
}

TouchHandler::TouchHandler(const int& fd) {
    mAreaPressed = 0;
    mLastTouch = std::chrono::system_clock::now();
    mTouchEventPath = FindTouchEv();
    mVirtualInput = fd;
}

void TouchHandler::sendEvent(int fd, int type, int code, int value) {
    struct input_event ev {};

    memset(&ev, 0, sizeof(ev));
    ev.type = type;
    ev.code = code;
    ev.value = value;

    if (write(fd, &ev, sizeof(ev)) < 0) {
        LOG(ERROR) << "Failed to write input " << ev.code;
    }
}

void TouchHandler::releasePendingTouch(const int duration) {
    int fodTest = 0;
    int fd = open(fodTestPath, O_WRONLY);

    usleep(1000 * duration);
    write(fd, &fodTest, sizeof(fodTest));

    close(fd);
}

void TouchHandler::startListener() {
    double delta;
    input_event ev;
    int fdTouch, fdBlank, ret = 0;
    pollfd pfds[2];
    std::chrono::time_point<std::chrono::system_clock> now;

    if (mTouchEventPath.empty()) {
        LOG(ERROR) << "No touchscreen detected, exiting.";
        return;
    }

    fdTouch = open(mTouchEventPath.c_str(), O_RDONLY | O_NONBLOCK);
    pfds[0].fd = fdTouch;
    pfds[0].events = POLLIN;
    pfds[0].revents = 0;

    fdBlank = open(fblankPath, O_RDONLY | O_NONBLOCK);
    pfds[1].fd = fdBlank;
    pfds[1].events = POLLIN;
    pfds[1].revents = 0;

    LOG(INFO) << "Listening touchscreen";
    while (true) {
        usleep(20000);
        poll(pfds, 2, -1);

        // wait for screen off
        if (pfds[1].revents & POLLIN) {
            if (read(pfds[1].fd, &ret, sizeof(ret)) < 0) {
                LOG(ERROR) << "Unable to read blank state, exiting.";
                goto out;
            }
            if (ret <= FB_BLANK_NORMAL) {
                usleep(200000);
                continue;
            }
        }

        // unblock touch listener by enabling fod_status
        ret = UnblockFodStatus();
        if (ret < 0) {
            LOG(ERROR) << "Unable to read fod_status, exiting.";
            goto out;
        }

        if (pfds[0].revents & POLLIN) {
            if (read(pfds[0].fd, &ev, sizeof(ev)) != sizeof(ev)) {
                LOG(ERROR) << "Invalid event size, exiting.";
                goto out;
            }

            if (ev.code == KEY_FOD_SCRNOFF_DOWN && ev.value == 1) {
                now = std::chrono::system_clock::now();
                delta = std::chrono::duration<double, std::milli>(now - mLastTouch).count();
                if (mAreaPressed < 1 || delta > ((TOUCH_RESET_DELAY_MS / 2) * 3)) {
                    sendEvent(mVirtualInput, EV_KEY, KEY_FOD_GESTURE_DOWN, 1);
                    sendEvent(mVirtualInput, EV_KEY, KEY_FOD_GESTURE_DOWN, 0);
                    sendEvent(mVirtualInput, EV_SYN, SYN_REPORT, 0);
                    mAreaPressed++;
                    mLastTouch = std::chrono::system_clock::now();
                    releasePendingTouch(TOUCH_RESET_DELAY_MS);
                }
            }
        }
    }

out:
    close(fdBlank);
    close(fdTouch);
}

} // namespace gfscreenoffd
} // namespace chaldeastudio
} // namespace vendor
