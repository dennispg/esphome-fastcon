#include <algorithm>
#include "esphome/core/log.h"
#include "fastcon_light.h"
#include "fastcon_controller.h"
#include "utils.h"

namespace esphome
{
    namespace fastcon
    {
        static const char *const TAG = "fastcon.light";

        void FastconLight::setup()
        {
            if (this->controller_ == nullptr)
            {
                ESP_LOGE(TAG, "Controller not set for light %d!", this->light_id_);
                this->mark_failed();
                return;
            }
            if (this->is_group_)
            {
                ESP_LOGCONFIG(TAG, "Setting up Fastcon BLE group light (group: %u, members: %u)...", this->group_id_,
                              (unsigned) this->get_members_().size());
                return;
            }
            ESP_LOGCONFIG(TAG, "Setting up Fastcon BLE light (ID: %d)...", this->light_id_);
        }

        void FastconLight::set_controller(FastconController *controller)
        {
            this->controller_ = controller;
        }

        void FastconLight::add_member(light::LightState *member)
        {
            // Config validation guarantees members are single-bulb fastcon lights
            this->members_.push_back(static_cast<FastconLight *>(member->get_output()));
        }

        void FastconLight::assign_members()
        {
            if (this->group_id_ == 0)
            {
                ESP_LOGW(TAG, "Group 0 already reaches every bulb, nothing to assign");
                return;
            }
            if (this->members_.empty())
            {
                ESP_LOGW(TAG, "Group %u has no members to assign", this->group_id_);
                return;
            }
            for (auto *member : this->members_)
                this->controller_->send_set_group(member->get_light_id(), this->group_id_);
        }

        const std::vector<FastconLight *> &FastconLight::get_members_() const
        {
            if (this->members_.empty() && this->group_id_ == 0)
                return this->controller_->get_lights();
            return this->members_;
        }

        light::LightTraits FastconLight::get_traits()
        {
            auto traits = light::LightTraits();
            traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::WHITE, light::ColorMode::BRIGHTNESS, light::ColorMode::COLD_WARM_WHITE});
            traits.set_min_mireds(153);
            traits.set_max_mireds(500);
            return traits;
        }

        void FastconLight::mirror(const light::LightColorValues &values, const std::vector<uint8_t> &light_data)
        {
            if (this->state_ == nullptr)
                return;
            this->mirrored_data_ = light_data;
            auto call = this->state_->make_call();
            call.from_light_color_values(values);
            call.set_transition_length(0);
            call.perform();
        }

        void FastconLight::write_state(light::LightState *state)
        {
            // Get the light data bits from the state
            auto light_data = this->controller_->get_light_data(state);

            if (this->is_group_)
            {
                if (this->boot_write_pending_)
                {
                    // The first write is ESPHome restoring the group entity's state at boot. Broadcasting
                    // it would override every member bulb's own restored state, so only the entity is restored.
                    this->boot_write_pending_ = false;
                    ESP_LOGD(TAG, "Group %u: not sending the restored boot state", this->group_id_);
                    return;
                }

                const auto &members = this->get_members_();
                std::vector<uint8_t> member_ids;
                member_ids.reserve(members.size());
                for (auto *member : members)
                    member_ids.push_back(member->get_light_id());

                // One broadcast for every bulb in the group
                this->controller_->send_group(this->group_id_, light_data, member_ids);

                // Show the result on the members' entities. Only the end state of a transition is
                // mirrored, so members don't each run (and send) the intermediate steps.
                if (state->current_values == state->remote_values)
                {
                    for (auto *member : members)
                        member->mirror(state->remote_values, light_data);
                }
                return;
            }

            if (!this->mirrored_data_.empty())
            {
                // A group command already delivered this exact state to the bulb, so don't send it
                // again. Anything different (a real change since the mirror) still goes out.
                const bool already_delivered = light_data == this->mirrored_data_;
                this->mirrored_data_.clear();
                if (already_delivered)
                {
                    ESP_LOGV(TAG, "Light %d: state set by a group command, nothing to send", light_id_);
                    return;
                }
            }

            // One short line per change; the full payload dumps are verbose-only
            bool is_on = (light_data[0] & 0x80) != 0;
            float brightness = ((light_data[0] & 0x7F) / 127.0f) * 100.0f;
            if (light_data.size() == 1)
            {
                ESP_LOGD(TAG, "Light %d: on=%d, brightness=%.0f%%", light_id_, is_on, brightness);
            }
            else
            {
                ESP_LOGD(TAG, "Light %d: on=%d, brightness=%.0f%%, rgb=(%d,%d,%d), warm=%d, cold=%d", light_id_, is_on, brightness,
                         light_data[2], light_data[3], light_data[1], light_data[4], light_data[5]);
            }

            // Generate the advertisement payload
            auto adv_data = this->controller_->single_control(this->light_id_, light_data);
            ESP_LOGV(TAG, "Advertisement Payload (%u bytes): %s", (unsigned) adv_data.size(), vector_to_hex_string(adv_data).c_str());

            // Send the advertisement
            this->controller_->queueCommand(this->light_id_, adv_data);
        }
    } // namespace fastcon
} // namespace esphome
