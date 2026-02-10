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
* Display shows Logo on screen for 3 seconds.
* After that Display shows stored Price in English & Hindi on the behalf of PDUID.
* If memory has message and device PDUID is "01", then Display shows message only. If message size is more than 1600 mm then it scrolls the message only.
* User can Connect the Wifi of Unit with the name of "RDUHisign" and the password is "HISIGNrdu@123". 
* User can search "192.168.1.1" on his/her browser.
* User will see the page where he/she can see and update PDUID, Message and prices.
* Admin can go to setting by clicking on "Settings" button.
* The default Username is "admin" and the default password is "admin@123".
* Admin can update the PDUID, Parameter Counts, Display Size, Wifi SSID, Admin username and password by login to the settings.
* The changes will reflect on the display after reset.