#pragma once
#include <string>
#include "light_output.h"
namespace esphome::light {
struct LightColorValues {
    bool state = false;
    float brightness = 1, red = 1, green = 1, blue = 1, warm_white = 1, cold_white = 1, color_temperature = 153;
    ColorMode mode = ColorMode::RGB;
    bool is_on() const { return state; }
    void set_state(bool on) { state = on; }
    ColorMode get_color_mode() const { return mode; }
    void set_brightness(float value) { brightness = value; }
#define FIELD(name) float get_##name() const { return name; }
    FIELD(brightness) FIELD(red) FIELD(green) FIELD(blue) FIELD(warm_white) FIELD(cold_white) FIELD(color_temperature)
#undef FIELD
    bool operator==(const LightColorValues &other) const { return state == other.state && brightness == other.brightness; }
};
class LightCall;
class LightState {
public:
    LightColorValues current_values, remote_values;
    LightOutput *output = nullptr;
    std::string name = "Test";
    const std::string &get_name() const { return name; }
    LightOutput *get_output() { return output; }
    LightCall make_call();
};
class LightCall {
public:
    LightState *state;
    void from_light_color_values(const LightColorValues &values) { state->remote_values = values; }
    void set_state(bool on) { state->remote_values.set_state(on); }
    void set_brightness(float brightness) { state->remote_values.set_brightness(brightness); }
    void set_transition_length(uint32_t) {}
    void perform() { state->current_values = state->remote_values; }
};
inline LightCall LightState::make_call() { return {this}; }
}
