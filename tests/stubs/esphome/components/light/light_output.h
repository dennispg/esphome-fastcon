#pragma once
#include <initializer_list>
#include "color_mode.h"
namespace esphome::light {
class LightState;
class LightTraits {
public:
    void set_supported_color_modes(std::initializer_list<ColorMode>) {}
    void set_min_mireds(float) {}
    void set_max_mireds(float) {}
};
class LightOutput {
public:
    virtual ~LightOutput() = default;
    virtual LightTraits get_traits() = 0;
    virtual void setup_state(LightState *) {}
    virtual void write_state(LightState *) = 0;
};
}
