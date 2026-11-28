#pragma once
#include <cstdint>
namespace esphome::light {
enum class ColorCapability : uint8_t { RGB = 4, WHITE = 8, COLD_WARM_WHITE = 16, COLOR_TEMPERATURE = 32 };
enum class ColorMode : uint8_t { BRIGHTNESS = 1, RGB = 4, WHITE = 8, COLD_WARM_WHITE = 16 };
}
