# 32 x 96 pixels P10 display
ESP32-WROOM-32E MCU based display controller. It has the following components:
* DMD C++ library to control P10 display over SPI lines
* Internal Wifi in AP mode
* Internal EEPROM
* MCP7940 RTC
* RS485 Communication
* Web Server
* Web Pages
* Hindi & English Font Library

# How does it work?
* First, Power up the Unit.
* Display shows Logo on screen for 5 seconds.
* After that Display shows stored Price in English & Hindi on the behalf of PDUID.
* If memory has message, then Display scrolls the message only.
* User can Connect the Wifi of Unit with the name of "HisignDisplay" and the password is "HisignWiFi". 
* User can search "192.168.1.1" on his/her browser.
* User will see the page from where user can see and update PDUID, Message and prices.
* User can go to setting by clicking on "Settings" button.
* The Username is "admin" and the password is "password".
* User can update the PDUID by login to the settings.
* The changes will reflect on the display instantly.