// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#include "esphome/components/climate/climate.h"
#include "esphome/components/climate/climate_mode.h"
#include "esppac.h"

namespace esphome {
namespace sinclair_ac {
namespace CNT {

static const int POLL_INTERVAL = 5000;  // The interval at which to poll the AC

enum class ACState {
    Initializing,  // Before first query response is receive
    Ready,   // All done, ready to receive regular packets
};

namespace protocol {
    /* packet types */
    static const uint8_t CMD_IN_UNIT_REPORT  = 0x31;
    static const uint8_t CMD_OUT_PARAMS_SET  = 0x01;
    static const uint8_t CMD_OUT_SYNC_TIME   = 0x03;
    static const uint8_t CMD_OUT_MAC_REPORT  = 0x04; /* 7e 7e 0d 04 04 00 00 00 AA BB CC DD EE FF 00 -> AA BB CC DD EE FF = MAC address */
    static const uint8_t CMD_OUT_UNKNOWN_1   = 0x02; /* 7e 7e 10 02 00 00 00 00 00 00 01 00 28 1e 19 23 23 00 b8 */
    static const uint8_t CMD_IN_UNKNOWN_1    = 0x44; /* 7e 7e 1a 44 01 00 01 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 01 */
    static const uint8_t CMD_IN_UNKNOWN_2    = 0x33; /* 7e 7e 2f 33 00 00 40 00 09 20 19 0a 00 10 00 14 17 5b 08 08 00 00 00 00 00 00 00 00 01 00 00 0d 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 */

    /* byte indexes are AFTER we remove first 4 bytes from the packet (sync, length, type) as well as a checksum */
    /* unit report packet data fields, for binary values there is no need to define bit offset/position */
    static const uint8_t REPORT_PWR_BYTE     = 4;
    static const uint8_t REPORT_PWR_MASK     = 0b1000_0000;

    static const uint8_t REPORT_MODE_BYTE    = 4;
    static const uint8_t REPORT_MODE_MASK    = 0b0111_0000;
    static const uint8_t REPORT_MODE_POS     = 4;
    static const uint8_t REPORT_MODE_AUTO      = 0;
    static const uint8_t REPORT_MODE_COOL      = 1;
    static const uint8_t REPORT_MODE_DRY       = 2;
    static const uint8_t REPORT_MODE_FAN       = 3;
    static const uint8_t REPORT_MODE_HEAT      = 4;
}

/* Define packets from AC that would be processed by software */
const std::vector<uint8_t> allowedPackets = {protocol::CMD_IN_UNIT_REPORT};

class SinclairACCNT : public SinclairAC {
    public:
    void control(const climate::ClimateCall &call) override;

    void on_horizontal_swing_change(const std::string &swing) override;
    void on_vertical_swing_change(const std::string &swing) override;

    void setup() override;
    void loop() override;

    protected:
    ACState state_ = ACState::Initializing;  // Stores the internal state of the AC, used during initialization

    //std::vector<uint8_t> data = std::vector<uint8_t>(255);  // Stores the data received from the AC
    void handle_poll();

    void processUnitReport();

    void send_command(std::vector<uint8_t> command, CommandType type, uint8_t header);
    void send_packet(const std::vector<uint8_t> &command, CommandType type);

    bool verify_packet();
    void handle_packet();

    climate::ClimateMode determine_mode();
    std::string determine_fan_speed(uint8_t speed);

    std::string determine_vertical_swing(uint8_t swing);
    std::string determine_horizontal_swing(uint8_t swing);
};

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
