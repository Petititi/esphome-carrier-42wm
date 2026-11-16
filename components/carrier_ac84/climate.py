import esphome.codegen as cg
from esphome.components import climate_ir, time
import esphome.config_validation as cv
from esphome.const import CONF_TIME_ID

AUTO_LOAD = ["climate_ir"]

carrier_ac84_ns = cg.esphome_ns.namespace("carrier_ac84")
CarrierAc84Climate = carrier_ac84_ns.class_("CarrierAc84Climate", climate_ir.ClimateIR)

CONFIG_SCHEMA = climate_ir.climate_ir_with_receiver_schema(CarrierAc84Climate).extend(
    {
        # Clock copied into every frame, as the remote does.
        cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
    }
)


async def to_code(config):
    var = await climate_ir.new_climate_ir(config)
    if CONF_TIME_ID in config:
        clock = await cg.get_variable(config[CONF_TIME_ID])
        cg.add(var.set_time(clock))
