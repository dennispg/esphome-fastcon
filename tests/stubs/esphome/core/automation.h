#pragma once
#include "helpers.h"
namespace esphome {
template<typename... Ts> class Action {
public:
    virtual ~Action() = default;
    virtual void play(const Ts &...x) = 0;
};
template<typename T> class TestTemplatable {
public:
    T value_{};
    template<typename... Ts> T value(const Ts &...) { return value_; }
};
}
#define TEMPLATABLE_VALUE(type, name) esphome::TestTemplatable<type> name##_;
