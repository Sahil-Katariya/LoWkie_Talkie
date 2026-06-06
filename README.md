# ISC-4012-B – LLCC68 LoRa Receiver

## Description

This project is a LoRa receive test application developed exclusively for the custom board **ISC_4012-B** using the **SX1262/LLCC68 radio** and **Zephyr RTOS (SDK v3.2.2)**.

The application initializes the LoRa module, continuously listens for incoming LoRa packets, and prints the received packet data along with reception status logs.

---

## SDK & Board

- **Zephyr / NCS SDK:** v3.2.2
- **Target Board:** EVK_ISC_4012_B

This firmware is only valid for **ISC_4012-B**.

---

## Project Structure (Relevant)

```text
Lora_Receive/
├── boards/EVK_ISC_4012_B/
├── dts/
├── lib/                 # LLCC68 driver
├── src/main.c
├── prj.conf
└── CMakeLists.txt
```
