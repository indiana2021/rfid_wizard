# rfid_wizard
nfc master tooling


# DrJones NFC Master


  <br/>
  <strong>A powerful, portable NFC/RFID analysis tool built on the ESP32-S3.</strong>
</p>

---

 NFC Master is an open-source firmware for the ESP32-S3 that turns a handful of common components into a versatile tool for NFC/RFID security research and data management. It features a simple, menu-driven interface on an OLED display, allowing for standalone operation in the field.

## Features

-   **Read Card Info**: Quickly identify ISO14443A cards and display their UID and other identifying information (ATQA/SAK).
-   **Dump MIFARE Classic Cards**: Perform a full 1K memory dump of MIFARE Classic cards. The entire card memory (1024 bytes) is saved to a `.mfd` file on the SD card, named after the card's UID for easy identification.
-   **Dynamic Key Loading**: Don't be limited to a few default keys! The device automatically loads a list of custom MIFARE keys from a `keys.txt` file on your SD card at boot, allowing it to authenticate with a much wider variety of cards.
-   **Multi-Key Authentication**: Automatically attempts to authenticate card sectors using all loaded keys from the `keys.txt` file (or a set of common default keys if the file is not found).
-   **Read/Write/Erase Blocks**: Directly read, write, or erase data on specific blocks of a MIFARE Classic card.
-   **NDEF Detection**: Basic detection for NDEF (NFC Data Exchange Format) formatted tags.
-   **SD Card Management**: An integrated file browser lets you view and delete files on the SD card.
-   **Standalone & Portable**: The compact hardware and OLED interface make it a perfect tool for on-the-go analysis.
-   **User-Friendly Interface**: A simple three-button navigation system (UP, DOWN, SELECT) and a clear OLED display make the tool easy to use.
-   **Safety First**: All destructive actions (Write, Erase, Delete File) require user confirmation to prevent accidental data loss.

## Hardware Requirements

To build your own DrJones NFC Master, you will need the following components:

1.  **Microcontroller**: ESP32-S3 Super Mini (or a compatible ESP32-S3 board).
2.  **NFC Reader**: PN532 NFC/RFID Module (must support I2C).
3.  **Display**: SSD1306 0.96" I2C OLED Display (128x64 resolution).
4.  **Storage**: Micro SD Card Module (SPI).
5.  **SD Card**: A Micro SD card, formatted as FAT32.
6.  **Input**: 3x Tactile Push Buttons.
7.  **Misc**: Breadboard and jumper wires.

## Wiring & Pinout

Connect the components to your ESP32-S3 board according to the table below. All pins are within the GPIO 1-13 range as requested.

| Component          | Component Pin | ESP32-S3 Pin |
| :----------------- | :------------ | :----------- |
| **I2C Devices** |               |              |
| OLED & PN532       | `SDA`         | `GPIO 8`     |
| OLED & PN532       | `SCL`         | `GPIO 9`     |
| **SD Card Module** |               |              |
|                    | `CS` / `SS`   | `GPIO 10`    |
|                    | `SCK` / `CLK` | `GPIO 4`     |
|                    | `MOSI` / `DI` | `GPIO 5`     |
|                    | `MISO` / `DO` | `GPIO 6`     |
| **Buttons** |               |              |
| UP Button          | One Leg       | `GPIO 3`     |
| DOWN Button        | One Leg       | `GPIO 1`     |
| SELECT Button      | One Leg       | `GPIO 2`     |
| **Power** |               |              |
| All Modules        | `VCC`         | `3V3`        |
| All Modules & Btns | `GND`         | `GND`        |

*Note: The second leg of each button should be connected to GND.*

## Setup & Installation

Follow these steps to compile and upload the firmware using the Arduino IDE.

#### 1. Install Arduino IDE
Download and install the latest version of the [Arduino IDE](https://www.arduino.cc/en/software).

#### 2. Configure ESP32 Board Support
-   Open the Arduino IDE and go to **File > Preferences**.
-   In the "Additional Board Manager URLs" field, enter the following URL:
    ```
    [https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json](https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json)
    ```
-   Click **OK**.
-   Go to **Tools > Board > Boards Manager...**.
-   Search for "esp32" and install the package by **Espressif Systems**.

#### 3. Select the Correct Board
-   Go to **Tools > Board > ESP32 Arduino** and select **ESP32S3 Dev Module**.
-   Connect your ESP32-S3 board to your computer via USB.
-   Go to **Tools > Port** and select the correct COM port for your device.

#### 4. Install Required Libraries
This project requires several libraries. Install them using the Arduino Library Manager.
-   Go to **Sketch > Include Library > Manage Libraries...**.
-   Search for and install each of the following libraries:
    -   `Adafruit PN532`
    -   `Adafruit GFX Library`
    -   `Adafruit SSD1306`
    -   `U8g2_for_Adafruit_GFX`

#### 5. Upload the Firmware
-   Open the `DrJones_NFC_Master.ino` file in the Arduino IDE.
-   Click the **Upload** button (the right-arrow icon).
-   If the upload fails, you may need to hold down the **"BOOT"** button on your ESP32-S3 board as the IDE tries to connect, then release it once the upload begins.

## How to Use

#### Preparing the SD Card
1.  Format your Micro SD card as **FAT32**.
2.  (Optional) Create a file named `keys.txt` in the root directory of the SD card.
3.  Add one MIFARE key per line in this file, with bytes separated by spaces. Example:
    ```
    FF FF FF FF FF FF
    A0 A1 A2 A3 A4 A5
    B0 B1 B2 B3 B4 B5
    00 00 00 00 00 00
    ```
    If `keys.txt` is not found, a list of common default keys will be used.

#### Device Operation
-   **Navigation**: Use the UP and DOWN buttons to scroll through menus and the SELECT button to choose an option.
-   **Read Card**: Displays the UID of a detected card.
-   **Dump Card**: Reads all 64 blocks of a MIFARE Classic 1K card and saves the data to a `.mfd` file on the SD card.
-   **Read/Write/Erase**: These options perform operations on Block 4 of a card by default. The firmware can be modified to target other blocks.
-   **SD Card Menu**: Allows you to browse, delete, or write data from files on the SD card.

## Troubleshooting

-   **`I2C transaction unexpected nack detected`**: This is a hardware error.
    -   Run an I2C scanner sketch to verify your devices are detected at the correct addresses (`0x3C` for the OLED, `0x24` for the PN532).
    -   Carefully check all wiring for the I2C bus (SDA, SCL) and power (3.3V, GND).
-   **`SD Error: Init failed!`**:
    -   Verify the SPI wiring (CS, SCK, MOSI, MISO) is correct.
    -   Ensure your SD card is formatted as FAT32 and is inserted correctly.
-   **`PN532 not found!`**:
    -   Check the I2C and power wiring for the PN532 module.
-   **Compilation Errors**:
    -   Ensure all required libraries listed above are installed.
    -   Make sure you have selected "ESP32S3 Dev Module" as your board in the Arduino IDE.

## License
This project is open source and available under the [MIT License](LICENSE.md).
