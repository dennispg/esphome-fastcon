import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components import light
from esphome.const import CONF_ID, CONF_LIGHT_ID
from esphome.core import HexInt

DEPENDENCIES = ["esp32_ble"]

CONF_MESH_KEY = "mesh_key"
CONF_ADV_INTERVAL_MIN = "adv_interval_min"
CONF_ADV_INTERVAL_MAX = "adv_interval_max"
CONF_ADV_DURATION = "adv_duration"
CONF_ADV_GAP = "adv_gap"
CONF_MAX_QUEUE_SIZE = "max_queue_size"
CONF_REPEAT_COUNT = "repeat_count"
CONF_GROUP_ID = "group_id"
CONF_GROUP = "group"

DEFAULT_ADV_INTERVAL_MIN = 0x20
DEFAULT_ADV_INTERVAL_MAX = 0x40
DEFAULT_ADV_DURATION = 50
DEFAULT_ADV_GAP = 10
DEFAULT_MAX_QUEUE_SIZE = 100
DEFAULT_REPEAT_COUNT = 1


def validate_hex_bytes(value):
    if isinstance(value, str):
        value = value.replace(" ", "")
        if len(value) != 8:
            raise cv.Invalid("Mesh key must be exactly 4 bytes (8 hex characters)")

        try:
            return HexInt(int(value, 16))
        except ValueError as err:
            raise cv.Invalid(f"Invalid hex value: {err}")
    raise cv.Invalid("Mesh key must be a string")


fastcon_ns = cg.esphome_ns.namespace("fastcon")
FastconController = fastcon_ns.class_("FastconController", cg.Component)
SetGroupAction = fastcon_ns.class_(
    "SetGroupAction", automation.Action, cg.Parented.template(FastconController)
)
AssignMembersAction = fastcon_ns.class_(
    "AssignMembersAction", automation.Action, cg.Parented.template(FastconController)
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_ID, default="fastcon_controller"): cv.declare_id(
            FastconController
        ),
        cv.Required(CONF_MESH_KEY): validate_hex_bytes,
        cv.Optional(
            CONF_ADV_INTERVAL_MIN, default=DEFAULT_ADV_INTERVAL_MIN
        ): cv.uint16_t,
        cv.Optional(
            CONF_ADV_INTERVAL_MAX, default=DEFAULT_ADV_INTERVAL_MAX
        ): cv.uint16_t,
        cv.Optional(CONF_ADV_DURATION, default=DEFAULT_ADV_DURATION): cv.uint16_t,
        cv.Optional(CONF_ADV_GAP, default=DEFAULT_ADV_GAP): cv.uint16_t,
        cv.Optional(
            CONF_MAX_QUEUE_SIZE, default=DEFAULT_MAX_QUEUE_SIZE
        ): cv.positive_int,
        # Broadcast each command this many times, interleaved across lights
        cv.Optional(CONF_REPEAT_COUNT, default=DEFAULT_REPEAT_COUNT): cv.int_range(
            min=1, max=5
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    if CONF_MESH_KEY in config:
        mesh_key = config[CONF_MESH_KEY]
        key_bytes = [(mesh_key >> (i * 8)) & 0xFF for i in range(3, -1, -1)]
        cg.add(var.set_mesh_key(key_bytes))

    if config[CONF_ADV_INTERVAL_MAX] < config[CONF_ADV_INTERVAL_MIN]:
        raise cv.Invalid(
            f"adv_interval_max ({config[CONF_ADV_INTERVAL_MAX]}) must be >= "
            f"adv_interval_min ({config[CONF_ADV_INTERVAL_MIN]})"
        )

    cg.add(var.set_adv_interval_min(config[CONF_ADV_INTERVAL_MIN]))
    cg.add(var.set_adv_interval_max(config[CONF_ADV_INTERVAL_MAX]))
    cg.add(var.set_adv_duration(config[CONF_ADV_DURATION]))
    cg.add(var.set_adv_gap(config[CONF_ADV_GAP]))
    cg.add(var.set_max_queue_size(config[CONF_MAX_QUEUE_SIZE]))
    cg.add(var.set_repeat_count(config[CONF_REPEAT_COUNT]))


# Write a group number into one bulb. The bulb keeps it; group 0 removes it from its group.
@automation.register_action(
    "fastcon.set_group",
    SetGroupAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(FastconController),
            cv.Required(CONF_LIGHT_ID): cv.templatable(cv.int_range(min=1, max=255)),
            cv.Required(CONF_GROUP_ID): cv.templatable(cv.int_range(min=0, max=255)),
        }
    ),
    synchronous=True,
)
async def set_group_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    light_id = await cg.templatable(config[CONF_LIGHT_ID], args, cg.uint8)
    cg.add(var.set_light_id(light_id))
    group_id = await cg.templatable(config[CONF_GROUP_ID], args, cg.uint8)
    cg.add(var.set_group_id(group_id))
    return var


# Write a group light's number into each bulb listed in its members.
@automation.register_action(
    "fastcon.assign_members",
    AssignMembersAction,
    cv.maybe_simple_value(
        {
            cv.GenerateID(): cv.use_id(FastconController),
            cv.Required(CONF_GROUP): cv.use_id(light.LightState),
        },
        key=CONF_GROUP,
    ),
    synchronous=True,
)
async def assign_members_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    group = await cg.get_variable(config[CONF_GROUP])
    cg.add(var.set_group(group))
    return var
