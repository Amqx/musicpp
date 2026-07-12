/**
 * @file credentials.hpp
 * @author Jonathan Deng (https://github.com/Amqx)
 * @date 26-Jun-26
 */

#pragma once

#include <string>

std::string readCredential(const std::string &target);

void writeCredential(const std::string &target, const std::string &secret);

void deleteCredential(const std::string &target);