# Bluetooth light sensor

The project is a battery powered BLE light sensor that I am going to use to sense light levels to have automatic control over my lights. It's developed using a nRF52 DK, with a VEML7700 light sensor.

I want to eventually print it out to a PCB running the nRF52832 SoC for size and power efficencies. 

## Hardware
- nRF52 DK
- VEML7700 sensor
- CR2032 battery

## Features
- BLE advertising only (non-connectable, non-scannable)
- BTHome v2-compliant packets
- Integrates with Home Assistant via passive Bluetooth