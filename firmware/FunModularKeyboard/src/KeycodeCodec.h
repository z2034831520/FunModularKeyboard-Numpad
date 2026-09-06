#pragma once

#include <Arduino.h>

struct KeycodeNameEntry
{
    const char *name;
    uint8_t code;
};

inline const KeycodeNameEntry *keycodeNameEntries(size_t &count)
{
    static const KeycodeNameEntry entries[] = {
        {"A", 'a'}, {"B", 'b'}, {"C", 'c'}, {"D", 'd'}, {"E", 'e'}, {"F", 'f'}, {"G", 'g'}, {"H", 'h'}, {"I", 'i'}, {"J", 'j'}, {"K", 'k'}, {"L", 'l'}, {"M", 'm'}, {"N", 'n'}, {"O", 'o'}, {"P", 'p'}, {"Q", 'q'}, {"R", 'r'}, {"S", 's'}, {"T", 't'}, {"U", 'u'}, {"V", 'v'}, {"W", 'w'}, {"X", 'x'}, {"Y", 'y'}, {"Z", 'z'}, {"NUM_0", '0'}, {"NUM_1", '1'}, {"NUM_2", '2'}, {"NUM_3", '3'}, {"NUM_4", '4'}, {"NUM_5", '5'}, {"NUM_6", '6'}, {"NUM_7", '7'}, {"NUM_8", '8'}, {"NUM_9", '9'}, {"Space", ' '}, {",", ','}, {".", '.'}, {";", ';'}, {"'", '\''}, {"[", '['}, {"]", ']'}, {"\\", '\\'}, {"/", '/'}, {"-", '-'}, {"=", '='}, {"`", '`'}, {"Enter", 0xB0}, {"Esc", 0xB1}, {"Backspace", 0xB2}, {"Tab", 0xB3}, {"CapsLock", 0xC1}, {"F1", 0xC2}, {"F2", 0xC3}, {"F3", 0xC4}, {"F4", 0xC5}, {"F5", 0xC6}, {"F6", 0xC7}, {"F7", 0xC8}, {"F8", 0xC9}, {"F9", 0xCA}, {"F10", 0xCB}, {"F11", 0xCC}, {"F12", 0xCD}, {"PrintScreen", 0xCE}, {"ScrollLock", 0xCF}, {"Pause", 0xD0}, {"Insert", 0xD1}, {"Home", 0xD2}, {"PageUp", 0xD3}, {"Delete", 0xD4}, {"End", 0xD5}, {"PageDown", 0xD6}, {"Right", 0xD7}, {"Left", 0xD8}, {"Down", 0xD9}, {"Up", 0xDA}, {"NumLock", 0xDB}, {"Menu", 0xED}, {"LCtrl", 0x80}, {"LShift", 0x81}, {"LAlt", 0x82}, {"LWin", 0x83}, {"RCtrl", 0x84}, {"RShift", 0x85}, {"RAlt", 0x86}, {"RWin", 0x87}};

    count = sizeof(entries) / sizeof(entries[0]);
    return entries;
}

inline bool parseNumericKeycodeToken(const String &token, uint8_t &code)
{
    String normalized = token;
    normalized.trim();
    if (normalized.isEmpty() || normalized == "0")
    {
        code = 0;
        return normalized == "0";
    }

    char *end = nullptr;
    unsigned long value = 0;
    if (normalized.startsWith("0x") || normalized.startsWith("0X"))
    {
        value = strtoul(normalized.c_str() + 2, &end, 16);
        if (end != nullptr && *end == '\0' && value <= 0xFF)
        {
            code = static_cast<uint8_t>(value);
            return true;
        }
    }

    value = strtoul(normalized.c_str(), &end, 16);
    if (end != nullptr && *end == '\0' && value <= 0xFF)
    {
        code = static_cast<uint8_t>(value);
        return true;
    }

    value = strtoul(normalized.c_str(), &end, 10);
    if (end != nullptr && *end == '\0' && value <= 0xFF)
    {
        code = static_cast<uint8_t>(value);
        return true;
    }

    return false;
}

inline bool lookupKeycodeByName(const String &token, uint8_t &code)
{
    String normalized = token;
    normalized.trim();
    if (normalized.isEmpty())
    {
        return false;
    }

    size_t count = 0;
    const KeycodeNameEntry *entries = keycodeNameEntries(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (normalized.equalsIgnoreCase(entries[i].name))
        {
            code = entries[i].code;
            return true;
        }
    }

    return parseNumericKeycodeToken(normalized, code);
}

inline String lookupKeyNameByCode(uint8_t code)
{
    size_t count = 0;
    const KeycodeNameEntry *entries = keycodeNameEntries(count);
    for (size_t i = 0; i < count; ++i)
    {
        if (entries[i].code == code)
        {
            return String(entries[i].name);
        }
    }

    return "0x" + String(code, HEX);
}

inline bool isNamedKeySequence(const String &value)
{
    String normalized = value;
    normalized.trim();
    if (normalized.isEmpty())
    {
        return false;
    }

    int start = 0;
    while (start < normalized.length())
    {
        int end = normalized.indexOf('+', start);
        if (end == -1)
        {
            end = normalized.length();
        }

        String token = normalized.substring(start, end);
        token.trim();
        if (token.isEmpty())
        {
            return false;
        }

        uint8_t code = 0;
        if (!lookupKeycodeByName(token, code) || code == 0)
        {
            return false;
        }

        start = end + 1;
    }

    return true;
}

inline String buildNamedKeySequence(const uint8_t *keys, uint8_t count)
{
    if (count == 0)
    {
        return "";
    }

    String sequence = lookupKeyNameByCode(keys[0]);
    for (uint8_t i = 1; i < count; ++i)
    {
        sequence += "+";
        sequence += lookupKeyNameByCode(keys[i]);
    }
    return sequence;
}