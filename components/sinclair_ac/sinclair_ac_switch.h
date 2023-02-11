// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#pragma once

#include "esphome/components/switch/switch.h"
#include "esphome/core/component.h"

namespace esphome {
namespace sinclair_ac {

class SinclairACSwitch : public switch_::Switch, public Component {
    protected:
        void write_state(bool state) override { this->publish_state(state); }
};

}  // namespace sinclair_ac
}  // namespace esphome
