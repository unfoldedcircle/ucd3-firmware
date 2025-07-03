// SPDX-FileCopyrightText: Copyright (c) 2024 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "string_util.h"

#include <string.h>

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <locale>
#include <sstream>

std::string toPrintableString(const uint8_t *buf, size_t len) {
    std::stringstream ss;

    for (int i = 0; i < len; i++) {
        if (buf[i] == 0) {
            break;
        } else if (std::isprint(buf[i])) {
            ss << static_cast<unsigned char>(buf[i]);
        } else if (buf[i] == '\r') {
            ss << "\\r";
        } else if (buf[i] == '\n') {
            ss << "\\n";
        } else if (buf[i] == '\t') {
            ss << "\\t";
        } else {
            ss << "\\x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(buf[i]);
        }
    }

    return ss.str();
}

int replacechar(char *str, char orig, char rep) {
    if (str == NULL) {
        return 0;
    }
    char *ix = str;
    int   n = 0;
    while ((ix = strchr(ix, orig)) != NULL) {
        *ix++ = rep;
        n++;
    }
    return n;
}

// @see Trim functions from: https://stackoverflow.com/a/217605
void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
}

void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
}

void trim(std::string &s) {
    rtrim(s);
    ltrim(s);
}

std::string ltrim_copy(std::string s) {
    ltrim(s);
    return s;
}

std::string rtrim_copy(std::string s) {
    rtrim(s);
    return s;
}

std::string trim_copy(std::string s) {
    trim(s);
    return s;
}
