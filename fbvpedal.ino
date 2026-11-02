#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <EEPROM.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

const int btnChannelUp   = 4; //switch up
const int btnChannelDown = 5; //switch down

int currentChannel = 0;
unsigned long lastBtnPress = 0;
unsigned long lastPollTime = 0;
bool isSynced = false;

const uint8_t fbvChannelIds[4] = { 0x20, 0x30, 0x40, 0x50 };
const int EEPROM_ADDR = 0;

uint8_t sigWindow[3] = {0}; //used for detecting the channel 

void sendFbvSwitch(uint8_t switchId, uint8_t state) {
  uint8_t pkt[5] = { 0xF0, 0x03, 0x81, switchId, state };
  Serial.write(pkt, 5);
  Serial.flush();
}

void sendStatusPoll() { //poll the amp for status
  uint8_t pollReq[] = { 0xF0, 0x02, 0x90, 0x00 };
  Serial.write(pollReq, sizeof(pollReq));
  Serial.flush();
}

void triggerChannelChange(int ch) {
  uint8_t targetId = fbvChannelIds[ch];
  sendFbvSwitch(targetId, 0x01);
  delay(40);
  sendFbvSwitch(targetId, 0x00);
}

void updateOLED() {
  display.clearDisplay();
  
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(14, 2);
  display.print(F("LINE6 FBV PEDAL"));
  display.drawLine(0, 12, 128, 12, SSD1306_WHITE);
  
  display.setTextSize(4);
  display.setCursor(52, 24); 
  display.print((char)('A' + currentChannel));
  
  display.display();
}

void setup() {
  pinMode(btnChannelUp, INPUT_PULLUP);
  pinMode(btnChannelDown, INPUT_PULLUP);

  Wire.begin();
  Wire.setClock(400000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    for (;;);
  }

  byte savedChannel = EEPROM.read(EEPROM_ADDR);
  if (savedChannel < 4) {
    currentChannel = savedChannel;
  }
  updateOLED();

  Serial.begin(31250);
  
  sendStatusPoll();
  lastPollTime = millis();
}

void loop() {
  bool stateChanged = false;

  if (!isSynced && (millis() - lastPollTime > 1500)) {
    sendStatusPoll();
    lastPollTime = millis();
  }

  while (Serial.available()) {
    uint8_t b = Serial.read();

    sigWindow[0] = sigWindow[1];
    sigWindow[1] = sigWindow[2];
    sigWindow[2] = b;
    //check 3 rolling hex codes for current channel
    if (sigWindow[0] == 0x7E && sigWindow[1] == 0xDF) {
      int detectedChannel = -1;
      if      (sigWindow[2] == 0xEB) detectedChannel = 0; //A
      else if (sigWindow[2] == 0xD6) detectedChannel = 1; //B
      else if (sigWindow[2] == 0xF5) detectedChannel = 2; //C
      else if (sigWindow[2] == 0xD5) detectedChannel = 3; //D

      if (detectedChannel != -1) {
        isSynced = true;
        if (currentChannel != detectedChannel) {
          currentChannel = detectedChannel;
          EEPROM.update(EEPROM_ADDR, currentChannel);
          stateChanged = true;
        }
      }
    }

    static uint8_t history[3] = {0};
    history[0] = history[1];
    history[1] = history[2];
    history[2] = b;

    if (history[0] == 0xF0 && history[1] == 0x02 && history[2] == 0x90) {
      uint8_t ack[4] = { 0xF0, 0x02, 0x90, 0x02 };
      Serial.write(ack, 4);
      Serial.flush();
    }
  }

  if (millis() - lastBtnPress > 200) {
    if (digitalRead(btnChannelUp) == LOW) {
      currentChannel = (currentChannel + 1) % 4;
      lastBtnPress = millis();
      triggerChannelChange(currentChannel);
      EEPROM.update(EEPROM_ADDR, currentChannel);
      stateChanged = true;
    } 
    else if (digitalRead(btnChannelDown) == LOW) {
      currentChannel = (currentChannel - 1 + 4) % 4;
      lastBtnPress = millis();
      triggerChannelChange(currentChannel);
      EEPROM.update(EEPROM_ADDR, currentChannel);
      stateChanged = true;
    }
  }

  if (stateChanged) {
    updateOLED();
  }
}