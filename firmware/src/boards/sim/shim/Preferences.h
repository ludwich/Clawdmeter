#pragma once
// In-memory stand-in for ESP32 NVS Preferences (brightness.cpp,
// clawd_prefs.cpp, pages.cpp). Keys live per namespace, as in NVS. State
// lasts for the process lifetime only — the sim boots with defaults.
#include <stdint.h>
#include <stddef.h>

class Preferences {
public:
    bool begin(const char* name, bool read_only = false);
    void end(void) {}
    uint8_t getUChar(const char* key, uint8_t def = 0);
    size_t  putUChar(const char* key, uint8_t value);
    int32_t getInt(const char* key, int32_t def = 0);
    size_t  putInt(const char* key, int32_t value);
    // As on the ESP32: 0 when the key is missing or its blob is longer than
    // max_len; otherwise the blob's length.
    size_t  getBytes(const char* key, void* buf, size_t max_len);
    size_t  putBytes(const char* key, const void* value, size_t len);
private:
    char ns[16] = "";
};
