import esphome.codegen as cg
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome import automation
from esphome.components import esp32_ble, esp32_ble_tracker, light
from esphome.const import CONF_ID, CONF_LIGHT_ID

DEPENDENCIES = ["esp32_ble"]

CONF_MESH_KEY = "mesh_key"
CONF_DISCOVERY = "discovery"
CONF_MAX_LIGHTS = "max_lights"
CONF_SCAN_TIMEOUT_S = "scan_timeout_s"
CONF_ADV_INTERVAL_MIN = "adv_interval_min"
CONF_ADV_INTERVAL_MAX = "adv_interval_max"
CONF_ADV_DURATION = "adv_duration"
CONF_ADV_GAP = "adv_gap"
CONF_MAX_QUEUE_SIZE = "max_queue_size"
CONF_REPEAT_COUNT = "repeat_count"
CONF_GROUP_ID = "group_id"
CONF_GROUP = "group"
CONF_TX_POWER = "tx_power"

esp_power_level_t = cg.global_ns.enum("esp_power_level_t")
# Advertising TX power in dBm
TX_POWER_LEVELS = {
    -12: esp_power_level_t.ESP_PWR_LVL_N12,
    -9: esp_power_level_t.ESP_PWR_LVL_N9,
    -6: esp_power_level_t.ESP_PWR_LVL_N6,
    -3: esp_power_level_t.ESP_PWR_LVL_N3,
    0: esp_power_level_t.ESP_PWR_LVL_N0,
    3: esp_power_level_t.ESP_PWR_LVL_P3,
    6: esp_power_level_t.ESP_PWR_LVL_P6,
    9: esp_power_level_t.ESP_PWR_LVL_P9,
}

DEFAULT_ADV_INTERVAL_MIN = 0x20
DEFAULT_ADV_INTERVAL_MAX = 0x40
DEFAULT_ADV_DURATION = 50
DEFAULT_ADV_GAP = 10
DEFAULT_MAX_QUEUE_SIZE = 100
DEFAULT_REPEAT_COUNT = 1


def validate_hex_bytes(value):
    # Returns the key as a normalised 8-character hex string (not a number), so that
    # cv.sensitive can mask it: it only masks string values.
    if isinstance(value, str):
        value = value.replace(" ", "")
        if len(value) != 8:
            raise cv.Invalid("Mesh key must be exactly 4 bytes (8 hex characters)")

        try:
            int(value, 16)
        except ValueError as err:
            raise cv.Invalid(f"Invalid hex value: {err}")
        return value.lower()
    raise cv.Invalid("Mesh key must be a string")


# Marks the mesh key as a secret so config dumps and frontends mask it. cv.sensitive is
# new in ESPHome 2026.x; older versions don't have it and get the plain validator.
_sensitive = getattr(cv, "sensitive", lambda inner: inner)


fastcon_ns = cg.esphome_ns.namespace("fastcon")
FastconController = fastcon_ns.class_("FastconController", cg.Component)
SetGroupAction = fastcon_ns.class_(
    "SetGroupAction", automation.Action, cg.Parented.template(FastconController)
)
SetMeshKeyAction = fastcon_ns.class_(
    "SetMeshKeyAction", automation.Action, cg.Parented.template(FastconController)
)
AssignMembersAction = fastcon_ns.class_(
    "AssignMembersAction", automation.Action, cg.Parented.template(FastconController)
)


def validate_action_key(value):
    if (not isinstance(value, str) or len(value) != 8
            or any(char not in "0123456789abcdefABCDEF" for char in value)):
        raise cv.Invalid("Runtime mesh key must be exactly eight hexadecimal characters")
    return value.lower()


def validate_mesh_key(value):
    if value == "auto":
        return value
    return validate_hex_bytes(value)


def validate_queue_size(config):
    if config[CONF_MAX_QUEUE_SIZE] < 2:
        raise cv.Invalid("max_queue_size must be at least 2 for the two-packet group scene operation")
    return config


def validate_discovery(config):
    if config[CONF_MESH_KEY] == "auto":
        config[CONF_DISCOVERY] = DISCOVERY_SCHEMA(config.get(CONF_DISCOVERY, {}))
    elif CONF_DISCOVERY in config:
        raise cv.Invalid("discovery requires mesh_key: auto", [CONF_DISCOVERY])
    return config


DISCOVERY_SCHEMA = cv.Schema(
    {
        cv.Optional(CONF_MAX_LIGHTS, default=4): cv.int_range(min=1, max=16),
        cv.Optional(CONF_SCAN_TIMEOUT_S, default=30): cv.int_range(min=1, max=600),
        cv.GenerateID(esp32_ble_tracker.CONF_ESP32_BLE_ID): cv.use_id(
            esp32_ble_tracker.ESP32BLETracker
        ),
    }
)


def final_validate_discovery(config):
    if CONF_DISCOVERY not in config:
        return config
    full = fv.full_config.get()
    tracker = full.get("esp32_ble_tracker")
    if tracker is None or tracker["scan_parameters"]["continuous"]:
        raise cv.Invalid(
            "Auto-discovery requires esp32_ble_tracker with scan_parameters: {continuous: false}"
        )
    return config


FINAL_VALIDATE_SCHEMA = final_validate_discovery

CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.Optional(CONF_ID, default="fastcon_controller"): cv.declare_id(
                FastconController
            ),
            cv.GenerateID(esp32_ble.CONF_BLE_ID): cv.use_id(esp32_ble.ESP32BLE),
            cv.Required(CONF_MESH_KEY): _sensitive(validate_mesh_key),
            cv.Optional(CONF_DISCOVERY): DISCOVERY_SCHEMA,
            # Advertising TX power in dBm. Left at the Bluetooth stack's default unless set.
            cv.Optional(CONF_TX_POWER): cv.enum(TX_POWER_LEVELS, int=True),
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
    ).extend(cv.COMPONENT_SCHEMA),
    validate_discovery,
    validate_queue_size,
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    # Get the Bluetooth controller's advertising start/stop results, which arrive
    # asynchronously, so a rejected advertisement shows up in the logs
    ble = await cg.get_variable(config[esp32_ble.CONF_BLE_ID])
    esp32_ble.register_gap_event_handler(ble, var)

    if CONF_TX_POWER in config:
        cg.add(var.set_tx_power(config[CONF_TX_POWER]))

    if config[CONF_MESH_KEY] == "auto":
        cg.add_define("USE_FASTCON_DISCOVERY")
        discovery = config[CONF_DISCOVERY]
        await esp32_ble_tracker.register_ble_device(var, discovery)
        tracker = await cg.get_variable(discovery[esp32_ble_tracker.CONF_ESP32_BLE_ID])
        cg.add(var.set_discovery(
            tracker, discovery[CONF_MAX_LIGHTS], discovery[CONF_SCAN_TIMEOUT_S]
        ))
        cg.add(var.set_preference_key(cg.RawExpression(
            f'fnv1_hash("fastcon.discovery.{config[CONF_ID]}")'
        )))
    else:
        mesh_key = int(str(config[CONF_MESH_KEY]), 16)
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


ASSIGN_MEMBERS_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.use_id(FastconController),
        # Omitted: every group light that has members
        cv.Optional(CONF_GROUP): cv.use_id(light.LightState),
    }
)


def _validate_assign_members(value):
    # `- fastcon.assign_members` (all groups) reaches here as {}, and
    # `- fastcon.assign_members: some_group` as the bare id
    if isinstance(value, dict):
        return ASSIGN_MEMBERS_SCHEMA(value)
    return ASSIGN_MEMBERS_SCHEMA({CONF_GROUP: value})


# Write a group light's number into each bulb listed in its members, or do that for
# every group light with members when no group is given.
@automation.register_action(
    "fastcon.assign_members",
    AssignMembersAction,
    _validate_assign_members,
    synchronous=True,
)
async def assign_members_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    if CONF_GROUP in config:
        group = await cg.get_variable(config[CONF_GROUP])
        cg.add(var.set_group(group))
    return var


@automation.register_action(
    "fastcon.set_mesh_key",
    SetMeshKeyAction,
    cv.Schema(
        {
            cv.GenerateID(): cv.use_id(FastconController),
            cv.Required(CONF_MESH_KEY): cv.templatable(_sensitive(validate_action_key)),
        }
    ),
    synchronous=True,
)
async def set_mesh_key_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    key = await cg.templatable(config[CONF_MESH_KEY], args, cg.std_string)
    cg.add(var.set_key(key))
    parent = await cg.get_variable(config[CONF_ID])
    cg.add(parent.set_preference_key(cg.RawExpression(
        f'fnv1_hash("fastcon.discovery.{config[CONF_ID]}")'
    )))
    return var
