/**
 * @file notifications.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 17-Jul-26
 */

#pragma once

#include <string>

/**
 * Shows a toast: @p title in bold with @p body beneath it.
 * @param title Bold headline line.
 * @param body Body text shown below the title.
 */
void showToastNotification(const std::string &title, const std::string &body);
