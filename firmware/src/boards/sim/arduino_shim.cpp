#include <Arduino.h>
#include <Preferences.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>

#include <map>
#include <string>
#include <vector>

#include "sim_platform.h"

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

// Under a controller (SIM_CTRL_FD, sim_ctrl.cpp) time is virtual: it starts
// at 0 and moves only through delay(), so a run is the same every time.
static uint64_t virtual_ms = 0;

unsigned long millis(void) {
    if (sim_ctrl_active()) return (unsigned long)virtual_ms;
    static uint64_t t0 = now_ms();
    return (unsigned long)(now_ms() - t0);
}

unsigned long micros(void) {
    if (sim_ctrl_active()) return (unsigned long)(virtual_ms * 1000ull);
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    static uint64_t t0 = (uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull;
    return (unsigned long)((uint64_t)ts.tv_sec * 1000000ull + (uint64_t)ts.tv_nsec / 1000ull - t0);
}

void delay(unsigned long ms) {
    if (sim_ctrl_active()) virtual_ms += ms;
    else usleep(ms * 1000);
}

size_t SimSerial::write(const uint8_t* buf, size_t len) {
    return fwrite(buf, 1, len, stdout);
}
void SimSerial::printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}
static char   rx_buf[512];
static size_t rx_head = 0, rx_len = 0;
int SimSerial::available(void) { return (int)rx_len; }
int SimSerial::read(void) {
    if (!rx_len) return -1;
    const char c = rx_buf[rx_head];
    rx_head = (rx_head + 1) % sizeof(rx_buf);
    rx_len--;
    return (uint8_t)c;
}
void SimSerial::feed(const char* line) {
    for (const char* c = line;; c++) {
        if (rx_len == sizeof(rx_buf)) return;
        rx_buf[(rx_head + rx_len) % sizeof(rx_buf)] = *c ? *c : '\n';
        rx_len++;
        if (!*c) return;
    }
}
SimSerial Serial;

// ---- Preferences: tiny in-memory key/value store, per namespace ----
static std::map<std::string, std::vector<uint8_t>> store;

static std::string pref_key(const char* ns, const char* key) {
    return std::string(ns) + '/' + key;
}

bool Preferences::begin(const char* name, bool read_only) {
    (void)read_only;
    strncpy(ns, name ? name : "", sizeof(ns) - 1);
    ns[sizeof(ns) - 1] = '\0';
    return true;
}
size_t Preferences::getBytes(const char* key, void* buf, size_t max_len) {
    auto it = store.find(pref_key(ns, key));
    if (it == store.end() || it->second.size() > max_len) return 0;
    if (!it->second.empty()) memcpy(buf, it->second.data(), it->second.size());
    return it->second.size();
}
size_t Preferences::putBytes(const char* key, const void* value, size_t len) {
    const uint8_t* p = (const uint8_t*)value;
    store[pref_key(ns, key)].assign(p, p + len);
    return len;
}
uint8_t Preferences::getUChar(const char* key, uint8_t def) {
    uint8_t v;
    return getBytes(key, &v, sizeof(v)) == sizeof(v) ? v : def;
}
size_t Preferences::putUChar(const char* key, uint8_t value) {
    return putBytes(key, &value, sizeof(value));
}
int32_t Preferences::getInt(const char* key, int32_t def) {
    int32_t v;
    return getBytes(key, &v, sizeof(v)) == sizeof(v) ? v : def;
}
size_t Preferences::putInt(const char* key, int32_t value) {
    return putBytes(key, &value, sizeof(value));
}
