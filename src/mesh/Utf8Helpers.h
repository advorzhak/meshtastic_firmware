#pragma once

#include <cstddef>
#include <cstdint>

inline size_t utf8SafePrefixLength(const char *text, size_t maxBytes)
{
    size_t i = 0;
    size_t lastValid = 0;

    while (text[i] != '\0' && i < maxBytes) {
        const uint8_t lead = static_cast<uint8_t>(text[i]);
        size_t charLen = 0;

        if ((lead & 0x80) == 0x00) {
            charLen = 1;
        } else if ((lead & 0xE0) == 0xC0) {
            charLen = 2;
        } else if ((lead & 0xF0) == 0xE0) {
            charLen = 3;
        } else if ((lead & 0xF8) == 0xF0) {
            charLen = 4;
        } else {
            break;
        }

        if (i + charLen > maxBytes) {
            break;
        }

        bool validContinuation = true;
        for (size_t j = 1; j < charLen; ++j) {
            const uint8_t cont = static_cast<uint8_t>(text[i + j]);
            if ((cont & 0xC0) != 0x80) {
                validContinuation = false;
                break;
            }
        }
        if (!validContinuation) {
            break;
        }

        i += charLen;
        lastValid = i;
    }

    return lastValid;
}
