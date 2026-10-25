import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import ethernet, sensor
from esphome.const import (
    CONF_DISABLED_BY_DEFAULT,
    CONF_ID,
    CONF_NAME,
    DEVICE_CLASS_ENERGY,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_VOLTAGE,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
)

DEPENDENCIES = ["ethernet"]
AUTO_LOAD = ["sensor"]
CODEOWNERS = []

myenergi_ns = cg.esphome_ns.namespace("myenergi_ethernet")
MyEnergiEthernetComponent = myenergi_ns.class_(
    "MyEnergiEthernetComponent", cg.Component
)

CONF_ETHERNET_ID = "ethernet_id"
CONF_SERIAL = "serial"
CONF_DEVICES = "devices"
CONF_GRID_POWER = "grid_power"
CONF_CHARGING_POWER = "charging_power"
CONF_SESSION_ENERGY = "session_energy"
CONF_PWM = "pwm"
CONF_VOLTAGE_1 = "voltage_candidate_1"
CONF_VOLTAGE_2 = "voltage_candidate_2"
CONF_VOLTAGE_3 = "voltage_candidate_3"
CONF_GRID_FREQUENCY = "grid_frequency_candidate"

SENSOR_NAME_SUFFIXES = {
    CONF_CHARGING_POWER: "charging power",
    CONF_SESSION_ENERGY: "session energy",
    CONF_PWM: "PWM",
    CONF_VOLTAGE_1: "voltage candidate 1",
    CONF_VOLTAGE_2: "voltage candidate 2",
    CONF_VOLTAGE_3: "voltage candidate 3",
    CONF_GRID_FREQUENCY: "grid frequency candidate",
}


def _candidate_sensor_schema(**kwargs):
    """Keep unverified candidate readings disabled in Home Assistant by default."""
    schema = sensor.sensor_schema(**kwargs)
    return cv.All(
        schema,
        lambda config: {**config, CONF_DISABLED_BY_DEFAULT: True},
    )


device_schema = cv.Schema(
    {
        cv.Required(CONF_SERIAL): cv.int_range(min=1, max=0xFFFFFFFF),
        cv.Optional(CONF_NAME): cv.string,
        cv.Optional(CONF_CHARGING_POWER): sensor.sensor_schema(
            unit_of_measurement="W",
            icon="mdi:flash",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_SESSION_ENERGY): sensor.sensor_schema(
            unit_of_measurement="kWh",
            icon="mdi:lightning-bolt",
            accuracy_decimals=2,
            device_class=DEVICE_CLASS_ENERGY,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_PWM): sensor.sensor_schema(
            unit_of_measurement="%",
            icon="mdi:gauge",
            accuracy_decimals=2,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Optional(CONF_VOLTAGE_1): _candidate_sensor_schema(
            unit_of_measurement="V",
            icon="mdi:current-ac",
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_VOLTAGE_2): _candidate_sensor_schema(
            unit_of_measurement="V",
            icon="mdi:current-ac",
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_VOLTAGE_3): _candidate_sensor_schema(
            unit_of_measurement="V",
            icon="mdi:current-ac",
            accuracy_decimals=1,
            device_class=DEVICE_CLASS_VOLTAGE,
            state_class=STATE_CLASS_MEASUREMENT,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
        cv.Optional(CONF_GRID_FREQUENCY): _candidate_sensor_schema(
            unit_of_measurement="Hz",
            icon="mdi:sine-wave",
            accuracy_decimals=2,
            entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        ),
    }
)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(MyEnergiEthernetComponent),
        cv.Required(CONF_ETHERNET_ID): cv.use_id(ethernet.EthernetComponent),
        cv.Optional(CONF_GRID_POWER): sensor.sensor_schema(
            unit_of_measurement="W",
            icon="mdi:flash",
            accuracy_decimals=0,
            device_class=DEVICE_CLASS_POWER,
            state_class=STATE_CLASS_MEASUREMENT,
        ),
        cv.Required(CONF_DEVICES): cv.ensure_list(device_schema),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)

    ethernet_var = await cg.get_variable(config[CONF_ETHERNET_ID])
    cg.add(var.set_ethernet_component(ethernet_var))

    if CONF_GRID_POWER in config:
        grid_power = await sensor.new_sensor(config[CONF_GRID_POWER])
        cg.add(var.set_grid_power_sensor(grid_power))

    for device in config[CONF_DEVICES]:
        serial = device[CONF_SERIAL]
        device_name = device.get(CONF_NAME) or f"Zappi {serial}"
        for key, setter in (
            (CONF_CHARGING_POWER, "set_charging_power_sensor"),
            (CONF_SESSION_ENERGY, "set_session_energy_sensor"),
            (CONF_PWM, "set_pwm_sensor"),
            (CONF_VOLTAGE_1, "set_voltage_candidate_1_sensor"),
            (CONF_VOLTAGE_2, "set_voltage_candidate_2_sensor"),
            (CONF_VOLTAGE_3, "set_voltage_candidate_3_sensor"),
            (CONF_GRID_FREQUENCY, "set_grid_frequency_candidate_sensor"),
        ):
            if key in device:
                sensor_config = dict(device[key])
                sensor_config.setdefault(
                    CONF_NAME, f"{device_name} {SENSOR_NAME_SUFFIXES[key]}"
                )
                entity = await sensor.new_sensor(sensor_config)
                cg.add(getattr(var, setter)(serial, entity))
