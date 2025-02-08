#include <vector>
#include <cstdio>
#include "esphome/core/log.h"
#include "utils.h"

namespace esphome
{
    namespace fastcon
    {
        uint8_t reverse_8(uint8_t d)
        {
            uint8_t result = 0;
            for (int i = 0; i < 8; i++)
            {
                result |= ((d >> i) & 1) << (7 - i);
            }
            return result;
        }

        uint16_t reverse_16(uint16_t d)
        {
            uint16_t result = 0;
            for (int i = 0; i < 16; i++)
            {
                result |= ((d >> i) & 1) << (15 - i);
            }
            return result;
        }

        uint16_t crc16(const std::vector<uint8_t> &addr, const std::vector<uint8_t> &data)
        {
            uint16_t crc = 0xffff;

            // Process address in reverse
            for (auto it = addr.rbegin(); it != addr.rend(); ++it)
            {
                crc ^= (static_cast<uint16_t>(*it) << 8);
                for (int j = 0; j < 4; j++)
                {
                    uint16_t tmp = crc << 1;
                    if (crc & 0x8000)
                    {
                        tmp ^= 0x1021;
                    }
                    crc = tmp << 1;
                    if (tmp & 0x8000)
                    {
                        crc ^= 0x1021;
                    }
                }
            }

            // Process data
            for (size_t i = 0; i < data.size(); i++)
            {
                crc ^= (static_cast<uint16_t>(reverse_8(data[i])) << 8);
                for (int j = 0; j < 4; j++)
                {
                    uint16_t tmp = crc << 1;
                    if (crc & 0x8000)
                    {
                        tmp ^= 0x1021;
                    }
                    crc = tmp << 1;
                    if (tmp & 0x8000)
                    {
                        crc ^= 0x1021;
                    }
                }
            }

            crc = ~reverse_16(crc);
            return crc;
        }

        void whitening_init(uint32_t val, WhiteningContext &ctx)
        {
            uint32_t v0[] = {(val >> 5), (val >> 4), (val >> 3), (val >> 2)};

            ctx.f_0x0 = 1;
            ctx.f_0x4 = v0[0] & 1;
            ctx.f_0x8 = v0[1] & 1;
            ctx.f_0xc = v0[2] & 1;
            ctx.f_0x10 = v0[3] & 1;
            ctx.f_0x14 = (val >> 1) & 1;
            ctx.f_0x18 = val & 1;
        }

        void whitening_encode(std::vector<uint8_t> &data, WhiteningContext &ctx)
        {
            for (size_t i = 0; i < data.size(); i++)
            {
                uint32_t varC = ctx.f_0xc;
                uint32_t var14 = ctx.f_0x14;
                uint32_t var18 = ctx.f_0x18;
                uint32_t var10 = ctx.f_0x10;
                uint32_t var8 = var14 ^ ctx.f_0x8;
                uint32_t var4 = var10 ^ ctx.f_0x4;
                uint32_t _var = var18 ^ varC;
                uint32_t var0 = _var ^ ctx.f_0x0;

                uint8_t c = data[i];
                data[i] = ((c & 0x80) ^ ((var8 ^ var18) << 7)) + ((c & 0x40) ^ (var0 << 6)) + ((c & 0x20) ^ (var4 << 5)) + ((c & 0x10) ^ (var8 << 4)) + ((c & 0x08) ^ (_var << 3)) + ((c & 0x04) ^ (var10 << 2)) + ((c & 0x02) ^ (var14 << 1)) + ((c & 0x01) ^ (var18 << 0));

                ctx.f_0x8 = var4;
                ctx.f_0xc = var8;
                ctx.f_0x10 = var8 ^ varC;
                ctx.f_0x14 = var0 ^ var10;
                ctx.f_0x18 = var4 ^ var14;
                ctx.f_0x0 = var8 ^ var18;
                ctx.f_0x4 = var0;
            }
        }

        std::vector<char> vector_to_hex_string(std::vector<uint8_t> &data)
        {
            std::vector<char> hex_str(data.size() * 2 + 1); // Allocate the vector with the required size
            for (size_t i = 0; i < data.size(); i++)
            {
                sprintf(hex_str.data() + (i * 2), "%02X", data[i]);
            }
            hex_str[data.size() * 2] = '\0'; // Ensure null termination
            return hex_str;
        }
    } // namespace fastcon
} // namespace esphome