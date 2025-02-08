#pragma once

#include <cstdint>
#include <vector>

namespace esphome
{
    namespace fastcon
    {
        // Bit manipulation utilities
        uint8_t reverse_8(uint8_t d);
        uint16_t reverse_16(uint16_t d);

        // CRC calculation
        uint16_t crc16(const std::vector<uint8_t> &addr, const std::vector<uint8_t> &data);

        // Whitening context and functions
        struct WhiteningContext
        {
            uint32_t f_0x0;
            uint32_t f_0x4;
            uint32_t f_0x8;
            uint32_t f_0xc;
            uint32_t f_0x10;
            uint32_t f_0x14;
            uint32_t f_0x18;

            WhiteningContext() : f_0x0(0), f_0x4(0), f_0x8(0), f_0xc(0), f_0x10(0), f_0x14(0), f_0x18(0) {}
        };

        void whitening_init(uint32_t val, WhiteningContext &ctx);
        void whitening_encode(std::vector<uint8_t> &data, WhiteningContext &ctx);

        std::string vector_to_hex_string(std::vector<uint8_t> &data);
    } // namespace fastcon
} // namespace esphome