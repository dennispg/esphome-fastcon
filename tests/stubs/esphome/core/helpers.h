#pragma once
#include <cstdint>
#include <cstring>
namespace esphome {
inline uint32_t random_uint32() { return 0x12345678; }
template<typename T> class Parented {
public:
    void set_parent(T *parent) { parent_ = parent; }
protected:
    T *parent_ = nullptr;
};
}
