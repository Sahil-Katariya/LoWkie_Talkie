# ISC-4012-B – LLCC68 LoRa Transmitter

## Description

This project is a LoRa transmit test application developed exclusively for the custom board **ISC_4012-B** using the **SX1262/LLCC68 radio** and **Zephyr RTOS (SDK v3.2.2)**.

The application initializes the LoRa module, periodically transmits LoRa packets, and prints transmission status logs for verification and debugging purposes.

---

## SDK & Board

* **Zephyr / NCS SDK:** v3.2.2
* **Target Board:** EVK_ISC_4012_B

This firmware is only valid for **ISC_4012-B**.

---

## Project Structure (Relevant)

```text
Lora_Transmit/
├── boards/EVK_ISC_4012_B/
├── dts/
├── lib/                 # LLCC68 driver
├── src/main.c
├── prj.conf
└── CMakeLists.txt
```

---

## Functionality

* Initializes the LoRa module
* Periodically transmits a LoRa packet:

```text
transmitting LoRa Packet
```

* Configurable transmission interval
* Logs transmission status and timing information

---

## Notes

* LLCC68 SPI and GPIO configurations are defined in the custom DTS files.
* Tested and verified with **NCS SDK v3.2.2** and **v3.2.4**.
* Intended for transmitter-side LoRa communication testing on the **ISC_4012-B** platform.
