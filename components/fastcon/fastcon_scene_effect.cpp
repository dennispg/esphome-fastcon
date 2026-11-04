#include "esphome/core/log.h"
#include "esphome/components/light/light_state.h"
#include "fastcon_scene_effect.h"

namespace esphome
{
    namespace fastcon
    {
        static const char *const TAG = "fastcon.effect";

        // LightState calls start() and stop() from LightCall::perform(), and then always writes the
        // state on its next loop: set_immediately_() or start_transition_() follow both
        // start_effect_() and stop_effect_(). So stop() only has to lift the hold on steady-colour
        // writes, and that write sends the steady colour.

        void FastconSceneEffect::start()
        {
            auto *light = this->get_light_();
            if (!light->is_group())
            {
                // Config validation rejects this too; the effect command only addresses groups
                ESP_LOGW(TAG, "'%s': effect '%s' only works on a group light (with group_id)",
                         this->state_->get_name().c_str(), this->get_name().c_str());
                return;
            }
            light->start_scene(&this->scene_);
        }

        void FastconSceneEffect::stop()
        {
            auto *light = this->get_light_();
            if (light->is_group())
                light->stop_scene(&this->scene_);
        }
    } // namespace fastcon
} // namespace esphome
