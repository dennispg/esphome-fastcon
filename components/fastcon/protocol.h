#pragma once

#include <vector>
#include <array>
#include <cstdint>
#include "utils.h"

namespace esphome
{
    namespace fastcon
    {
        static const std::array<uint8_t, 4> DEFAULT_ENCRYPT_KEY = {0x5e, 0x36, 0x7b, 0xc4};
        static const std::array<uint8_t, 3> DEFAULT_BLE_FASTCON_ADDRESS = {0xC1, 0xC2, 0xC3};

        std::vector<uint8_t> get_rf_payload(const std::vector<uint8_t> &addr, const std::vector<uint8_t> &data);
        std::vector<uint8_t> prepare_payload(const std::vector<uint8_t> &addr, const std::vector<uint8_t> &data);

        // Discovery uses the same outgoing RF framing as normal control. Received bulb
        // advertisements contain a 16-byte body, or 18 bytes including company ID F0 FF.
        std::array<uint8_t, 4> app_mesh_key(uint16_t candidate);
        bool decode_factory_identity(const std::vector<uint8_t> &frame, std::array<uint8_t, 6> &did);
        bool decode_heartbeat(const std::vector<uint8_t> &frame, const std::array<uint8_t, 4> &key, uint8_t &light_id);
        std::vector<uint8_t> encode_discovery_command(uint8_t type, uint8_t sequence, const std::vector<uint8_t> &data,
                                                     const std::array<uint8_t, 4> &key, uint8_t safe_key);
    } // namespace fastcon
} // namespace esphome