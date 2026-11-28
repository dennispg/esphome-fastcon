#include <algorithm>
#include "esphome/core/log.h"
#include "fastcon_controller.h"
#include "fastcon_light.h"
#include "protocol.h"

namespace esphome
{
    namespace fastcon
    {
        static const char *const TAG = "fastcon.controller";

        void FastconController::setup_preferences_()
        {
            this->preference_ = global_preferences->make_preference<DiscoveryRecord>(this->preference_key_, true);
            DiscoveryRecord saved{};
            if (this->preference_.load(&saved) && saved.version == 1 && saved.count <= 16 &&
                saved.candidate <= 10000 && saved.phase <= DiscoveryPhase::FAILED)
            {
                this->discovery_ = saved;
                if (saved.runtime_override && saved.configured_key == this->configured_key_)
                    this->mesh_key_ = saved.key;
            }
        }

        bool FastconController::save_discovery_()
        {
            if (!this->preference_.save(&this->discovery_) || !global_preferences->sync())
            {
                ESP_LOGW(TAG, "Could not persist discovery; stopping before sending further key changes");
                return false;
            }
            return true;
        }

        bool FastconController::apply_mesh_key(const std::string &text)
        {
            if (text.size() != 8)
            {
                ESP_LOGW(TAG, "Runtime mesh key must contain exactly eight hexadecimal characters");
                return false;
            }
            std::array<uint8_t, 4> key{};
            for (size_t i = 0; i < text.size(); i++)
            {
                const char c = text[i];
                const int digit = c >= '0' && c <= '9' ? c - '0' :
                                  c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                                  c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
                if (digit < 0)
                {
                    ESP_LOGW(TAG, "Runtime mesh key contains a non-hexadecimal character");
                    return false;
                }
                key[i / 2] = (key[i / 2] << 4) | digit;
            }
            const auto previous = this->discovery_;
            this->discovery_.key = key;
            this->discovery_.key_known = 1;
            this->discovery_.runtime_override = 1;
            this->discovery_.configured_key = this->configured_key_;
#ifdef USE_FASTCON_DISCOVERY
            if (this->tracker_ != nullptr)
            {
                // A different mesh has different identities. Never bind old slots to it.
                this->discovery_.bulbs = {};
                this->factory_seen_ = {};
                this->probe_next_ = {};
                this->restored_pending_ = {};
                this->repair_seen_ = {};
                this->repair_attempts_ = {};
                this->discovery_.count = 0;
                this->discovery_.phase = DiscoveryPhase::ENUMERATE;
            }
#endif
            if (!this->save_discovery_())
            {
                this->discovery_ = previous;
                return false;
            }
            this->clear_queue();
            this->mesh_key_ = key;
#ifdef USE_FASTCON_DISCOVERY
            if (this->tracker_ != nullptr)
            {
                this->discovery_stopped_ = false;
                this->discovery_failed_ = false;
                this->failure_reported_ = false;
                this->probe_id_ = 1;
                this->heartbeat_seen_ = false;
                this->mesh_key_confirmed_ = false;
                this->bind_slots_();
                if (this->discovery_io_ == DiscoveryIO::SCAN || this->discovery_io_ == DiscoveryIO::STOPPING)
                    this->restart_discovery_ = true;
                else
                    this->discovery_io_ = DiscoveryIO::SEND;
            }
#endif
            ESP_LOGI(TAG, "Runtime mesh key applied and saved");
            return true;
        }

#ifdef USE_FASTCON_DISCOVERY
        static constexpr uint32_t SCAN_BURST_MS = 200;
        static constexpr uint32_t SCAN_REST_MS = 50;
        static constexpr uint32_t DISCOVERY_RETRY_MS = 10000;
        static constexpr uint8_t MAX_REPAIR_ATTEMPTS = 3; // per restored bulb, per boot

        uint8_t FastconController::claimed_count_() const
        {
            uint8_t count = 0;
            for (uint8_t i = 0; i < this->discovery_.count; i++)
                count += this->discovery_.bulbs[i].claimed != 0;
            return count;
        }

        void FastconController::setup_discovery_()
        {
            this->discovery_started_ = millis();
            this->discovery_sequence_ = random_uint32() % 254 + 1;
            this->tracker_->set_scan_continuous(false);
            this->tracker_->set_scan_duration(1); // seconds; also stopped after each short burst
            this->tracker_->set_scan_active(false);
            if (this->discovery_.phase == DiscoveryPhase::FACTORY && this->discovery_.count == 0)
            {
                const uint32_t random = random_uint32();
                for (size_t i = 0; i < 4; i++)
                    this->discovery_.key[i] = (random >> (8 * i)) & 0xff;
                // One SHARED controller key, not a different key per bulb: group 0 must work.
                if (!this->save_discovery_())
                    this->discovery_failed_ = true;
            }
            if (this->discovery_.phase == DiscoveryPhase::RECOVERY && this->discovery_.candidate >= 10000)
                this->discovery_.phase = DiscoveryPhase::FAILED;
            this->mesh_key_ = this->discovery_.key;
            for (uint8_t i = 0; i < this->discovery_.count; i++)
                this->restored_pending_[i] = !this->discovery_.bulbs[i].claimed;
            this->bind_slots_();
            ESP_LOGI(TAG, "Auto-discovery: phase %u, %u saved bulbs, recovery progress %u/10000",
                     (unsigned) this->discovery_.phase, this->claimed_count_(), this->discovery_.candidate);
            if (this->discovery_.phase == DiscoveryPhase::FACTORY)
                ESP_LOGI(TAG, "Factory scan: power-cycle unclaimed bulbs now (%us)", (unsigned) (this->scan_timeout_ms_ / 1000));
            if (this->discovery_.phase == DiscoveryPhase::FAILED)
                this->finish_discovery_();
        }

        void FastconController::bind_slots_()
        {
            for (auto *light : this->lights_)
            {
                if (light->get_slot() == 0)
                    continue;
                uint8_t id = 0;
                const uint8_t index = light->get_slot() - 1;
                if (index < this->discovery_.count && this->discovery_.bulbs[index].claimed)
                    id = this->discovery_.bulbs[index].light_id;
                light->bind_slot(id);
            }
        }

        uint8_t FastconController::allocate_light_id_() const
        {
            for (uint16_t id = 1; id <= 255; id++)
            {
                bool used = false;
                for (auto *light : this->lights_)
                    used |= light->get_slot() == 0 && light->get_light_id() == id;
                for (uint8_t i = 0; i < this->discovery_.count; i++)
                    used |= this->discovery_.bulbs[i].light_id == id;
                if (!used)
                    return id;
            }
            return 0;
        }

        void FastconController::confirm_bulb_(uint8_t index)
        {
            auto &bulb = this->discovery_.bulbs[index];
            if (bulb.claimed)
                return;
            bulb.claimed = 1;
            this->discovery_.key_known = 1;
            if (!this->save_discovery_())
            {
                bulb.claimed = 0;
                this->discovery_failed_ = true;
                return;
            }
            this->mesh_key_confirmed_ = true;
            ESP_LOGI(TAG, "Discovered light_id=%u mesh_key=%02X%02X%02X%02X", bulb.light_id,
                     this->mesh_key_[0], this->mesh_key_[1], this->mesh_key_[2], this->mesh_key_[3]);
            this->bind_slots_();
        }

        bool FastconController::parse_device(const esp32_ble_tracker::ESPBTDevice &device)
        {
            if (this->tracker_ == nullptr || (this->discovery_io_ != DiscoveryIO::SCAN && this->discovery_io_ != DiscoveryIO::STOPPING) ||
                this->discovery_failed_ || this->restart_discovery_)
                return false;
            bool handled = false;
            for (const auto &manufacturer : device.get_manufacturer_datas())
            {
                if (manufacturer.uuid != esp32_ble_tracker::ESPBTUUID::from_uint16(0xfff0))
                    continue;
                const auto &frame = manufacturer.data;
                std::array<uint8_t, 6> did{};
                if ((this->discovery_.phase == DiscoveryPhase::FACTORY || this->discovery_.phase == DiscoveryPhase::RECOVERY) &&
                    decode_factory_identity(frame, did))
                {
                    handled = true;
                    // The factory window starts once and is never extended by
                    // repeated advertisements of an identity already recorded.
                    if (this->discovery_.phase == DiscoveryPhase::RECOVERY)
                    {
                        // A boot-window identity always preempts the slow app-key search.
                        // Generate one shared key, never reuse a tentative app candidate.
                        // It is persisted with the identity before any SET_KEY can send.
                        const uint32_t random = random_uint32();
                        for (size_t i = 0; i < 4; i++)
                            this->discovery_.key[i] = (random >> (8 * i)) & 0xff;
                        this->mesh_key_ = this->discovery_.key;
                        this->discovery_.phase = DiscoveryPhase::FACTORY;
                        this->discovery_started_ = millis();
                        ESP_LOGI(TAG, "Boot-window reclaim detected; pausing app-key recovery for factory keying");
                    }
                    uint8_t index = 0;
                    for (; index < this->discovery_.count; index++)
                        if (this->discovery_.bulbs[index].did == did)
                            break;
                    if (index < this->discovery_.count)
                    {
                        this->factory_seen_[index] = true;
                        continue;
                    }
                    if (this->discovery_.count >= this->max_lights_)
                        continue;
                    const uint8_t id = this->allocate_light_id_();
                    if (id == 0)
                        continue;
                    auto &bulb = this->discovery_.bulbs[index];
                    bulb.light_id = id;
                    bulb.did = did;
                    bulb.mac = device.address_uint64();
                    this->discovery_.count++;
                    this->factory_seen_[index] = true;
                    if (!this->save_discovery_())
                        this->discovery_failed_ = true;
                    continue;
                }
                uint8_t id = 0;
                const auto key = this->discovery_.phase == DiscoveryPhase::RECOVERY
                                     ? app_mesh_key(this->discovery_.candidate) : this->mesh_key_;
                if (!decode_heartbeat(frame, key, id))
                    continue;
                handled = true;
                if (this->discovery_.phase == DiscoveryPhase::RECOVERY)
                {
                    this->heartbeat_seen_ = true;
                    this->mesh_key_ = key;
                    // A heartbeat proves a possible key, not state telemetry. Low IDs
                    // are probed separately before making any slot controllable.
                }
                else if (this->discovery_.phase == DiscoveryPhase::ENUMERATE)
                {
                    // Any valid keyed heartbeat proves mesh membership, even if an
                    // explicit entity reserves the address or no slot can bind it.
                    this->mesh_key_confirmed_ = true;
                    // Keep the old handling for invalid addresses; only delayed
                    // in-range probe IDs are eligible for retention.
                    if (id != this->probe_id_ && (id == 0 || id > 16))
                        continue;
                    // A keyed heartbeat can be delayed from an earlier probe. Its
                    // address is still authoritative; retain any eligible ID now.
                    bool known = false;
                    for (uint8_t i = 0; i < this->discovery_.count; i++)
                        known |= this->discovery_.bulbs[i].light_id == id;
                    for (auto *light : this->lights_)
                        known |= light->get_slot() == 0 && light->get_light_id() == id;
                    if (!known && this->discovery_.count < this->max_lights_ && !this->heartbeat_seen_)
                    {
                        const uint8_t index = this->discovery_.count++;
                        this->discovery_.bulbs[index].light_id = id;
                        this->discovery_.bulbs[index].mac = device.address_uint64();
                        this->confirm_bulb_(index);
                        if (this->discovery_failed_)
                        {
                            // The count/address and claim are one durable discovery
                            // operation. Never leave an unclaimed entry that the
                            // duplicate check would make impossible to confirm later.
                            this->discovery_.bulbs[index] = {};
                            this->discovery_.count--;
                        }
                        this->heartbeat_seen_ = true;
                    }
                }
                else if (this->discovery_.phase == DiscoveryPhase::FACTORY)
                {
                    for (uint8_t i = 0; i < this->discovery_.count; i++)
                    {
                        auto &bulb = this->discovery_.bulbs[i];
                        if (bulb.light_id == id)
                        {
                            // The advertised address can change; the assigned number and
                            // correctly keyed heartbeat identify this pending bulb.
                            bulb.mac = device.address_uint64();
                            this->confirm_bulb_(i);
                        }
                        else if (!bulb.claimed && this->restored_pending_[i] && bulb.mac != 0 &&
                                 bulb.mac == device.address_uint64() && this->repair_attempts_[i] < MAX_REPAIR_ATTEMPTS)
                        {
                            // The checksum-valid heartbeat decoded under our saved key,
                            // but this restored identity reports a different number.
                            this->repair_seen_[i] = true;
                        }
                    }
                }
            }
            return handled;
        }

        void FastconController::queue_discovery_(uint32_t target, const std::vector<uint8_t> &data)
        {
            // Recovery sends EXACTLY one advertisement per candidate, independent of
            // normal repeat_count. Factory retries use later nonblocking rounds.
            std::lock_guard<std::mutex> lock(this->queue_mutex_);
            this->queue_.push_back(Command{target, data, 1});
        }

        void FastconController::send_discovery_round_()
        {
            this->heartbeat_seen_ = false;
            this->discovery_adv_duration_ = this->adv_duration_;
            if (++this->discovery_sequence_ == 255)
                this->discovery_sequence_ = 1;
            if (this->discovery_.phase == DiscoveryPhase::FACTORY)
            {
                // Alternate WAKE and one pending bulb; even max_queue_size: 1 works.
                if (++this->factory_round_ % 2 && this->discovery_.count != 0)
                {
                    for (uint8_t step = 0; step < this->discovery_.count; step++)
                    {
                        const uint8_t i = (this->pending_index_ + step) % this->discovery_.count;
                        const auto &bulb = this->discovery_.bulbs[i];
                        if (bulb.claimed)
                            continue;
                        this->pending_index_ = (i + 1) % this->discovery_.count;
                        if (this->repair_seen_[i] && this->repair_attempts_[i] < MAX_REPAIR_ATTEMPTS &&
                            !this->probe_next_[i])
                        {
                            std::vector<uint8_t> inner(bulb.did.begin(), bulb.did.end());
                            inner.push_back(bulb.light_id);
                            inner.push_back(1);
                            inner.insert(inner.end(), this->mesh_key_.begin(), this->mesh_key_.end());
                            this->repair_attempts_[i]++;
                            this->probe_next_[i] = true;
                            this->discovery_adv_duration_ = std::max<uint16_t>(300, this->adv_duration_);
                            this->queue_discovery_(DISCOVERY_QUEUE_KEY,
                                encode_discovery_command(2, this->discovery_sequence_, inner, this->mesh_key_, this->mesh_key_[3]));
                        }
                        else if (this->factory_seen_[i] && !this->probe_next_[i])
                        {
                            this->probe_next_[i] = true;
                            std::vector<uint8_t> inner(bulb.did.begin(), bulb.did.end());
                            inner.push_back(bulb.light_id);
                            inner.push_back(1);
                            inner.insert(inner.end(), this->mesh_key_.begin(), this->mesh_key_.end());
                            this->discovery_adv_duration_ = std::max<uint16_t>(300, this->adv_duration_);
                            this->queue_discovery_(DISCOVERY_QUEUE_KEY,
                                encode_discovery_command(2, this->discovery_sequence_, inner, DEFAULT_ENCRYPT_KEY, DEFAULT_ENCRYPT_KEY[3]));
                        }
                        else
                        {
                            // A missed SET_KEY reply may mean the bulb already holds the
                            // saved key. Probe it before retrying factory provisioning.
                            this->probe_next_[i] = false;
                            this->queue_discovery_(DISCOVERY_QUEUE_KEY, this->single_control(bulb.light_id, {0xff}));
                        }
                        return;
                    }
                }
                this->discovery_adv_duration_ = std::max<uint16_t>(400, this->adv_duration_);
                this->queue_discovery_(DISCOVERY_QUEUE_KEY,
                    encode_discovery_command(0, this->discovery_sequence_, std::vector<uint8_t>(12, 0), DEFAULT_ENCRYPT_KEY, 0xff));
            }
            else if (this->discovery_.phase == DiscoveryPhase::RECOVERY)
            {
                // Keep boot-window reclaim reachable throughout the long search.
                // This extra WAKE is not a second group probe for the candidate.
                this->recovery_wake_round_ = this->discovery_.candidate % 4 == 0 && !this->recovery_wake_sent_;
                if (this->recovery_wake_round_)
                {
                    this->recovery_wake_sent_ = true;
                    this->discovery_adv_duration_ = std::max<uint16_t>(100, this->adv_duration_);
                    this->queue_discovery_(DISCOVERY_QUEUE_KEY,
                        encode_discovery_command(0, this->discovery_sequence_, std::vector<uint8_t>(12, 0), DEFAULT_ENCRYPT_KEY, 0xff));
                    return;
                }
                // All-bulbs ON at full brightness, preserving color. Only the matching
                // key has a visible effect. No per-light sweep of the keyspace.
                const auto key = app_mesh_key(this->discovery_.candidate);
                this->queue_discovery_(DISCOVERY_QUEUE_KEY,
                    encode_discovery_command(5, this->discovery_sequence_, {0x43, 0x2a, 0xa8, 0, 0xff}, key, key[3]));
            }
            else if (this->discovery_.phase == DiscoveryPhase::ENUMERATE)
                this->queue_discovery_(DISCOVERY_QUEUE_KEY, this->single_control(this->probe_id_, {0xff}));
        }

        void FastconController::finish_discovery_()
        {
            const bool found = this->claimed_count_() != 0 ||
                               (this->discovery_.phase == DiscoveryPhase::ENUMERATE && this->mesh_key_confirmed_);
            // Partial success stops radio work for this boot, not future retries.
            // Keep FACTORY in NVS while pending identities may already hold this key.
            const bool pending = this->discovery_.phase == DiscoveryPhase::FACTORY &&
                                 this->claimed_count_() < this->discovery_.count;
            this->discovery_.phase = pending ? DiscoveryPhase::FACTORY :
                                     found ? DiscoveryPhase::DONE : DiscoveryPhase::FAILED;
            this->discovery_stopped_ = true;
            if (pending)
                ESP_LOGW(TAG, "Factory keying partially confirmed; pending identities retained. Reboot to retry");
            if (!this->save_discovery_())
                this->discovery_failed_ = true;
            if (found)
            {
                // Predictable final state: ON, full brightness, color unchanged.
                this->send_group(0, {0xff}, {});
                for (auto *light : this->lights_)
                    if (light->get_slot() != 0 && light->get_light_id() != 0)
                        light->mirror_discovery_on();
                ESP_LOGI(TAG, "Auto-discovery complete: %u bulbs", this->claimed_count_());
            }
            else
            {
                ESP_LOGW(TAG, "No bulbs confirmed. See README 'Finding Your Mesh Key' (ADB fallback): "
                              "run adb logcat, toggle a bulb in brMesh, find jyq_helper payload/key; "
                              "apply the 8-hex key with fastcon.set_mesh_key or reflash with mesh_key");
                ESP_LOGW(TAG, "adb logcat | { grep -m 1 -o 'jyq_helper: .* payload:.\\{24\\},[[:space:]]*key:[[:space:]]*.\\{8\\}' | "
                              "awk '{print $NF}'; kill -2 $(pgrep -P $$ adb); }");
            }
            this->bind_slots_();
        }

        void FastconController::finish_factory_phase_()
        {
            if (this->claimed_count_() != 0)
                this->finish_discovery_();
            else if (this->discovery_.count != 0)
            {
                // A lost confirmation must not discard a key already written to a bulb.
                ESP_LOGW(TAG, "Factory keying unconfirmed; saved pending identities retained. Reboot to retry");
                this->discovery_stopped_ = true;
            }
            else
            {
                this->discovery_.phase = DiscoveryPhase::RECOVERY;
                if (!this->save_discovery_())
                    this->discovery_failed_ = true;
                ESP_LOGI(TAG, "Recovering app key: one all-bulbs ON probe per key, progress %u/10000", this->discovery_.candidate);
            }
        }

        void FastconController::finish_discovery_round_()
        {
            if (this->discovery_.phase == DiscoveryPhase::FACTORY)
            {
                if (this->claimed_count_() >= this->max_lights_ || millis() - this->discovery_started_ >= this->scan_timeout_ms_)
                    this->finish_factory_phase_();
            }
            else if (this->discovery_.phase == DiscoveryPhase::RECOVERY)
            {
                if (this->recovery_wake_round_)
                    return;
                if (this->heartbeat_seen_)
                {
                    this->discovery_.key = this->mesh_key_;
                    this->discovery_.key_known = 1;
                    this->discovery_.phase = DiscoveryPhase::ENUMERATE;
                    this->mesh_key_confirmed_ = false;
                    this->probe_id_ = 1;
                    if (!this->save_discovery_())
                        this->discovery_failed_ = true;
                }
                else
                {
                    this->discovery_.candidate++;
                    this->recovery_wake_sent_ = false;
                    // Checkpoint every 50 keys, not every packet: bound flash wear.
                    if (this->discovery_.candidate % 50 == 0 && !this->save_discovery_())
                        this->discovery_failed_ = true;
                    if (this->discovery_.candidate % 500 == 0)
                        ESP_LOGI(TAG, "App-key recovery progress: %u/10000", this->discovery_.candidate);
                    if (this->discovery_.candidate == 10000)
                        this->finish_discovery_();
                }
            }
            else if (this->discovery_.phase == DiscoveryPhase::ENUMERATE)
            {
                if (++this->probe_id_ > 16 || this->claimed_count_() >= this->max_lights_)
                {
                    if (!this->mesh_key_confirmed_ && this->claimed_count_() == 0 &&
                        !this->discovery_.runtime_override && this->discovery_.candidate < 9999)
                    {
                        // Checksum-only accidental key match: no address answered it.
                        this->discovery_.candidate++;
                        this->discovery_.phase = DiscoveryPhase::RECOVERY;
                        this->discovery_.key_known = 0;
                        if (!this->save_discovery_())
                            this->discovery_failed_ = true;
                    }
                    else
                        this->finish_discovery_();
                }
            }
        }

        void FastconController::discovery_loop_(uint32_t now)
        {
            if (!this->discovery_radio_ready_)
            {
                this->discovery_radio_ready_ = true;
                this->discovery_started_ = now;
            }
            if (!this->discovery_stopped_ && this->discovery_.phase == DiscoveryPhase::FACTORY &&
                now - this->discovery_started_ >= this->scan_timeout_ms_)
            {
                // A factory advertisement/listen cannot count as a recovery probe.
                if (this->discovery_io_ == DiscoveryIO::DRAIN || this->discovery_io_ == DiscoveryIO::SCAN ||
                    this->discovery_io_ == DiscoveryIO::STOPPING)
                    this->restart_discovery_ = true;
                // Persist any in-memory pending identity even if the deadline ends retries.
                if (this->discovery_failed_)
                    this->save_discovery_();
                this->finish_factory_phase_();
            }
            if (this->discovery_stopped_ || this->discovery_.phase == DiscoveryPhase::DONE ||
                this->discovery_.phase == DiscoveryPhase::FAILED)
            {
                if (this->discovery_io_ == DiscoveryIO::SCAN)
                {
                    this->tracker_->stop_scan();
                    this->discovery_io_ = DiscoveryIO::STOPPING;
                }
                else if (this->discovery_io_ == DiscoveryIO::STOPPING && this->scan_finished_)
                    this->discovery_io_ = DiscoveryIO::REST;
                return;
            }
            if (this->discovery_failed_)
            {
                if (!this->failure_reported_)
                {
                    ESP_LOGW(TAG, "Discovery failed; progress and pending keys retained, retrying in 10s");
                    this->save_discovery_();
                    this->discovery_retry_started_ = now;
                    this->failure_reported_ = true;
                }
                if (this->discovery_io_ == DiscoveryIO::SCAN)
                {
                    this->tracker_->stop_scan();
                    this->discovery_io_ = DiscoveryIO::STOPPING;
                }
                else if (this->discovery_io_ != DiscoveryIO::STOPPING || this->scan_finished_)
                    this->discovery_io_ = DiscoveryIO::REST;
                this->clear_queue();
                if (now - this->discovery_retry_started_ >= DISCOVERY_RETRY_MS)
                {
                    // Ensure pending records and keys are durable before any new SET_KEY.
                    if (!this->save_discovery_())
                    {
                        this->discovery_retry_started_ = now;
                        return;
                    }
                    this->discovery_failed_ = false;
                    this->failure_reported_ = false;
                    this->scan_started_ = false;
                    this->scan_finished_ = false;
                    this->restart_discovery_ = false;
                    this->discovery_io_ = DiscoveryIO::SEND;
                }
                return;
            }
            switch (this->discovery_io_)
            {
            case DiscoveryIO::SEND:
                // Never start another radio round at or beyond the factory deadline.
                if (this->discovery_.phase == DiscoveryPhase::FACTORY &&
                    now - this->discovery_started_ >= this->scan_timeout_ms_)
                {
                    this->finish_factory_phase_();
                    return;
                }
                if (this->adv_state_ != AdvertiseState::IDLE || !this->is_queue_empty())
                    return;
                this->send_discovery_round_();
                this->discovery_io_ = DiscoveryIO::DRAIN;
                this->discovery_io_started_ = now;
                break;
            case DiscoveryIO::DRAIN:
                if (this->adv_state_ != AdvertiseState::IDLE || !this->is_queue_empty())
                    return;
                this->scan_finished_ = false;
                this->scan_started_ = false;
                this->tracker_->start_scan();
                this->discovery_io_ = DiscoveryIO::SCAN;
                this->discovery_io_started_ = now;
                break;
            case DiscoveryIO::SCAN:
                if (this->scan_finished_ ||
                    (this->scan_started_ && now - this->scan_started_at_ >= SCAN_BURST_MS))
                {
                    this->tracker_->stop_scan();
                    this->discovery_io_ = DiscoveryIO::STOPPING;
                }
                else if (now - this->discovery_io_started_ > 3000)
                {
                    ESP_LOGW(TAG, "Discovery scan start not confirmed; key candidate retained");
                    this->discovery_failed_ = true;
                }
                break;
            case DiscoveryIO::STOPPING:
                if (this->scan_finished_)
                {
                    // ESPHome also fires scan-end during failed-start cleanup. Only
                    // a confirmed, full active listen window may consume a candidate.
                    if (!this->restart_discovery_)
                    {
                        if (this->scan_started_ && this->scan_ended_at_ - this->scan_started_at_ >= SCAN_BURST_MS)
                            this->finish_discovery_round_();
                        else
                        {
                            ESP_LOGW(TAG, "Discovery scan ended without a complete listen window; key candidate retained");
                            this->discovery_failed_ = true;
                        }
                    }
                    this->restart_discovery_ = false;
                    this->discovery_io_ = DiscoveryIO::REST;
                    this->discovery_io_started_ = now;
                }
                else if (now - this->discovery_io_started_ > 3000)
                {
                    ESP_LOGW(TAG, "Discovery scan did not finish; key candidate retained");
                    this->discovery_failed_ = true;
                }
                break;
            case DiscoveryIO::REST:
                if (now - this->discovery_io_started_ >= SCAN_REST_MS)
                    this->discovery_io_ = DiscoveryIO::SEND;
                break;
            }
        }
#endif
    } // namespace fastcon
} // namespace esphome
