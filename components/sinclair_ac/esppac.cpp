// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esppac.h"

#include "esphome/core/log.h"

namespace esphome {
namespace sinclair_ac {

static const char *const TAG = "sinclair_ac";

namespace fan_modes{
    const std::string FAN_AUTO  = "0 - Auto";
    const std::string FAN_QUIET = "1 - Quiet";
    const std::string FAN_LOW   = "2 - Low";
    const std::string FAN_MEDL  = "3 - Medium-Low";
    const std::string FAN_MED   = "4 - Medium";
    const std::string FAN_MEDH  = "5 - Medium-High";
    const std::string FAN_HIGH  = "6 - High";
    const std::string FAN_TURBO = "7 - Turbo";
}

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

    traits.add_supported_custom_fan_mode(fan_modes::FAN_AUTO);
    traits.add_supported_custom_fan_mode(fan_modes::FAN_QUIET);
    traits.add_supported_custom_fan_mode(fan_modes::FAN_LOW);
    traits.add_supported_custom_fan_mode(fan_modes::FAN_MEDL);
    traits.add_supported_custom_fan_mode(fan_modes::FAN_MED);
    traits.add_supported_custom_fan_mode(fan_modes::FAN_MEDH);
    traits.add_supported_custom_fan_mode(fan_modes::FAN_HIGH);
    traits.add_supported_custom_fan_mode(fan_modes::FAN_TURBO);

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
        /* If we had a packet or a packet had not been decoded yet - do not recieve more data */
        if (this->serialProcess_.state == STATE_COMPLETE)
        {
            break;
        }
        uint8_t c;
        this->read_byte(&c);  // Store in receive buffer

        if (this->serialProcess_.state == STATE_RESTART)
        {
            this->serialProcess_.data.clear();
            this->serialProcess_.state = STATE_WAIT_SYNC;
        }
        
        this->serialProcess_.data.push_back(c);
        if (this->serialProcess_.data.size() >= DATA_MAX)
        {
            this->serialProcess_.data.clear();
            continue;
        }
        switch (this->serialProcess_.state)
        {
            case STATE_WAIT_SYNC:
                /* Frame begins with 0x7E 0x7E LEN CMD
                   LEN - frame length in bytes
                   CMD - command
                 */
                if (c != 0x7E && 
                    this->serialProcess_.data.size() > 2 && 
                    this->serialProcess_.data[this->serialProcess_.data.size()-2] == 0x7E && 
                    this->serialProcess_.data[this->serialProcess_.data.size()-3] == 0x7E)
                {
                    this->serialProcess_.data.clear();

                    this->serialProcess_.data.push_back(0x7E);
                    this->serialProcess_.data.push_back(0x7E);
                    this->serialProcess_.data.push_back(c);

                    this->serialProcess_.frame_size = c;
                    this->serialProcess_.state = STATE_RECIEVE;
                }
                break;
            case STATE_RECIEVE:
                this->serialProcess_.frame_size--;
                if (this->serialProcess_.frame_size == 0)
                {
                    /* WE HAVE A FRAME FROM AC */
                    this->serialProcess_.state = STATE_COMPLETE;
                }
                break;
            case STATE_RESTART:
            case STATE_COMPLETE:
                break;
            default:
                this->serialProcess_.state = STATE_WAIT_SYNC;
                this->serialProcess_.data.clear();
                break;
        }

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
        this->horizontal_swing_select_->state != this->horizontal_swing_state_) \
    {
        this->horizontal_swing_select_->publish_state(this->horizontal_swing_state_);  // Set current horizontal swing position
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
