/*--------------------------------------------------------------------------------------
  Includes
--------------------------------------------------------------------------------------*/
#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_task_wdt.h>

#include <WiFi.h> 
#include <WebServer.h>
#include <MCP7940.h>
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
#include "Pages/Reorder.h"
#include "Pages/Icon.h"

// #if !defined(DEBUG)
// #define DEBUG
// #endif

// #define DEFAULT_CONFIG

#define LED_PIN       14
#define BUTTON_PIN    34
#define RS485_TX      32
#define RS485_FC      33
#define RS485_RX      25

IPAddress ip (192, 168, 1, 1);
IPAddress netmask (255, 255, 255, 0);

WebServer Server(80);
MCP7940_Class Rtc;
DMD *Screen = NULL;
HardwareSerial Rs485(2);

TaskHandle_t WebTaskHandle = NULL;
TaskHandle_t DmdTaskHandle = NULL;

typedef struct {
  uint8_t *EnglishFont;
  uint8_t *HindiFont;
  String Value;
} ParameterStruct;

typedef enum {
  DisplaySize640mm = 0,
  DisplaySize960mm,
  DisplaySize1600mm,
} DisplaySizes;

String WifiSsid = "HISIGNrdu";
String Username = "admin";
String Password = "admin@123";
DisplaySizes DisplaySize = DisplaySizes::DisplaySize960mm;
JsonDocument GlobalVariable;
bool LoggedIn = false;
uint8_t PduId = 1;
uint8_t TotalParametersToDisplay = 1;
uint8_t MsgId = 1;
ParameterStruct Parameters[8] = {0};
uint8_t CurrentItemIndex = 0;
String Message;
bool HaveMessage = false;
bool ScrollEnabled = false;
bool IsValid = false;

// Initialize RS485 Serial port
void Rs485Init() {
  pinMode(RS485_FC, OUTPUT);
  digitalWrite(RS485_FC, LOW);    // RX_Mode
  
  Rs485.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
}

// Send data over RS485 Serial port
void Rs485Send(const char *data, uint16_t len) {
  digitalWrite(RS485_FC, HIGH);
  delayMicroseconds(20);

  Rs485.write(data, len);
  Rs485.flush();

  digitalWrite(RS485_FC, LOW);
}

// Get brightness % by passing hour
uint8_t GetBrightnessFromHour(uint8_t hour) {
  if(hour >= 6 && hour < 18) {
    return 240;  // Day 90%
  } 
  return 200;    // Night 70%
}

// Write data to internal EEPROM
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

// Read data from internal EEPROM
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

// Print logo on screen
void PrintLogoOnScreen(const uint8_t *str) {
  // Not fit for 640mm display
  if(DisplaySize == DisplaySizes::DisplaySize640mm) {
    return;
  }

  int x = (DisplaySize == DisplaySizes::DisplaySize1600mm) ? 32 : 0;
  for(int i=0;i<96;i++) {
    uint32_t y1 = (str[4*i + 3] << 24) | (str[4*i + 2] << 16) | (str[4*i + 1] << 8) | str[4*i];
    for(int j=0;j<32;j++){
      Screen->writePixel(x + i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
    }
  }
}

// Print String on Screen
void PrintStringOnScreen(const uint8_t *str) {
  // On 640mm display, string will not present
  if(DisplaySize == DisplaySizes::DisplaySize640mm) {
    return;
  }

  int x = (DisplaySize == DisplaySizes::DisplaySize1600mm) ? 32 : 0;
  for(int i=0;i<48;i++) {
    uint32_t y1 = (str[4*i + 3] << 24) | (str[4*i + 2] << 16) | (str[4*i + 1] << 8) | str[4*i];
    for(int j=0;j<32;j++){
      Screen->writePixel(x + i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
    }
  }
}

// Print Numbers on Screen
void PrintNumberOnScreen(const char* number) {
  int len = strlen(number);
  int x = 48;
  if(DisplaySize == DisplaySizes::DisplaySize1600mm) {
    x = 80;
  } else if(DisplaySize == DisplaySizes::DisplaySize960mm) {
    x = 48;
  } else {
    x = 0;
  }

  // Spaces
  while(*number == '0' && *(number + 1) != '.' && number != NULL) {
    for(int i=0;i<9;i++) {
      for(int j=0;j<32;j++){
        Screen->writePixel(x + i, j, GRAPHICS_NORMAL, false);
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
        Screen->writePixel(x + i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
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
      Screen->writePixel(x + i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
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
        Screen->writePixel(x + i, j, GRAPHICS_NORMAL, (y1 >> j) & 1);
      }
    }
    x += 8;
    number++;
  }
}

// Get Item details
ParameterStruct GetItemDetail(String& item) {
  ParameterStruct param;
  String price = GlobalVariable[item];
  if(item == "Petrol") {
    // Petrol
    param.EnglishFont = PetrolEnglish;
    param.HindiFont = PetrolHindi;
    param.Value = price;
  } else if(item == "Diesel") {
    // Diesel
    param.EnglishFont = DieselEnglish;
    param.HindiFont = DieselHindi;
    param.Value = price;
  } else if(item == "Power") {
    // poWer
    param.EnglishFont = PowerEnglish;
    param.HindiFont = PowerHindi;
    param.Value = price;
  } else if(item == "Auto LPG") {
    // Auto LPG
    param.EnglishFont = LpgEnglish;
    param.HindiFont = LpgHindi;
    param.Value = price;
  } else if(item == "TurboJet") {
    // TurboJet
    param.EnglishFont = TurboEnglish;
    param.HindiFont = TurboHindi;
    param.Value = price;
  } else if(item == "Power99") {
    // poWer99
    param.EnglishFont = Power99English;
    param.HindiFont = Power99Hindi;
    param.Value = price;
  } else if(item == "CNG") {
    // CNG
    param.EnglishFont = CngEnglish;
    param.HindiFont = CngHindi;
    param.Value = price;
  } else if(item == "Power95") {
    // poWer95
    param.EnglishFont = Power95English;
    param.HindiFont = Power95Hindi;
    param.Value = price;
  }

  return param;
}

int CharWidth(const unsigned char letter) {
    unsigned char c = letter;
    const uint8_t *font = Droid_Sans_24;
    // Space is often not included in font so use width of 'n'
    if (c == ' ') c = 'n';
    uint8_t width = 0;

    uint8_t firstChar = pgm_read_byte(font + FONT_FIRST_CHAR);
    uint8_t charCount = pgm_read_byte(font + FONT_CHAR_COUNT);

    uint16_t index = 0;

    if (c < firstChar || c >= (firstChar + charCount)) {
	    return 0;
    }
    c -= firstChar;

    if (pgm_read_byte(font + FONT_LENGTH) == 0
	&& pgm_read_byte(font + FONT_LENGTH + 1) == 0) {
	    // zero length is flag indicating fixed width font (array does not contain width data entries)
	    width = pgm_read_byte(font + FONT_FIXED_WIDTH);
    } else {
	    // variable width font, read width data
	    width = pgm_read_byte(font + FONT_WIDTH_TABLE + c);
    }
    return width;
}

// Update Pointers
bool UpdatePointers() {
  // SSID
  String ssid = GlobalVariable["SSID"];
  if(ssid == "null") {
    return false;
  }
  WifiSsid = ssid;
  
  // USER
  String user = GlobalVariable["USER"];
  Username = user;
  
  // PSWD
  String pswd = GlobalVariable["PSWD"];
  Password = pswd;
  
  // Display Size
  String displaySize = GlobalVariable["SIZE"];
  if(displaySize == "1600mm") {
    DisplaySize = DisplaySizes::DisplaySize1600mm;
  } else if(displaySize == "960mm") {
    DisplaySize = DisplaySizes::DisplaySize960mm;
  } else {
    DisplaySize = DisplaySizes::DisplaySize640mm;
  }

  // PDU ID
  String pduId = GlobalVariable["PDUID"];
  PduId = pduId.toInt();
  
  // Parameter Counts
  String paramCounts = GlobalVariable["CNTS"];
  TotalParametersToDisplay = paramCounts.toInt();
  if(PduId + TotalParametersToDisplay > 9) {
    TotalParametersToDisplay = 9 - PduId;
    // char str[4] = {0};
    // sprintf(str, "%02d", TotalParametersToDisplay);
    // GlobalVariable["CNTS"] = String(str);
  }

  // Message ID
  String msgId = GlobalVariable["MSGID"];
  MsgId = msgId.toInt();

  #if defined(DEBUG)
  Serial.printf("PDU ID: %d\nCNTS: %d\n", PduId, TotalParametersToDisplay);
  #endif
  
  // 1 <= PDUID >= 8 
  // 1 <= CNTS  >= 4 
  // 1 <= MSGID >= 8 
  if(!(PduId > 0 && PduId < 9) || 
      !(TotalParametersToDisplay > 0 && TotalParametersToDisplay < 5) || 
      !(MsgId > 0 && MsgId < 9)) {
    return false;
  }

  // Message
  String msg = GlobalVariable["MSG"];
  if(!msg.isEmpty() && msg != "null" && pduId == msgId) {
    Message = msg;
    Message.toUpperCase();
    HaveMessage = true;
    
    int messagewidth = 0;
    const char *ptr = Message.c_str();
    for (int i = 0; i < Message.length(); i++) {
	    messagewidth += CharWidth(ptr[i]) + 1;
    }

    if(DisplaySize == DisplaySizes::DisplaySize1600mm) {
      ScrollEnabled = (messagewidth > 159);
    } if(DisplaySize == DisplaySizes::DisplaySize960mm) {
      ScrollEnabled = (messagewidth > 95);
    } else {
      ScrollEnabled = (messagewidth > 31);
    }
    return true;
  }

  // Items
  HaveMessage = false;
  JsonArray params = GlobalVariable["PARAM"];
  int startIndex = (PduId - 1) * TotalParametersToDisplay;
  if(!msg.isEmpty() && msg != "null" && pduId > msgId) {
    // If message exist but message id 
    startIndex = startIndex - TotalParametersToDisplay;
  }

  for(uint8_t i = 0;i<TotalParametersToDisplay;i++) {
    String item = params[startIndex + i];
    Parameters[i] = GetItemDetail(item);
    #if defined(DEBUG)
    Serial.println(item);
    #endif
  }

  return true;
}

// Shows on Screen
void Show() {
  if(!IsValid) {
    return;
  }
  
  // Get the current time
  DateTime now = Rtc.now();  
  Screen->setBrightness(GetBrightnessFromHour(now.hour()));

  if(HaveMessage) {
    if(ScrollEnabled) {
      if(DisplaySize == DisplaySizes::DisplaySize1600mm) {
        Screen->drawMarquee(Message.c_str(), Message.length(), 159, 6);
      } if(DisplaySize == DisplaySizes::DisplaySize960mm) {
        Screen->drawMarquee(Message.c_str(), Message.length(), 95, 6);
      } else {
        Screen->drawMarquee(Message.c_str(), Message.length(), 31, 6);
      }
    } else {
      Screen->drawMarquee(Message.c_str(), Message.length(), 0, 6);
    }
  } else {
    PrintStringOnScreen(Parameters[0].EnglishFont);
    PrintNumberOnScreen(Parameters[0].Value.c_str());
  }
}

// Sync Data & Settings
void SyncDataAndSettings(bool sendToAll) {
  // Save to EEPROM
  String jsonString;
  serializeJson(GlobalVariable, jsonString);
  if(!WriteDataToEeprom(jsonString)) {
    #if defined(DEBUG)
    Serial.println("EEPROM Write Failed");
    #endif
  }
  
  if(sendToAll)  {
    // Send to all
    Rs485Send(jsonString.c_str(), jsonString.length());
    Rs485Send("\r\n", 2);
  }

  IsValid = UpdatePointers();
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

  Screen->clearScreen(true);
  String body = Server.arg("plain");

  if (body.length() > 750) {
    Server.send(413, "text/html", "Paylod too large");
    return;
  }

  JsonDocument json;
  deserializeJson(json, body);

  // Update Values
  GlobalVariable["Petrol"] = json["Petrol"];
  GlobalVariable["Diesel"] = json["Diesel"];
  GlobalVariable["Power"] = json["Power"];
  GlobalVariable["Auto_LPG"] = json["Auto_LPG"];
  GlobalVariable["TurboJet"] = json["TurboJet"];
  GlobalVariable["Power99"] = json["Power99"];
  GlobalVariable["CNG"] = json["CNG"];
  GlobalVariable["Power95"] = json["Power95"];
  GlobalVariable["MSG"] = json["MSG"];
  
  String dateString = json["date"];
  String timeString = json["time"];

  // Update date and time
  Rtc.adjust(DateTime(dateString.c_str(), timeString.c_str()));

  #if defined(DEBUG)
  // get the current time
  DateTime now = Rtc.now();
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(), 
            now.hour(), now.minute(), now.second());
  #endif

  Server.send(200, "application/json", "{\"status\":\"UPDATED SUCCESSFULLY\"}");

  SyncDataAndSettings(true);
  Show();
}

// http://192.168.1.1/get_price
void HandleGetPrices() {
  LoggedIn = false;

  JsonDocument json;
  json["PDUID"] = GlobalVariable["PDUID"];
  json["Petrol"] = GlobalVariable["Petrol"];
  json["Diesel"] = GlobalVariable["Diesel"];
  json["Power"] = GlobalVariable["Power"];
  json["Auto_LPG"] = GlobalVariable["Auto_LPG"];
  json["TurboJet"] = GlobalVariable["TurboJet"];
  json["Power99"] = GlobalVariable["Power99"];
  json["CNG"] = GlobalVariable["CNG"];
  json["Power95"] = GlobalVariable["Power95"];
  json["MSG"] = GlobalVariable["MSG"];

  String temp;
  serializeJson(json, temp);

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

  if(username == Username && password == Password) {
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

// http://192.168.1.1/reorder_parameters
void HandleReorderParamPage() {
  if(!LoggedIn) {
    Server.send(401, "text/html", "Unauthorized"); 
    return;
  }

  Server.send(200, "text/html", ReorderHtml);
}

// http://192.168.1.1/get_settings
void HandleGetSettings() {
  if(!LoggedIn) {
    Server.send(401, "text/html", "Unauthorized"); 
    return;
  }

  String id = GlobalVariable["PDUID"];
  String cnt = GlobalVariable["CNTS"];
  String sze = GlobalVariable["SIZE"];
  String ssid = GlobalVariable["SSID"];
  String user = GlobalVariable["USER"];
  String pswd = GlobalVariable["PSWD"];
  String pduId = "{\"PDUID\":\"" + id + "\",\"CNTS\":\"" + cnt + "\",\"SIZE\":\"" + sze + "\",\"SSID\":\"" + ssid + "\",\"USER\":\"" + user + "\",\"PSWD\":\"" + pswd + "\"}";
  Server.send(200, "application/json", pduId); 
}

// http://192.168.1.1/get_order
void HandleGetOrder() {
  if(!LoggedIn) {
    Server.send(401, "text/html", "Unauthorized"); 
    return;
  }

  JsonArray params = GlobalVariable["PARAM"];
  String p1 = params[0];
  String p2 = params[1];
  String p3 = params[2];
  String p4 = params[3];
  String p5 = params[4];
  String p6 = params[5];
  String p7 = params[6];
  String p8 = params[7];
  String str = "[\"" + p1 + "\",\"" + p2 + "\",\"" + p3 + "\",\"" + p4 + "\",\"" + p5 + "\",\"" + p6 + "\",\"" + p7 + "\",\"" + p8 + "\"]";

  Server.send(200, "application/json", str); 
}

// http://192.168.1.1/icon.png
void HandleIconPng() {
  Server.send_P(200, "image/png", IconPng, sizeof(IconPng)); 
}

// http://192.168.1.1/setting_login_page
void HandleLoginPage() {
  Server.send(200, "text/html", LoginHtml); 
}

// http://192.168.1.1/set_settings
void HandleSetSettings() {
  if(!LoggedIn) {
    Server.send(401, "text/html", "Unauthorized"); 
    return;
  }

  if (!Server.hasArg("plain")) {
    Server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  Screen->clearScreen(true);

  String body = Server.arg("plain");

  JsonDocument json;
  deserializeJson(json, body);

  GlobalVariable["PDUID"] = json["PDUID"];
  GlobalVariable["CNTS"] = json["CNTS"];
  GlobalVariable["SIZE"] = json["SIZE"];
  GlobalVariable["SSID"] = json["SSID"];
  GlobalVariable["USER"] = json["USER"];
  GlobalVariable["PSWD"] = json["PSWD"];

  Server.send(200, "application/json", "{\"status\":\"UPDATED SUCCESSFULLY\"}");

  SyncDataAndSettings(true);
  Show();
}

// http://192.168.1.1/set_order
void HandleSetOrder() {
  if(!LoggedIn) {
    Server.send(401, "text/html", "Unauthorized"); 
    return;
  }

  if (!Server.hasArg("plain")) {
    Server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }

  Screen->clearScreen(true);

  String body = Server.arg("plain");

  JsonDocument json;
  deserializeJson(json, body);

  JsonArray params = json["PARAM"];
  if(params[0] = "null") {
    Server.send(400, "application/json", "{\"error\":\"Missing body\"}");
    return;
  }
  
  GlobalVariable["PARAM"] = json["PARAM"];

  Server.send(200, "application/json", "{\"status\":\"UPDATED SUCCESSFULLY\"}");

  SyncDataAndSettings(true);
  Show();
}

void WebTask(void *pv) {
  #if defined(DEBUG)
  Serial.println("Web Task Started");
  #endif

  Rs485Init();
  
  WiFi.softAPConfig(ip, ip, netmask);
  WiFi.softAP(WifiSsid, "HISIGNrdu@123");
  
  Server.on("/", HTTP_GET, HandleRoot);
  Server.on("/get_price", HTTP_GET, HandleGetPrices);
  Server.on("/setting_login_page", HTTP_GET, HandleLoginPage);
  Server.on("/setting_page", HTTP_GET, HandleSettingPage);
  Server.on("/reorder_parameters", HTTP_GET, HandleReorderParamPage);
  Server.on("/get_settings", HTTP_GET, HandleGetSettings);
  Server.on("/get_order", HTTP_GET, HandleGetOrder);
  Server.on("/icon.png", HTTP_GET, HandleIconPng);
  Server.on("/set_price", HTTP_POST, HandleSetPrices);
  Server.on("/validate_auth", HTTP_POST, HandleValidateAuth);
  Server.on("/set_settings", HTTP_POST, HandleSetSettings);
  Server.on("/set_order", HTTP_POST, HandleSetOrder);
  Server.begin();

  // Initialise delay
  vTaskDelay(5000);
  
  // Update Font and price
  Screen->clearScreen(true);
  Show();

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
          Screen->clearScreen(true);

          // Update Values
          GlobalVariable["CNTS"] = json["CNTS"];
          GlobalVariable["MSGID"] = json["MSGID"];
          GlobalVariable["Petrol"] = json["Petrol"];
          GlobalVariable["Diesel"] = json["Diesel"];
          GlobalVariable["Power"] = json["Power"];
          GlobalVariable["Auto_LPG"] = json["Auto_LPG"];
          GlobalVariable["TurboJet"] = json["TurboJet"];
          GlobalVariable["Power99"] = json["Power99"];
          GlobalVariable["CNG"] = json["CNG"];
          GlobalVariable["Power95"] = json["Power95"];
          GlobalVariable["MSG"] = json["MSG"];
          GlobalVariable["PARAMS"] = json["PARAMS"];

          SyncDataAndSettings(false);
          Show();
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
    
    // Show
    if(IsValid) {
      if(HaveMessage) {
        // Show Message only
        if(ScrollEnabled & ledCounter % 3 == 0) {
          if(Screen->stepMarquee(-1,0)) {
            Screen->clearScreen(true);
            if(DisplaySize == DisplaySizes::DisplaySize1600mm) {
              Screen->drawMarquee(Message.c_str(), Message.length(), 159, 6);
            } if(DisplaySize == DisplaySizes::DisplaySize960mm) {
              Screen->drawMarquee(Message.c_str(), Message.length(), 95, 6);
            } else {
              Screen->drawMarquee(Message.c_str(), Message.length(), 31, 6);
            }
          }
        }
      } else {
        if(ledCounter % 500 == 0) {
          Screen->clearScreen(true);
          if(ledCounter % 1000 == 0) {
            ledCounter = 0;
            CurrentItemIndex = (CurrentItemIndex + 1) % TotalParametersToDisplay;
            PrintStringOnScreen(Parameters[CurrentItemIndex].EnglishFont);
          } else {
            PrintStringOnScreen(Parameters[CurrentItemIndex].HindiFont);        
          }

          // Show Value
          PrintNumberOnScreen(Parameters[CurrentItemIndex].Value.c_str());
        }
      }
    }
  }
}

void DmdTask(void *pv) { 
  #if defined(DEBUG)
  Serial.println("DMD Task Started");
  #endif 

  // Initialize Screen
  if(DisplaySize == DisplaySizes::DisplaySize1600mm) {
    Screen = new DMD(5, 2);
  } if(DisplaySize == DisplaySizes::DisplaySize960mm) {
    Screen = new DMD(3, 2);
  } else {
    Screen = new DMD(2, 2);
  }

  // Clear the DMD pixels held in RAM
  Screen->clearScreen( true );
  Screen->selectFont(Droid_Sans_24);

  // Print Logo for 5 seconds
  PrintLogoOnScreen(Logo);

  while(true) {
    Screen->scanDisplayBySPI();
    vTaskDelay(1);
  }
}

void setup(void) { 
  // Deinitialize Watchdog timer
  esp_task_wdt_deinit();

  #if defined(DEBUG)
  Serial.begin(115200);
  #endif

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT);

  EEPROM.begin(1024);

  while (!Rtc.begin()) {  // Initialize RTC communications
    #if defined(DEBUG)
    Serial.println(F("Unable to find MCP7940M. Checking again in 3s."));  // Show error text
    #endif
    delay(3000);                                                          // wait a second
  }  // of loop until device is located
  #if defined(DEBUG)
  Serial.println(F("MCP7940 initialized."));
  #endif
  while (!Rtc.deviceStatus()) {  // Turn oscillator on if necessary
    #if defined(DEBUG)
    Serial.println(F("Oscillator is off, turning it on."));
    #endif
    bool deviceStatus = Rtc.deviceStart();  // Start oscillator and return state
    if (!deviceStatus) {                        // If it didn't start
    #if defined(DEBUG)
      Serial.println(F("Oscillator did not start, trying again."));  // Show error and
    #endif
      // wait for a second
      delay(1000);      // of if-then oscillator didn't start
    }                
  } 
  // Rtc.adjust();
  #if defined(DEBUG)
  delay(3000);
  // get the current time
  DateTime now = Rtc.now();  
  Serial.printf("%04d-%02d-%02d %02d:%02d:%02d",
            now.year(), now.month(), now.day(), 
            now.hour(), now.minute(), now.second());
  #endif
  
  #if !defined(DEFAULT_CONFIG)
  String jsonString;
  if(ReadDataFromEeprom(jsonString)) {
    deserializeJson(GlobalVariable, jsonString);
  } else {
  #endif
    // Default
    #if defined(DEBUG)
    Serial.println("Default Values loaded");
    #endif

    GlobalVariable["PDUID"] = "01";
    GlobalVariable["CNTS"] = "01";
    GlobalVariable["MSGID"] = "01";
    GlobalVariable["Petrol"] = "000.00";
    GlobalVariable["Diesel"] = "000.00";
    GlobalVariable["Power"] = "000.00";
    GlobalVariable["Auto_LPG"] = "000.00";
    GlobalVariable["TurboJet"] = "000.00";
    GlobalVariable["Power99"] = "000.00";
    GlobalVariable["CNG"] = "000.00";
    GlobalVariable["Power95"] = "000.00";
    GlobalVariable["MSG"] = "";
    GlobalVariable["SSID"] = "RDUHisign";
    GlobalVariable["USER"] = "admin";
    GlobalVariable["PSWD"] = "admin@123";
    GlobalVariable["SIZE"] = "960mm";

    JsonArray paramList = GlobalVariable.createNestedArray("PARAM");
    paramList.add("Petrol");
    paramList.add("Diesel");
    paramList.add("Power");
    paramList.add("Auto_LPG");
    paramList.add("TurboJet");
    paramList.add("Power99");
    paramList.add("CNG");
    paramList.add("Power95");
  #if !defined(DEFAULT_CONFIG)
  }
  #endif
  
  // Update pointers
  IsValid = UpdatePointers();

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
