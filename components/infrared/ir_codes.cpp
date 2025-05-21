// SPDX-FileCopyrightText: Copyright (c) 2023 Unfolded Circle ApS and/or its affiliates <hello@unfoldedcircle.com>
//
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ir_codes.h"

#include <errno.h>

#include <climits>

uint32_t parseUint32(const char *number, int *error, int base) {
    if (number == NULL) {
        if (error != NULL) {
            *error = 1;
        }
        return 0;
    }
    char *end;
    // Attention: "unsigned long" returned from strtoul is only 32 bit on ESP32
    // but 64 bit when building & testing on the host!
    uint64_t value = strtoul(number, &end, base);
    if (end == number || *end != '\0' || errno == ERANGE || (value == ULONG_MAX && errno) || value > UINT32_MAX) {
        if (error != NULL) {
            *error = 1;
        }
        return 0;
    }

    if (error != NULL) {
        *error = 0;
    }
    return static_cast<uint32_t>(value);
}

decode_type_t parseProtocol(const char *input) {
    int      error;
    uint32_t protocol = parseUint32(input, &error);

    if (error || protocol > kLastDecodeType) {
        return UNKNOWN;
    }

    return static_cast<decode_type_t>(protocol);
}

uint64_t parseCommand(const char *input) {
    errno = 0;
    char *end;
    errno = 0;
    uint64_t command = strtoull(input, &end, 16);
    if (command == 0 && end == input) {
        // input was not a number
        return 0;
    } else if (command == UINT64_MAX && errno) {
        // the value of input does not fit in unsigned long long
        return 0;
    } else if (*end) {
        // input began with a number but has junk left over at the end
        return 0;
    }

    return command;
}

bool buildIRHexData(const std::string &message, IRHexData *data) {
    // Format is: "<protocol>;<hex-ir-code>;<bits>;<repeat-count>" e.g. "4;0x640C;15;0"
    int firstIndex = message.find_first_of(';');
    if (firstIndex == std::string::npos) {
        return false;
    }
    int secondIndex = message.find_first_of(';', firstIndex + 1);
    if (secondIndex == std::string::npos) {
        return false;
    }
    const int thirdIndex = message.find_first_of(';', secondIndex + 1);
    if (thirdIndex == std::string::npos) {
        return false;
    }

    data->protocol = parseProtocol(message.substr(0, firstIndex).c_str());
    if (data->protocol <= 0) {
        return false;
    }

    firstIndex++;
    data->command = parseCommand(message.substr(firstIndex, secondIndex - firstIndex).c_str());
    if (data->command == 0) {
        return false;
    }

    secondIndex++;
    int         error;
    std::string number = message.substr(secondIndex, thirdIndex - secondIndex);
    uint32_t    value = parseUint32(number.c_str(), &error, 10);
    if (error || value > 0xFFFF) {
        return false;
    }
    data->bits = value;
    if (data->bits == 0) {
        return false;
    }

    number = message.substr(thirdIndex + 1);
    value = parseUint32(number.c_str(), &error, 10);
    if (error || value > 0xFFFF || value > 20) {
        return false;
    }
    data->repeat = value;

    return true;
}

uint16_t countValuesInCStr(const char *str, char sep) {
    if (str == NULL || *str == 0) {
        return 0;
    }

    int16_t  index = 0;
    uint16_t count = 0;

    while (str[index] != 0) {
        if (str[index] == sep) {
            count++;
        }
        index++;
    }

    return count + 1;  // for value after last separator
}

uint16_t *prontoBufferToArray(const char *msg, char separator, uint16_t *codeCount, int *memError) {
    if (memError) {
        *memError = 0;
    }

    uint16_t count = countValuesInCStr(msg, separator);
    // minimal length is 6:
    // - preamble of 4 (raw, frequency, # code pairs sequence 1, # code pairs sequence 2)
    // - 1 code pair
    if (count < 6) {
        return NULL;
    }

    uint16_t *codeArray = reinterpret_cast<uint16_t *>(malloc(count * sizeof(uint16_t)));
    if (codeArray == NULL) {  // malloc failed, so give up.
        if (memError) {
            *memError = 1;
        }
        return NULL;
    }

    int16_t  index = 0;
    uint16_t startFrom = 0;
    count = 0;
    while (msg[index] != 0) {
        if (msg[index] == separator) {
            codeArray[count] = strtoul(msg + startFrom, NULL, 16);
            startFrom = index + 1;
            count++;
        }
        index++;
    }
    if (index > startFrom) {
        codeArray[count] = strtoul(msg + startFrom, NULL, 16);
        count++;
    }

    // Validate PRONTO code
    // Only raw pronto codes are supported
    if (codeArray[0] != 0) {
        free(codeArray);
        return NULL;
    }

    uint16_t seq1Len = codeArray[2] * 2;
    uint16_t seq2Len = codeArray[3] * 2;
    uint16_t seq1Start = 4;
    uint16_t seq2Start = seq1Start + seq1Len;

    if (seq1Len > 0 && seq1Len + seq1Start > count) {
        free(codeArray);
        return NULL;
    }

    if (seq2Len > 0 && seq2Len + seq2Start > count) {
        free(codeArray);
        return NULL;
    }

    *codeCount = count;
    return codeArray;
}

uint16_t *globalCacheBufferToArray(const char *msg, uint16_t *codeCount, int *memError) {
    if (memError) {
        *memError = 0;
    }

    char     separator = ',';
    uint16_t count = countValuesInCStr(msg, separator);

    uint16_t startIndex = 0;
    if (strncmp(msg, "sendir", 6) == 0) {
        count -= 3;
        startIndex = 3;
    }

    // minimal length is ???:
    if (count < 6) {
        return NULL;
    }

    uint16_t *codeArray = reinterpret_cast<uint16_t *>(malloc(count * sizeof(uint16_t)));
    if (codeArray == NULL) {  // malloc failed, so give up.
        if (memError) {
            *memError = 1;
        }
        return NULL;
    }

    int16_t  msgIndex = 0;
    uint16_t codeIndex = 0;
    uint16_t startFrom = 0;
    count = 0;
    while (msg[msgIndex] != 0) {
        if (msg[msgIndex] == separator) {
            if (count >= startIndex) {
                codeArray[codeIndex] = strtoul(msg + startFrom, NULL, 10);
                codeIndex++;
            }
            startFrom = msgIndex + 1;
            count++;
        }
        msgIndex++;
    }
    if (msgIndex > startFrom) {
        codeArray[codeIndex] = strtoul(msg + startFrom, NULL, 10);
        codeIndex++;
    }

    *codeCount = codeIndex;
    return codeArray;
}
