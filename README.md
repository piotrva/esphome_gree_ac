# ESPHOME component to support Gree/Sinclair AC units
This repository adds support for ESP32-based WiFi modules to interface with Gree/Sinclair AC units.

**USE AT YOUR OWN RISK!**

Work is still in progress!

Tested with Sinclair AC (MV-H09BIF), mostly works, sometimes need to send parameter change twice - need to investigate.

Communication protocol is based on my own reverse-engineering.

ESPHome interface/binding based on:
* https://github.com/DomiStyle/esphome-panasonic-ac

**NOTES**
* It was reported [#1](https://github.com/piotrva/esphome_gree_ac/issues/1) that with some changes the code works with Lennox li024ci AC

**TODO**
* Support Timers - maybe unnecessray as timers can be managed by Home Assistant
* Support Time sync - maybe unnecessray as timers can be managed by Home Assistant
