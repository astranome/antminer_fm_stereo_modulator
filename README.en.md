# Antminer S9 FM Stereo Modulator

**Stereo FM modulator based on Antminer S9 / Astra S9 FPGA board, without external DAC.** Direct Digital Synthesis (DDS) for experimental FM broadcasting.

[![Status: In Development](https://img.shields.io/badge/Status-In%20Development-orange)]()
[![Platform](https://img.shields.io/badge/Platform-Zynq%20(Antminer%20S9)-lightgrey)]()

## ⚠️ Important Warning
This is an **experimental project** for enthusiasts. The device may cause interference to other radio stations and services. Use with caution.

## 📋 Features
*   **Full digital path:** FM signal generation directly in Zynq FPGA
*   **High sound quality:** Direct Digital Synthesis (DDS) ensures clean sound with excellent detail and attack, comparable to or better than many commercial FM transmitters
*   **Stereo sound:** MPX signal generation with pilot tone
*   **RDS ready:** RDS modulator already implemented in FPGA, needs software part
*   **Multiple audio sources:**
    *   Internet radio (via VLC)
    *   Local audio files
    *   Axia LiveWire (AES67) streams
*   **Low-cost solution:** Uses old mining hardware

---

## 📸 Demonstration

### Vivado Project
![Vivado Project](images/vivado_project.png)

*Vivado project schematic with DDS and MPX modulator implementation*

### SDR# Spectrum
![SDR# Spectrum](images/sdrsharp_spectrum.png)

*MPX signal spectrum in SDR#: visible L+R main channel (up to 15 kHz), 19 kHz pilot tone, L-R stereo subcarrier at 38 kHz and RDS at 57 kHz*

---

## 🚀 Quick Start

1.  **SD Card Image:** Download and write the image to a flash drive.
2.  **Antenna:** Solder a ~78 cm wire to the **TXD9** contact on the **back side** of the board.
3.  **Network:** Connect the board via Ethernet to a network with DHCP and internet access.
4.  **Reception:** Tune your FM receiver to **96.0 MHz**. The carrier will appear shortly after power-on.
5.  **Ready:** After system boot, a script will automatically start and internet radio broadcast will begin.

> ℹ️ **Note:** To disable automatic internet radio startup, comment out the line `/root/broadcast_internet_radio.sh` in `/etc/rc.local`.

---

## 🛠 Usage

*   **Test file:** If internet radio doesn't work:
    ```bash
    ./play_song.sh
    ```
*   **Axia LiveWire (for experiments):**
    ```bash
    ./rx_aes67_livewire_ch51.sh
    ```
    Channel 51 will be received.
*   **Stop broadcasting:**
    ```bash
    killall vlc
    ```

---

## 🤝 Contributing

The project is under active development. Many planned features are not yet implemented or need refinement, but you can already enjoy high-quality stereo sound.

**Priority tasks:**
*   **RDS Encoder:** Implementation of software part for the already existing hardware modulator in FPGA
*   **Management Interface:** Console and web interface for frequency setting, pre-emphasis control, modulation parameter monitoring, etc.
*   **Testing and debugging:** Help with testing on different devices
*   **Documentation:** Improving instructions and technical descriptions

If you have experience with FPGA, SDR, embedded Linux, or web interface development — join the project!

---

**License:** GNU GPL v3.0
