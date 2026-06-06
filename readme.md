ISC-4012-B – LLCC68 LoRa Transmitter
Description

This project is a LoRa transmit test application developed exclusively for the custom board ISC_4012-B using the SX1262 radio and Zephyr RTOS (SDK v3.2.2).

The application initializes the LoRa and periodically transmits a LoRa packet while printing transmission status logs.

SDK & Board

Zephyr / NCS SDK: v3.2.2

Target Board: EVK_ISC_4012_B

This firmware is only valid for ISC_4012-B.

Project Structure (Relevant)
Lora_Transmit/
├── boards/EVK_ISC_4012_B/
├── dts/
├── lib/ # LLCC68 driver
├── src/main.c
├── prj.conf
└── CMakeLists.txt

Build

From the project root:

west build -b EVK_ISC_4012_B

Flash
west flash

Functionality

Initializes LoRa Module

Transmits a LoRa packet:

"transmitting LoRa Packet"

Transmission interval: ms

Logs TX status and timing

Notes

LLCC68 SPI and GPIOs are defined in custom DTS files

Tested only with SDK v3.2.2 & 3.2.4
