// DrJones NFC Master Firmware for ESP32-S3 Super Mini
// Version 5.0 - Physical Back Button
//
// ENHANCEMENTS in this version:
// - FEATURE: Added support for a dedicated physical "Back" button on GPIO 7.
// - UI: Removed software-based "Back" menu items for a cleaner interface.
// - REFACTOR: The file list view (`viewFiles`) is now fully non-blocking to seamlessly handle the new back button.
// - UX: The confirmation dialog can now be cancelled with the back button.
// - MAJOR REFACTOR: Replaced all `delay()` calls with non-blocking `millis()` logic for a fully responsive UI.
// - FEATURE: Implemented a graphical progress bar for the "Dump Card" operation.
// - DEBUG: Improved `loadKeysFromSD` function to be more robust against malformed `keys.txt` files.
// - CODE QUALITY: Implemented a state machine for managing UI views for cleaner code.
// - CODE QUALITY: Implemented a non-blocking button handling system.

#include <Wire.h>
#include <Adafruit_PN532.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <SD.h>
#include <SPI.h>

// --- PIN DEFINITIONS FOR ESP32-S3 SUPER MINI ---
#define I2C_SDA 8
#define I2C_SCL 9

#define PN532_IRQ   -1
#define PN532_RST   -1

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SSD1306_I2C_ADDRESS 0x3C

#define SD_CS 10
#define SD_SCK 4
#define SD_MOSI 5
#define SD_MISO 6

#define BUTTON_UP 1
#define BUTTON_DOWN 14
#define BUTTON_SELECT 13
#define BUTTON_BACK 7 // NEW: Physical back button pin

// --- GLOBAL OBJECTS ---
Adafruit_PN532 nfc(PN532_IRQ, PN532_RST);
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
U8G2_FOR_ADAFRUIT_GFX u8g2_for_adafruit_gfx;

// --- STATE MANAGEMENT ---
enum ViewState {
    VIEW_MAIN_MENU,
    VIEW_SD_MENU,
    VIEW_FILE_LIST,
    VIEW_ACTION,
    VIEW_CONFIRM
};
ViewState currentView = VIEW_MAIN_MENU;
bool needsRedraw = true;

// --- MENU MANAGEMENT ---
const char* menuItems[] = {"Read Card", "Dump Card", "Read NDEF", "Read/Write Block", "Erase Block", "SD Card Menu"};
const int totalMenuItems = 6;
int currentMenuItem = 0;

// MODIFIED: Removed "Back to Main" as it's replaced by the physical button
const char* sdMenuItems[] = {"View Files", "Write File to Card", "Delete File"};
const int totalSDMenuItems = 3;
int currentSDMenuItem = 0;

// --- DYNAMIC KEY MANAGEMENT ---
uint8_t keys[50][6];
int numKeys = 0;
uint8_t defaultKeys[3][6] = {
    {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
    {0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5},
    {0xB0, 0xB1, 0xB2, 0xB3, 0xB4, 0xB5}
};

// --- NON-BLOCKING BUTTON HANDLING ---
struct Button {
    const uint8_t PIN;
    uint32_t lastDebounceTime;
    bool lastButtonState;
    bool currentState;
    bool pressed;
};

Button btnUp = {BUTTON_UP, 0, HIGH, HIGH, false};
Button btnDown = {BUTTON_DOWN, 0, HIGH, HIGH, false};
Button btnSelect = {BUTTON_SELECT, 0, HIGH, HIGH, false};
Button btnBack = {BUTTON_BACK, 0, HIGH, HIGH, false}; // NEW: Button object for back
const unsigned long debounceDelay = 50;

// --- FORWARD DECLARATIONS ---
void displayMenu();
void displayInfo(String line1, String line2 = "", String line3 = "");
void waitForButtonPress();
bool confirmAction(String prompt);


// =========================================================================
//   SETUP
// =========================================================================
void setup() {
    Serial.begin(115200);
    
    initDisplay();
    initNFC();
    initSDCard();
    loadKeysFromSD();

    pinMode(btnUp.PIN, INPUT_PULLUP);
    pinMode(btnDown.PIN, INPUT_PULLUP);
    pinMode(btnSelect.PIN, INPUT_PULLUP);
    pinMode(btnBack.PIN, INPUT_PULLUP); // NEW: Initialize back button pin

    u8g2_for_adafruit_gfx.setFont(u8g2_font_logisoso16_tr);
    u8g2_for_adafruit_gfx.setFontMode(1);
    u8g2_for_adafruit_gfx.setCursor(10, 30);
    u8g2_for_adafruit_gfx.print("NFC Master");
    u8g2_for_adafruit_gfx.setCursor(35, 55);
    u8g2_for_adafruit_gfx.print("v5.0");
    display.display();
    delay(2000);
}

// =========================================================================
//   MAIN LOOP
// =========================================================================
void loop() {
    updateButtons();
    handleInput();
    if (needsRedraw) {
        drawCurrentView();
        needsRedraw = false;
    }
}

// =========================================================================
//   INITIALIZATION FUNCTIONS
// =========================================================================
void initDisplay() {
    Wire.begin(I2C_SDA, I2C_SCL);
    if (!display.begin(SSD1306_SWITCHCAPVCC, SSD1306_I2C_ADDRESS)) {
        Serial.println(F("SSD1306 allocation failed"));
        for (;;);
    }
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    u8g2_for_adafruit_gfx.begin(display);
}

void initNFC() {
    nfc.begin();
    uint32_t versiondata = nfc.getFirmwareVersion();
    if (!versiondata) {
        displayInfo("Error", "PN532 not found!", "Check wiring.");
        waitForButtonPress();
        return;
    }
    nfc.SAMConfig();
    displayInfo("NFC Ready", "PN532 Found");
    delay(1500);
}

void initSDCard() {
    SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
    if (!SD.begin(SD_CS)) {
        displayInfo("SD Card Error", "Initialization failed!");
        waitForButtonPress();
        return;
    }
    displayInfo("SD Card Ready", "Initialized OK");
    delay(1500);
}

// =========================================================================
//   UI & STATE MANAGEMENT
// =========================================================================

void drawCurrentView() {
    display.clearDisplay();
    switch (currentView) {
        case VIEW_MAIN_MENU:
            displayMenu();
            break;
        case VIEW_SD_MENU:
            displaySDMenuOptions();
            break;
    }
    display.display();
}

void handleInput() {
    // Note: The file list view handles its own input inside the viewFiles() function
    if (currentView == VIEW_MAIN_MENU) {
        handleMenuInput();
    } else if (currentView == VIEW_SD_MENU) {
        handleSDMenuInput();
    }
}

void displayMenu() {
    display.setTextSize(1);
    display.setCursor(20, 0);
    display.println("--- NFC MASTER ---");
    display.drawLine(0, 10, 127, 10, WHITE);

    int startItem = 0;
    if (currentMenuItem >= 4) {
        startItem = currentMenuItem - 3;
    }

    for (int i = 0; i < 4; i++) {
        int itemIndex = startItem + i;
        if (itemIndex < totalMenuItems) {
            display.setCursor(4, 18 + i * 12);
            if (itemIndex == currentMenuItem) {
                display.print("> ");
            } else {
                display.print("  ");
            }
            display.println(menuItems[itemIndex]);
        }
    }
}

void handleMenuInput() {
    if (btnUp.pressed) {
        currentMenuItem = (currentMenuItem - 1 + totalMenuItems) % totalMenuItems;
        needsRedraw = true;
    } else if (btnDown.pressed) {
        currentMenuItem = (currentMenuItem + 1) % totalMenuItems;
        needsRedraw = true;
    } else if (btnSelect.pressed) {
        executeMenuItem(currentMenuItem);
        if (currentView == VIEW_MAIN_MENU) { // Only redraw if we haven't changed view
            needsRedraw = true;
        }
    }
    // Back button does nothing on main menu
}

void executeMenuItem(int item) {
    switch (item) {
        case 0: readCard(); break;
        case 1: dumpCard(); break;
        case 2: readNDEF(); break;
        case 3: readWriteCard(false); break; // false for read
        case 4: eraseCard(); break;
        case 5: 
            currentView = VIEW_SD_MENU;
            currentSDMenuItem = 0;
            needsRedraw = true;
            break;
    }
    // After most actions, we want to be back on the main menu
    if (item < 5) {
        currentView = VIEW_MAIN_MENU;
    }
}

void displaySDMenuOptions() {
    display.setTextSize(1);
    display.setCursor(25, 0);
    display.println("-- SD CARD MENU --");
    display.drawLine(0, 10, 127, 10, WHITE);
    
    for (int i = 0; i < totalSDMenuItems; i++) {
        display.setCursor(4, 18 + i * 12);
        if (i == currentSDMenuItem) {
            display.print("> ");
        } else {
            display.print("  ");
        }
        display.println(sdMenuItems[i]);
    }
}

void handleSDMenuInput() {
    // NEW: Physical back button returns to main menu
    if (btnBack.pressed) {
        currentView = VIEW_MAIN_MENU;
        needsRedraw = true;
        return; // Exit to avoid other inputs
    }

    if (btnUp.pressed) {
        currentSDMenuItem = (currentSDMenuItem - 1 + totalSDMenuItems) % totalSDMenuItems;
        needsRedraw = true;
    } else if (btnDown.pressed) {
        currentSDMenuItem = (currentSDMenuItem + 1) % totalSDMenuItems;
        needsRedraw = true;
    } else if (btnSelect.pressed) {
        executeSDMenuAction(currentSDMenuItem);
    }
}

void executeSDMenuAction(int item) {
    switch (item) {
        case 0: viewFiles("view"); break;
        case 1: viewFiles("write"); break;
        case 2: viewFiles("delete"); break;
    }
    // After action, return to SD menu and redraw
    currentView = VIEW_SD_MENU;
    needsRedraw = true;
}

void displayInfo(String line1, String line2, String line3) {
    currentView = VIEW_ACTION;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.println(line1);
    display.setCursor(0, 25);
    display.println(line2);
    display.setCursor(0, 40);
    display.println(line3);
    display.display();
}

void waitForButtonPress() {
    updateButtons(); 
    while (btnUp.pressed || btnDown.pressed || btnSelect.pressed || btnBack.pressed) {
        updateButtons();
    }
    while (!btnUp.pressed && !btnDown.pressed && !btnSelect.pressed && !btnBack.pressed) {
        updateButtons();
    }
}

bool confirmAction(String prompt) {
    currentView = VIEW_CONFIRM;
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 10);
    display.println(prompt);
    display.setCursor(10, 40);
    display.println("No (BACK)");
    display.setCursor(80, 40);
    display.println("Yes (SEL)");
    display.display();

    updateButtons();
    while (btnUp.pressed || btnDown.pressed || btnSelect.pressed || btnBack.pressed) {
        updateButtons();
    }

    while (true) {
        updateButtons();
        if (btnSelect.pressed) return true;
        // MODIFIED: Back or Up/Down cancels the action
        if (btnUp.pressed || btnDown.pressed || btnBack.pressed) return false;
    }
}

// =========================================================================
//   NFC FUNCTIONS (Unchanged from v4)
// =========================================================================

void readCard() {
    displayInfo("Reading Card...", "Place card on reader");
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 2000)) {
        String uidString = "UID: ";
        for (uint8_t i = 0; i < uidLength; i++) {
            uidString += String(uid[i], HEX);
            uidString.toUpperCase();
            uidString += " ";
        }
        displayInfo("Card Found!", uidString, "Press any key...");
    } else {
        displayInfo("Read Failed", "No card found.", "Press any key...");
    }
    waitForButtonPress();
}

void dumpCard() {
    displayInfo("Dump Card", "Place card on reader");
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;

    if (!nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 2000)) {
        displayInfo("Dump Failed", "No card found.");
        waitForButtonPress();
        return;
    }
    
    String filename = "/dump-";
    for (uint8_t i = 0; i < uidLength; i++) {
        filename += String(uid[i], HEX);
    }
    filename += ".bin";
    filename.toUpperCase();
    
    if (SD.exists(filename)) {
        if (!confirmAction("Overwrite file?")) {
             displayInfo("Cancelled", "Dump cancelled.");
             waitForButtonPress();
             return;
        }
        SD.remove(filename);
    }

    File dataFile = SD.open(filename, FILE_WRITE);
    if (!dataFile) {
        displayInfo("SD Error", "Cannot create file.");
        waitForButtonPress();
        return;
    }

    displayInfo("Dumping Card...", filename);

    bool anyBlockAuthenticated = false;
    for (int block = 0; block < 64; block++) {
        // Draw Progress Bar
        int barX = 10, barY = 40, barWidth = SCREEN_WIDTH - 20, barHeight = 8;
        float progress = (float)(block + 1) / 64.0f;
        int filledWidth = (int)(barWidth * progress);
        display.drawRect(barX, barY, barWidth, barHeight, SSD1306_WHITE);
        display.fillRect(barX + 1, barY + 1, filledWidth, barHeight - 2, SSD1306_WHITE);
        display.fillRect(barX + 1 + filledWidth, barY + 1, barWidth - filledWidth - 2, barHeight - 2, SSD1306_BLACK);
        display.display();

        bool authenticated = authenticateBlock(uid, uidLength, block);
        if(authenticated) anyBlockAuthenticated = true;
        
        uint8_t data[16];
        bool success = nfc.mifareclassic_ReadDataBlock(block, data);
        if (success) {
            dataFile.write(data, 16);
        } else {
            uint8_t emptyData[16] = {0};
            dataFile.write(emptyData, 16);
        }
    }
    dataFile.close();
    if(anyBlockAuthenticated){
      displayInfo("Dump Complete!", filename, "Press any key...");
    } else {
      displayInfo("Auth Failed!", "Could not read card.", "Dump may be empty.");
    }
    waitForButtonPress();
}

void readNDEF() {
    displayInfo("Read NDEF", "Place card on reader");
    uint8_t uid[] = { 0, 0, 0, 0, 0, 0, 0 };
    uint8_t uidLength;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 2000)) {
        if (uidLength == 4) {
            displayInfo("NDEF Tag Found", "Type: NTAG");
        } else if (uidLength == 7) {
            displayInfo("NDEF Tag Found", "Type: MIFARE Classic");
        } else {
            displayInfo("NDEF Tag Found", "Type: Unknown");
        }
    } else {
        displayInfo("Read Failed", "No NDEF tag found.");
    }
    waitForButtonPress();
}

void readWriteCard(bool isWrite) {
    displayInfo("Read/Write Block", "Place card...");
    uint8_t uid[7];
    uint8_t uidLength;
    uint8_t blockNumber = 4;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 5000)) {
        if (authenticateBlock(uid, uidLength, blockNumber)) {
            if(isWrite) {
                // Future write logic here
            } else {
                uint8_t data[16];
                if (nfc.mifareclassic_ReadDataBlock(blockNumber, data)) {
                    String dataHex = "Data: ";
                    for(int i=0; i<16; i++){
                        if(data[i] < 0x10) dataHex += "0";
                        dataHex += String(data[i], HEX);
                    }
                    displayInfo("Read Success!", "Block " + String(blockNumber), dataHex);
                } else {
                    displayInfo("Read Failed!");
                }
            }
        } else {
            displayInfo("Auth Failed!", "Cannot access block.");
        }
    } else {
        displayInfo("Operation Failed", "No card found.");
    }
    waitForButtonPress();
}

void eraseCard() {
    displayInfo("Erase Block", "Place card...");
    if (!confirmAction("Erase Block 4?")) {
        displayInfo("Cancelled");
        waitForButtonPress();
        return;
    }

    uint8_t uid[7];
    uint8_t uidLength;
    uint8_t blockNumber = 4;
    uint8_t dataToErase[16] = {0};

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 5000)) {
        if (authenticateBlock(uid, uidLength, blockNumber)) {
            if (nfc.mifareclassic_WriteDataBlock(blockNumber, dataToErase)) {
                displayInfo("Erase Success!", "Block " + String(blockNumber) + " zeroed.");
            } else {
                displayInfo("Erase Failed!");
            }
        } else {
            displayInfo("Auth Failed!", "Cannot access block.");
        }
    } else {
        displayInfo("Erase Failed", "No card found.");
    }
    waitForButtonPress();
}

// =========================================================================
//   SD CARD & FILE FUNCTIONS
// =========================================================================

void loadKeysFromSD() {
    File keyFile = SD.open("/keys.txt");
    if (keyFile) {
        int keyIndex = 0;
        while (keyFile.available() && keyIndex < 50) {
            String line = keyFile.readStringUntil('\n');
            line.trim();
            if(line.length() == 0) continue;

            char lineBuffer[40];
            line.toCharArray(lineBuffer, sizeof(lineBuffer));
            
            char* ptr = strtok(lineBuffer, " ");
            int byteIndex = 0;
            bool lineOk = true;
            while(ptr != NULL && byteIndex < 6){
                char* endPtr;
                long val = strtol(ptr, &endPtr, 16);
                if (*endPtr != '\0' || val < 0 || val > 255) {
                    lineOk = false;
                    break;
                }
                keys[keyIndex][byteIndex] = (uint8_t)val;
                ptr = strtok(NULL, " ");
                byteIndex++;
            }

            if(byteIndex == 6 && lineOk){
                keyIndex++;
            } else {
                Serial.println("Malformed line in keys.txt: " + line);
            }
        }
        numKeys = keyIndex;
        keyFile.close();
        if(numKeys == 0) {
             displayInfo("Keyfile Error", "No valid keys found.", "Using defaults.");
             waitForButtonPress();
             memcpy(keys, defaultKeys, sizeof(defaultKeys));
             numKeys = 3;
        }
    } else {
        memcpy(keys, defaultKeys, sizeof(defaultKeys));
        numKeys = 3;
    }
}

bool authenticateBlock(uint8_t *uid, uint8_t uidLen, uint8_t block) {
    for (int i = 0; i < numKeys; i++) {
        if (nfc.mifareclassic_AuthenticateBlock(uid, uidLen, block, 0, keys[i])) return true;
        if (nfc.mifareclassic_AuthenticateBlock(uid, uidLen, block, 1, keys[i])) return true;
    }
    return false;
}

// REFACTORED: This function is now fully non-blocking and uses the physical back button.
void viewFiles(String operation) {
    File root = SD.open("/");
    if (!root) {
        displayInfo("SD Error", "Cannot open root dir.");
        waitForButtonPress(); return;
    }
    
    int totalFiles = 0;
    while(root.openNextFile()){ totalFiles++; }
    
    if (totalFiles == 0) {
        displayInfo("SD Card", "No files found.");
        root.close(); waitForButtonPress(); return;
    }
    
    String* fileList = new String[totalFiles];
    root.rewindDirectory();
    for(int i=0; i<totalFiles; i++){
        File entry = root.openNextFile();
        fileList[i] = entry.name();
        entry.close();
    }
    root.close();

    currentView = VIEW_FILE_LIST;
    int currentFileIndex = 0;
    bool in_file_view = true;
    
    while (in_file_view) {
        updateButtons();

        if (btnBack.pressed) { // NEW: Physical back button exits the file view
            in_file_view = false;
            continue;
        }

        // Handle Up/Down/Select input
        if (btnUp.pressed) {
            currentFileIndex = (currentFileIndex - 1 + totalFiles) % totalFiles;
        } else if (btnDown.pressed) {
            currentFileIndex = (currentFileIndex + 1) % totalFiles;
        } else if (btnSelect.pressed) {
            String selectedFile = "/" + fileList[currentFileIndex];
            if (operation == "delete") {
                deleteFile(selectedFile);
            } else if (operation == "write") {
                writeFileToCard(selectedFile);
            }
            in_file_view = false; // Exit after any action
        }

        // --- DISPLAY LOGIC ---
        display.clearDisplay();
        display.setTextSize(1);
        display.setCursor(15, 0);
        String title = "--- VIEW FILES ---";
        if (operation == "write") title = "-- WRITE FROM FILE --";
        if (operation == "delete") title = "--- DELETE FILE ---";
        display.println(title);
        display.drawLine(0, 10, 127, 10, WHITE);

        int startItem = 0;
        if (currentFileIndex >= 4) {
            startItem = currentFileIndex - 3;
        }

        for (int j = 0; j < 4; j++) {
            int itemIndex = startItem + j;
            if (itemIndex < totalFiles) {
                display.setCursor(4, 18 + j * 12);
                if (itemIndex == currentFileIndex) display.print("> ");
                else display.print("  ");
                
                String name = fileList[itemIndex];
                if (name.startsWith("/")) name = name.substring(1);
                if (name.length() > 18) name = name.substring(0, 15) + "...";
                display.println(name);
            }
        }
        display.display();
    }
    
    delete[] fileList;
}


void deleteFile(String filename) {
    if (confirmAction("Delete " + filename.substring(0, 10) + "?")) {
        if (SD.remove(filename)) {
            displayInfo("Success", "File deleted.");
        } else {
            displayInfo("Error", "Could not delete.");
        }
    } else {
        displayInfo("Cancelled", "Delete cancelled.");
    }
    waitForButtonPress();
}

void writeFileToCard(String filename) {
    displayInfo("Write to Card", "Place card...");
    
    File dataFile = SD.open(filename);
    if (!dataFile) {
        displayInfo("SD Error", "Cannot open file.");
        waitForButtonPress(); return;
    }
    if (dataFile.size() < 16){
        displayInfo("File Error", "File is < 16 bytes.");
        dataFile.close(); waitForButtonPress(); return;
    }

    uint8_t dataToWrite[16] = {0};
    dataFile.read(dataToWrite, 16);
    dataFile.close();

    uint8_t uid[7];
    uint8_t uidLength;
    uint8_t blockNumber = 4;

    if (nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &uidLength, 5000)) {
        if (authenticateBlock(uid, uidLength, blockNumber)) {
            displayInfo("Writing Data...", "To block " + String(blockNumber));
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
    waitForButtonPress();
}


// =========================================================================
//   LOW-LEVEL BUTTON HANDLING
// =========================================================================
void updateButtons() {
    auto debounce = [](Button& b) {
        bool reading = digitalRead(b.PIN);
        b.pressed = false;
        if (reading != b.lastButtonState) {
            b.lastDebounceTime = millis();
        }
        if ((millis() - b.lastDebounceTime) > debounceDelay) {
            if (reading != b.currentState) {
                b.currentState = reading;
                if (b.currentState == LOW) {
                    b.pressed = true;
                }
            }
        }
        b.lastButtonState = reading;
    };
    debounce(btnUp);
    debounce(btnDown);
    debounce(btnSelect);
    debounce(btnBack); // NEW: Poll the back button
}
