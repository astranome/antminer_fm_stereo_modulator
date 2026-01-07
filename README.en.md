# Antminer S9 FM Stereo Modulator

**Stereo FM modulator based on Antminer S9 / Astra S9 FPGA board, without external DAC.** Direct Digital Synthesis (DDS) for experimental FM broadcasting.

**Russian version available:** [README in Russian](README.md)

[![Status: In Development](https://img.shields.io/badge/Status-In%20Development-orange)]()
[![Platform](https://img.shields.io/badge/Platform-Zynq%20(Antminer%20S9)-lightgrey)]()

## ⚠️ Important Warning
This is an **experimental project** for enthusiasts. The device may interfere with other radio stations and services. Use with caution.

## 📋 Features
*   **Full digital path:** FM signal generation directly in Zynq FPGA
*   **High sound quality:** Direct Digital Synthesis (DDS) ensures clean sound with excellent detail and attack, comparable to or better than many commercial FM transmitters
*   **Stereo sound:** MPX signal generation with pilot tone
*   **RDS ready:** RDS modulator already implemented in FPGA, needs software part
*   **Multiple audio sources:**
    *   Internet radio (via VLC, MPC, gstreamer, madplay, etc.)
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

### Board Preparation

#### 1. Setting Jumpers for SD Card Boot
![Jumpers for SD Boot](images/jumpers_sd_boot.png)

*Set the jumpers in this configuration for booting from SD card*

#### 2. Antenna Solder Point
![Antenna Solder Point](images/antenna_solder_point.png)

*Antenna solder point on the board - TXD9 contact on the back side*

---

## 🚀 Quick Start

1.  **SD Card Image:** Download and write the image to a flash drive:
    [📥 **antminer_fm_sd_image.img.zip**](https://drive.google.com/file/d/1Pwia-9_7UBmRfj1oRfHILwK6f-1vbIpU/view?usp=sharing)

2.  **Jumper Settings:** Set the jumpers on the board to the SD card boot position as shown in the photo above.

3.  **Antenna:** Solder a ~78 cm wire to the **TXD9** contact on the **back side** of the board (see solder point photo).

4.  **Network:** Connect the board via Ethernet to a network with DHCP and internet access.

5.  **Reception:** Tune your FM receiver to **96.0 MHz**. The carrier will appear shortly after power-on.

6.  **Ready:** After system boot, a script will automatically start and internet radio broadcast will begin.

> ℹ️ **Note:** To disable automatic internet radio startup, comment out the line `/root/broadcast_internet_radio.sh` in `/etc/rc.local`.

---

## 🛠 Usage

*   **Local file:** Play test audio file:
    ```bash
    ./play_song.sh
    ```
*   **Internet radio:**
    ```bash
    ./ep.sh
    ```
    Stop broadcasting: `killall vlc`.
*   **Axia LiveWire (AES67) playback:**
    ```bash
    ./rx_livewire_aes67.sh [channel_number]
    ```
    Example for channel 51: `./rx_livewire_aes67.sh 51`.

---

## ❓ FAQ (Frequently Asked Questions)

### What is the actual sound quality?
**Pure stereo sound without analog distortion.** The only drawback - there is sometimes a chance to hear slight digital noise in silence, but in most cases it's unnoticeable.

### What is the actual range?
**In urban conditions:** 100-200 meters  
**In open areas with line of sight:** up to 300-400 meters  
Range also depends on receiver sensitivity, interference presence, and antennas used.

### How to transmit sound from a computer?
Install the Axia LiveWire virtual sound card, and your computer will become a professional audio source for the transmitter.

### How to connect via SSH?
Connect via SSH to host **tx.local** (login **root**, no password).

### Can the frequency be changed?
Currently not, but this feature may appear in future versions (if they are developed).

### How to increase power?
This implementation doesn't support power increase, as using an amplifier would amplify noise along with the useful signal.

### Are boards from other miners compatible?
Yes, if they have Zynq 70xx and similar architecture.

### What audio level provides full deviation of +/-75kHz?
Full deviation of +/-75 kHz, including RDS and pilot tone deviation, corresponds to audio level of -9dBFS.

---

## 🤝 Contributing

The project is under active development. Many planned features are not yet implemented or need refinement, but you can already enjoy high-quality stereo sound.

**Priority tasks:**
*   **RDS Encoder:** Implementation of software part for the already existing hardware modulator in FPGA
*   **Management Interface:** Console and web interface for frequency setting, pre-emphasis control, modulation parameter monitoring, etc.
*   **Multichannel capability:** With proper optimization, it will be possible to broadcast at least 2-4 radio stations simultaneously.
*   **Documentation:** Improving instructions and technical descriptions

If you have experience with FPGA, SDR, embedded Linux, or web interface development — join the project!

---

**License:** GNU GPL v3.0