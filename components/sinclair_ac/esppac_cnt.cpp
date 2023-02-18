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

        this->last_packet_received_ = millis();  /* Set the time at which we received our last packet */

        /* A valid recieved packet of accepted type marks module as being ready */
        if (this->state_ != ACState::Ready)
        {
            this->state_ = ACState::Ready;  
            Component::status_clear_error();
        }

        switch(this->update_)
        {
            case ACUpdate::NoUpdate:
                handle_packet(); /* this will update state of components in HA as well as internal settings */
                break;
            case ACUpdate::UpdateStart:
                this->update_ = ACUpdate::UpdateClear;
                break;
            case ACUpdate::UpdateClear:
                this->update_ = ACUpdate::NoUpdate;
                break;
            default:
                this->update_ = ACUpdate::NoUpdate;
                break;
        }

        /* we will send a packet to the AC as a reponse to indicate changes */
        send_packet();
    }

    /* if there are no packets for 5 seconds - mark module as not ready */
    if (millis() - this->last_packet_received_ > 5000UL)
    {
        if (this->state_ != ACState::Initializing)
        {
            this->state_ = ACState::Initializing;
            Component::status_set_error();
        }
    }
}

/*
 * ESPHome control request
 */

void SinclairACCNT::control(const climate::ClimateCall &call)
{
    if (this->state_ != ACState::Ready)
        return;

    if (call.get_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested mode change");
        this->update_ = ACUpdate::UpdateStart;
        this->mode = *call.get_mode();
    }

    if (call.get_target_temperature().has_value())
    {
        //this->data[1] = *call.get_target_temperature() / TEMPERATURE_STEP;
    }

    if (call.get_custom_fan_mode().has_value())
    {
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

    if (call.get_swing_mode().has_value())
    {
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
}

/*
 * Send a raw packet, as is
 */
void SinclairACCNT::send_packet()
{
    std::vector<uint8_t> packet(protocol::SET_PACKET_LEN, 0);  /* Initialize packet contents */
    
    packet[protocol::SET_CONST_02_BYTE] = protocol::SET_CONST_02_VAL; /* Some always 0x02 byte... */

    /* Prepare the rest of the frame */

    /* Do the command, length */
    packet.insert(packet.begin(), protocol::CMD_OUT_PARAMS_SET);
    packet.insert(packet.begin(), protocol::SET_PACKET_LEN + 2); /* Add 2 bytes as we added a command and will add checksum */

    /* Do checksum - sum of all bytes except sync and checksum itself% 0x100 
       the module would be realized by the fact that we are using uint8_t*/
    uint8_t checksum = 0;
    for (uint8_t i = 0 ; i < packet.size() ; i++)
    {
        checksum += packet[i];
    }
    packet.push_back(checksum);

    /* Do SYNC bytes */
    packet.insert(packet.begin(), protocol::SYNC);
    packet.insert(packet.begin(), protocol::SYNC);

    this->last_packet_sent_ = millis();  /* Save the time when we sent the last packet */
    write_array(packet);                 /* Sent the packet by UART */
    log_packet(packet, true);            /* Log uart for debug purposes */
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

    /* Check checksum - sum of all bytes except sync and checksum itself% 0x100 
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
        this->serialProcess_.data.erase(this->serialProcess_.data.begin(), this->serialProcess_.data.begin() + 4); /* remove header */
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
    this->custom_fan_mode = determine_fan_mode();
    
    this->update_target_temperature(
        (float)(((this->serialProcess_.data[protocol::REPORT_TEMP_SET_BYTE] & protocol::REPORT_TEMP_SET_MASK) >> protocol::REPORT_TEMP_SET_POS)
        + protocol::REPORT_TEMP_SET_OFF));
    
    /* if there is no external sensor mapped to represent current temperature we will get data from AC unit */
    if (this->current_temperature_sensor_ == nullptr)
    {
        this->update_current_temperature(
            (float)(((this->serialProcess_.data[protocol::REPORT_TEMP_ACT_BYTE] & protocol::REPORT_TEMP_ACT_MASK) >> protocol::REPORT_TEMP_ACT_POS)
            - protocol::REPORT_TEMP_ACT_OFF) / protocol::REPORT_TEMP_ACT_DIV);
    }

    std::string verticalSwing = determine_vertical_swing();
    std::string horizontalSwing = determine_horizontal_swing();

    this->update_swing_vertical(verticalSwing);
    this->update_swing_horizontal(horizontalSwing);

    /* update legacy swing mode to somehow represent actual state and support
       this setting without detailed settings done with additional switches */
    if (verticalSwing == vertical_swing_options::FULL && horizontalSwing == horizontal_swing_options::FULL)
        this->swing_mode = climate::CLIMATE_SWING_BOTH;
    else if (verticalSwing == vertical_swing_options::FULL)
        this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
    else if (horizontalSwing == horizontal_swing_options::FULL)
        this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
    else
        this->swing_mode = climate::CLIMATE_SWING_OFF;
    
    this->update_display(determine_display());
    this->update_display_unit(determine_display_unit());

    this->update_plasma(determine_plasma());
    this->update_sleep(determine_sleep());
    this->update_xfan(determine_xfan());
    this->update_save(determine_save());
}

climate::ClimateMode SinclairACCNT::determine_mode()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_MODE_BYTE] & protocol::REPORT_MODE_MASK) >> protocol::REPORT_MODE_POS;
    /* check unit power flag - if unit is off - no need to process mode */
    if (!(this->serialProcess_.data[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK))
    {
        return climate::CLIMATE_MODE_OFF;
    }
    /* check unit mode */
    switch (mode)
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

std::string SinclairACCNT::determine_fan_mode()
{
    /* fan setting has quite complex representation in the packet, brace for it */
    uint8_t fanSpeed1 = (this->serialProcess_.data[protocol::REPORT_FAN_SPD1_BYTE]  & protocol::REPORT_FAN_SPD1_MASK) >> protocol::REPORT_FAN_SPD1_POS;
    uint8_t fanSpeed2 = (this->serialProcess_.data[protocol::REPORT_FAN_SPD2_BYTE]  & protocol::REPORT_FAN_SPD2_MASK) >> protocol::REPORT_FAN_SPD2_POS;
    bool    fanQuiet  = (this->serialProcess_.data[protocol::REPORT_FAN_QUIET_BYTE] & protocol::REPORT_FAN_QUIET_MASK) != 0;
    bool    fanTurbo  = (this->serialProcess_.data[protocol::REPORT_FAN_TURBO_BYTE] & protocol::REPORT_FAN_TURBO_MASK) != 0;
    /* we have extracted all the data, let's do the processing */
    if      (fanSpeed1 == 0 && fanSpeed2 == 0 && fanQuiet == false && fanTurbo == false)
    {
        return fan_modes::FAN_AUTO;
    }
    else if (fanSpeed1 == 1 && fanSpeed2 == 1 && fanQuiet == false && fanTurbo == false)
    {
        return fan_modes::FAN_LOW;
    }
    else if (fanSpeed1 == 1 && fanSpeed2 == 1 && fanQuiet == true  && fanTurbo == false)
    {
        return fan_modes::FAN_QUIET;
    }
    else if (fanSpeed1 == 2 && fanSpeed2 == 2 && fanQuiet == false && fanTurbo == false)
    {
        return fan_modes::FAN_MEDL;
    }
    else if (fanSpeed1 == 3 && fanSpeed2 == 2 && fanQuiet == false && fanTurbo == false)
    {
        return fan_modes::FAN_MED;
    }
    else if (fanSpeed1 == 4 && fanSpeed2 == 3 && fanQuiet == false && fanTurbo == false)
    {
        return fan_modes::FAN_MEDH;
    }
    else if (fanSpeed1 == 5 && fanSpeed2 == 3 && fanQuiet == false && fanTurbo == false)
    {
        return fan_modes::FAN_HIGH;
    }
    else if (fanSpeed1 == 5 && fanSpeed2 == 3 && fanQuiet == false && fanTurbo == true )
    {
        return fan_modes::FAN_TURBO;
    }
    else 
    {
        ESP_LOGW(TAG, "Received unknown fan mode");
        return fan_modes::FAN_AUTO;
    }
}

std::string SinclairACCNT::determine_vertical_swing()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_VSWING_BYTE]  & protocol::REPORT_VSWING_MASK) >> protocol::REPORT_VSWING_POS;

    switch (mode) {
        case protocol::REPORT_VSWING_OFF:
            return vertical_swing_options::OFF;
        case protocol::REPORT_VSWING_FULL:
            return vertical_swing_options::FULL;
        case protocol::REPORT_VSWING_DOWN:
            return vertical_swing_options::DOWN;
        case protocol::REPORT_VSWING_MIDD:
            return vertical_swing_options::MIDD;
        case protocol::REPORT_VSWING_MID:
            return vertical_swing_options::MID;
        case protocol::REPORT_VSWING_MIDU:
            return vertical_swing_options::MIDU;
        case protocol::REPORT_VSWING_UP:
            return vertical_swing_options::UP;
        case protocol::REPORT_VSWING_CDOWN:
            return vertical_swing_options::CDOWN;
        case protocol::REPORT_VSWING_CMIDD:
            return vertical_swing_options::CMIDD;
        case protocol::REPORT_VSWING_CMID:
            return vertical_swing_options::CMID;
        case protocol::REPORT_VSWING_CMIDU:
            return vertical_swing_options::CMIDU;
        case protocol::REPORT_VSWING_CUP:
            return vertical_swing_options::CUP;
        default:
            ESP_LOGW(TAG, "Received unknown vertical swing mode");
            return "Unknown";
    }
}

std::string SinclairACCNT::determine_horizontal_swing()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_HSWING_BYTE]  & protocol::REPORT_HSWING_MASK) >> protocol::REPORT_HSWING_POS;

    switch (mode) {
        case protocol::REPORT_HSWING_OFF:
            return horizontal_swing_options::OFF;
        case protocol::REPORT_HSWING_FULL:
            return horizontal_swing_options::FULL;
        case protocol::REPORT_HSWING_CLEFT:
            return horizontal_swing_options::CLEFT;
        case protocol::REPORT_HSWING_CMIDL:
            return horizontal_swing_options::CMIDL;
        case protocol::REPORT_HSWING_CMID:
            return horizontal_swing_options::CMID;
        case protocol::REPORT_HSWING_CMIDR:
            return horizontal_swing_options::CMIDR;
        case protocol::REPORT_HSWING_CRIGHT:
            return horizontal_swing_options::CRIGHT;
        default:
            ESP_LOGW(TAG, "Received unknown horizontal swing mode");
            return "Unknown";
    }
}

std::string SinclairACCNT::determine_display()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_DISP_MODE_BYTE] & protocol::REPORT_DISP_MODE_MASK) >> protocol::REPORT_DISP_MODE_POS;

    if (!(this->serialProcess_.data[protocol::REPORT_DISP_ON_BYTE] & protocol::REPORT_DISP_ON_MASK))
    {
        return display_options::OFF;
    }

    switch (mode) {
        case protocol::REPORT_DISP_MODE_AUTO:
            return display_options::AUTO;
        case protocol::REPORT_DISP_MODE_SET:
            return display_options::SET;
        case protocol::REPORT_DISP_MODE_ACT:
            return display_options::ACT;
        case protocol::REPORT_DISP_MODE_OUT:
            return display_options::OUT;
        default:
            ESP_LOGW(TAG, "Received unknown display mode");
            return "Unknown";
    }
}

std::string SinclairACCNT::determine_display_unit()
{
    if (this->serialProcess_.data[protocol::REPORT_DISP_F_BYTE] & protocol::REPORT_DISP_F_MASK)
    {
        return display_unit_options::DEGF;
    }
    else
    {
        return display_unit_options::DEGC;
    }
}

bool SinclairACCNT::determine_plasma(){
    bool plasma1 = (this->serialProcess_.data[protocol::REPORT_PLASMA1_BYTE] & protocol::REPORT_PLASMA1_MASK) != 0;
    bool plasma2 = (this->serialProcess_.data[protocol::REPORT_PLASMA2_BYTE] & protocol::REPORT_PLASMA2_MASK) != 0;
    return plasma1 || plasma2;
}

bool SinclairACCNT::determine_sleep(){
    return (this->serialProcess_.data[protocol::REPORT_SLEEP_BYTE] & protocol::REPORT_SLEEP_MASK) != 0;
}

bool SinclairACCNT::determine_xfan(){
    return (this->serialProcess_.data[protocol::REPORT_XFAN_BYTE] & protocol::REPORT_XFAN_MASK) != 0;
}

bool SinclairACCNT::determine_save(){
    return (this->serialProcess_.data[protocol::REPORT_SAVE_BYTE] & protocol::REPORT_SAVE_MASK) != 0;
}


/*
 * Sensor handling
 */

void SinclairACCNT::on_vertical_swing_change(const std::string &swing)
{
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
}

void SinclairACCNT::on_horizontal_swing_change(const std::string &swing)
{
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
}

void SinclairACCNT::on_display_change(const std::string &display)
{
    ESP_LOGD(TAG, "Setting display mode");
}

void SinclairACCNT::on_display_unit_change(const std::string &display_unit)
{
    ESP_LOGD(TAG, "Setting display unit");
}

void SinclairACCNT::on_plasma_change(bool plasma)
{
    ESP_LOGD(TAG, "Setting plasma");
}

void SinclairACCNT::on_sleep_change(bool sleep)
{
    ESP_LOGD(TAG, "Setting sleep");
}

void SinclairACCNT::on_xfan_change(bool xfan)
{
    ESP_LOGD(TAG, "Setting zfan");
}

void SinclairACCNT::on_save_change(bool save)
{
    ESP_LOGD(TAG, "Setting save");
}

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
