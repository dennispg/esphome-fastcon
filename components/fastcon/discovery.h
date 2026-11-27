#pragma once

#include <array>
#include <cstdint>

namespace esphome
{
    namespace fastcon
    {
        enum class DiscoveryPhase : uint8_t
        {
            FACTORY,
            RECOVERY,
            ENUMERATE,
            DONE,
            FAILED
        };

        struct DiscoveredBulb
        {
            uint8_t light_id{0};
            uint8_t claimed{0};
            std::array<uint8_t, 6> did{}; // identity bytes, in the bulb's wire order
            uint64_t mac{0};
        };

        // Fixed-size, versioned record: one atomic ESPHome preference per controller.
        // Save pending identities BEFORE SET_KEY, so a restart cannot lose their key/ID.
        struct DiscoveryRecord
        {
            uint32_t version{1};
            std::array<uint8_t, 4> key{};
            std::array<uint8_t, 4> configured_key{}; // explicit YAML key before a runtime override
            uint16_t candidate{0}; // next app key to try, 10000 means exhausted
            DiscoveryPhase phase{DiscoveryPhase::FACTORY};
            uint8_t count{0};
            uint8_t key_known{0};
            uint8_t runtime_override{0};
            std::array<DiscoveredBulb, 16> bulbs{};
        };
    } // namespace fastcon
} // namespace esphome
