# LoWkie_Talkie

**LoRa Based Audio Transceiver** built on Zephyr RTOS.

## Overview
LoWkie_Talkie is a half-duplex digital walkie-talkie project that transmits compressed audio over long distances using LoRa (Long Range) radio. It leverages the Zephyr RTOS for multi-threading, hardware abstraction, and timing control. The project uses the Speex audio codec (integer API) to heavily compress audio to fit within LoRa packet limits, and it features a custom jitter buffer to ensure smooth playback on the receiving end.

## Features
- **Half-Duplex Communication**: Push-to-Talk (PTT) operation with automatic RX/TX switching.
- **LoRa Transceiver**: Custom driver implementation for Semtech SX126x / LLCC68 modules over SPI.
- **Jitter Buffer**: Handles packet loss, out-of-order packets, and latency variations for smooth audio reconstruction.
- **Hardware Agnostic**: Built on Zephyr RTOS, making it easy to port to various boards via devicetree overlays.
- **I2S Audio Pipeline**: Support for I2S digital microphones and DACs.

## Hardware Requirements
- **Microcontroller**: Nordic nRF54L15 DK or nRF52840 DK (or any Zephyr-supported board with sufficient RAM/Flash).
- **LoRa Module**: Semtech SX1262 / LLCC68.
- **Audio Interface**: I2S Digital Microphone (e.g., INMP441) and I2S DAC/Speaker.
- **Miscellaneous**: A push button for PTT functionality.

## Software Dependencies
- [Zephyr RTOS](https://zephyrproject.org/) framework
- [Speex DSP library](https://www.speex.org/) (Integer API)
- Nordic nRF Connect SDK (if building for Nordic dev boards)

## Project Structure
- `src/main.c`: Core application logic, thread definitions, audio pipeline, and transceiver state machine.
- `src/packet.h`: Definition of the LoRa packet structure (`lowkie_packet_t`).
- `src/audio_data.h`: Stored PCM test audio data for debugging without a live microphone.
- `lib/llcc68_driver.h` & `.c`: Low-level SPI driver for the Semtech LLCC68/SX1262 LoRa module.
- `boards/`: Zephyr devicetree overlay files for hardware-specific pin mappings (SPI, I2S, GPIOs).
- `prj.conf`: Zephyr configuration file enabling necessary subsystems (SPI, GPIO, I2S, Logging, IPC, IPC queues).
- `CMakeLists.txt`: Build script for the Zephyr environment.

## Architecture & Threading
The application uses Zephyr's threading capabilities to manage the audio and RF pipelines asynchronously:
- **`tx_audio_id`**: Captures PCM data from the I2S microphone (or uses stored test audio).
- **`tx_codec_id`**: Compresses the PCM data into Speex frames and queues them for transmission.
- **`lora_trans_id`**: Half-duplex master thread. Manages SPI communication with the LoRa module to either transmit queued packets or listen for incoming ones based on the PTT state.
- **`rx_codec_id`**: Manages the jitter buffer, handles missing/delayed packets, and decodes incoming Speex payloads back to PCM.
- **`rx_audio_id`**: (Highest Priority) Feeds decoded PCM audio out to the I2S DAC to prevent stuttering.
