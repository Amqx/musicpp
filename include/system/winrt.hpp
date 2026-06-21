/**
 * @file winrt.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 21-Jun-26
 */

/**
 * This class creates a singleton object that makes sure WinRT is initialized before Apple Music Windows pollers
 * can utilize its functions. This makes sure init_apartment() and uninit_apartment() always gets called once.
 */

#pragma once

#include <winrt/base.h>

class WinRtInit final {
public:
    static void initialize() {
        static WinRtInit instance;
    }

    WinRtInit(const WinRtInit&) = delete;
    WinRtInit& operator=(const WinRtInit&) = delete;
    WinRtInit(WinRtInit&&) = delete;
    WinRtInit& operator=(WinRtInit&&) = delete;

private:
    WinRtInit() {
        winrt::init_apartment();
    }

    ~WinRtInit() {
        winrt::uninit_apartment();
    }
};
