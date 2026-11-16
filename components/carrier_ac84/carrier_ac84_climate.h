#pragma once

#include "esphome/components/climate_ir/climate_ir.h"
#include "esphome/components/time/real_time_clock.h"
#include "carrier_ac84.h"

namespace esphome::carrier_ac84 {

// Range of the original remote.
static const float TEMP_MIN = 10.0f;
static const float TEMP_MAX = 30.0f;

class CarrierAc84Climate final : public climate_ir::ClimateIR {
 public:
  CarrierAc84Climate()
      : climate_ir::ClimateIR(TEMP_MIN, TEMP_MAX, 1.0f, false, true,
                              {climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW, climate::CLIMATE_FAN_MEDIUM,
                               climate::CLIMATE_FAN_HIGH},
                              {climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL}) {}

  void set_time(time::RealTimeClock *clock) { this->clock_ = clock; }
  void dump_config() override;

 protected:
  void transmit_state() override;
  bool on_receive(remote_base::RemoteReceiveData data) override;

  time::RealTimeClock *clock_{nullptr};
  // Last known frame: a template at first, then the last frame received.
  // Only the known fields are changed before sending.
  uint8_t state_[carrier84::STATE_LEN];
  bool state_init_{false};
};

}  // namespace esphome::carrier_ac84
