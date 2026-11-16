#include "carrier_ac84_climate.h"
#include "esphome/core/log.h"

#include <algorithm>
#include <cmath>

namespace esphome::carrier_ac84 {

static const char *const TAG = "carrier_ac84";

void CarrierAc84Climate::dump_config() {
  climate_ir::ClimateIR::dump_config();
  ESP_LOGCONFIG(TAG, "  Protocol: CARRIER_AC84 (wired, no carrier)");
}

void CarrierAc84Climate::transmit_state() {
  uint8_t *s = this->state_;
  if (!this->state_init_) {
    std::copy(carrier84::DEFAULT_STATE, carrier84::DEFAULT_STATE + carrier84::STATE_LEN, s);
    this->state_init_ = true;
  }

  // When off, the previous mode stays in the frame (as the remote does).
  switch (this->mode) {
    case climate::CLIMATE_MODE_HEAT_COOL:
      carrier84::set_mode(s, carrier84::MODE_AUTO);
      break;
    case climate::CLIMATE_MODE_FAN_ONLY:
      carrier84::set_mode(s, carrier84::MODE_FAN);
      break;
    case climate::CLIMATE_MODE_HEAT:
      carrier84::set_mode(s, carrier84::MODE_HEAT);
      break;
    case climate::CLIMATE_MODE_COOL:
      carrier84::set_mode(s, carrier84::MODE_COOL);
      break;
    default:
      break;
  }
  carrier84::set_power(s, this->mode != climate::CLIMATE_MODE_OFF);

  // Setpoint
  float t = std::isnan(this->target_temperature) ? 20.0f : this->target_temperature;
  t = std::min(std::max(t, TEMP_MIN), TEMP_MAX);
  carrier84::set_temp(s, (int) lroundf(t));

  // Fan speed
  uint8_t fan = 0;
  switch (this->fan_mode.value_or(climate::CLIMATE_FAN_AUTO)) {
    case climate::CLIMATE_FAN_LOW:
      fan = 1;
      break;
    case climate::CLIMATE_FAN_MEDIUM:
      fan = 2;
      break;
    case climate::CLIMATE_FAN_HIGH:
      fan = 3;
      break;
    default:
      fan = 0;
      break;
  }
  carrier84::set_fan(s, fan);

  // The appliance timer is never used: Home Assistant automations replace it.
  carrier84::set_swing(s, this->swing_mode == climate::CLIMATE_SWING_VERTICAL);
  s[2] &= (uint8_t) ~carrier84::FLAG_TIMER;

  // Constant bits: byte 2 bits 0-1 = 1 (the frame is rejected if bit 1 is 0),
  // byte 3 high nibble = 3.
  s[2] |= 0x03;
  s[3] = (uint8_t) ((s[3] & 0x0F) | 0x30);

  if (this->clock_ != nullptr) {
    auto now = this->clock_->now();
    if (now.is_valid())
      carrier84::set_clock(s, now.hour, now.minute);
  }
  carrier84::fix_checksum(s);

  ESP_LOGI(TAG, "Sending: %s", carrier84::to_hex(s).c_str());
  auto transmit = this->transmitter_->transmit();
  auto *data = transmit.get_data();
  data->set_carrier_frequency(0);  // wired: no 38 kHz carrier
  for (int32_t d : carrier84::encode(s)) {
    if (d > 0)
      data->mark(d);
    else
      data->space(-d);
  }
  transmit.perform();
}

bool CarrierAc84Climate::on_receive(remote_base::RemoteReceiveData data) {
  uint8_t s[carrier84::STATE_LEN];
  if (!carrier84::decode(data.get_raw_data(), s))
    return false;
  if (!carrier84::checksum_ok(s)) {
    ESP_LOGW(TAG, "Frame received, bad checksum: %s", carrier84::to_hex(s).c_str());
    return false;
  }
  ESP_LOGD(TAG, "Received: %s", carrier84::to_hex(s).c_str());
  std::copy(s, s + carrier84::STATE_LEN, this->state_);
  this->state_init_ = true;

  if (!carrier84::get_power(s)) {
    this->mode = climate::CLIMATE_MODE_OFF;
  } else {
    switch (carrier84::get_mode(s)) {
      case carrier84::MODE_AUTO:
        this->mode = climate::CLIMATE_MODE_HEAT_COOL;
        break;
      case carrier84::MODE_FAN:
        this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        break;
      case carrier84::MODE_HEAT:
        this->mode = climate::CLIMATE_MODE_HEAT;
        break;
      case carrier84::MODE_COOL:
        this->mode = climate::CLIMATE_MODE_COOL;
        break;
      default:
        ESP_LOGW(TAG, "Unknown mode %X: mode left unchanged", carrier84::get_mode(s));
        break;
    }
  }

  const int t = carrier84::get_temp(s);
  if (t >= (int) TEMP_MIN && t <= (int) TEMP_MAX)
    this->target_temperature = t;

  switch (carrier84::get_fan(s)) {
    case 0:
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
      break;
    case 1:
      this->fan_mode = climate::CLIMATE_FAN_LOW;
      break;
    case 2:
      this->fan_mode = climate::CLIMATE_FAN_MEDIUM;
      break;
    case 3:
      this->fan_mode = climate::CLIMATE_FAN_HIGH;
      break;
    default:
      break;
  }
  this->swing_mode = carrier84::get_swing(s) ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;

  this->publish_state();
  return true;
}

}  // namespace esphome::carrier_ac84
