#include <algorithm>
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
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
                ESP_LOGCONFIG(TAG, "Setting up Fastcon BLE group light (group from the controller)...");
                return;
            }
            ESP_LOGCONFIG(TAG, "Setting up Fastcon BLE light (ID: %d)...", this->light_id_);
            this->controller_->register_member(this);
        }

        void FastconLight::loop()
        {
            // When an effect is stopped without the colour changing, ESPHome doesn't call
            // write_state, so the bulbs would carry on with the effect. Send the steady colour ourselves.
            if (!this->is_group_ || this->state_ == nullptr)
                return;
            const bool active = this->state_->get_effect_name() != "None";
            if (this->effect_was_active_ && !active)
            {
                ESP_LOGD(TAG, "Group: effect stopped, sending the steady colour");
                this->write_state(this->state_);
            }
            this->effect_was_active_ = active;
        }

        void FastconLight::mirror(const light::LightColorValues &values)
        {
            if (this->state_ == nullptr)
                return;
            // The next write for this bulb comes from this mirror, so it mustn't be sent (the group
            // command already reached the bulb). Time-limited so a real command later still goes out.
            this->mirror_until_ = millis() + 1000;
            auto call = this->state_->make_call();
            call.from_light_color_values(values);
            call.set_transition_length(0);
            call.perform();
        }

        void FastconLight::set_controller(FastconController *controller)
        {
            this->controller_ = controller;
        }

        light::LightTraits FastconLight::get_traits()
        {
            auto traits = light::LightTraits();
            traits.set_supported_color_modes({light::ColorMode::RGB, light::ColorMode::WHITE, light::ColorMode::BRIGHTNESS, light::ColorMode::COLD_WARM_WHITE});
            traits.set_min_mireds(153);
            traits.set_max_mireds(500);
            return traits;
        }

        void FastconLight::write_state(light::LightState *state)
        {
            // Get the light data bits from the state
            auto light_data = this->controller_->get_light_data(state);

            if (this->is_group_)
            {
                // While a built-in effect runs, the bulbs run it themselves. A steady-colour
                // command here would replace the effect in the queue (or stop it on the bulbs), so skip it.
                // When the effect is turned off, the next write sends the steady colour and ends it.
                // (Turning off always goes out, so an effect can be switched off.)
                if (state->get_effect_name() != "None" && state->remote_values.is_on())
                {
                    ESP_LOGD(TAG, "Group: effect running, no steady-colour command sent");
                    return;
                }
                // One broadcast for every bulb in the group, then show it on the bulbs' entities
                this->controller_->send_group(light_data);
                this->controller_->mirror_to_members(state->remote_values);
                return;
            }
            if (this->mirror_until_ != 0)
            {
                const bool from_mirror = millis() < this->mirror_until_;
                this->mirror_until_ = 0;
                if (from_mirror)
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
