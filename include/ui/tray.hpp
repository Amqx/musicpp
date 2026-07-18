/**
 * @file tray.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 17-Jul-26
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

#include "types/track.hpp"

/**
 * The system-tray icon and its right-click menu.
 */
class Tray {
public:
    /**
     * Registers the tray icon.
     * @param appName Window-class identifier; not user-visible.
     */
    explicit Tray(std::string appName);

    ~Tray();

    Tray(const Tray &) = delete;

    Tray &operator=(const Tray &) = delete;

    Tray(Tray &&) = delete;

    Tray &operator=(Tray &&) = delete;

    /**
     * Appends an item to the right-click context menu, in registration order.
     * @param label Menu text.
     * @param onClick Invoked on the UI thread when the item is chosen.
     */
    void addMenuItem(std::string label, std::function<void()> onClick) const;

    /**
     * Updates the icon tooltip to describe the given track. Thread-safe.
     * @param track Current track, or an empty identity when nothing is playing.
     */
    void setTooltip(const EnrichedTrack &track) const;

    /**
     * Pumps the message loop on the calling thread until the window is destroyed. Must run on the
     * thread that constructed the Tray.
     */
    void runMessageLoop() const;

    /**
     * Asks the message loop to exit: tears the icon down and ends runMessageLoop(). Thread-safe.
     */
    void requestQuit() const;

private:
    struct Impl;
    std::unique_ptr<Impl> _impl;
};
