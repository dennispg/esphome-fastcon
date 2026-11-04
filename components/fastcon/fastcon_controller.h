#pragma once

#include <deque>
#include <mutex>
#include <vector>
#include "esphome/core/component.h"
#include "esphome/components/esp32_ble_server/ble_server.h"
#include "esphome/components/light/light_state.h"

namespace esphome
{
    namespace fastcon
    {
        class FastconLight;

        class FastconController : public Component
        {
        public:
            FastconController() = default;

            void setup() override;
            void loop() override;

            std::vector<uint8_t> get_light_data(light::LightState *state);
            std::vector<uint8_t> single_control(uint32_t addr, const std::vector<uint8_t> &light_data);

            void queueCommand(uint32_t light_id_, const std::vector<uint8_t> &data);

            void clear_queue();
            bool is_queue_empty() const
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                return queue_.empty();
            }
            size_t get_queue_size() const
            {
                std::lock_guard<std::mutex> lock(queue_mutex_);
                return queue_.size();
            }
            void set_max_queue_size(size_t size) { max_queue_size_ = size; }
            // How many times each command is broadcast (interleaved across lights)
            void set_repeat_count(uint8_t count) { repeat_count_ = count < 1 ? 1 : count; }

            void set_mesh_key(std::array<uint8_t, 4> key) { mesh_key_ = key; }
            void set_adv_interval_min(uint16_t val) { adv_interval_min_ = val; }
            void set_adv_interval_max(uint16_t val)
            {
                adv_interval_max_ = val;
                if (adv_interval_max_ < adv_interval_min_)
                {
                    adv_interval_max_ = adv_interval_min_;
                }
            }
            void set_adv_duration(uint16_t val) { adv_duration_ = val; }
            void set_adv_gap(uint16_t val) { adv_gap_ = val; }

            // ---- Group control and the bulbs' built-in effects ----
            // Group the "all lights" entity talks to. 0 is the app's "All" group.
            void set_group_id(uint8_t gid) { group_id_ = gid; }
            uint8_t get_group_id() const { return group_id_; }
            // One broadcast that every bulb in the group obeys: 1- or 6-byte light data, as for single control
            void send_group(const std::vector<uint8_t> &light_data);
            // Built-in effect run by the bulbs themselves. statuses: up to 9 steps, each a bit set of
            // 0x01 blue, 0x02 red, 0x04 green, 0x08 warm, 0x10 cold (0x00 = off). speed 1-100, higher = faster.
            void send_group_scene(bool flash, float speed, std::vector<uint8_t> statuses);
            // Store a group number in one bulb (kept by the bulb; 0 removes it from its group)
            void send_set_group(uint8_t light_id, uint8_t gid);
            // Individual lights, so a group command can update their state in Home Assistant
            void register_member(FastconLight *light) { members_.push_back(light); }
            void mirror_to_members(const light::LightColorValues &values);

        protected:
            // One pending command per light. A newer command for the same light
            // replaces the older one in place, so stale states never queue up behind each other.
            struct Command
            {
                uint32_t light_id;
                std::vector<uint8_t> data;
                uint8_t sends_left;
            };

            std::deque<Command> queue_;
            mutable std::mutex queue_mutex_;
            size_t max_queue_size_{100};
            uint8_t repeat_count_{1};
            uint8_t group_id_{0};
            std::vector<FastconLight *> members_;
            // Queue keys outside the 1-255 light ids, so group frames get their own "newest wins" slot
            static constexpr uint32_t GROUP_QUEUE_KEY = 0x10000;
            static constexpr uint32_t SET_GROUP_QUEUE_KEY = 0x20000;
            static constexpr uint32_t GROUP_ON_QUEUE_KEY = 0x10001;

            enum class AdvertiseState
            {
                IDLE,
                ADVERTISING,
                GAP
            };

            AdvertiseState adv_state_{AdvertiseState::IDLE};
            uint32_t state_start_time_{0};

            // Protocol implementation
            std::vector<uint8_t> generate_command(uint8_t n, uint32_t light_id_, const std::vector<uint8_t> &data, bool forward = true);

            std::array<uint8_t, 4> mesh_key_{};

            uint16_t adv_interval_min_{0x20};
            uint16_t adv_interval_max_{0x40};
            uint16_t adv_duration_{50};
            uint16_t adv_gap_{10};

            static const uint16_t MANUFACTURER_DATA_ID = 0xfff0;
        };

    } // namespace fastcon
} // namespace esphome
