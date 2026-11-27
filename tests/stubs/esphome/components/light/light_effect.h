#pragma once
#include "light_state.h"
namespace esphome::light {
class LightEffect {
public:
    explicit LightEffect(const char *name) : name_(name) {}
    virtual ~LightEffect() = default;
    virtual void start() {} virtual void stop() {} virtual void apply() = 0;
    std::string get_name() const { return name_; }
protected:
    LightState *state_ = nullptr;
    std::string name_;
};
}
