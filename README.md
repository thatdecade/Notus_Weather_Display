# Notus Weather Display
Goal of this project is to recreate the forecasted weather inside the chamber.  Using Wunderground's API to download weather forecasts.  If the forecast is for a warm temperature, shine a warm color.  If it is raining, make it rain in the chamber.  Fog, Lightning, Sunlight, and so on.

Winner of the SeeedStudio Best Wio Project Idea Contest - 
http://www.seeed.cc/%5BActivity%5D-Best-Wio-Project-Idea-Contest-t-5486.html

This project is losely based on [opentempescope](https://github.com/kenkawakenkenke/tempescope) which uses bluetooth and a smartphone app to get weather updates.  [Hardware build](https://github.com/kenkawakenkenke/tempescope/wiki/1.-Building-OpenTempescope) instructions are similar, but I wrote my software from scratch to run on a ESP8266 controller. 

Basic Hardware List (misc circuits not listed)
- ESP8266
- Water Pump
- Water Ultrasonic Atomizer (humidifier)
- DC Fan
- NeoPixel Ring, cool white
- 2 liter bottles (creates lower basin and tornado chamber)

Libraries:
- [Arduino core for ESP8266 WiFi chip](https://github.com/esp8266/Arduino)
- [ESP8266 Connect To WiFi](https://github.com/kentaylor/WiFiManager)
- [ESP8266 Weather Station](https://github.com/squix78/esp8266-weather-station)
- [Adafruit NeoPixel Library](https://github.com/adafruit/Adafruit_NeoPixel)

Video Demo: https://www.facebook.com/westabyd/videos/776901591346/

You must change the wunderground API key, WUNDERGRROUND_API_KEY, to your own number.
https://www.wunderground.com/weather/api/

**Usage:**
On first boot, a wifi AP is created.  Join with your smartphone and fill out your wifi connection settings along with your zip code and timezone.
The web settings can be re-entered by holding the config button.

Debugged:
- Wifi Connect
- Weather Parse
- Temp -> LED Color
- Lightning Effects
- Wifi AP for initial wireless config
  - SSID, Password, Location, Timezone
- Press config button to redo wireless config
- Time sensitive forcast (today's forcast is in the past at night)

Issues:
None

Todo:
- Atomizer
- Water Pump
- Fan
- Elavated forcast, reads tonight and tomorrow, displays worse of the two
- Demo Mode (TSTORM while cycles through temperture colors)
- Party Mode (Fog with sound reactive lightning, black alt color)
  - Set weather API key
- Sound Sensitive wake-up from display off
- Convert lighting effects to not use delays
