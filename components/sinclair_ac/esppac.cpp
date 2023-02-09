// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esppac.h"

#include "esphome/core/log.h"

namespace esphome {
namespace sinclair_ac {

static const char *const TAG = "sinclair_ac";

climate::ClimateTraits SinclairAC::traits() {
  auto traits = climate::ClimateTraits();

  traits.set_supports_action(false);

  traits.set_supports_current_temperature(true);
  traits.set_supports_two_point_target_temperature(false);
  traits.set_visual_min_temperature(MIN_TEMPERATURE);
  traits.set_visual_max_temperature(MAX_TEMPERATURE);
  traits.set_visual_temperature_step(TEMPERATURE_STEP);

  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_AUTO, climate::CLIMATE_MODE_COOL,
                              climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_FAN_ONLY, climate::CLIMATE_MODE_DRY});

  // traits.set_supported_custom_fan_modes({"Automatic", "1", "2", "3", "4", "5"});

  traits.add_supported_custom_fan_mode("0 - Auto");
  traits.add_supported_custom_fan_mode("1 - Quiet");
  traits.add_supported_custom_fan_mode("2 - Low");
  traits.add_supported_custom_fan_mode("3 - Med-Low");
  traits.add_supported_custom_fan_mode("4 - Medium");
  traits.add_supported_custom_fan_mode("5 - Med-High");
  traits.add_supported_custom_fan_mode("6 - High");
  traits.add_supported_custom_fan_mode("7 - Turbo");

  traits.set_supported_swing_modes({climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_BOTH,
                                    climate::CLIMATE_SWING_VERTICAL, climate::CLIMATE_SWING_HORIZONTAL});

  return traits;
}

void SinclairAC::setup() {
  // Initialize times
  this->init_time_ = millis();
  this->last_packet_sent_ = millis();

  ESP_LOGI(TAG, "Sinclair AC component v%s starting...", VERSION);
}

void SinclairAC::loop() {
  read_data();  // Read data from UART (if there is any)
}

void SinclairAC::read_data() {
  while (available())  // Read while data is available
  {
    uint8_t c;
    this->read_byte(&c);  // Store in receive buffer
    this->rx_buffer_.push_back(c);

    this->last_read_ = millis();  // Update lastRead timestamp
  }
}

void SinclairAC::update_current_temperature(int8_t temperature) {
  if (temperature > TEMPERATURE_THRESHOLD) {
    ESP_LOGW(TAG, "Received out of range inside temperature: %d", temperature);
    return;
  }

  this->current_temperature = temperature;
}

void SinclairAC::update_target_temperature(uint8_t raw_value) {
  float temperature = raw_value * TEMPERATURE_STEP;

  if (temperature > TEMPERATURE_THRESHOLD) {
    ESP_LOGW(TAG, "Received out of range target temperature %.2f", temperature);
    return;
  }

  this->target_temperature = temperature;
}

void SinclairAC::update_swing_horizontal(const std::string &swing) {
  this->horizontal_swing_state_ = swing;

  if (this->horizontal_swing_select_ != nullptr &&
      this->horizontal_swing_select_->state != this->horizontal_swing_state_) {
    this->horizontal_swing_select_->publish_state(
        this->horizontal_swing_state_);  // Set current horizontal swing position
  }
}

void SinclairAC::update_swing_vertical(const std::string &swing) {
  this->vertical_swing_state_ = swing;

  if (this->vertical_swing_select_ != nullptr && this->vertical_swing_select_->state != this->vertical_swing_state_)
    this->vertical_swing_select_->publish_state(this->vertical_swing_state_);  // Set current vertical swing position
}

climate::ClimateAction SinclairAC::determine_action() {
  if (this->mode == climate::CLIMATE_MODE_OFF) {
    return climate::CLIMATE_ACTION_OFF;
  } else if (this->mode == climate::CLIMATE_MODE_FAN_ONLY) {
    return climate::CLIMATE_ACTION_FAN;
  } else if (this->mode == climate::CLIMATE_MODE_DRY) {
    return climate::CLIMATE_ACTION_DRYING;
  } else if ((this->mode == climate::CLIMATE_MODE_COOL || this->mode == climate::CLIMATE_MODE_HEAT_COOL) &&
             this->current_temperature + TEMPERATURE_TOLERANCE >= this->target_temperature) {
    return climate::CLIMATE_ACTION_COOLING;
  } else if ((this->mode == climate::CLIMATE_MODE_HEAT || this->mode == climate::CLIMATE_MODE_HEAT_COOL) &&
             this->current_temperature - TEMPERATURE_TOLERANCE <= this->target_temperature) {
    return climate::CLIMATE_ACTION_HEATING;
  } else {
    return climate::CLIMATE_ACTION_IDLE;
  }
}

/*
 * Sensor handling
 */

void SinclairAC::set_current_temperature_sensor(sensor::Sensor *current_temperature_sensor)
{
  this->current_temperature_sensor_ = current_temperature_sensor;
  this->current_temperature_sensor_->add_on_state_callback([this](float state)
                                                           {
                                                             this->current_temperature = state;
                                                             this->publish_state();
                                                           });
}

void SinclairAC::set_vertical_swing_select(select::Select *vertical_swing_select) {
  this->vertical_swing_select_ = vertical_swing_select;
  this->vertical_swing_select_->add_on_state_callback([this](const std::string &value, size_t index) {
    if (value == this->vertical_swing_state_)
      return;
    this->on_vertical_swing_change(value);
  });
}

void SinclairAC::set_horizontal_swing_select(select::Select *horizontal_swing_select) {
  this->horizontal_swing_select_ = horizontal_swing_select;
  this->horizontal_swing_select_->add_on_state_callback([this](const std::string &value, size_t index) {
    if (value == this->horizontal_swing_state_)
      return;
    this->on_horizontal_swing_change(value);
  });
}

/*
 * Debugging
 */

void SinclairAC::log_packet(std::vector<uint8_t> data, bool outgoing) {
  if (outgoing) {
    ESP_LOGV(TAG, "TX: %s", format_hex_pretty(data).c_str());
  } else {
    ESP_LOGV(TAG, "RX: %s", format_hex_pretty(data).c_str());
  }
}

}  // namespace sinclair_ac
}  // namespace esphome
