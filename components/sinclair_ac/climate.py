#based on: https://github.com/DomiStyle/esphome-panasonic-ac

from esphome.const import (
    CONF_ID,
)
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import uart, climate, sensor, select, switch

AUTO_LOAD = ["switch", "sensor", "select"]
DEPENDENCIES = ["uart"]

sinclair_ac_ns = cg.esphome_ns.namespace("sinclair_ac")
SinclairAC = sinclair_ac_ns.class_(
    "SinclairAC", cg.Component, uart.UARTDevice, climate.Climate
)
sinclair_ac_cnt_ns = sinclair_ac_ns.namespace("CNT")
SinclairACCNT = sinclair_ac_cnt_ns.class_("SinclairACCNT", SinclairAC)

SinclairACSwitch = sinclair_ac_ns.class_(
    "SinclairACSwitch", switch.Switch, cg.Component
)
SinclairACSelect = sinclair_ac_ns.class_(
    "SinclairACSelect", select.Select, cg.Component
)


CONF_HORIZONTAL_SWING_SELECT    = "horizontal_swing_select"
CONF_VERTICAL_SWING_SELECT      = "vertical_swing_select"
CONF_DISPLAY_SELECT             = "display_select"
CONF_DISPLAY_UNIT_SELECT        = "display_unit_select"

CONF_PLASMA_SWITCH              = "plasma_switch"
CONF_SLEEP_SWITCH               = "sleep_switch"
CONF_XFAN_SWITCH                = "xfan_switch"
CONF_SAVE_SWITCH                = "save_switch"

CONF_CURRENT_TEMPERATURE_SENSOR = "current_temperature_sensor"

HORIZONTAL_SWING_OPTIONS = [
    "0 - OFF",
    "1 - Swing - Full",
    "2 - Constant - Left",
    "3 - Constant - Mid-Left",
    "4 - Constant - Middle",
    "5 - Constant - Mid-Right",
    "6 - Constant - Right",
]


VERTICAL_SWING_OPTIONS = [
    "00 - OFF",
    "01 - Swing - Full",
    "02 - Swing - Down",
    "03 - Swing - Mid-Down",
    "04 - Swing - Middle",
    "05 - Swing - Mid-Up",
    "06 - Swing - Up",
    "07 - Constant - Down",
    "08 - Constant - Mid-Down",
    "09 - Constant - Middle",
    "10 - Constant - Mid-Up",
    "11 - Constant - Up",
]

DISPLAY_OPTIONS = [
    "0 - OFF",
    "1 - Auto",
    "2 - Set temperature",
    "3 - Actual temperature",
    "4 - Outside temperature",
]

DISPLAY_UNIT_OPTIONS = [
    "C",
    "F",
]

switch_schema = switch.switch_schema(switch.Switch).extend(cv.COMPONENT_SCHEMA).extend(
    {cv.GenerateID(): cv.declare_id(SinclairACSwitch)}
)
select_schema = select.select_schema(select.Select).extend(
    {cv.GenerateID(CONF_ID): cv.declare_id(SinclairACSelect)}
)

SCHEMA = climate.climate_schema(climate.Climate).extend(
    {
        cv.Optional(CONF_HORIZONTAL_SWING_SELECT): select_schema,
        cv.Optional(CONF_VERTICAL_SWING_SELECT): select_schema,
        cv.Optional(CONF_DISPLAY_SELECT): select_schema,
        cv.Optional(CONF_DISPLAY_UNIT_SELECT): select_schema,
        cv.Optional(CONF_PLASMA_SWITCH): switch_schema,
        cv.Optional(CONF_SLEEP_SWITCH): switch_schema,
        cv.Optional(CONF_XFAN_SWITCH): switch_schema,
        cv.Optional(CONF_SAVE_SWITCH): switch_schema,
    }
).extend(uart.UART_DEVICE_SCHEMA)

CONFIG_SCHEMA = cv.All(
    SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(SinclairACCNT),
            cv.Optional(CONF_CURRENT_TEMPERATURE_SENSOR): cv.use_id(sensor.Sensor),
        }
    ),
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await climate.register_climate(var, config)
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_HORIZONTAL_SWING_SELECT in config:
        conf = config[CONF_HORIZONTAL_SWING_SELECT]
        hswing_select = await select.new_select(conf, options=HORIZONTAL_SWING_OPTIONS)
        await cg.register_component(hswing_select, conf)
        cg.add(var.set_horizontal_swing_select(hswing_select))

    if CONF_VERTICAL_SWING_SELECT in config:
        conf = config[CONF_VERTICAL_SWING_SELECT]
        vswing_select = await select.new_select(conf, options=VERTICAL_SWING_OPTIONS)
        await cg.register_component(vswing_select, conf)
        cg.add(var.set_vertical_swing_select(vswing_select))
    
    if CONF_DISPLAY_SELECT in config:
        conf = config[CONF_DISPLAY_SELECT]
        display_select = await select.new_select(conf, options=DISPLAY_OPTIONS)
        await cg.register_component(display_select, conf)
        cg.add(var.set_display_select(display_select))
    
    if CONF_DISPLAY_UNIT_SELECT in config:
        conf = config[CONF_DISPLAY_UNIT_SELECT]
        display_unit_select = await select.new_select(conf, options=DISPLAY_UNIT_OPTIONS)
        await cg.register_component(display_unit_select, conf)
        cg.add(var.set_display_unit_select(display_unit_select))

    if CONF_CURRENT_TEMPERATURE_SENSOR in config:
        sens = await cg.get_variable(config[CONF_CURRENT_TEMPERATURE_SENSOR])
        cg.add(var.set_current_temperature_sensor(sens))
        
    for s in [CONF_PLASMA_SWITCH, CONF_SLEEP_SWITCH, CONF_XFAN_SWITCH, CONF_SAVE_SWITCH]:
        if s in config:
            conf = config[s]
            a_switch = cg.new_Pvariable(conf[CONF_ID])
            await cg.register_component(a_switch, conf)
            await switch.register_switch(a_switch, conf)
            cg.add(getattr(var, f"set_{s}")(a_switch))
