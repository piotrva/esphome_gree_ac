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

/* Define packets from AC that would be processed by software
   0x31 - Unit Report */
const std::vector<uint8_t> allowedPackets = {0x31};

class SinclairACCNT : public SinclairAC {
    public:
    void control(const climate::ClimateCall &call) override;

    void on_horizontal_swing_change(const std::string &swing) override;
    void on_vertical_swing_change(const std::string &swing) override;

    void setup() override;
    void loop() override;

    protected:
    ACState state_ = ACState::Initializing;  // Stores the internal state of the AC, used during initialization

    std::vector<uint8_t> data = std::vector<uint8_t>(10);  // Stores the data received from the AC
    void handle_poll();

    void set_data(bool set);

    void send_command(std::vector<uint8_t> command, CommandType type, uint8_t header);
    void send_packet(const std::vector<uint8_t> &command, CommandType type);

    bool verify_packet();
    void handle_packet();

    climate::ClimateMode determine_mode(uint8_t mode);
    std::string determine_fan_speed(uint8_t speed);

    std::string determine_vertical_swing(uint8_t swing);
    std::string determine_horizontal_swing(uint8_t swing);
};

}  // namespace CNT
}  // namespace sinclair_ac
}  // namespace esphome
