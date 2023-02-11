# ESPHOME component to support Gree/Sinclair AC units
This repository will add support for ESP32-based WiFi modules to interface with Gree/Sinclair AC units.

**USE AT YOUR OWN RISK!**

Work is still in progress!

Communication protocol is based on my own reverse-engineering.

ESPHome interface/binding based on:
* https://github.com/GrKoR/esphome_aux_ac_component
* https://github.com/DomiStyle/esphome-panasonic-ac

**TODO**
* Support SENDING commands
* Support display control: OFF, AUTO, SET, CURRENT, OUT (unsupported by my unit)
* Support temperatre display units: C, F
* Support PLASMA (ionization mode)
* Support XFAN
* Support Save/8deg Heat
* Support Sleep
* Support Timers
* Support Time sync
