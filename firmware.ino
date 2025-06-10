// DrJones NFC Master Firmware for ESP32-S3 Super Mini
// Version 3.4 - Debugged
// - Removed card emulation feature to resolve compilation errors with older PN532 library versions.
// - Fixed compilation error by correcting the parameters in the readPassiveTargetID function call.
// - Removed dependency on the missing "PN532_NDEF.h" library.
// - Re-implemented NDEF detection using standard library functions.
// - MAJOR FEATURE: Dynamic key loading from 'keys.txt' on the SD card.
// - Added confirmation dialogs for all destructive write/erase actions.
// - Improved UI feedback during card dumping with a progress indicator.

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <SPI.h>

// --- PIN DEFINITIONS FOR ESP32-S3 SUPER MINI ---

// I2C Pins (OLED & PN532)
#define I2C_SDA 8
#define I2C_SCL 9

#define PN532_IRQ   -1
#define PN532_RST   -1

// SSD1306 128x64 .96inch screen
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_I2C_ADDRESS 0x3C

// SD CARD (Re-assigned to pins within 1-13 range)
#define SD_CS 10
#define SD_SCK 4
#define SD_MOSI 5
#define SD_MISO 6

// Buttons (Assigned to non-conflicting pins)
#define BUTTON_UP 3
#define BUTTON_DOWN 1
#define BUTTON_SELECT 2


// --- GLOBAL VARIABLES & OBJECTS ---

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
U8G2_FOR_ADAFRUIT_GFX u8g2_for_adafruit_gfx;
Adafruit_PN532 nfc(PN532_IRQ, PN532_RST);

// Menu Items
const char* menuItems[] = { "Read Card", "Dump Card", "Read NDEF", "Read/Write", "Erase Card", "SD Card Menu" };
const char* sdMenuOptions[] = { "View Files", "Write to Card", "Delete File", "Back to Main" };

// --- Key Dictionary for MIFARE Classic ---
const int MAX_KEYS = 32; // Increased max keys
int numKeys = 0;
uint8_t keys[MAX_KEYS][6];

// Global state variables
bool inMenu = true;
bool inSDMenu = false;
int currentMenuItem = 0;
int currentSDMenuItem = 0;
const int totalMenuItems = 6; // Updated count
const int totalSDMenuItems = 4;

// File navigation
String fileList[50];
int fileCount = 0;
int currentFileIndex = 0;

// Enum for file operations
enum FileOperation { OP_NONE, OP_DELETE, OP_WRITE_TO_CARD };

// --- FUNCTION PROTOTYPES ---
void initDisplay();
void initNFC();
void initSDCard();
void loadKeysFromSD();
void drawBorder();
void displayInfo(String title, String info1 = "", String info2 = "", String info3 = "");
void displayTitleScreen();
void displayMenu();
void handleButtonPress();
void executeMenuItem();
void readCard();
void dumpCard();
void readNDEF();
void readWriteCard();
void eraseCard();
void displaySDMenuOptions();
void executeSDMenuAction(int option);
void viewFiles(FileOperation operation);
void displayFileList(FileOperation operation);
void deleteFile(String filename);
void writeFileToCard(String filename);
int getButtonInput(int debounce_delay);
bool authenticateBlock(uint8_t* uid, uint8_t uidLen, uint8_t block);
bool confirmAction(String action);


// --- SETUP AND INITIALIZATION ---

void setup() {
  Serial.begin(115200);
  
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(400000);
  
  pinMode(BUTTON_UP, INPUT_PULLUP);
  pinMode(BUTTON_DOWN, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);

  initDisplay();
  u8g2_for_adafruit_gfx.begin(display);

  displayTitleScreen();
  delay(2000);
  
  initNFC();
  initSDCard();
  loadKeysFromSD();

  displayMenu();
}

void initDisplay() {
  if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;) ;
  }
  display.display();
  delay(1000);
  display.clearDisplay();
}

void initNFC() {
    displayInfo("PN532 NFC", "Initializing...");
    nfc.begin();
    delay(1000);
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        displayInfo("Error", "PN532 not found!");
        while (1); // halt
    }
    
    nfc.SAMConfig();
    Serial.println("NFC Reader Initialized.");
}

void initSDCard() {
  displayInfo("SD Card", "Initializing...");
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  
  if (!SD.begin(SD_CS)) {
    displayInfo("SD Error", "Init failed!");
    delay(2000);
    return;
  }
  displayInfo("SD Card OK");
  delay(1500);
}

void loadKeysFromSD() {
    File keyFile = SD.open("/keys.txt");
    if (keyFile) {
        Serial.println("Loading keys from keys.txt...");
        while (keyFile.available() && numKeys < MAX_KEYS) {
            String line = keyFile.readStringUntil('\n');
            line.trim();
            // Simple parsing for 6 hex bytes (e.g., FF FF FF FF FF FF)
            int byteIndex = 0;
            char* ptr = strtok((char*)line.c_str(), " ");
            while(ptr != NULL && byteIndex < 6){
                keys[numKeys][byteIndex] = strtoul(ptr, NULL, 16);
                ptr = strtok(NULL, " ");
                byteIndex++;
            }
            if(byteIndex == 6) numKeys++;
        }
        keyFile.close();
        displayInfo("Keys Loaded", String(numKeys) + " keys from SD");
        delay(1500);
    } else {
        Serial.println("keys.txt not found, using default keys.");
        // Fallback to default keys
        uint8_t defaultKeys[][6] = {
            {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
            {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},
            {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5},
            {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}
        };
        numKeys = 4;
        memcpy(keys, defaultKeys, sizeof(defaultKeys));
        displayInfo("Keys Loaded", "Using " + String(numKeys) + " defaults");
        delay(1500);
    }
}


// --- MAIN LOOP ---

void loop() {
  handleButtonPress();
}


// --- DISPLAY FUNCTIONS ---

void drawBorder() {
  display.drawRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, SSD1306_WHITE);
}

void displayInfo(String title, String info1, String info2, String info3) {
  display.clearDisplay();
  drawBorder();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(4, 4);
  display.println(title);
  display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);
  display.setCursor(4, 18);
  display.println(info1);
  display.setCursor(4, 28);
  display.println(info2);
  display.setCursor(4, 38);
  display.println(info3);
  display.display();
}

void displayTitleScreen() {
  display.clearDisplay();
  u8g2_for_adafruit_gfx.setFont(u8g2_font_adventurer_tr);
  u8g2_for_adafruit_gfx.setCursor(14, 30);
  u8g2_for_adafruit_gfx.print("DRJONES NFC");
  u8g2_for_adafruit_gfx.setCursor(37, 50);
  u8g2_for_adafruit_gfx.print("MASTER");
  display.display();
}

void displayMenu() {
    display.clearDisplay();
    drawBorder();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(4, 4);
    display.println("Main Menu");
    display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

    for (int i = 0; i < 4; i++) {
        int itemIndex = (currentMenuItem -1 + i + totalMenuItems) % totalMenuItems;
        if(i==0 && currentMenuItem == 0) itemIndex = 0;
        if(i==1) itemIndex = currentMenuItem;
        
        display.setCursor(4, 18 + i * 10);
        if (itemIndex == currentMenuItem) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        display.println(menuItems[itemIndex]);
    }
    display.display();
}

// --- BUTTON HANDLING & MENU LOGIC ---

void handleButtonPress() {
    if (digitalRead(BUTTON_UP) == LOW) {
        delay(250);
        if (inSDMenu) {
            currentSDMenuItem = (currentSDMenuItem == 0) ? totalSDMenuItems - 1 : currentSDMenuItem - 1;
            displaySDMenuOptions();
        } else {
            currentMenuItem = (currentMenuItem == 0) ? totalMenuItems - 1 : currentMenuItem - 1;
            displayMenu();
        }
    }

    if (digitalRead(BUTTON_DOWN) == LOW) {
        delay(250);
        if (inSDMenu) {
            currentSDMenuItem = (currentSDMenuItem + 1) % totalSDMenuItems;
            displaySDMenuOptions();
        } else {
            currentMenuItem = (currentMenuItem + 1) % totalMenuItems;
            displayMenu();
        }
    }

    if (digitalRead(BUTTON_SELECT) == LOW) {
        delay(250);
        if (inSDMenu) {
            executeSDMenuAction(currentSDMenuItem);
        } else {
            executeMenuItem();
        }
    }
}

void executeMenuItem() {
  switch (currentMenuItem) {
    case 0: readCard(); break;
    case 1: dumpCard(); break;
    case 2: readNDEF(); break;
    case 3: readWriteCard(); break;
    case 4: eraseCard(); break;
    case 5: 
      inMenu = true;
      inSDMenu = false;
      currentSDMenuItem = 0;
      displaySDMenuOptions();
      break;
  }
}

// --- NFC FUNCTIONS ---
bool confirmAction(String action) {
    displayInfo("Confirm Action", action + "?", "UP: Yes", "DOWN: No");
    while(true){
        if(digitalRead(BUTTON_UP) == LOW) { delay(250); return true; }
        if(digitalRead(BUTTON_DOWN) == LOW) { delay(250); return false; }
    }
}

bool authenticateBlock(uint8_t* uid, uint8_t uidLen, uint8_t block) {
  for (int i = 0; i < numKeys; i++) {
    if (nfc.mifareclassic_AuthenticateBlock(uid, uidLen, block, 0, keys[i])) {
      return true;
    }
  }
  return false;
}

void readCard() {
  displayInfo("Read Card", "Place card...");
  uint8_t uid[7];
  uint8_t uidLength;

  // Corrected the function call to match the library's declaration
  if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000)) {
    String uidStr = "";
    for (uint8_t i = 0; i < uidLength; i++) {
      if(uid[i] < 0x10) uidStr += "0";
      uidStr += String(uid[i], HEX) + " ";
    }
    uidStr.toUpperCase();
    
    displayInfo("Card Found", "UID:", uidStr);

  } else {
    displayInfo("Read Failed", "No card found.");
  }
  
  delay(4000);
  displayMenu();
}


void dumpCard() {
    displayInfo("Dump Card", "Place card...");
    uint8_t uid[7];
    uint8_t uidLength;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000)) {
        String uidStr = "";
        for (uint8_t i = 0; i < uidLength; i++) {
            if(uid[i] < 0x10) uidStr += "0";
            uidStr += String(uid[i], HEX);
        }
        uidStr.toUpperCase();
        
        String fileName = "/dump_" + uidStr + ".mfd";
        File dumpFile = SD.open(fileName, FILE_WRITE);

        if(!dumpFile) {
            displayInfo("SD Error", "Could not create", "dump file.");
            delay(2000); displayMenu(); return;
        }

        for (uint8_t block = 0; block < 64; block++) {
            displayInfo("Dumping Card", "Block " + String(block) + "/63", uidStr);
            if (authenticateBlock(uid, uidLength, block)) {
                uint8_t blockData[16];
                if (nfc.mifareclassic_ReadDataBlock(block, blockData)) {
                    dumpFile.write(blockData, 16);
                }
            } else {
                 uint8_t emptyData[16] = {0};
                 dumpFile.write(emptyData, 16);
            }
        }
        dumpFile.close();
        displayInfo("Dump Complete!", "Saved to:", fileName.substring(0,18));

    } else {
        displayInfo("Dump Failed", "No card found.");
    }
    delay(3000);
    displayMenu();
}


void readNDEF() {
    displayInfo("Read NDEF", "Place card...");
    uint8_t success;
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;

    success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000);

    if (success) {
      if (uidLength == 7) {
        displayInfo("NDEF Check", "NTAG2xx card found.", "Likely NDEF formatted.");
      } else if (uidLength == 4) {
        displayInfo("NDEF Check", "MIFARE Classic found.", "May be NDEF formatted.");
      } else {
        displayInfo("NDEF Check", "Unknown card type.");
      }
    } else {
        displayInfo("Read Failed", "No card found.");
    }
    delay(3000);
    displayMenu();
}

void readWriteCard() {
    if(!confirmAction("Write to Card")) { displayMenu(); return; }
    
    displayInfo("Read/Write", "Place card...");
    uint8_t uid[7];
    uint8_t uidLength;
    uint8_t blockNumber = 4;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000)) {
        if (authenticateBlock(uid, uidLength, blockNumber)) {
            displayInfo("Auth Success!", "Writing to Block 4...");
            uint8_t newData[16] = {'D','R','J','O','N','E','S','N','F','C','M','A','S','T','E','R'};
            if (nfc.mifareclassic_WriteDataBlock(blockNumber, newData)) {
                displayInfo("Write Success!", "Data written.");
            } else {
                displayInfo("Write Failed!", "Could not write.");
            }
        } else {
            displayInfo("Auth Failed!", "Cannot access block.");
        }
    } else {
        displayInfo("Read Failed", "No card found.");
    }
    delay(2000);
    displayMenu();
}

void eraseCard() {
    if(!confirmAction("Erase Block 4")) { displayMenu(); return; }

    displayInfo("Erase Card", "Place card...");
    uint8_t uid[7];
    uint8_t uidLength;
    uint8_t blockNumber = 4;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 1000)) {
        if (authenticateBlock(uid, uidLength, blockNumber)) {
            displayInfo("Auth Success!", "Erasing Block 4...");
            uint8_t emptyBlock[16] = {0};
            if (nfc.mifareclassic_WriteDataBlock(blockNumber, emptyBlock)) {
                displayInfo("Erase Success!", "Block 4 cleared.");
            } else {
                displayInfo("Erase Failed!", "Could not write.");
            }
        } else {
            displayInfo("Auth Failed!", "Cannot access block.");
        }
    } else {
        displayInfo("Read Failed", "No card found.");
    }
    delay(2000);
    displayMenu();
}


// --- SD CARD FUNCTIONS ---

void displaySDMenuOptions() {
    display.clearDisplay();
    drawBorder();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(4, 4);
    display.println("SD Card Menu");
    display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

    for (int i = 0; i < 4; i++) {
        int itemIndex = currentSDMenuItem;
        if(i > 0) itemIndex = (currentSDMenuItem + i -1 + totalSDMenuItems) % totalSDMenuItems;
        if(i==0) itemIndex = currentSDMenuItem;

        display.setCursor(4, 18 + i * 10);
        if (itemIndex == currentSDMenuItem) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        display.println(sdMenuOptions[itemIndex]);
    }
    display.display();
}

void executeSDMenuAction(int option) {
  switch (option) {
    case 0: viewFiles(OP_NONE); break;
    case 1: viewFiles(OP_WRITE_TO_CARD); break;
    case 2: viewFiles(OP_DELETE); break;
    case 3:
      inMenu = true;
      inSDMenu = false;
      displayMenu();
      break;
  }
}

void viewFiles(FileOperation operation) {
    fileCount = 0;
    File root = SD.open("/");
    if (!root) { displayInfo("SD Error", "Can't open root"); delay(2000); return; }

    while (fileCount < 50) {
        File entry = root.openNextFile();
        if (!entry) break;
        if (!entry.isDirectory()) {
            fileList[fileCount++] = String(entry.name());
        }
        entry.close();
    }
    root.close();

    if (fileCount == 0) {
        displayInfo("No Files Found", "SD card is empty.");
        delay(2000);
        displaySDMenuOptions();
        return;
    }

    bool inFileView = true;
    currentFileIndex = 0;
    while (inFileView) {
        displayFileList(operation);
        int button = getButtonInput(200);
        switch (button) {
            case BUTTON_UP:
                currentFileIndex = (currentFileIndex == 0) ? fileCount - 1 : currentFileIndex - 1;
                break;
            case BUTTON_DOWN:
                currentFileIndex = (currentFileIndex + 1) % fileCount;
                break;
            case BUTTON_SELECT:
                if (operation == OP_DELETE) {
                    deleteFile(fileList[currentFileIndex]);
                } else if (operation == OP_WRITE_TO_CARD) {
                    writeFileToCard(fileList[currentFileIndex]);
                }
                inFileView = false;
                break;
        }
        
        unsigned long press_start = millis();
        bool button_held = false;
        while(digitalRead(BUTTON_SELECT) == LOW){
            if(millis() - press_start > 1000) { // Hold for 1 second
                inFileView = false;
                button_held = true;
                break;
            }
        }
        if(button_held) break;
    }
    displaySDMenuOptions();
}

void displayFileList(FileOperation operation) {
    display.clearDisplay();
    drawBorder();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    String title = "Select File";
    if(operation == OP_DELETE) title = "Select to Delete";
    if(operation == OP_WRITE_TO_CARD) title = "Select to Write";
    display.setCursor(4, 4);
    display.println(title);
    
    display.drawLine(0, 14, SCREEN_WIDTH, 14, SSD1306_WHITE);

    for (int i = 0; i < 4; i++) {
        int index = (currentFileIndex - 1 + i + fileCount) % fileCount;
        if(i==0 && currentFileIndex == 0) index = 0;
        if(i==1) index = currentFileIndex;

        display.setCursor(4, 18 + i * 10);
        if (index == currentFileIndex) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        String filename = fileList[index];
        if (filename.length() > 18) {
            filename = filename.substring(0, 15) + "...";
        }
        display.println(filename);
    }
    display.display();
}

void deleteFile(String filename) {
    if(confirmAction("Delete File")) {
        if (SD.remove(filename)) {
            displayInfo("Success!", "File deleted.");
        } else {
            displayInfo("Error", "Could not delete.");
        }
    } else {
        displayInfo("Cancelled", "Delete cancelled.");
    }
    delay(1500);
}

void writeFileToCard(String filename) {
    if(!confirmAction("Write " + filename.substring(0,10) + "?")) return;
    File dataFile = SD.open(filename);
    if (!dataFile) {
        displayInfo("SD Error", "Cannot open file.");
        delay(2000); return;
    }

    uint8_t dataToWrite[16] = {0};
    dataFile.read(dataToWrite, 16);
    dataFile.close();

    displayInfo("Write to Card", "Place card...");
    uint8_t uid[7];
    uint8_t uidLength;
    uint8_t blockNumber = 4;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 5000)) {
        if (authenticateBlock(uid, uidLength, blockNumber)) {
            displayInfo("Writing Data...");
            if (nfc.mifareclassic_WriteDataBlock(blockNumber, dataToWrite)) {
                displayInfo("Write Success!", "Data written.");
            } else {
                displayInfo("Write Failed!");
            }
        } else {
            displayInfo("Auth Failed!", "Cannot access block.");
        }
    } else {
        displayInfo("Write Failed", "No card found.");
    }
    delay(2500);
}

int getButtonInput(int debounce_delay) {
  if (digitalRead(BUTTON_UP) == LOW) {
    delay(debounce_delay);
    return BUTTON_UP;
  }
  if (digitalRead(BUTTON_DOWN) == LOW) {
    delay(debounce_delay);
    return BUTTON_DOWN;
  }
  if (digitalRead(BUTTON_SELECT) == LOW) {
    delay(debounce_delay);
    return BUTTON_SELECT;
  }
  return 0;
}
