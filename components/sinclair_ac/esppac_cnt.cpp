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
        /* mark that we have recieved a response */
        this->wait_response_ = false;
        /* log for ESPHome debug */
        log_packet(this->serialProcess_.data);

        if (!verify_packet())  /* Verify length, header, counter and checksum */
        {
            return;
        }

        this->last_packet_received_ = millis();  /* Set the time at which we received our last packet */

        /* A valid recieved packet of accepted type marks module as being ready */
        if (this->state_ != ACState::Ready)
        {
            this->state_ = ACState::Ready;  
            Component::status_clear_error();
            this->last_packet_sent_ = millis();
        }

        if (this->update_ == ACUpdate::NoUpdate)
        {
            handle_packet(); /* this will update state of components in HA as well as internal settings */
        }
    }

    /* we will send a packet to the AC as a reponse to indicate changes */
    send_packet();

    /* if there are no packets for 5 seconds - mark module as not ready */
    if (millis() - this->last_packet_received_ >= protocol::TIME_TIMEOUT_INACTIVE_MS)
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
        ESP_LOGV(TAG, "Requested target teperature change");
        this->update_ = ACUpdate::UpdateStart;
        this->target_temperature = *call.get_target_temperature();
        if (this->target_temperature < MIN_TEMPERATURE)
        {
            this->target_temperature = MIN_TEMPERATURE;
        }
        else if (this->target_temperature > MAX_TEMPERATURE)
        {
            this->target_temperature = MAX_TEMPERATURE;
        }
    }

    if (call.get_custom_fan_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested fan mode change");
        this->update_ = ACUpdate::UpdateStart;
        this->custom_fan_mode = *call.get_custom_fan_mode();
    }

    if (call.get_swing_mode().has_value())
    {
        ESP_LOGV(TAG, "Requested swing mode change");
        this->update_ = ACUpdate::UpdateStart;
        switch (*call.get_swing_mode()) {
            case climate::CLIMATE_SWING_BOTH:
                this->vertical_swing_state_   =   vertical_swing_options::FULL;
                this->horizontal_swing_state_ = horizontal_swing_options::FULL;
                break;
            case climate::CLIMATE_SWING_OFF:
                /* both center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
            case climate::CLIMATE_SWING_VERTICAL:
                /* vertical full, horizontal center */
                this->vertical_swing_state_   =   vertical_swing_options::FULL;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
            case climate::CLIMATE_SWING_HORIZONTAL:
                /* horizontal full, vertical center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::FULL;
                break;
            default:
                ESP_LOGV(TAG, "Unsupported swing mode requested");
                /* both center */
                this->vertical_swing_state_   =   vertical_swing_options::CMID;
                this->horizontal_swing_state_ = horizontal_swing_options::CMID;
                break;
        }
    }
}

/*
 * Send a raw packet, as is
 */
void SinclairACCNT::send_packet()
{
    std::vector<uint8_t> packet(protocol::SET_PACKET_LEN, 0);  /* Initialize packet contents */

    if (this->wait_response_ == true && (millis() - this->last_packet_sent_) < protocol::TIME_REFRESH_PERIOD_MS)
    {
        /* do net send packet too often or when we are waiting for report to come */
        return;
    }
    
    packet[protocol::SET_CONST_02_BYTE] = protocol::SET_CONST_02_VAL; /* Some always 0x02 byte... */
    packet[protocol::SET_CONST_BIT_BYTE] = protocol::SET_CONST_BIT_MASK; /* Some always true bit */

    /* Prepare the rest of the frame */
    /* this handles tricky part of 0xAF value and flag marking that WiFi does not apply any changes */
    switch(this->update_)
    {
        default:
        case ACUpdate::NoUpdate:
            packet[protocol::SET_NOCHANGE_BYTE] |= protocol::SET_NOCHANGE_MASK;
            break;
        case ACUpdate::UpdateStart:
            packet[protocol::SET_AF_BYTE] = protocol::SET_AF_VAL;
            break;
        case ACUpdate::UpdateClear:
            break;
    }

    /* MODE and POWER --------------------------------------------------------------------------- */
    uint8_t mode = protocol::REPORT_MODE_AUTO;
    bool power = false;
    switch (this->mode)
    {
        case climate::CLIMATE_MODE_AUTO:
            mode = protocol::REPORT_MODE_AUTO;
            power = true;
            break;
        case climate::CLIMATE_MODE_COOL:
            mode = protocol::REPORT_MODE_COOL;
            power = true;
            break;
        case climate::CLIMATE_MODE_DRY:
            mode = protocol::REPORT_MODE_DRY;
            power = true;
            break;
        case climate::CLIMATE_MODE_FAN_ONLY:
            mode = protocol::REPORT_MODE_FAN;
            power = true;
            break;
        case climate::CLIMATE_MODE_HEAT:
            mode = protocol::REPORT_MODE_HEAT;
            power = true;
            break;
        default:
        case climate::CLIMATE_MODE_OFF:
            /* In case of MODE_OFF we will not alter the last mode setting recieved from AC, see determine_mode() */
            switch (this->mode_internal_)
            {
                case climate::CLIMATE_MODE_AUTO:
                    mode = protocol::REPORT_MODE_AUTO;
                    break;
                case climate::CLIMATE_MODE_COOL:
                    mode = protocol::REPORT_MODE_COOL;
                    break;
                case climate::CLIMATE_MODE_DRY:
                    mode = protocol::REPORT_MODE_DRY;
                    break;
                case climate::CLIMATE_MODE_FAN_ONLY:
                    mode = protocol::REPORT_MODE_FAN;
                    break;
                case climate::CLIMATE_MODE_HEAT:
                    mode = protocol::REPORT_MODE_HEAT;
                    break;
            }
            power = false;
            break;
    }

    packet[protocol::REPORT_MODE_BYTE] |= (mode << protocol::REPORT_MODE_POS);
    if (power)
    {
        packet[protocol::REPORT_PWR_BYTE] |= protocol::REPORT_PWR_MASK;
    }

    /* TARGET TEMPERATURE --------------------------------------------------------------------------- */
    uint8_t target_temperature = ((((uint8_t)this->target_temperature) - protocol::REPORT_TEMP_SET_OFF) << protocol::REPORT_TEMP_SET_POS);
    packet[protocol::REPORT_TEMP_SET_BYTE] |= (target_temperature & protocol::REPORT_TEMP_SET_MASK);

    /* FAN SPEED --------------------------------------------------------------------------- */
    /* below will default to AUTO */
    uint8_t fanSpeed1 = 0;
    uint8_t fanSpeed2 = 0;
    bool    fanQuiet  = false;
    bool    fanTurbo  = false;

    if (this->custom_fan_mode == fan_modes::FAN_AUTO)
    {
        fanSpeed1 = 0;
        fanSpeed2 = 0;
        fanQuiet  = false;
        fanTurbo  = false;
    }
    else if (this->custom_fan_mode == fan_modes::FAN_LOW)
    {
        fanSpeed1 = 1;
        fanSpeed2 = 1;
        fanQuiet  = false;
        fanTurbo  = false;
    }
    else if (this->custom_fan_mode == fan_modes::FAN_QUIET)
    {
        fanSpeed1 = 1;
        fanSpeed2 = 1;
        fanQuiet  = true;
        fanTurbo  = false;
    }
    else if (this->custom_fan_mode == fan_modes::FAN_MEDL)
    {
        fanSpeed1 = 2;
        fanSpeed2 = 2;
        fanQuiet  = false;
        fanTurbo  = false;
    }
    else if (this->custom_fan_mode == fan_modes::FAN_MED)
    {
        fanSpeed1 = 3;
        fanSpeed2 = 2;
        fanQuiet  = false;
        fanTurbo  = false;
    }
    else if (this->custom_fan_mode == fan_modes::FAN_MEDH)
    {
        fanSpeed1 = 4;
        fanSpeed2 = 3;
        fanQuiet  = false;
        fanTurbo  = false;
    }
    else if (this->custom_fan_mode == fan_modes::FAN_HIGH)
    {
        fanSpeed1 = 5;
        fanSpeed2 = 3;
        fanQuiet  = false;
        fanTurbo  = false;
    }
    else if (this->custom_fan_mode == fan_modes::FAN_TURBO)
    {
        fanSpeed1 = 5;
        fanSpeed2 = 3;
        fanQuiet  = false;
        fanTurbo  = true;
    }
    else
    {
        fanSpeed1 = 0;
        fanSpeed2 = 0;
        fanQuiet  = false;
        fanTurbo  = false;
    }

    packet[protocol::REPORT_FAN_SPD1_BYTE] |= (fanSpeed1 << protocol::REPORT_FAN_SPD1_POS);
    packet[protocol::REPORT_FAN_SPD2_BYTE] |= (fanSpeed2 << protocol::REPORT_FAN_SPD2_POS);
    if (fanTurbo)
    {
        packet[protocol::REPORT_FAN_TURBO_BYTE] |= protocol::REPORT_FAN_TURBO_MASK;
    }
    if (fanQuiet)
    {
        packet[protocol::REPORT_FAN_QUIET_BYTE] |= protocol::REPORT_FAN_QUIET_MASK;
    }

    /* VERTICAL SWING --------------------------------------------------------------------------- */
    uint8_t mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    if (this->vertical_swing_state_ == vertical_swing_options::OFF)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::FULL)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_FULL;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::DOWN)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_DOWN;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDD)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MIDD;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MID)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MID;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::MIDU)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_MIDU;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::UP)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_UP;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CDOWN)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CDOWN;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDD)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDD;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMID)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMID;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CMIDU)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CMIDU;
    }
    else if (this->vertical_swing_state_ == vertical_swing_options::CUP)
    {
        mode_vertical_swing = protocol::REPORT_VSWING_CUP;
    }
    else
    {
        mode_vertical_swing = protocol::REPORT_VSWING_OFF;
    }
    packet[protocol::REPORT_VSWING_BYTE] |= (mode_vertical_swing << protocol::REPORT_VSWING_POS);

    /* HORIZONTAL SWING --------------------------------------------------------------------------- */
    uint8_t mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    if (this->horizontal_swing_state_ == horizontal_swing_options::OFF)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::FULL)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_FULL;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CLEFT)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CLEFT;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDL)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDL;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMID)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMID;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CMIDR)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CMIDR;
    }
    else if (this->horizontal_swing_state_ == horizontal_swing_options::CRIGHT)
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_CRIGHT;
    }
    else
    {
        mode_horizontal_swing = protocol::REPORT_HSWING_OFF;
    }
    packet[protocol::REPORT_HSWING_BYTE] |= (mode_horizontal_swing << protocol::REPORT_HSWING_POS);

    /* DISPLAY --------------------------------------------------------------------------- */
    uint8_t display_mode = protocol::REPORT_DISP_MODE_AUTO;
    if (this->display_state_ == display_options::AUTO)
    {
        display_mode = protocol::REPORT_DISP_MODE_AUTO;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::SET)
    {
        display_mode = protocol::REPORT_DISP_MODE_SET;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::ACT)
    {
        display_mode = protocol::REPORT_DISP_MODE_ACT;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::OUT)
    {
        display_mode = protocol::REPORT_DISP_MODE_OUT;
        this->display_power_internal_ = true;
    }
    else if (this->display_state_ == display_options::OFF)
    {
        /* we do not want to alter display setting - only turn it off */
        this->display_power_internal_ = false;
        if (this->display_mode_internal_ == display_options::AUTO)
        {
            display_mode = protocol::REPORT_DISP_MODE_AUTO;
        }
        else if (this->display_mode_internal_ == display_options::SET)
        {
            display_mode = protocol::REPORT_DISP_MODE_SET;
        }
        else if (this->display_mode_internal_ == display_options::ACT)
        {
            display_mode = protocol::REPORT_DISP_MODE_ACT;
        }
        else if (this->display_mode_internal_ == display_options::OUT)
        {
            display_mode = protocol::REPORT_DISP_MODE_OUT;
        }
        else
        {
            display_mode = protocol::REPORT_DISP_MODE_AUTO;
        }
    }
    else
    {
        display_mode = protocol::REPORT_DISP_MODE_AUTO;
        this->display_power_internal_ = true;
    }

    packet[protocol::REPORT_DISP_MODE_BYTE] |= (display_mode << protocol::REPORT_DISP_MODE_POS);

    if (this->display_power_internal_)
    {
        packet[protocol::REPORT_DISP_ON_BYTE] |= protocol::REPORT_DISP_ON_MASK;
    }

    /* DISPLAY UNIT --------------------------------------------------------------------------- */
    if (this->display_unit_state_ == display_unit_options::DEGF)
    {
        packet[protocol::REPORT_DISP_F_BYTE] |= protocol::REPORT_DISP_F_MASK;
    }

    /* PLASMA --------------------------------------------------------------------------- */
    if (this->plasma_state_)
    {
        packet[protocol::REPORT_PLASMA1_BYTE] |= protocol::REPORT_PLASMA1_MASK;
        packet[protocol::REPORT_PLASMA2_BYTE] |= protocol::REPORT_PLASMA2_MASK;
    }

    /* SLEEP --------------------------------------------------------------------------- */
    if (this->sleep_state_)
    {
        packet[protocol::REPORT_SLEEP_BYTE] |= protocol::REPORT_SLEEP_MASK;
    }

    /* XFAN --------------------------------------------------------------------------- */
    if (this->xfan_state_)
    {
        packet[protocol::REPORT_XFAN_BYTE] |= protocol::REPORT_XFAN_MASK;
    }

    /* SAVE --------------------------------------------------------------------------- */
    if (this->save_state_)
    {
        packet[protocol::REPORT_SAVE_BYTE] |= protocol::REPORT_SAVE_MASK;
    }
    
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
    this->wait_response_ = true;
    write_array(packet);                 /* Sent the packet by UART */
    log_packet(packet, true);            /* Log uart for debug purposes */

    /* update setting state-machine */
    switch(this->update_)
    {
        case ACUpdate::NoUpdate:
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
bool SinclairACCNT::processUnitReport()
{
    bool hasChanged = false;

    climate::ClimateMode newMode = determine_mode();
    if (this->mode != newMode) hasChanged = true;
    this->mode = newMode;

    std::string newFanMode = determine_fan_mode();
    if (this->custom_fan_mode != newFanMode) hasChanged = true;
    this->custom_fan_mode = newFanMode;
    
    float newTargetTemperature = (float)(((this->serialProcess_.data[protocol::REPORT_TEMP_SET_BYTE] & protocol::REPORT_TEMP_SET_MASK) >> protocol::REPORT_TEMP_SET_POS)
        + protocol::REPORT_TEMP_SET_OFF);
    if (this->target_temperature != newTargetTemperature) hasChanged = true;
    this->update_target_temperature(newTargetTemperature);
    
    /* if there is no external sensor mapped to represent current temperature we will get data from AC unit */
    if (this->current_temperature_sensor_ == nullptr)
    {
        float newCurrentTemperature = (float)(((this->serialProcess_.data[protocol::REPORT_TEMP_ACT_BYTE] & protocol::REPORT_TEMP_ACT_MASK) >> protocol::REPORT_TEMP_ACT_POS)
            - protocol::REPORT_TEMP_ACT_OFF) / protocol::REPORT_TEMP_ACT_DIV;
        if (this->current_temperature != newCurrentTemperature) hasChanged = true;
        this->update_current_temperature(newCurrentTemperature);
    }

    std::string verticalSwing = determine_vertical_swing();
    std::string horizontalSwing = determine_horizontal_swing();

    this->update_swing_vertical(verticalSwing);
    this->update_swing_horizontal(horizontalSwing);

    climate::ClimateSwingMode newSwingMode;
    /* update legacy swing mode to somehow represent actual state and support
       this setting without detailed settings done with additional switches */
    if (verticalSwing == vertical_swing_options::FULL && horizontalSwing == horizontal_swing_options::FULL)
        newSwingMode = climate::CLIMATE_SWING_BOTH;
    else if (verticalSwing == vertical_swing_options::FULL)
        newSwingMode = climate::CLIMATE_SWING_VERTICAL;
    else if (horizontalSwing == horizontal_swing_options::FULL)
        newSwingMode = climate::CLIMATE_SWING_HORIZONTAL;
    else
        newSwingMode = climate::CLIMATE_SWING_OFF;
    
    if (this->swing_mode != newSwingMode) hasChanged = true;
    this->swing_mode = newSwingMode;

    this->update_display(determine_display());
    this->update_display_unit(determine_display_unit());

    this->update_plasma(determine_plasma());
    this->update_sleep(determine_sleep());
    this->update_xfan(determine_xfan());
    this->update_save(determine_save());

    return hasChanged;
}

climate::ClimateMode SinclairACCNT::determine_mode()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_MODE_BYTE] & protocol::REPORT_MODE_MASK) >> protocol::REPORT_MODE_POS;

    /* as mode presented by climate component incorporates both power and mode we will store this separately for Sinclair
       in _internal_ fields */
    /* check unit power flag */
    this->power_internal_ = (this->serialProcess_.data[protocol::REPORT_PWR_BYTE] & protocol::REPORT_PWR_MASK) != 0;

    /* check unit mode */
    switch (mode)
    {
        case protocol::REPORT_MODE_AUTO:
            this->mode_internal_ = climate::CLIMATE_MODE_AUTO;
            break;
        case protocol::REPORT_MODE_COOL:
            this->mode_internal_ = climate::CLIMATE_MODE_COOL;
            break;
        case protocol::REPORT_MODE_DRY:
            this->mode_internal_ = climate::CLIMATE_MODE_DRY;
            break;
        case protocol::REPORT_MODE_FAN:
            this->mode_internal_ = climate::CLIMATE_MODE_FAN_ONLY;
            break;
        case protocol::REPORT_MODE_HEAT:
            this->mode_internal_ = climate::CLIMATE_MODE_HEAT;
            break;
        default:
            ESP_LOGW(TAG, "Received unknown climate mode");
            this->mode_internal_ = climate::CLIMATE_MODE_OFF;
            break;
    }

    /* if unit is powered on - return the mode, otherwise return CLIMATE_MODE_OFF */
    if (this->power_internal_)
    {
        return this->mode_internal_;
    }
    else
    {
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
            return vertical_swing_options::OFF;;
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
            return horizontal_swing_options::OFF;
    }
}

std::string SinclairACCNT::determine_display()
{
    uint8_t mode = (this->serialProcess_.data[protocol::REPORT_DISP_MODE_BYTE] & protocol::REPORT_DISP_MODE_MASK) >> protocol::REPORT_DISP_MODE_POS;

    this->display_power_internal_ = (this->serialProcess_.data[protocol::REPORT_DISP_ON_BYTE] & protocol::REPORT_DISP_ON_MASK);

    switch (mode) {
        case protocol::REPORT_DISP_MODE_AUTO:
            this->display_mode_internal_ = display_options::AUTO;
            break;
        case protocol::REPORT_DISP_MODE_SET:
            this->display_mode_internal_ = display_options::SET;
            break;
        case protocol::REPORT_DISP_MODE_ACT:
            this->display_mode_internal_ = display_options::ACT;
            break;
        case protocol::REPORT_DISP_MODE_OUT:
            this->display_mode_internal_ = display_options::OUT;
            break;
        default:
            ESP_LOGW(TAG, "Received unknown display mode");
            this->display_mode_internal_ = display_options::AUTO;
            break;
    }

    if (this->display_power_internal_)
    {
        return this->display_mode_internal_;
    }
    else
    {
        return display_options::OFF;
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

    this->update_ = ACUpdate::UpdateStart;
    this->vertical_swing_state_ = swing;
}

void SinclairACCNT::on_horizontal_swing_change(const std::string &swing)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting horizontal swing position");

    this->update_ = ACUpdate::UpdateStart;
    this->horizontal_swing_state_ = swing;
}

void SinclairACCNT::on_display_change(const std::string &display)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting display mode");

    this->update_ = ACUpdate::UpdateStart;
    this->display_state_ = display;
}

void SinclairACCNT::on_display_unit_change(const std::string &display_unit)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting display unit");

    this->update_ = ACUpdate::UpdateStart;
    this->display_unit_state_ = display_unit;
}

void SinclairACCNT::on_plasma_change(bool plasma)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting plasma");

    this->update_ = ACUpdate::UpdateStart;
    this->plasma_state_ = plasma;
}

void SinclairACCNT::on_sleep_change(bool sleep)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting sleep");

    this->update_ = ACUpdate::UpdateStart;
    this->sleep_state_ = sleep;
}

void SinclairACCNT::on_xfan_change(bool xfan)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting xfan");

    this->update_ = ACUpdate::UpdateStart;
    this->xfan_state_ = xfan;
}

void SinclairACCNT::on_save_change(bool save)
{
    if (this->state_ != ACState::Ready)
        return;

    ESP_LOGD(TAG, "Setting save");

    this->update_ = ACUpdate::UpdateStart;
    this->save_state_ = save;
}

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
