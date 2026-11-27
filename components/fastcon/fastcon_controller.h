#pragma once

#include <deque>
#include "esphome/core/defines.h"
#include "esphome/core/preferences.h"
#include "discovery.h"
#ifdef USE_FASTCON_DISCOVERY
#include "esphome/components/esp32_ble/ble.h"
#include "esphome/components/esp32_ble_tracker/esp32_ble_tracker.h"
#endif
#include <initializer_list>
#include <mutex>
#include <vector>
#include "esphome/core/automation.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/esp32_ble_server/ble_server.h"
#include <esp_bt.h>
#include <esp_gap_ble_api.h>
#include "esphome/components/light/light_state.h"

namespace esphome
{
    namespace fastcon
    {
        class FastconLight;

        class FastconController : public Component
#ifdef USE_FASTCON_DISCOVERY
                                , public esp32_ble_tracker::ESPBTDeviceListener
#endif
        {
        public:
            FastconController() = default;

            void set_preference_key(uint32_t key) { preference_key_ = key; }
            bool apply_mesh_key(const std::string &key);
            bool ready() const
            {
#ifdef USE_FASTCON_DISCOVERY
                return tracker_ == nullptr || discovery_.phase == DiscoveryPhase::DONE ||
                       (discovery_.phase == DiscoveryPhase::FACTORY && discovery_stopped_ &&
                        discovery_.key_known && !discovery_failed_);
#else
                return true;
#endif
            }
#ifdef USE_FASTCON_DISCOVERY
            void set_discovery(esp32_ble_tracker::ESP32BLETracker *tracker, uint8_t max_lights, uint16_t timeout_s)
            {
                tracker_ = tracker;
                max_lights_ = max_lights;
                scan_timeout_ms_ = uint32_t(timeout_s) * 1000;
            }
            bool parse_device(const esp32_ble_tracker::ESPBTDevice &device) override;
            void on_scan_end() override { scan_finished_ = true; }
#endif
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

            void set_mesh_key(std::array<uint8_t, 4> key) { mesh_key_ = key; configured_key_ = key; }
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
            // Advertising TX power. Left at the Bluetooth stack's default unless set.
            void set_tx_power(esp_power_level_t level)
            {
                tx_power_ = level;
                has_tx_power_ = true;
            }

            // Registered with esp32_ble: reports when the Bluetooth controller rejects our advertising
            // data or start/stop asynchronously (the API calls themselves can still return ESP_OK).
            void gap_event_handler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

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
            // The same for every group light that has members (group 0 needs no assigning)
            void assign_all_members();

        protected:
            uint32_t preference_key_{0};
            ESPPreferenceObject preference_;
            DiscoveryRecord discovery_{};
            void setup_preferences_();
            bool save_discovery_();
#ifdef USE_FASTCON_DISCOVERY
            enum class DiscoveryIO { SEND, DRAIN, SCAN, STOPPING, REST };
            esp32_ble_tracker::ESP32BLETracker *tracker_{nullptr};
            DiscoveryIO discovery_io_{DiscoveryIO::SEND};
            uint8_t max_lights_{4};
            uint32_t scan_timeout_ms_{30000};
            uint32_t discovery_started_{0};
            uint32_t discovery_io_started_{0};
            uint8_t discovery_sequence_{1};
            uint8_t probe_id_{1};
            uint8_t factory_round_{0};
            uint16_t discovery_adv_duration_{50};
            bool recovery_wake_round_{false};
            bool recovery_wake_sent_{false};
            uint8_t pending_index_{0};
            std::array<bool, 16> factory_seen_{};
            bool scan_finished_{false};
            bool restart_discovery_{false};
            bool heartbeat_seen_{false};
            bool discovery_stopped_{false}; // bounded partial success; FACTORY stays resumable in NVS
            bool discovery_failed_{false};
            bool failure_reported_{false};
            bool discovery_radio_ready_{false};
            void setup_discovery_();
            void discovery_loop_(uint32_t now);
            void send_discovery_round_();
            void finish_discovery_round_();
            void finish_discovery_();
            void bind_slots_();
            void confirm_bulb_(uint8_t index);
            uint8_t allocate_light_id_() const;
            uint8_t claimed_count_() const;
            void queue_discovery_(uint32_t target, const std::vector<uint8_t> &data);
#endif
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
            static constexpr uint32_t DISCOVERY_QUEUE_KEY = 0x40000; // one discovery command at a time

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
            std::array<uint8_t, 4> configured_key_{};

            uint16_t adv_interval_min_{0x20};
            uint16_t adv_interval_max_{0x40};
            uint16_t adv_duration_{50};
            uint16_t adv_gap_{10};
            esp_power_level_t tx_power_{ESP_PWR_LVL_P9};
            bool has_tx_power_{false};
            bool tx_power_applied_{false};

            static const uint16_t MANUFACTURER_DATA_ID = 0xfff0;
        };

        template <typename... Ts>
        class SetMeshKeyAction : public Action<Ts...>, public Parented<FastconController>
        {
        public:
            TEMPLATABLE_VALUE(std::string, key)
            void play(const Ts &...x) override { this->parent_->apply_mesh_key(this->key_.value(x...)); }
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

            void play(const Ts &...x) override
            {
                if (this->group_ == nullptr)
                    this->parent_->assign_all_members();
                else
                    this->parent_->assign_members(this->group_);
            }

        protected:
            light::LightState *group_{nullptr};
        };

    } // namespace fastcon
} // namespace esphome
