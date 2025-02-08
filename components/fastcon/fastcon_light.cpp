#include <algorithm>
#include "esphome/core/log.h"
#include "fastcon_light.h"
#include "fastcon_controller.h"

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
            ESP_LOGCONFIG(TAG, "Setting up Fastcon BLE light (ID: %d)...", this->light_id_);
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

            // Debug output - print the light state values
            bool is_on = (light_data[0] & 0x80) == 1;
            float brightness = (light_data[0] & 0x7F) / 127.0f;
            auto r = light_data[2];
            auto g = light_data[3];
            auto b = light_data[1];
            auto warm = light_data[4];
            auto cold = light_data[5];
            ESP_LOGD(TAG, "Writing state: light_id=%d, on=%d, brightness=%.1f%%, rgb=(%d,%d,%d), warm=%d, cold=%d", light_id_, is_on, (brightness / 127.0f) * 100.0f, r, g, b, warm, cold);

            // Generate the advertisement payload
            auto adv_data = this->controller_->single_control(this->light_id_, light_data);

            // Debug output - print payload as hex
            char hex_str[adv_data.size() * 2 + 1]; // Each byte needs 2 chars + null terminator
            for (size_t i = 0; i < adv_data.size(); i++)
            {
                sprintf(hex_str + (i * 2), "%02X", adv_data[i]);
            }
            hex_str[adv_data.size() * 2] = '\0'; // Ensure null termination
            ESP_LOGD(TAG, "Advertisement Payload (%d bytes): %s", adv_data.size(), hex_str);

            // Send the advertisement
            this->controller_->queueCommand(this->light_id_, adv_data);
        }
    } // namespace fastcon
} // namespace esphome
