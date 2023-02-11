// based on: https://github.com/DomiStyle/esphome-panasonic-ac
#pragma once

#include "esphome/components/select/select.h"
#include "esphome/core/component.h"

namespace esphome {
namespace sinclair_ac {

class SinclairACSelect : public select::Select, public Component {
    protected:
        void control(const std::string &value) override { this->publish_state(value); }
};

}  // namespace sinclair_ac
}  // namespace esphome
