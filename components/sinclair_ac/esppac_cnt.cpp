// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esppac_cnt.h"

namespace esphome {
namespace sinclair_ac {
namespace CNT {

static const char *const TAG = "sinclair_ac.serial";

void SinclairACCNT::setup()
{
    SinclairAC::setup();

    ESP_LOGD(TAG, "Using serial protocol for Sinclair AC");
}

void SinclairACCNT::loop()
{
    /* this reads data from UART */
    SinclairAC::loop();

    /* we have a frame from AC */
    if (this->serialProcess_.state == STATE_COMPLETE)
    {
        /* do not forget to order for restart of the recieve state machine */
        this->serialProcess_.state = STATE_RESTART;
        log_packet(this->serialProcess_.data);

        if (!verify_packet())  // Verify length, header, counter and checksum
            return;

        this->waiting_for_response_ = false;
        this->last_packet_received_ = millis();  /* Set the time at which we received our last packet */

        /* A valid recieved packet of accepted type marks module as being ready */
        if (this->state_ != ACState::Ready)
            this->state_ = ACState::Ready;  

        handle_packet();
    }

    /* if there are no packets for 5 seconds - mark module as not ready */
    if (millis() - this->last_packet_received_ > 5000UL)
    {
        if (this->state_ != ACState::Initializing)
            this->state_ = ACState::Initializing; 
    }

    handle_poll();  // Handle sending poll packets
}

/*
 * ESPHome control request
 */

void SinclairACCNT::control(const climate::ClimateCall &call) {
    if (this->state_ != ACState::Ready)
        return;

    if (call.get_mode().has_value()) {
        ESP_LOGV(TAG, "Requested mode change");

        // switch (*call.get_mode()) {
        //     case climate::CLIMATE_MODE_COOL:
        //         this->data[0] = 0x34;
        //         break;
        //     case climate::CLIMATE_MODE_HEAT:
        //         this->data[0] = 0x44;
        //         break;
        //     case climate::CLIMATE_MODE_DRY:
        //         this->data[0] = 0x24;
        //         break;
        //     case climate::CLIMATE_MODE_HEAT_COOL:
        //         this->data[0] = 0x04;
        //         break;
        //     case climate::CLIMATE_MODE_FAN_ONLY:
        //         this->data[0] = 0x64;
        //         break;
        //     case climate::CLIMATE_MODE_OFF:
        //         this->data[0] = this->data[0] & 0xF0;  // Strip right nib to turn AC off
        //         break;
        //     default:
        //         ESP_LOGV(TAG, "Unsupported mode requested");
        //         break;
        // }
    }

    if (call.get_target_temperature().has_value()) {
        //this->data[1] = *call.get_target_temperature() / TEMPERATURE_STEP;
    }

    if (call.get_custom_fan_mode().has_value()) {
        ESP_LOGV(TAG, "Requested fan mode change");

        if(this->custom_preset != "Normal")
        {
            ESP_LOGV(TAG, "Resetting preset");
            //this->data[5] = (this->data[5] & 0xF0);  // Clear right nib for normal mode
        }

        std::string fanMode = *call.get_custom_fan_mode();

        // if (fanMode == "Automatic")
        //     this->data[3] = 0xA0;
        // else if (fanMode == "1")
        //     this->data[3] = 0x30;
        // else if (fanMode == "2")
        //     this->data[3] = 0x40;
        // else if (fanMode == "3")
        //     this->data[3] = 0x50;
        // else if (fanMode == "4")
        //     this->data[3] = 0x60;
        // else if (fanMode == "5")
        //     this->data[3] = 0x70;
        // else
        //     ESP_LOGV(TAG, "Unsupported fan mode requested");
    }

    if (call.get_swing_mode().has_value()) {
        ESP_LOGV(TAG, "Requested swing mode change");

        // switch (*call.get_swing_mode()) {
        //     case climate::CLIMATE_SWING_BOTH:
        //         this->data[4] = 0xFD;
        //         break;
        //     case climate::CLIMATE_SWING_OFF:
        //         this->data[4] = 0x36;  // Reset both to center
        //         break;
        //     case climate::CLIMATE_SWING_VERTICAL:
        //         this->data[4] = 0xF6;  // Swing vertical, horizontal center
        //         break;
        //     case climate::CLIMATE_SWING_HORIZONTAL:
        //         this->data[4] = 0x3D;  // Swing horizontal, vertical center
        //         break;
        //     default:
        //         ESP_LOGV(TAG, "Unsupported swing mode requested");
        //         break;
        // }
    }

    //send_command(this->data, CommandType::Normal, CTRL_HEADER);
}

/*
 * Send a command, attaching header, packet length and checksum
 */
// void SinclairACCNT::send_command(std::vector<uint8_t> command, CommandType type, uint8_t header = CNT::CTRL_HEADER) {
//     uint8_t length = command.size();
//     command.insert(command.begin(), header);
//     command.insert(command.begin() + 1, length);

//     uint8_t checksum = 0;

//     for (uint8_t i : command)
//         checksum -= i;  // Add to checksum

//     command.push_back(checksum);

//     send_packet(command, type);  // Actually send the constructed packet
// }

/*
 * Send a raw packet, as is
 */
void SinclairACCNT::send_packet(const std::vector<uint8_t> &packet, CommandType type) {
    this->last_packet_sent_ = millis();  // Save the time when we sent the last packet

    if (type != CommandType::Response)   // Don't wait for a response for responses
        this->waiting_for_response_ = true;  // Mark that we are waiting for a response

    write_array(packet);       // Write to UART
    log_packet(packet, true);  // Write to log
}

/*
 * Loop handling
 */

void SinclairACCNT::handle_poll() {
    // if (millis() - this->last_packet_sent_ > POLL_INTERVAL) {
    //     ESP_LOGV(TAG, "Polling AC");
    //     //send_command(CMD_POLL, CommandType::Normal, POLL_HEADER);
    // }
}

/*
 * Packet handling
 */

bool SinclairACCNT::verify_packet()
{
    /* At least 2 sync bytes + length + type + checksum */
    if (this->serialProcess_.data.size() < 5)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (length)");
        return false;
    }

    /* The header (aka sync bytes) was checked by SinclairAC::read_data() */

    /* The frame len was assumed by SinclairAC::read_data() */

    /* Check if this packet type sould be processed */
    bool commandAllowed = false;
    for (uint8_t packet : allowedPackets)
    {
        if (this->serialProcess_.data[3] == packet)
        {
            commandAllowed = true;
            break;
        }
    }
    if (!commandAllowed)
    {
        ESP_LOGW(TAG, "Dropping invalid packet (command [%02X] not allowed)", this->serialProcess_.data[3]);
        return false;
    }

    /* Check checksum - sum of al bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 2 ; i < this->serialProcess_.data.size() - 1 ; i++)
    {
        checksum += this->serialProcess_.data[i];
    }
    if (checksum != this->serialProcess_.data[this->serialProcess_.data.size()-1])
    {
        ESP_LOGD(TAG, "Dropping invalid packet (checksum)");
        return false;
    }

    return true;
}

void SinclairACCNT::handle_packet()
{
    if (this->serialProcess_.data[3] == protocol::CMD_IN_UNIT_REPORT)
    {
        /* here we will remove unnecessary elements - header and checksum */
        this->serialProcess_.data.erase(0, 3); /* remove header */
        this->serialProcess_.data.pop_back();  /* remove checksum */
        /* now process the data */
        this->processUnitReport();
        this->publish_state();
    } 
    else 
    {
        ESP_LOGD(TAG, "Received unknown packet");
    }
}

/*
 * This decodes frame recieved from AC Unit
 */
void SinclairACCNT::processUnitReport()
{
    this->mode = determine_mode();
    // this->custom_fan_mode = determine_fan_speed(this->data[3]);

    // std::string verticalSwing = determine_vertical_swing(this->data[4]);
    // std::string horizontalSwing = determine_horizontal_swing(this->data[4]);
    
    //this->update_target_temperature((int8_t) this->data[1]);

  // Also set current and outside temperature
  // 128 means not supported
        // if (this->current_temperature_sensor_ == nullptr) {
        //     if(this->rx_buffer_[18] != 0x80)
        //         this->update_current_temperature((int8_t)this->rx_buffer_[18]);
        //     else if(this->rx_buffer_[21] != 0x80)
        //         this->update_current_temperature((int8_t)this->rx_buffer_[21]);
        //     else
        //         ESP_LOGV(TAG, "Current temperature is not supported");
        // }

    // if (verticalSwing == "auto" && horizontalSwing == "auto")
    //     this->swing_mode = climate::CLIMATE_SWING_BOTH;
    // else if (verticalSwing == "auto")
    //     this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
    // else if (horizontalSwing == "auto")
    //     this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
    // else
    //     this->swing_mode = climate::CLIMATE_SWING_OFF;

    // this->update_swing_vertical(verticalSwing);
    // this->update_swing_horizontal(horizontalSwing);
}

climate::ClimateMode SinclairACCNT::determine_mode()
{
    /* check unit power flag - if unit is off - no need to process mode */
    if (!(this->serialProcess_.data[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK))
    {
        climate::CLIMATE_MODE_OFF;
    }

    switch ((this->serialProcess_.data[protocol::REPORT_MODE_BYTE] & protocol::REPORT_MODE_MASK) >> protocol::REPORT_MODE_POS)
    {
        case protocol::REPORT_MODE_AUTO:
            return climate::CLIMATE_MODE_AUTO;
        case protocol::REPORT_MODE_COOL:
            return climate::CLIMATE_MODE_COOL;
        case protocol::REPORT_MODE_DRY:
            return climate::CLIMATE_MODE_DRY;
        case protocol::REPORT_MODE_FAN:
            return climate::CLIMATE_MODE_FAN_ONLY;
        case protocol::REPORT_MODE_HEAT:
            return climate::CLIMATE_MODE_HEAT;
        default:
            ESP_LOGW(TAG, "Received unknown climate mode");
            return climate::CLIMATE_MODE_OFF;
    }
}

std::string SinclairACCNT::determine_fan_speed(uint8_t speed) {
    switch (speed) {
        case 0xA0:  // Auto
            return "Automatic";
        case 0x30:  // 1
            return "1";
        case 0x40:  // 2
            return "2";
        case 0x50:  // 3
            return "3";
        case 0x60:  // 4
            return "4";
        case 0x70:  // 5
            return "5";
        default:
            ESP_LOGW(TAG, "Received unknown fan speed");
            return "Unknown";
    }
}

std::string SinclairACCNT::determine_vertical_swing(uint8_t swing) {
    uint8_t nib = (swing >> 4) & 0x0F;  // Left nib for vertical swing

    switch (nib) {
        case 0x0E:
            return "swing";
        case 0x0F:
            return "auto";
        case 0x01:
            return "up";
        case 0x02:
            return "up_center";
        case 0x03:
            return "center";
        case 0x04:
            return "down_center";
        case 0x05:
            return "down";
        case 0x00:
            return "unsupported";
        default:
            ESP_LOGW(TAG, "Received unknown vertical swing mode: 0x%02X", nib);
            return "Unknown";
    }
}

std::string SinclairACCNT::determine_horizontal_swing(uint8_t swing) {
    uint8_t nib = (swing >> 0) & 0x0F;  // Right nib for horizontal swing

    switch (nib) {
        case 0x0D:
            return "auto";
        case 0x09:
            return "left";
        case 0x0A:
            return "left_center";
        case 0x06:
            return "center";
        case 0x0B:
            return "right_center";
        case 0x0C:
            return "right";
        case 0x00:
            return "unsupported";
        default:
            ESP_LOGW(TAG, "Received unknown horizontal swing mode");
            return "Unknown";
    }
}

/*
 * Sensor handling
 */

void SinclairACCNT::on_vertical_swing_change(const std::string &swing) {
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting vertical swing position");

    // if (swing == "down")
    //     this->data[4] = (this->data[4] & 0x0F) + 0x50;
    // else if (swing == "down_center")
    //     this->data[4] = (this->data[4] & 0x0F) + 0x40;
    // else if (swing == "center")
    //     this->data[4] = (this->data[4] & 0x0F) + 0x30;
    // else if (swing == "up_center")
    //     this->data[4] = (this->data[4] & 0x0F) + 0x20;
    // else if (swing == "up")
    //     this->data[4] = (this->data[4] & 0x0F) + 0x10;
    // else if (swing == "swing")
    //     this->data[4] = (this->data[4] & 0x0F) + 0xE0;
    // else if (swing == "auto")
    //     this->data[4] = (this->data[4] & 0x0F) + 0xF0;
    // else {
    //     ESP_LOGW(TAG, "Unsupported vertical swing position received");
    //     return;
    // }

    //send_command(this->data, CommandType::Normal, CTRL_HEADER);
}

void SinclairACCNT::on_horizontal_swing_change(const std::string &swing) {
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting horizontal swing position");

    // if (swing == "left")
    //     this->data[4] = (this->data[4] & 0xF0) + 0x09;
    // else if (swing == "left_center")
    //     this->data[4] = (this->data[4] & 0xF0) + 0x0A;
    // else if (swing == "center")
    //     this->data[4] = (this->data[4] & 0xF0) + 0x06;
    // else if (swing == "right_center")
    //     this->data[4] = (this->data[4] & 0xF0) + 0x0B;
    // else if (swing == "right")
    //     this->data[4] = (this->data[4] & 0xF0) + 0x0C;
    // else if (swing == "auto")
    //     this->data[4] = (this->data[4] & 0xF0) + 0x0D;
    // else {
    //     ESP_LOGW(TAG, "Unsupported horizontal swing position received");
    //     return;
    // }

    //send_command(this->data, CommandType::Normal, CTRL_HEADER);
}

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
