#include <algorithm>
#include <cmath>
#include "esphome/core/component_iterator.h"
#include "esphome/core/log.h"
#include "esphome/components/light/color_mode.h"
#include "fastcon_controller.h"
#include "protocol.h"

namespace esphome
{
    namespace fastcon
    {
        static const char *const TAG = "fastcon.controller";

        void FastconController::queueCommand(uint32_t light_id_, const std::vector<uint8_t> &data)
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);

            // Newest command wins. If this light already has a command waiting
            // (a first send or a repeat), replace it in place and restart its repeat count.
            for (auto &cmd : queue_)
            {
                if (cmd.light_id == light_id_)
                {
                    cmd.data = data;
                    cmd.sends_left = repeat_count_;
                    ESP_LOGV(TAG, "Replaced pending command for light %u", (unsigned) light_id_);
                    return;
                }
            }

            if (queue_.size() >= max_queue_size_)
            {
                ESP_LOGW(TAG, "Command queue full (size=%u), dropping command for light %u",
                         (unsigned) queue_.size(), (unsigned) light_id_);
                return;
            }

            queue_.push_back(Command{light_id_, data, repeat_count_});
            ESP_LOGV(TAG, "Command queued, queue size: %u", (unsigned) queue_.size());
        }

        void FastconController::clear_queue()
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_.clear();
        }

        void FastconController::setup()
        {
            ESP_LOGCONFIG(TAG, "Setting up Fastcon BLE Controller...");
            ESP_LOGCONFIG(TAG, "  Advertisement interval: %d-%d", this->adv_interval_min_, this->adv_interval_max_);
            ESP_LOGCONFIG(TAG, "  Advertisement duration: %dms", this->adv_duration_);
            ESP_LOGCONFIG(TAG, "  Advertisement gap: %dms", this->adv_gap_);
            ESP_LOGCONFIG(TAG, "  Sends per command: %d", this->repeat_count_);
        }

        void FastconController::loop()
        {
            const uint32_t now = millis();

            switch (adv_state_)
            {
            case AdvertiseState::IDLE:
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                if (queue_.empty())
                    return;

                Command cmd = queue_.front();
                queue_.pop_front();

                // Send each command repeat_count_ times, round-robin with the other
                // lights, so a bulb that misses one broadcast catches the next. A repeat is only
                // re-queued if no newer command for the same light has arrived meanwhile.
                if (cmd.sends_left > 1)
                {
                    bool newer_waiting = std::any_of(queue_.begin(), queue_.end(),
                                                     [&](const Command &c)
                                                     { return c.light_id == cmd.light_id; });
                    if (!newer_waiting)
                    {
                        Command again = cmd;
                        again.sends_left--;
                        queue_.push_back(again);
                    }
                }

                esp_ble_adv_params_t adv_params = {
                    .adv_int_min = adv_interval_min_,
                    .adv_int_max = adv_interval_max_,
                    .adv_type = ADV_TYPE_NONCONN_IND,
                    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
                    .peer_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
                    .channel_map = ADV_CHNL_ALL,
                    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
                };

                uint8_t adv_data_raw[31] = {0};
                uint8_t adv_data_len = 0;

                // Add flags
                adv_data_raw[adv_data_len++] = 2;
                adv_data_raw[adv_data_len++] = ESP_BLE_AD_TYPE_FLAG;
                adv_data_raw[adv_data_len++] = ESP_BLE_ADV_FLAG_BREDR_NOT_SPT | ESP_BLE_ADV_FLAG_GEN_DISC;

                // Add manufacturer data
                adv_data_raw[adv_data_len++] = cmd.data.size() + 2;
                adv_data_raw[adv_data_len++] = ESP_BLE_AD_MANUFACTURER_SPECIFIC_TYPE;
                adv_data_raw[adv_data_len++] = MANUFACTURER_DATA_ID & 0xFF;
                adv_data_raw[adv_data_len++] = (MANUFACTURER_DATA_ID >> 8) & 0xFF;

                memcpy(&adv_data_raw[adv_data_len], cmd.data.data(), cmd.data.size());
                adv_data_len += cmd.data.size();

                esp_err_t err = esp_ble_gap_config_adv_data_raw(adv_data_raw, adv_data_len);
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG, "Error setting raw advertisement data (err=%d): %s", err, esp_err_to_name(err));
                    return;
                }

                err = esp_ble_gap_start_advertising(&adv_params);
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG, "Error starting advertisement (err=%d): %s", err, esp_err_to_name(err));
                    return;
                }

                adv_state_ = AdvertiseState::ADVERTISING;
                state_start_time_ = now;
                ESP_LOGV(TAG, "Started advertising for light %u (%d sends left)", (unsigned) cmd.light_id, cmd.sends_left - 1);
                break;
            }

            case AdvertiseState::ADVERTISING:
            {
                if (now - state_start_time_ >= adv_duration_)
                {
                    esp_ble_gap_stop_advertising();
                    adv_state_ = AdvertiseState::GAP;
                    state_start_time_ = now;
                    ESP_LOGV(TAG, "Stopped advertising, entering gap period");
                }
                break;
            }

            case AdvertiseState::GAP:
            {
                if (now - state_start_time_ >= adv_gap_)
                {
                    adv_state_ = AdvertiseState::IDLE;
                    ESP_LOGV(TAG, "Gap period complete");
                }
                break;
            }
            }
        }

        std::vector<uint8_t> FastconController::get_light_data(light::LightState *state)
        {
            std::vector<uint8_t> light_data = {
                0, // 0 - On/Off Bit + 7-bit Brightness
                0, // 1 - Blue byte
                0, // 2 - Red byte
                0, // 3 - Green byte
                0, // 4 - Warm byte
                0  // 5 - Cold byte
            };

            auto values = state->current_values;

            bool is_on = values.is_on();
            if (!is_on)
            {
                return std::vector<uint8_t>({0x00});
            }

            // At least 1, so "on" never goes out as brightness 0
            const int brightness = std::clamp(static_cast<int>(std::lround(values.get_brightness() * 127.0f)), 1, 127);
            const uint8_t on_brightness = 0x80 | static_cast<uint8_t>(brightness);
            light_data[0] = on_brightness;

            auto color_mode = values.get_color_mode();
            auto has = [&](light::ColorCapability cap)
            { return (static_cast<uint8_t>(color_mode) & static_cast<uint8_t>(cap)) != 0; };
            const bool has_rgb = has(light::ColorCapability::RGB);
            const bool has_cold_warm = has(light::ColorCapability::COLD_WARM_WHITE);
            const bool has_temp = has(light::ColorCapability::COLOR_TEMPERATURE);
            const bool has_white = has(light::ColorCapability::WHITE);

            if (!has_rgb && !has_cold_warm && !has_temp)
            {
                if (has_white)
                {
                    // White mode switches the bulb to its white LEDs. This is the
                    // payload noted in the original TODO, captured from the app (ff0000007f7f), at the requested brightness.
                    // (Sending only the brightness byte, without the "on" bit, would leave the bulb off.)
                    light_data[4] = 0x7f;
                    light_data[5] = 0x7f;
                    return light_data;
                }
                // No colour chosen yet (e.g. a plain "turn on" after the controller
                // restarted). Send the short "on + brightness" command so the bulb keeps its own
                // last colour. Sending every colour channel as 0 here would light the bulb "black".
                return std::vector<uint8_t>({on_brightness});
            }

            if (has_rgb)
            {
                light_data[1] = static_cast<uint8_t>(values.get_blue() * 255.0f);
                light_data[2] = static_cast<uint8_t>(values.get_red() * 255.0f);
                light_data[3] = static_cast<uint8_t>(values.get_green() * 255.0f);
            }

            if (has_cold_warm)
            {
                light_data[4] = static_cast<uint8_t>(values.get_warm_white() * 255.0f);
                light_data[5] = static_cast<uint8_t>(values.get_cold_white() * 255.0f);
            }

            if (has_temp)
            {
                float temperature = values.get_color_temperature();
                if (temperature < 153)
                {
                    light_data[4] = 0xff;
                    light_data[5] = 0x00;
                }
                else if (temperature > 500)
                {
                    light_data[4] = 0x00;
                    light_data[5] = 0xff;
                }
                else
                {
                    // Linear interpolation between (153, 0xff) and (500, 0x00)
                    light_data[4] = (uint8_t)(((500 - temperature) * 255.0f + (temperature - 153) * 0x00) / (500 - 153));
                    light_data[5] = (uint8_t)(((temperature - 153) * 255.0f + (500 - temperature) * 0x00) / (500 - 153));
                }
            }

            return light_data;
        }

        std::vector<uint8_t> FastconController::single_control(uint32_t light_id_, const std::vector<uint8_t> &light_data)
        {
            std::vector<uint8_t> result_data(12);

            result_data[0] = 2 | (((0xfffffff & (light_data.size() + 1)) << 4));
            result_data[1] = light_id_;
            std::copy(light_data.begin(), light_data.end(), result_data.begin() + 2);

            // Verbose only; the string must stay alive while it's logged
            ESP_LOGV(TAG, "Inner Payload (%u bytes): %s", (unsigned) result_data.size(), vector_to_hex_string(result_data).c_str());

            return this->generate_command(5, light_id_, result_data, true);
        }

        std::vector<uint8_t> FastconController::generate_command(uint8_t n, uint32_t light_id_, const std::vector<uint8_t> &data, bool forward)
        {
            static uint8_t sequence = 0;

            // Create command body with header
            std::vector<uint8_t> body(data.size() + 4);
            uint8_t i2 = (light_id_ / 256);

            // Construct header
            body[0] = (i2 & 0b1111) | ((n & 0b111) << 4) | (forward ? 0x80 : 0);
            body[1] = sequence++; // Use and increment sequence number
            if (sequence >= 255)
                sequence = 1;

            body[2] = this->mesh_key_[3]; // Safe key

            // Copy data
            std::copy(data.begin(), data.end(), body.begin() + 4);

            // Calculate checksum
            uint8_t checksum = 0;
            for (size_t i = 0; i < body.size(); i++)
            {
                if (i != 3)
                {
                    checksum = checksum + body[i];
                }
            }
            body[3] = checksum;

            // Encrypt header and data
            for (size_t i = 0; i < 4; i++)
            {
                body[i] = DEFAULT_ENCRYPT_KEY[i & 3] ^ body[i];
            }

            for (size_t i = 0; i < data.size(); i++)
            {
                body[4 + i] = this->mesh_key_[i & 3] ^ body[4 + i];
            }

            // Prepare the final payload with RF protocol formatting
            std::vector<uint8_t> addr = {DEFAULT_BLE_FASTCON_ADDRESS.begin(), DEFAULT_BLE_FASTCON_ADDRESS.end()};
            return prepare_payload(addr, body);
        }

        void FastconController::drop_queued_(const std::vector<uint8_t> &light_ids, std::initializer_list<uint32_t> keys)
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_.erase(std::remove_if(queue_.begin(), queue_.end(),
                                        [&](const Command &c)
                                        {
                                            return std::find(keys.begin(), keys.end(), c.light_id) != keys.end() ||
                                                   std::find(light_ids.begin(), light_ids.end(), c.light_id) != light_ids.end();
                                        }),
                         queue_.end());
        }

        // ---- Group control and built-in effects, decoded from the BRmesh app's Broadlink library
        // (package_broadcast_control, package_groupId_scene_control, package_device_groupId). All use
        // command type 5 and the same packet format as single control; only the inner payload differs.

        void FastconController::send_group(uint8_t group_id, const std::vector<uint8_t> &light_data,
                                           const std::vector<uint8_t> &member_light_ids)
        {
            // inner: ((len+3)<<4)|3, device type 0xA82A (lights, little-endian), group id, light data
            std::vector<uint8_t> inner(12, 0);
            inner[0] = static_cast<uint8_t>(((light_data.size() + 3) << 4) | 3);
            inner[1] = 0x2A;
            inner[2] = 0xA8;
            inner[3] = group_id;
            std::copy(light_data.begin(), light_data.end(), inner.begin() + 4);
            ESP_LOGD(TAG, "Group %u: %s", group_id, vector_to_hex_string(light_data).c_str());

            // The group command supersedes anything still queued for its members (including pending
            // repeats), so an older single-bulb command can't go out after it and undo it. Likewise a
            // pending "on" from starting an effect on this group, which would follow this command.
            drop_queued_(member_light_ids, {GROUP_ON_QUEUE_KEY + group_id});
            queueCommand(GROUP_QUEUE_KEY + group_id, generate_command(5, 0, inner, true));
        }

        void FastconController::send_group_scene(uint8_t group_id, bool flash, uint8_t speed, std::vector<uint8_t> steps,
                                                 const std::vector<uint8_t> &member_light_ids)
        {
            if (steps.empty())
                return;
            if (steps.size() > 9)
                steps.resize(9);
            const int s = std::clamp(static_cast<int>(speed), 1, 100);
            // Speed in jiffies (1/64 s). The app's flash speed is 200 - 2*(slider-1); fade uses the
            // slider value directly, inverted here so higher is faster for both.
            const uint8_t jiffies = static_cast<uint8_t>(flash ? 200 - 2 * (s - 1) : 101 - s);
            // Mode bit, plus the "grayscale" bit the app always sets, on the first step
            steps[0] |= (flash ? 0x80 : 0x00) | 0x40;

            // inner: ((k+2)<<4)|0x0C, group id, speed, then the k steps
            std::vector<uint8_t> inner(12, 0);
            inner[0] = static_cast<uint8_t>(((steps.size() + 2) << 4) | 0x0C);
            inner[1] = group_id;
            inner[2] = jiffies;
            std::copy(steps.begin(), steps.end(), inner.begin() + 3);
            ESP_LOGD(TAG, "Group %u effect: %s, %u steps, %u jiffies: %s", group_id, flash ? "flash" : "fade",
                     (unsigned) steps.size(), jiffies, vector_to_hex_string(inner).c_str());

            // Make sure the bulbs are on first: a group command with 1 byte of light data, on at full
            // brightness, which keeps their colour.
            std::vector<uint8_t> on(12, 0);
            on[0] = 0x43; // ((1+3)<<4)|3
            on[1] = 0x2A;
            on[2] = 0xA8;
            on[3] = group_id;
            on[4] = 0xFF;

            // Clear this group's slots before queueing, so the "on" always goes out ahead of the effect
            // (a pending steady command would otherwise be replaced in place, ahead of a newly queued
            // "on"). With round-robin repeats the effect is still the last thing sent.
            drop_queued_(member_light_ids, {GROUP_QUEUE_KEY + group_id, GROUP_ON_QUEUE_KEY + group_id});
            queueCommand(GROUP_ON_QUEUE_KEY + group_id, generate_command(5, 0, on, true));
            queueCommand(GROUP_QUEUE_KEY + group_id, generate_command(5, 0, inner, true));
        }

        void FastconController::send_set_group(uint8_t light_id, uint8_t group_id)
        {
            // inner: 0x21, bulb address, group id. Sent without the forward bit, as the app does.
            std::vector<uint8_t> inner(12, 0);
            inner[0] = 0x21;
            inner[1] = light_id;
            inner[2] = group_id;
            ESP_LOGI(TAG, "Writing group %u into light %u", group_id, light_id);
            queueCommand(SET_GROUP_QUEUE_KEY + light_id, generate_command(5, light_id, inner, false));
        }
    } // namespace fastcon
} // namespace esphome
