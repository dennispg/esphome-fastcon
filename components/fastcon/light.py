"""Light platform for Fastcon BLE lights."""

import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import light
from esphome.const import CONF_LIGHT_ID, CONF_OUTPUT_ID

from .fastcon_controller import FastconController

DEPENDENCIES = ["esp32_ble"]
AUTO_LOAD = ["light"]

CONF_CONTROLLER_ID = "controller_id"
CONF_GROUP = "group"

fastcon_ns = cg.esphome_ns.namespace("fastcon")
FastconLight = fastcon_ns.class_("FastconLight", light.LightOutput, cg.Component)

def _validate_light_id(config):
    if not config[CONF_GROUP] and config[CONF_LIGHT_ID] < 1:
        raise cv.Invalid("light_id (1-255) is required unless group: true")
    return config


CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(FastconLight),
            cv.Optional(CONF_LIGHT_ID, default=0): cv.int_range(min=0, max=255),
            # group: true makes this the "all lights" entity (one group command
            # to the controller's group id) instead of a single bulb.
            cv.Optional(CONF_GROUP, default=False): cv.boolean,
            cv.Optional(CONF_CONTROLLER_ID, default="fastcon_controller"): cv.use_id(
                FastconController
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    _validate_light_id,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], config[CONF_LIGHT_ID])
    await cg.register_component(var, config)
    await light.register_light(var, config)

    controller = await cg.get_variable(config[CONF_CONTROLLER_ID])
    cg.add(var.set_controller(controller))
    if config[CONF_GROUP]:
        cg.add(var.set_group(True))
