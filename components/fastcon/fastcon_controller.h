#pragma once

#include <deque>
#include <initializer_list>
#include <mutex>
#include <vector>
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
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

            // 1 byte (off, or on + brightness) or 6 bytes (on + brightness, then blue, red, green, warm, cold)
            std::vector<uint8_t> get_light_data(const light::LightColorValues &values);
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

            // ---- Group control ----
            // One broadcast that every bulb in the group obeys (group 0 is the app's "All" group).
            // light_data is the same 1- or 6-byte light data as for single control. Commands still
            // queued for member_light_ids are dropped, since the group command supersedes them.
            void send_group(uint8_t group_id, const std::vector<uint8_t> &light_data,
                            const std::vector<uint8_t> &member_light_ids);
            // Start one of the bulbs' built-in effects on a group; the bulbs then run it by themselves.
            // flash jumps between the steps, otherwise they fade. speed is 1-100, higher is faster.
            // steps: up to 9, each a bit set of 0x01 blue, 0x02 red, 0x04 green, 0x08 warm white,
            // 0x10 cold white (0x00 is off). An "on" for the group goes out first, so it also starts from off.
            // Like send_group, commands still queued for member_light_ids are dropped.
            void send_group_scene(uint8_t group_id, bool flash, uint8_t speed, std::vector<uint8_t> steps,
                                  const std::vector<uint8_t> &member_light_ids = {});
            // Store a group number in one bulb (kept by the bulb; 0 removes it from its group)
            void send_set_group(uint8_t light_id, uint8_t group_id);
            // Every single-bulb light, so group 0 ("All") can mirror its state onto all of them
            void register_light(FastconLight *light) { lights_.push_back(light); }
            const std::vector<FastconLight *> &get_lights() const { return lights_; }
            // Every group light, so fastcon.assign_members can find a group by its light entity
            void register_group(FastconLight *group) { groups_.push_back(group); }
            // Write a group light's number into each of its member bulbs (a one-off setup step)
            void assign_members(light::LightState *group);

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
            std::vector<FastconLight *> lights_;
            std::vector<FastconLight *> groups_;
            // Queue keys outside the 1-255 light ids, so each group and each bulb's set-group
            // command gets its own "newest wins" slot
            static constexpr uint32_t GROUP_QUEUE_KEY = 0x10000;     // + group id (steady state or effect)
            static constexpr uint32_t SET_GROUP_QUEUE_KEY = 0x20000; // + light id
            static constexpr uint32_t GROUP_ON_QUEUE_KEY = 0x30000;  // + group id (the "on" sent before an effect)

            // Drop anything still queued (including pending repeats) for these bulbs or queue keys
            void drop_queued_(const std::vector<uint8_t> &light_ids, std::initializer_list<uint32_t> keys);

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

        // fastcon.set_group: write a group number into one bulb (a one-off setup step)
        template <typename... Ts>
        class SetGroupAction : public Action<Ts...>, public Parented<FastconController>
        {
        public:
            TEMPLATABLE_VALUE(uint8_t, light_id)
            TEMPLATABLE_VALUE(uint8_t, group_id)

            void play(const Ts &...x) override
            {
                this->parent_->send_set_group(this->light_id_.value(x...), this->group_id_.value(x...));
            }
        };

        // fastcon.assign_members: write a group light's number into each of its member bulbs
        template <typename... Ts>
        class AssignMembersAction : public Action<Ts...>, public Parented<FastconController>
        {
        public:
            void set_group(light::LightState *group) { group_ = group; }

            void play(const Ts &...x) override { this->parent_->assign_members(this->group_); }

        protected:
            light::LightState *group_{nullptr};
        };

    } // namespace fastcon
} // namespace esphome
