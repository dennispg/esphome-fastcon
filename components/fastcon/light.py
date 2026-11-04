"""Light platform for Fastcon BLE lights."""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import light
from esphome.const import CONF_LIGHT_ID, CONF_OUTPUT_ID, CONF_PLATFORM

from .fastcon_controller import CONF_GROUP_ID, FastconController

DEPENDENCIES = ["esp32_ble"]
AUTO_LOAD = ["light"]

CONF_CONTROLLER_ID = "controller_id"
CONF_MEMBERS = "members"

fastcon_ns = cg.esphome_ns.namespace("fastcon")
FastconLight = fastcon_ns.class_("FastconLight", light.LightOutput, cg.Component)


def _validate_members_need_group(config):
    if CONF_MEMBERS in config and CONF_GROUP_ID not in config:
        raise cv.Invalid(
            "members can only be used on a group light (one with group_id)",
            [CONF_MEMBERS],
        )
    return config


CONFIG_SCHEMA = cv.All(
    light.BRIGHTNESS_ONLY_LIGHT_SCHEMA.extend(
        {
            cv.GenerateID(CONF_OUTPUT_ID): cv.declare_id(FastconLight),
            # A single bulb...
            cv.Optional(CONF_LIGHT_ID): cv.int_range(min=1, max=255),
            # ...or a group: one broadcast that every bulb in the group obeys.
            # 0 is the brMesh app's "All" group.
            cv.Optional(CONF_GROUP_ID): cv.int_range(min=0, max=255),
            # Single-bulb lights whose entities follow the group's state.
            # Group 0 without members follows every single-bulb fastcon light.
            cv.Optional(CONF_MEMBERS): cv.ensure_list(cv.use_id(light.LightState)),
            cv.Optional(CONF_CONTROLLER_ID, default="fastcon_controller"): cv.use_id(
                FastconController
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.has_exactly_one_key(CONF_LIGHT_ID, CONF_GROUP_ID),
    _validate_members_need_group,
)


def _validate_member(member_id):
    fconf = fv.full_config.get()
    path = fconf.get_path_for_id(member_id)[:-1]
    member_config = fconf.get_config_for_path(path)
    if (
        member_config.get(CONF_PLATFORM) != "fastcon"
        or CONF_LIGHT_ID not in member_config
    ):
        raise cv.Invalid(
            f"Group member '{member_id}' must be a fastcon light with a light_id (not a group)"
        )
    return member_id


FINAL_VALIDATE_SCHEMA = cv.Schema(
    {cv.Optional(CONF_MEMBERS): [_validate_member]},
    extra=cv.ALLOW_EXTRA,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_OUTPUT_ID], config.get(CONF_LIGHT_ID, 0))
    await cg.register_component(var, config)
    await light.register_light(var, config)

    controller = await cg.get_variable(config[CONF_CONTROLLER_ID])
    cg.add(var.set_controller(controller))

    if CONF_GROUP_ID in config:
        cg.add(var.set_group_id(config[CONF_GROUP_ID]))
        for member_id in config.get(CONF_MEMBERS, []):
            member = await cg.get_variable(member_id)
            cg.add(var.add_member(member))
    else:
        cg.add(controller.register_light(var))
