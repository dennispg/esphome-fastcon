#include <algorithm>
#include "protocol.h"

namespace esphome
{
    namespace fastcon
    {
        std::vector<uint8_t> get_rf_payload(const std::vector<uint8_t> &addr, const std::vector<uint8_t> &data)
        {
            const size_t data_offset = 0x12;
            const size_t inverse_offset = 0x0f;
            const size_t result_data_size = data_offset + addr.size() + data.size();

            // Create result buffer including space for checksum
            std::vector<uint8_t> resultbuf(result_data_size + 2, 0);

            // Set hardcoded values
            resultbuf[0x0f] = 0x71;
            resultbuf[0x10] = 0x0f;
            resultbuf[0x11] = 0x55;

            // Copy address in reverse
            for (size_t i = 0; i < addr.size(); i++)
            {
                resultbuf[data_offset + addr.size() - i - 1] = addr[i];
            }

            // Copy data
            std::copy(data.begin(), data.end(), resultbuf.begin() + data_offset + addr.size());

            // Reverse bytes in specified range
            for (size_t i = inverse_offset; i < inverse_offset + addr.size() + 3; i++)
            {
                resultbuf[i] = reverse_8(resultbuf[i]);
            }

            // Add CRC
            uint16_t crc = crc16(addr, data);
            resultbuf[result_data_size] = crc & 0xFF;
            resultbuf[result_data_size + 1] = (crc >> 8) & 0xFF;

            return resultbuf;
        }

        std::vector<uint8_t> prepare_payload(const std::vector<uint8_t> &addr, const std::vector<uint8_t> &data)
        {
            auto payload = get_rf_payload(addr, data);

            // Initialize whitening
            WhiteningContext context;
            whitening_init(0x25, context);

            // Apply whitening to the payload
            whitening_encode(payload, context);

            // Return only the portion after 0xf bytes
            return std::vector<uint8_t>(payload.begin() + 0xf, payload.end());
        }
        std::array<uint8_t, 4> app_mesh_key(uint16_t candidate)
        {
            std::array<uint8_t, 4> key{};
            for (int i = 3; i >= 0; i--)
            {
                key[i] = 0x30 + candidate % 10;
                candidate /= 10;
            }
            return key;
        }

        bool decode_factory_identity(const std::vector<uint8_t> &frame, std::array<uint8_t, 6> &did)
        {
            // AdysTech captured 18 manufacturer bytes including F0 FF. ESPHome
            // strips those two company bytes, exposing the same 16-byte body.
            const size_t offset = frame.size() == 18 && frame[0] == 0xf0 && frame[1] == 0xff ? 2 : 0;
            if (frame.size() != offset + 16 || ((frame[offset] ^ DEFAULT_ENCRYPT_KEY[0]) & 0x70) != 0x10 ||
                !std::equal(DEFAULT_ENCRYPT_KEY.begin(), DEFAULT_ENCRYPT_KEY.end(), frame.begin() + offset + 12))
                return false;
            std::copy_n(frame.begin() + offset + 4, 6, did.begin());
            return true;
        }

        bool decode_heartbeat(const std::vector<uint8_t> &frame, const std::array<uint8_t, 4> &key, uint8_t &light_id)
        {
            const size_t offset = frame.size() == 18 && frame[0] == 0xf0 && frame[1] == 0xff ? 2 : 0;
            if (frame.size() != offset + 16)
                return false;
            std::array<uint8_t, 16> body{};
            for (size_t i = 0; i < body.size(); i++)
                body[i] = frame[offset + i] ^ (i < 4 ? DEFAULT_ENCRYPT_KEY[i] : key[(i - 4) & 3]);
            const uint8_t type = (body[0] >> 4) & 7;
            // Both heartbeat type variants occur in community decoders. Never accept
            // a relayed control command (payload opcode 2/3) as proof of a bulb.
            // Reference heartbeats use key[3] in byte 2; captured flood lights
            // instead repeat their number there. Neither is authentication.
            if ((type != 3 && type != 5) || (body[0] & 0x0f) != 0 ||
                (body[4] & 0x0f) != 4 || body[5] == 0 ||
                (body[2] != key[3] && body[2] != body[5]))
                return false;
            uint8_t checksum = 0;
            for (size_t i = 0; i < body.size(); i++)
                if (i != 3)
                    checksum += body[i];
            if (checksum != body[3])
                return false;
            light_id = body[5];
            return true;
        }

        std::vector<uint8_t> encode_discovery_command(uint8_t type, uint8_t sequence, const std::vector<uint8_t> &data,
                                                     const std::array<uint8_t, 4> &key, uint8_t safe_key)
        {
            if (data.size() > 12)
                return {};
            std::vector<uint8_t> body(16, 0);
            body[0] = ((type & 7) << 4) | (type == 5 ? 0x80 : 0);
            body[1] = sequence;
            body[2] = safe_key;
            std::copy(data.begin(), data.end(), body.begin() + 4);
            uint8_t checksum = 0;
            for (auto byte : body)
                checksum += byte;
            body[3] = checksum;
            for (size_t i = 0; i < body.size(); i++)
                body[i] ^= i < 4 ? DEFAULT_ENCRYPT_KEY[i] : key[(i - 4) & 3];
            return prepare_payload({DEFAULT_BLE_FASTCON_ADDRESS.begin(), DEFAULT_BLE_FASTCON_ADDRESS.end()}, body);
        }
    } // namespace fastcon
} // namespace esphome