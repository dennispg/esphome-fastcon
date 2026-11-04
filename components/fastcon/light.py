"""Light platform for Fastcon BLE lights."""

import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.components import light
from esphome.components.light.effects import MONOCHROMATIC_EFFECTS, register_effect
from esphome.components.light.types import LightEffect
from esphome.const import (
    CONF_EFFECTS,
    CONF_LIGHT_ID,
    CONF_MODE,
    CONF_NAME,
    CONF_OUTPUT_ID,
    CONF_PLATFORM,
    CONF_SPEED,
)
from esphome.core import HexInt

from .fastcon_controller import CONF_GROUP_ID, FastconController

DEPENDENCIES = ["esp32_ble"]
AUTO_LOAD = ["light"]

CONF_CONTROLLER_ID = "controller_id"
CONF_MEMBERS = "members"
CONF_FASTCON_SCENE = "fastcon_scene"
CONF_STEPS = "steps"

fastcon_ns = cg.esphome_ns.namespace("fastcon")
FastconLight = fastcon_ns.class_("FastconLight", light.LightOutput, cg.Component)
FastconSceneEffect = fastcon_ns.class_("FastconSceneEffect", LightEffect)

# Built-in effect steps: each is a bit set of the bulb's channels
SCENE_BLUE = 0x01
SCENE_RED = 0x02
SCENE_GREEN = 0x04
SCENE_COLORS = {
    "off": 0x00,
    "blue": SCENE_BLUE,
    "red": SCENE_RED,
    "green": SCENE_GREEN,
    "yellow": SCENE_RED | SCENE_GREEN,
    "cyan": SCENE_GREEN | SCENE_BLUE,
    "magenta": SCENE_RED | SCENE_BLUE,
    "purple": SCENE_RED | SCENE_BLUE,
    "white": SCENE_RED | SCENE_GREEN | SCENE_BLUE,
    "warm_white": 0x08,
    "cold_white": 0x10,
}
SCENE_MAX_STEPS = 9


def _validate_scene_step(value):
    # YAML reads an unquoted `off` as false
    if value is False:
        return 0x00
    if isinstance(value, int) and not isinstance(value, bool):
        return cv.int_range(min=0x00, max=0x1F)(value)
    if isinstance(value, str):
        name = value.strip().lower().replace(" ", "_").replace("-", "_")
        if name in SCENE_COLORS:
            return SCENE_COLORS[name]
    raise cv.Invalid(
        f"Unknown effect step {value!r}. Use one of {', '.join(SCENE_COLORS)}, "
        "or a number 0-0x1F (0x01 blue, 0x02 red, 0x04 green, 0x08 warm white, "
        "0x10 cold white, added together)"
    )


# One of the bulbs' own flash/fade effects. The bulbs run it by themselves after a single
# command, which only addresses groups, so it is only allowed on fastcon group lights.
@register_effect(
    CONF_FASTCON_SCENE,
    FastconSceneEffect,
    "Scene",
    {
        cv.Optional(CONF_MODE, default="fade"): cv.one_of("flash", "fade", lower=True),
        # 1-100, higher is faster
        cv.Optional(CONF_SPEED, default=50): cv.int_range(min=1, max=100),
        cv.Required(CONF_STEPS): cv.All(
            cv.ensure_list(_validate_scene_step),
            cv.Length(
                min=1,
                max=SCENE_MAX_STEPS,
                msg=f"An effect has 1 to {SCENE_MAX_STEPS} steps",
            ),
        ),
    },
)
async def fastcon_scene_effect_to_code(config, effect_id):
    var = cg.new_Pvariable(effect_id, config[CONF_NAME])
    cg.add(var.set_flash(config[CONF_MODE] == "flash"))
    cg.add(var.set_speed(config[CONF_SPEED]))
    cg.add(var.set_steps([HexInt(step) for step in config[CONF_STEPS]]))
    return var


def _validate_effects(value):
    # The usual brightness-light effects, plus fastcon_scene. That one is registered on its own
    # (not in the shared lists), so other light platforms don't accept it. The list is built when
    # validating, so effects that other components register later are included.
    return light.validate_effects([*MONOCHROMATIC_EFFECTS, CONF_FASTCON_SCENE])(value)


def _validate_members_need_group(config):
    if CONF_MEMBERS in config and CONF_GROUP_ID not in config:
        raise cv.Invalid(
            "members can only be used on a group light (one with group_id)",
            [CONF_MEMBERS],
        )
    return config


def _validate_scenes_need_group(config):
    if CONF_GROUP_ID in config:
        return config
    for i, effect in enumerate(config.get(CONF_EFFECTS, [])):
        if CONF_FASTCON_SCENE in effect:
            raise cv.Invalid(
                "fastcon_scene effects only work on a group light (one with group_id)",
                [CONF_EFFECTS, i],
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
            cv.Optional(CONF_EFFECTS): _validate_effects,
            cv.Optional(CONF_CONTROLLER_ID, default="fastcon_controller"): cv.use_id(
                FastconController
            ),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    cv.has_exactly_one_key(CONF_LIGHT_ID, CONF_GROUP_ID),
    _validate_members_need_group,
    _validate_scenes_need_group,
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
        cg.add(controller.register_group(var))
        for member_id in config.get(CONF_MEMBERS, []):
            member = await cg.get_variable(member_id)
            cg.add(var.add_member(member))
    else:
        cg.add(controller.register_light(var))
