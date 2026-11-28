#pragma once
#include <cstdint>
namespace esphome {
inline uint32_t test_now = 0;
inline uint32_t millis() { return test_now; }
class Component {
public:
    bool warning = false;
    virtual ~Component() = default;
    virtual void setup() {}
    virtual void loop() {}
    void mark_failed() {}
    void status_set_warning(const char * = nullptr) { warning = true; }
    void status_clear_warning() { warning = false; }
};
}
