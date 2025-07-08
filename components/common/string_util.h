// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <string>

/// @brief Create a printable string from a byte buffer.
///
/// This is intended to print SSIDs, which are just byte buffers and can contain any value, including UTF-8 emojis and
/// non-printable characters!
///
/// - Carriage return, line feed and tab are returned as: `\r`, `\n`, `\t`.
///
/// - Non-printable characters are escaped as hex value, e.g. `\xf0`
/// @param buf buffer
/// @param len length of buffer
/// @return string with printable characters, all other characters are escaped hex-characters or control characters like
/// `\n`.
std::string toPrintableString(const uint8_t *buf, size_t len);

/// @brief Replace characters in string buffer.
/// @param str string buffer
/// @param orig character to replace
/// @param rep replacement character
/// @return number of characters replaced
int replacechar(char *str, char orig, char rep);

/// Trim from the start (in place)
void ltrim(std::string &s);

/// Trim from the end (in place)
void rtrim(std::string &s);

// Trim from both ends (in place)
void trim(std::string &s);

// Trim from the start (copying)
std::string ltrim_copy(std::string s);

// Trim from the end (copying)
std::string rtrim_copy(std::string s);

// Trim from both ends (copying)
std::string trim_copy(std::string s);
