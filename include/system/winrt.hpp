/**
 * @file winrt.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

/**
 * This class makes sure WinRT is initialized before anything uses it.
 */

#pragma once

#include <winrt/base.h>

class WinRtInit final {
public:
    static void initialize() {
        static thread_local WinRtInit instance;
    }

    WinRtInit(const WinRtInit &) = delete;

    WinRtInit &operator=(const WinRtInit &) = delete;

    WinRtInit(WinRtInit &&) = delete;

    WinRtInit &operator=(WinRtInit &&) = delete;

private:
    WinRtInit() {
        winrt::init_apartment();
    }

    ~WinRtInit() {
        winrt::uninit_apartment();
    }
};