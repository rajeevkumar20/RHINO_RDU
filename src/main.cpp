/*--------------------------------------------------------------------------------------
  Includes
--------------------------------------------------------------------------------------*/
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

#include <WiFi.h> 
#include <WebServer.h>
#include <I2C_RTC.h>
#include <EEPROM.h>
#include <DMD32.h>

#include "Fonts/Logo.h"
// #include "Fonts/Arial_black_16.h"
#include "Fonts/Droid_Sans_24.h"
#include "Fonts/CngFont.h"
#include "Fonts/DieselFont.h"
#include "Fonts/LpgFont.h"
#include "Fonts/PetrolFont.h"
#include "Fonts/PowerFont.h"
#include "Fonts/TurboFont.h"
#include "Fonts/SmallSizeFont.h"
#include "Fonts/LargeSizeFont.h"

#include "Pages/Index.h"
#include "Pages/Login.h"
#include "Pages/Setting.h"
#include "Pages/Icon.h"


#define LED_PIN       14
#define BUTTON_PIN    34

const char* ssid     = "HisignDisplay02";
const char* password = "HisignWiFi";

IPAddress ip (192, 168, 1, 1);
IPAddress netmask (255, 255, 255, 0);

WebServer Server(80);

static MCP7940 Rtc;

//Fire up the DMD library as dmd
#define DISPLAYS_ACROSS 3
#define DISPLAYS_DOWN 2

DMD Screen(DISPLAYS_ACROSS, DISPLAYS_DOWN);

TaskHandle_t WebTaskHandle = NULL;
TaskHandle_t DmdTaskHandle = NULL;
TaskHandle_t Rs485TaskHandle = NULL;

JsonDocument GlobalVariable;
bool LoggedIn = false;
uint8_t *EnglishFont = NULL;
uint8_t *HindiFont = NULL;
String Price;
String Message;
bool HaveMessage = false;
bool ScrollEnabled = false;

#define USERNAME    "admin"    
#define PASSWORD    "password"

HardwareSerial Rs485(2);

#define RS485_TX  32
#define RS485_FC  33
#define RS485_RX  25

void Rs485Init() {
  pinMode(RS485_FC, OUTPUT);
  digitalWrite(RS485_FC, LOW);    // RX_Mode
  
  Rs485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
}

void Rs485Send(const char *data, uint16_t len) {
  digitalWrite(RS485_FC, HIGH);
  delayMicroseconds(20);

  Rs485.write(data, len);
  Rs485.flush();

  digitalWrite(RS485_FC, LOW);
}

uint8_t GetBrightnessFromHour(uint8_t hour) {
  if(hour >= 6 && hour < 18) {
    return 255;  // Day
  } 
  return 225;    // Night
}

bool WriteDataToEeprom(String &dataIn) {
  uint16_t length = dataIn.length();
  if(length > 0) {
    EEPROM.put(0x08, length);
    EEPROM.commit();

    uint16_t writeBytes = 0;
    while(writeBytes < length) {
      char chr = dataIn[writeBytes];
      EEPROM.put(writeBytes + 0x10, chr);
      EEPROM.commit();
      writeBytes++;
    }

    return true;
  }

  return false;
}

bool ReadDataFromEeprom(String &dataOut) {
  uint16_t length;
  EEPROM.get(0x08, length);
  
  if(length > 0 && length < 2000) {
    uint16_t readBytes = 0;
    while(readBytes < length) {
      char chr;
      EEPROM.get(readBytes + 0x10, chr);
      dataOut += chr;
      readBytes++;
    }
    
    return true;
  }

  return false;
}

void PrintLogoOnScreen(const uint8_t *str) {
  for(int i=0;i<96;i++) {
    uint32_t y1 = (str[4*i + 3] << 24) | (str[4*i + 2] << 16) | (str[4*i + 1] << 8) | str[4*i];
    for(int j=0;j<32;j++){
      Screen.writePixel(i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
    }
  }
}

void PrintStringOnScreen(const uint8_t *str) {
  for(int i=0;i<48;i++) {
    uint32_t y1 = (str[4*i + 3] << 24) | (str[4*i + 2] << 16) | (str[4*i + 1] << 8) | str[4*i];
    for(int j=0;j<32;j++){
      Screen.writePixel(i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
    }
  }
}

void PrintNumberOnScreen(const char* number) {
  int len = strlen(number);
  int x = 48;

  // Spaces
  while(*number == '0' && *(number + 1) != '.' && number != NULL) {
    for(int i=0;i<9;i++) {
      for(int j=0;j<32;j++){
        Screen.writePixel(x + i, j, GRAPHICS_NORMAL, false);
      }
    }
    x += 9;
    number++;
  }

  // Large Numbers
  while(*number != '.' && number != NULL) {
    for(int i=0;i<9;i++) {
      uint32_t y1 = (LargeNumbers[*number - 48][4*i + 3] << 24) | 
                    (LargeNumbers[*number - 48][4*i + 2] << 16) | 
                    (LargeNumbers[*number - 48][4*i + 1] << 8) | 
                    (LargeNumbers[*number - 48][4*i]);
      for(int j=0;j<32;j++){
        Screen.writePixel(x + i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
      }
    }
    x += 9;
    number++;
  }

  // Dot
  uint8_t dot[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x03};
  for(int i=0;i<3;i++) {
    uint32_t y1 = (dot[4*i + 3] << 24) | 
                  (dot[4*i + 2] << 16) | 
                  (dot[4*i + 1] << 8) | 
                  (dot[4*i]);
    for(int j=0;j<32;j++){
      Screen.writePixel(x + i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
    }
  }
  x += 3;
  number++;

  // Small Numbers
  while(*number != '.' && *number >= '0' && *number <= '9' && number != NULL) {
    for(int i=0;i<8;i++) {
      uint32_t y1 = (SmallNumbers[*number - 48][4*i + 3] << 24) | 
                    (SmallNumbers[*number - 48][4*i + 2] << 16) | 
                    (SmallNumbers[*number - 48][4*i + 1] << 8) | 
                    (SmallNumbers[*number - 48][4*i]);
      for(int j=0;j<32;j++){
        Screen.writePixel(x + i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
      }
    }
    x += 8;
    number++;
  }
}

void UpdatePointers() {
  String msg = GlobalVariable["MSG"];
  if(!msg.isEmpty() && msg != "null") {
    Message = msg;
    Message.toUpperCase();
    HaveMessage = true;
    if(Message.length() > 10) {
      ScrollEnabled = true;
    } else {
      ScrollEnabled = false;
    }
    return;
  }

  HaveMessage = false;
  String pduId = GlobalVariable["PDUID"];
  if(pduId == "01") {
    // Petrol
    EnglishFont = PetrolEnglish;
    HindiFont = PetrolHindi;
    String price = GlobalVariable["item1"];
    Price = price;
  } else if(pduId == "02") {
    // Diesel
    EnglishFont = DieselEnglish;
    HindiFont = DieselHindi;
    String price = GlobalVariable["item2"];
    Price = price;
  } else if(pduId == "03") {
    // poWer
    EnglishFont = PowerEnglish;
    HindiFont = PowerHindi;
    String price = GlobalVariable["item3"];
    Price = price;
  } else if(pduId == "04") {
    // Auto LPG
    EnglishFont = LpgEnglish;
    HindiFont = LpgHindi;
    String price = GlobalVariable["item4"];
    Price = price;
  } else if(pduId == "05") {
    // TurboJet
    EnglishFont = TurboEnglish;
    HindiFont = TurboHindi;
    String price = GlobalVariable["item5"];
    Price = price;
  } else if(pduId == "06") {
    // poWer99
    EnglishFont = PowerEnglish;
    HindiFont = PowerHindi;
    String price = GlobalVariable["item6"];
    Price = price;
  } else if(pduId == "07") {
    // CNG
    EnglishFont = CngEnglish;
    HindiFont = CngHindi;
    String price = GlobalVariable["item7"];
    Price = price;
  } else if(pduId == "08") {
    // poWer95
    EnglishFont = PowerEnglish;
    HindiFont = PowerHindi;
    String price = GlobalVariable["item8"];
    Price = price;
  }
}

// http://192.168.1.1/
void HandleRoot() {
  LoggedIn = false;
  Server.send(200, "text/html", IndexHtml);
}

// http://192.168.1.1/set_price
void HandleSetPrices() {
  LoggedIn = false;
  if (!Server.hasArg("plain")) {
    Server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  Screen.clearScreen(true);
  String body = Server.arg("plain");

  JsonDocument json;
  deserializeJson(json, body);

  // Update Values
  GlobalVariable["item1"] = json["item1"];
  GlobalVariable["item2"] = json["item2"];
  GlobalVariable["item3"] = json["item3"];
  GlobalVariable["item4"] = json["item4"];
  GlobalVariable["item5"] = json["item5"];
  GlobalVariable["item6"] = json["item6"];
  GlobalVariable["item7"] = json["item7"];
  GlobalVariable["item8"] = json["item8"];
  GlobalVariable["MSG"] = json["MSG"];
  
  String dateString = json["DATE"];
  String timeString = json["TIME"];

  // Update date and time
  Rtc.setDay(dateString.substring(0,2).toInt());
  Rtc.setMonth(dateString.substring(3,5).toInt());
  Rtc.setYear(dateString.substring(6,10).toInt());
  Rtc.setHours(timeString.substring(0,2).toInt());
  Rtc.setMinutes(timeString.substring(3,5).toInt());
  Rtc.setSeconds(timeString.substring(6,8).toInt());

  Server.send(200, "application/json", "{\"status\":\"UPDATED SUCCESSFULLY\"}");

  // Save to EEPROM
  String jsonString;
  serializeJson(GlobalVariable, jsonString);
  if(!WriteDataToEeprom(jsonString)) {
    Serial.println("EEPROM Write Failed");
  }
  
  // Send to all
  json.remove("MSG");
  serializeJson(json, jsonString);
  Rs485Send(jsonString.c_str(), jsonString.length());
  Rs485Send("\r\n", 2);

  UpdatePointers();

  if(HaveMessage) {
    Screen.drawMarquee(Message.c_str(), Message.length(), 0, 6);
  } else {
    PrintStringOnScreen(EnglishFont);
    PrintNumberOnScreen(Price.c_str());
  }
}

// http://192.168.1.1/get_price
void HandleGetPrices() {
  LoggedIn = false;
  String temp;
  serializeJson(GlobalVariable, temp);

  Server.send(200, "application/json", temp);
}

// http://192.168.1.1/validate_auth
void HandleValidateAuth() {
  if (!Server.hasArg("plain")) {
    Server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  String body = Server.arg("plain");

  JsonDocument json;
  deserializeJson(json, body);

  String username = json["username"];
  String password = json["password"];

  if(username == USERNAME && password == PASSWORD) {
    LoggedIn = true;
    Server.send(200, "application/json", "{\"status\":\"LOGIN SUCCESSFULLY\"}");
  } else {
    Server.send(401, "text/html", "Unauthorized");
  }
}

// http://192.168.1.1/setting_page
void HandleSettingPage() {
  if(!LoggedIn) {
    Server.send(401, "text/html", "Unauthorized"); 
    return;
  }

  Server.send(200, "text/html", SettingHtml); 
}

// http://192.168.1.1/get_pduid
void HandleGetPduId() {
  if(!LoggedIn) {
    Server.send(401, "text/html", "Unauthorized"); 
    return;
  }

  String id = GlobalVariable["PDUID"];
  String pduId = "{\"PDUID\":\"" + id + "\"}";
  Server.send(200, "application/json", pduId); 
}

// http://192.168.1.1/icon.png
void HandleIconPng() {
  Server.send_P(200, "image/png", IconPng, sizeof(IconPng)); 
}

// http://192.168.1.1/setting_login_page
void HandleLoginPage() {
  Server.send(200, "text/html", LoginHtml); 
}

// http://192.168.1.1/set_pduid
void HandleSetPduId() {
  if(!LoggedIn) {
    Server.send(401, "text/html", "Unauthorized"); 
    return;
  }

  if (!Server.hasArg("plain")) {
    Server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  Screen.clearScreen(true);

  String body = Server.arg("plain");

  JsonDocument json;
  deserializeJson(json, body);

  String id = json["PDUID"];
  GlobalVariable["PDUID"] = id;

  Server.send(200, "application/json", "{\"status\":\"UPDATED SUCCESSFULLY\"}");

  String jsonString;
  serializeJson(GlobalVariable, jsonString);
  if(!WriteDataToEeprom(jsonString)) {
    Serial.println("EEPROM Write Failed");
  }

  UpdatePointers();

  if(HaveMessage) {
    Screen.drawMarquee(Message.c_str(), Message.length(), 0, 6);
  } else {
    PrintStringOnScreen(EnglishFont);
    PrintNumberOnScreen(Price.c_str());
  }
}

void WebTask(void *pv) {
  Rs485Init();
  
  WiFi.softAPConfig(ip, ip, netmask);
  WiFi.softAP(ssid, password);
  
  Server.on("/", HTTP_GET, HandleRoot);
  Server.on("/get_price", HTTP_GET, HandleGetPrices);
  Server.on("/setting_login_page", HTTP_GET, HandleLoginPage);
  Server.on("/setting_page", HTTP_GET, HandleSettingPage);
  Server.on("/get_pduid", HTTP_GET, HandleGetPduId);
  Server.on("/icon.png", HTTP_GET, HandleIconPng);
  Server.on("/set_price", HTTP_POST, HandleSetPrices);
  Server.on("/validate_auth", HTTP_POST, HandleValidateAuth);
  Server.on("/set_pduid", HTTP_POST, HandleSetPduId);
  Server.begin();

  // Initialise delay
  vTaskDelay(5000);
  
  // Update Font and price
  Screen.clearScreen(true);
  if(HaveMessage) {
    Screen.drawMarquee(Message.c_str(), Message.length(), 0, 6);
  } else {
    PrintStringOnScreen(EnglishFont);
    PrintNumberOnScreen(Price.c_str());
  }

  uint16_t ledCounter = 0;
  while(true) {
    Server.handleClient();
    if(Rs485.available() > 0) {
      // Handle received packets
      String rawJson = Rs485.readString();

      if(rawJson.length() > 0 && !rawJson.isEmpty() && rawJson.endsWith("\r\n")) {
        JsonDocument json;
        deserializeJson(json, rawJson);
        if(!json.isNull()) {
          Screen.clearScreen(true);

          // Update Values
          GlobalVariable["item1"] = json["item1"];
          GlobalVariable["item2"] = json["item2"];
          GlobalVariable["item3"] = json["item3"];
          GlobalVariable["item4"] = json["item4"];
          GlobalVariable["item5"] = json["item5"];
          GlobalVariable["item6"] = json["item6"];
          GlobalVariable["item7"] = json["item7"];
          GlobalVariable["item8"] = json["item8"];

          // Save to EEPROM
          serializeJson(GlobalVariable, rawJson);
          if(!WriteDataToEeprom(rawJson)) {
            Serial.println("EEPROM Write Failed");
          }

          UpdatePointers();

          if(HaveMessage) {
            Screen.drawMarquee(Message.c_str(), Message.length(), 0, 6);
          } else {
            PrintStringOnScreen(EnglishFont);
            PrintNumberOnScreen(Price.c_str());
          }
        }
      }
    }

    vTaskDelay(10);
    ledCounter++;

    if(ledCounter%50 == 0) {
      if(digitalRead(LED_PIN) == HIGH) {
        digitalWrite(LED_PIN, LOW);
      } else {
        digitalWrite(LED_PIN, HIGH);
        // Rs485Send("Hello\n", 6);
      }
    }
    
    if(HaveMessage) {
      if(ScrollEnabled & ledCounter % 3 == 0) {
        if(Screen.stepMarquee(-1,0)) {
          Screen.clearScreen(true);
          Screen.drawMarquee(Message.c_str(), Message.length(), 0, 6);
        }
      }
    } else {
      if(ledCounter % 500 == 0) {
        Screen.clearScreen(true);
        if(ledCounter % 1000 == 0) {
          ledCounter = 0;
          PrintStringOnScreen(EnglishFont);
        } else {
          PrintStringOnScreen(HindiFont);        
        }

        PrintNumberOnScreen(Price.c_str());
      }
    }
  }
}

void DmdTask(void *pv) {  
  // Clear the DMD pixels held in RAM
  Screen.setBrightness(GetBrightnessFromHour(Rtc.getHours()));
  Screen.clearScreen( true );
  Screen.selectFont(Droid_Sans_24);

  // Print Logo for 5 seconds
  PrintLogoOnScreen(Logo);

  while(true) {
    Screen.scanDisplayBySPI();
    vTaskDelay(1);
  }
}

void Rs485Task(void *pv) {
  while(true) {
    if(Rs485.available() > 0) {
      // Handle received packets
      String rawJson = Rs485.readString();

      if(rawJson.length() > 0 && !rawJson.isEmpty() && rawJson.endsWith("\r\n")) {
        JsonDocument json;
        deserializeJson(json, rawJson);
        if(!json.isNull()) {
          Screen.clearScreen(true);

          // Update Values
          GlobalVariable["item1"] = json["item1"];
          GlobalVariable["item2"] = json["item2"];
          GlobalVariable["item3"] = json["item3"];
          GlobalVariable["item4"] = json["item4"];
          GlobalVariable["item5"] = json["item5"];
          GlobalVariable["item6"] = json["item6"];
          GlobalVariable["item7"] = json["item7"];
          GlobalVariable["item8"] = json["item8"];

          // Save to EEPROM
          serializeJson(GlobalVariable, rawJson);
          if(!WriteDataToEeprom(rawJson)) {
            Serial.println("EEPROM Write Failed");
          }

          UpdatePointers();

          if(HaveMessage) {
            Screen.drawMarquee(Message.c_str(), Message.length(), 0, 6);
          } else {
            PrintStringOnScreen(EnglishFont);
            PrintNumberOnScreen(Price.c_str());
          }
        }
      }
    }
    vTaskDelay(1);
  }
}

void setup(void) { 
  esp_task_wdt_deinit();

  Serial.begin(115200);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  EEPROM.begin(512);

  // delay(3000);
  Rtc.enableExternalBattery();
  if(!Rtc.isConnected()) {
    Serial.println("RTC Error");
  }
  Rtc.startClock();
  // Rtc.setDateTime("Sun Jan 26 02:44:53 2026");
  struct tm now = Rtc.getDateTime();
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d\n", 1900 + now.tm_yday, now.tm_mon + 1, now.tm_mday, now.tm_hour, now.tm_min, now.tm_sec);
  
  String jsonString;
  if(ReadDataFromEeprom(jsonString)) {
    deserializeJson(GlobalVariable, jsonString);
  } else {
    // Default
    Serial.println("Default Values loaded");
    GlobalVariable["PDUID"] = "01";
    GlobalVariable["item1"] = "000.00";
    GlobalVariable["item2"] = "000.00";
    GlobalVariable["item3"] = "000.00";
    GlobalVariable["item4"] = "000.00";
    GlobalVariable["item5"] = "000.00";
    GlobalVariable["item6"] = "000.00";
    GlobalVariable["item7"] = "000.00";
    GlobalVariable["item8"] = "000.00";
    GlobalVariable["MSG"] = "";
  }
  
  // Update pointers
  UpdatePointers();

  xTaskCreatePinnedToCore(
    WebTask,
    "WEB Server",
    4096,
    NULL,
    1,
    &WebTaskHandle,
    0 // core 0
  );

  xTaskCreatePinnedToCore(
    DmdTask,
    "DMD Refresh",
    4096,
    NULL,
    2,
    &DmdTaskHandle,
    1 // core 1
  );
}

void loop(void) {
}
