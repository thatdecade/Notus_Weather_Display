/*
   NOTUS - The Wunderground Powered Physical Weather Display.

   Basic Hardware List (misc circuits not listed)
   - ESP8266
   - Water Pump
   - Water Ultrasonic Atomizer (humidifier)
   - DC Fan
   - NeoPixel Ring, cool white
   - 2 liter bottles (creates lower basin and tornado chamber)

   To make work, you must enter a wunderground API key below.
   https://www.wunderground.com/weather/api/
   
   On first boot, a wifi AP is created.  Join with your smartphone and fill out 
   your wifi connection settings.

   Debugged
    Wifi Connect
    Weather Parse
    Temp -> LED Color
    Lightning Effects
    Wifi AP for initial wireless config
    - SSID, Password, Location, Timezone
    Press config button to redo wireless config
    Time sensitive forcast (today's forcast is in the past at night)

   Issues:

   Todo:
    Atomizer
    Water Pump
    Fan
    Elavated forcast, reads tonight and tomorrow, displays worse of the two
    Demo Mode (TSTORM while cycles through temperture colors)
    Party Mode (Fog with sound reactive lightning, black alt color)
    - Set weather API key
    Sound Sensitive wake-up from display off
    Convert lighting effects to not use delays

   Warning: Watchdog will reset software if any process takes longer than 1 second (delay function resets watchdog timer)

*/


#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino

//needed for library
#include <ESP8266WebServer.h>
#include <DNSServer.h>
#include "WiFiManager.h"          //https://github.com/kentaylor/WiFiManager

#include <Adafruit_NeoPixel.h>    //https://github.com/adafruit/Adafruit_NeoPixel

#include <EEPROM.h>

#include "WundergroundClient.h"   //https://github.com/squix78/esp8266-weather-station
#include "TimeClient.h"
#include <Ticker.h>

/* Animation Settings */
#define FADE_INTERVAL 30 //duration of fade pulse of forcasted temperature color
#define FADE_DELAY 15   //duration between pulses (38 = 15 seconds)

/* EEPROM Data Store */
char eeprom_compatibility = 33; //this number MUST be changed when changing the eeprom data structure
char weather_location_string[40];
float utc_global_float = -5;
int eeprom_address_compatibility = 0;
int eeprom_address_weather_location = 1;
int eeprom_address_utc_offset_string = 41;


/* ****************** */
/* Pin Address Setup  */
/* ****************** */
const int FAN_PIN = 13;
const int FAN_ON = 1;
const int FAN_OFF = 0;

const int MIST_PIN = 14;
const int MIST_ON = 1;
const int MIST_OFF = 0;

const int BLUE_LED_PIN = 2; /* This is also the peripheral power enable */
#define LED_ON  LOW
#define LED_OFF HIGH

const int PERIPH_POWER_PIN = 15;
const int PERIPH_OFF = 0;
const int PERIPH_ON = 1;

const int CONFIG_PIN = 0; /*Trigger for inititating config mode  */

/* ****************** */
/* Other Variables    */
/* ****************** */

// Indicates whether ESP has WiFi credentials saved from previous session
bool initialConfig = false;
bool weatherUpdateTemp = false;

/* Weather and Time Servers */
TimeClient timeClient(0);
Ticker ticker_weather_update;
const int UPDATE_WEATHER_INTERVAL_SECS = 30 * 60; // Update every 30 minutes
const String WUNDERGRROUND_API_KEY = "00000000000000000000000";          //https://www.wunderground.com/weather/api/
bool readyForWeatherUpdate = true;
const boolean IS_METRIC = false;
WundergroundClient wunderground(IS_METRIC);


/* ****************** */
/* Neo Pixel Setup    */
/* ****************** */
#define PIN 12
#define NUM_LEDS 12
int brightness = 50;

//Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRBW + NEO_KHZ800);


// Pattern types supported:
enum  pattern { NONE, RAINBOW_CYCLE, THEATER_CHASE, COLOR_WIPE, SCANNER, FADE, PATTERN_DELAY };
// Patern directions supported:
enum  direction { FORWARD, REVERSE };

// NeoPattern Class - derived from the Adafruit_NeoPixel class
class NeoPatterns : public Adafruit_NeoPixel
{
  public:

    // Member Variables:
    pattern  ActivePattern;  // which pattern is running
    direction Direction;     // direction to run the pattern

    unsigned long Interval;   // milliseconds between updates
    unsigned long lastUpdate; // last update of position

    uint32_t Color1, Color2;  // What colors are in use
    uint16_t TotalSteps;  // total number of steps in the pattern
    uint16_t Index;  // current step within the pattern

    void (*OnComplete)();  // Callback on completion of pattern

    // Constructor - calls base-class constructor to initialize strip
    NeoPatterns(uint16_t pixels, uint8_t pin, uint8_t type, void (*callback)())
      : Adafruit_NeoPixel(pixels, pin, type)
    {
      OnComplete = callback;
    }

    // Update the pattern
    void Update()
    {
      if ((millis() - lastUpdate) > Interval) // time to update
      {
        lastUpdate = millis();
        switch (ActivePattern)
        {
          case FADE:
            FadeUpdate();
            break;
          case PATTERN_DELAY:
            Increment();
          default:
            break;
        }
      }
    }

    // Increment the Index and reset at the end
    void Increment()
    {
      /*
        Serial.print("Index: ");
        Serial.print(Index);
        Serial.print(" TotalSteps: ");
        Serial.println(TotalSteps);
      */

      if (Direction == FORWARD)
      {
        Index++;
        if (Index >= TotalSteps)
        {
          Index = 0;
          if (OnComplete != NULL)
          {
            OnComplete(); // call the comlpetion callback
          }
        }
      }
      else // Direction == REVERSE
      {
        --Index;
        if (Index <= 0)
        {
          Index = TotalSteps - 1;
          if (OnComplete != NULL)
          {
            OnComplete(); // call the comlpetion callback
          }
        }
      }
    }

    // Reverse pattern direction
    void Reverse()
    {
      if (Direction == FORWARD)
      {
        Direction = REVERSE;
        Index = TotalSteps - 1;
      }
      else
      {
        Direction = FORWARD;
        Index = 0;
      }
    }

    // Initialize for a Delay
    void SetDelay(uint16_t counts, uint8_t interval) //multiply to find delay in ms
    {
      ActivePattern = PATTERN_DELAY;
      TotalSteps = counts;
      Interval = interval;
      Direction = FORWARD;
      Index = 0;
    }

    // Initialize for a Fade
    void Fade(uint32_t color1, uint32_t color2, uint16_t steps, uint8_t interval, direction dir = FORWARD)
    {
      ActivePattern = FADE;
      Interval = interval;
      TotalSteps = steps;
      Color1 = color1;
      Color2 = color2;
      Index = 0;
      Direction = dir;
    }

    // Update the Fade Pattern
    void FadeUpdate()
    {
      // Calculate linear interpolation between Color1 and Color2
      // Optimise order of operations to minimize truncation error
      uint8_t red = ((Red(Color1) * (TotalSteps - Index)) + (Red(Color2) * Index)) / TotalSteps;
      uint8_t green = ((Green(Color1) * (TotalSteps - Index)) + (Green(Color2) * Index)) / TotalSteps;
      uint8_t blue = ((Blue(Color1) * (TotalSteps - Index)) + (Blue(Color2) * Index)) / TotalSteps;

      ColorSet(Color(red, green, blue));
      show();
      Increment();
    }

    // Calculate 50% dimmed version of a color (used by ScannerUpdate)
    uint32_t DimColor(uint32_t color)
    {
      // Shift R, G and B components one bit to the right
      uint32_t dimColor = Color(Red(color) >> 1, Green(color) >> 1, Blue(color) >> 1);
      return dimColor;
    }

    // Set all pixels to a color (synchronously)
    void ColorSet(uint32_t color)
    {
      for (int i = 0; i < numPixels(); i++)
      {
        setPixelColor(i, color);
      }
      show();
    }

    // Returns the Red component of a 32-bit color
    uint8_t Red(uint32_t color)
    {
      return (color >> 16) & 0xFF;
    }

    // Returns the Green component of a 32-bit color
    uint8_t Green(uint32_t color)
    {
      return (color >> 8) & 0xFF;
    }

    // Returns the Blue component of a 32-bit color
    uint8_t Blue(uint32_t color)
    {
      return color & 0xFF;
    }

    // Input a value 0 to 255 to get a color value.
    // The colours are a transition r - g - b - back to r.
    uint32_t Wheel(byte WheelPos)
    {
      WheelPos = 255 - WheelPos;
      if (WheelPos < 85)
      {
        return Color(255 - WheelPos * 3, 0, WheelPos * 3);
      }
      else if (WheelPos < 170)
      {
        WheelPos -= 85;
        return Color(0, WheelPos * 3, 255 - WheelPos * 3);
      }
      else
      {
        WheelPos -= 170;
        return Color(WheelPos * 3, 255 - WheelPos * 3, 0);
      }
    }
};


int gamma_here[] = {
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,
  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  0,  1,  1,  1,  1,
  1,  1,  1,  1,  1,  1,  1,  1,  1,  2,  2,  2,  2,  2,  2,  2,
  2,  3,  3,  3,  3,  3,  3,  3,  4,  4,  4,  4,  4,  5,  5,  5,
  5,  6,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  9,  9,  9, 10,
  10, 10, 11, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
  17, 17, 18, 18, 19, 19, 20, 20, 21, 21, 22, 22, 23, 24, 24, 25,
  25, 26, 27, 27, 28, 29, 29, 30, 31, 32, 32, 33, 34, 35, 35, 36,
  37, 38, 39, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 50,
  51, 52, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 66, 67, 68,
  69, 70, 72, 73, 74, 75, 77, 78, 79, 81, 82, 83, 85, 86, 87, 89,
  90, 92, 93, 95, 96, 98, 99, 101, 102, 104, 105, 107, 109, 110, 112, 114,
  115, 117, 119, 120, 122, 124, 126, 127, 129, 131, 133, 135, 137, 138, 140, 142,
  144, 146, 148, 150, 152, 154, 156, 158, 160, 162, 164, 167, 169, 171, 173, 175,
  177, 180, 182, 184, 186, 189, 191, 193, 196, 198, 200, 203, 205, 208, 210, 213,
  215, 218, 220, 223, 225, 228, 231, 233, 236, 239, 241, 244, 247, 249, 252, 255
};

enum modes_e
{
  STARTUP,
  DEMO,
  NORMAL,
  PARTY,
};

modes_e sys_mode = STARTUP;

enum weather_e
{
  RAIN,
  SUNNY,
  FOG,
  TSTORM,
  CLOUDY,
};

weather_e current_weather = SUNNY;
weather_e forcast_weather = SUNNY;

enum period_e
{
  TODAY = 0,
  TONIGHT = 1,
  TOMORROW = 2,
  TOMORROW_NIGHT = 3,
  NEXT_DAY = 4,
  NEXT_DAY_NIGHT = 5,
  DISPLAY_OFF = 6,
};


/* Function Prototypes */
void check_for_config_button();
void setup_wifi();

void solidColor(uint32_t c, uint8_t wait);
void constant_lightning();
void rolling();
void thunderburst();
void crack();
void reset();

void EEPROMWritelong(int address, long value);
float EEPROMReadDouble(int p_address);
void load_config_from_eeprom();

void setReadyForWeatherUpdate();
int update_weather();

void update_light_for_temperature();
period_e determine_time_and_brightness();
uint32 convert_temp_to_color(int temperature);
void Light_RingComplete();

weather_e sort_forcast(String iconfound);
void update_animation();


NeoPatterns Light_Ring(12, 12, NEO_GRBW + NEO_KHZ800, &Light_RingComplete);


/* Main Code */
void setup() {

  pinMode(BLUE_LED_PIN, OUTPUT);
  digitalWrite(BLUE_LED_PIN, LED_ON);

  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, FAN_OFF);

  pinMode(MIST_PIN, OUTPUT);
  digitalWrite(MIST_PIN, MIST_OFF);

  Serial.begin(115200);
  Serial.println("\n");

  //LED Strip Setup
  //  strip.setBrightness(20);
  //  strip.begin();
  //  strip.show(); // Initialize all pixels to 'off'
  Light_Ring.begin();

  /* Turn on the peripheral Power Bus */
  pinMode(PERIPH_POWER_PIN, OUTPUT);
  digitalWrite(PERIPH_POWER_PIN, PERIPH_ON);

  //brief light test
  //  constant_lightning();
  //  solidColor(Light_Ring.Color(254, 0, 0, gamma_here[255]), 500); //Pink
  //  solidColor(Light_Ring.Color(0, 0, 0, gamma_here[0]), 0); //Black

  load_config_from_eeprom();

  setup_wifi();

  update_weather();

  ticker_weather_update.attach(UPDATE_WEATHER_INTERVAL_SECS, setReadyForWeatherUpdate);

}


void loop() {
  delay(1); //pets watchdog

  //config button pressed = send to wifi portal
  check_for_config_button();

  //runs every 30 mins
  if( update_weather() )
  {
    update_animation();
  }

  //performs the requested animation
  Light_Ring.Update();

//   constant_lightning();
  digitalWrite(FAN_PIN, FAN_ON);

}

void update_animation()
{
    Serial.print("Current Weather: ");
    switch (current_weather)
    {
      case RAIN: Serial.println("RAIN"); break;
      case SUNNY: Serial.println("SUNNY"); break;
      case FOG: Serial.println("FOG"); break;
      case TSTORM: Serial.println("TSTORM"); break;
      case CLOUDY: Serial.println("CLOUDY"); break;
      default: Serial.println("UNKNOWN"); break;
    }

    Serial.print("Forcast Weather: ");
    switch (forcast_weather)
    {
      case RAIN: Serial.println("RAIN"); break;
      case SUNNY: Serial.println("SUNNY"); break;
      case FOG: Serial.println("FOG"); break;
      case TSTORM: Serial.println("TSTORM"); break;
      case CLOUDY: Serial.println("CLOUDY"); break;
      default: Serial.println("UNKNOWN"); break;
    }

}







void setup_wifi()
{
  Serial.println("\n Starting");
  WiFi.printDiag(Serial); //Remove this line if you do not want to see WiFi password printed
  if (WiFi.SSID() == "") {
    Serial.println("We haven't got any access point credentials, so get them now");
    initialConfig = true;
  }
  else {
    digitalWrite(BLUE_LED_PIN, LED_OFF); // Turn led off as we are not in configuration mode.
    WiFi.mode(WIFI_STA); // Force to station mode because if device was switched off while in access point mode it will start up next time in access point mode.
    unsigned long startedAt = millis();
    Serial.print("After waiting ");
    int connRes = WiFi.waitForConnectResult();
    float waited = (millis() - startedAt);
    Serial.print(waited / 1000);
    Serial.print(" secs in setup() connection result is ");
    Serial.println(connRes);
  }

  pinMode(CONFIG_PIN, INPUT_PULLUP); //TBD: May not be needed

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("failed to connect, finishing setup anyway");
  } else {
    Serial.print("local ip: ");
    Serial.println(WiFi.localIP());
  }

}

void check_for_config_button()
{

  float utc_local_float = -5;
  String utc_local_string = "-5";
  long utc_storage;
  char utc_local_char_array[3];

  if ((digitalRead(CONFIG_PIN) == LOW) || (initialConfig)) {
    Serial.println("Configuration portal requested.");
    digitalWrite(BLUE_LED_PIN, LED_ON);
    digitalWrite(PERIPH_POWER_PIN, PERIPH_OFF); //turn peripherals off

    WiFiManagerParameter custom_zipcode("server", "Location or Zip Code", weather_location_string, 40);

    String utc_string = String(utc_global_float, 0);
    utc_local_char_array[0] = utc_string[0];
    utc_local_char_array[1] = utc_string[1];
    utc_local_char_array[2] = utc_string[2];

    WiFiManagerParameter custom_utc_offset("offset", "Time Zone as UTC Offset", utc_local_char_array, 3);

    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //sets timeout in seconds until configuration portal gets turned off. (600 = 10 mins)
    wifiManager.setConfigPortalTimeout(600);

    //ask for zip code and timezone
    wifiManager.addParameter(&custom_zipcode);
    wifiManager.addParameter(&custom_utc_offset);

    //it starts an access point
    //and goes into a blocking loop awaiting configuration
    if (!wifiManager.startConfigPortal("Notus Weather Display")) {
      Serial.println("Not connected to WiFi but continuing anyway.");
    } else {
      //if you get here you have connected to the WiFi
      Serial.println("connected...yay :)");

    }


    //update zipcode
    strcpy(weather_location_string, custom_zipcode.getValue());

    strcpy(utc_local_char_array, custom_utc_offset.getValue());
    Serial.println(utc_local_char_array);

    utc_local_string[0] = utc_local_char_array[0];

    if (utc_local_string[0] == 45) //check for - symbol
    {
      Serial.println("Negative Number");
      //Serial.println(utc_local_char_array[1] - 48); //convert to int
      utc_local_float = utc_local_char_array[1] - 48;
      utc_local_float = utc_local_float * -1;
      Serial.println(utc_local_float);
    }
    else
    {
      Serial.println(utc_local_char_array[0] - 48); //convert to int
      utc_local_float = utc_local_char_array[0] - 48;
    }



    //set new offset
    //      timeClient.setOffset(utc_local_float);

    //prepare offset for memory save
    utc_global_float = utc_local_float;
    utc_storage = utc_local_float * 100;

    //save values to eeprom
    Serial.println("Writing to EEPROM....");
    Serial.print("TimeZone Offset (x100): ");
    Serial.print(utc_storage);
    Serial.print(" to address: ");
    Serial.println(eeprom_address_utc_offset_string);
    Serial.print("Weather Location: ");
    Serial.print(weather_location_string);
    Serial.print(" to address: ");
    Serial.println(eeprom_address_weather_location);
    Serial.print("EEPROM Compatibility: ");
    Serial.print(eeprom_compatibility);
    Serial.print(" to address: ");
    Serial.println(eeprom_address_compatibility);

    for (int i = 0; i < 40; i++)
    {
      EEPROM.write(eeprom_address_weather_location + i, weather_location_string[i]);
    }
    EEPROM.write(eeprom_address_compatibility, eeprom_compatibility);
    EEPROMWritelong(eeprom_address_utc_offset_string, utc_storage);
    EEPROM.commit();

    digitalWrite(BLUE_LED_PIN, LED_OFF); // Turn led off
    delay(1000);
    Serial.println("Resetting Device");
    delay(1000);

    ESP.reset(); // This is a bit crude. For some unknown reason webserver can only be started once per boot up
    // so resetting the device allows to go back into config mode again when it reboots.

    delay(5000);
  }

}

void constant_lightning() {
  
  Light_Ring.setBrightness(100);
  
  switch (random(1, 3)) {
    case 1:
      thunderburst();
      delay(random(100, 5000));
      Serial.println("Thunderburst");
      break;

    case 2:
      rolling();
      Serial.println("Rolling");
      delay(random(500, 2000));
      break;

    case 3:
      crack();
      Serial.println("Crack");
      delay(random(500, 2500));
      break;
  }
}

void rolling() {
  // a simple method where we go through every LED with 1/10 chance
  // of being turned on, up to 10 times, with a random delay wbetween each time
  for (int r = 0; r < random(5, 20); r++) {
    //iterate through every LED
    for (int i = 0; i < NUM_LEDS; i++) {
      if (random(0, 100) > 90) {
        Light_Ring.setPixelColor(i, Light_Ring.Color(0, 0, 255, gamma_here[200] ) ); //white blue

      }
      else {
        //dont need reset as we're blacking out other LEDs her
        Light_Ring.setPixelColor(i, Light_Ring.Color(0, 0, 0, gamma_here[0] ) ); //black
      }
    }
    Light_Ring.show();
    delay(random(5, 100));
    reset();

  }
}

void thunderburst() {

  // this thunder works by lighting two random lengths
  // of the strand from 10-20 pixels.
  int rs1 = random(0, NUM_LEDS / 2);
  int rl1 = random(10, 20);
  int rs2 = random(rs1 + rl1, NUM_LEDS);
  int rl2 = random(10, 20);

  //repeat this chosen strands a few times, adds a bit of realism
  for (int r = 0; r < random(3, 6); r++) {

    for (int i = 0; i < rl1; i++) {
      //leds[i+rs1] = CHSV( 0, 0, 255);
      Light_Ring.setPixelColor(i + rs1, Light_Ring.Color(0, 0, 255, gamma_here[200] ) ); //white blue
    }

    if (rs2 + rl2 < NUM_LEDS) {
      for (int i = 0; i < rl2; i++) {
        //leds[i+rs2] = CHSV( 0, 0, 255);
        Light_Ring.setPixelColor(i + rs2, Light_Ring.Color(0, 0, 255, gamma_here[200] ) ); //white blue
      }
    }

    Light_Ring.show();
    //stay illuminated for a set time
    delay(random(10, 50));

    reset();
    delay(random(10, 50));
  }

}

void crack() {
  //turn everything white briefly

  Light_Ring.ColorSet(Light_Ring.Color(0, 0, 255, gamma_here[200])); //black
  delay(random(10, 100));
  Light_Ring.ColorSet(Light_Ring.Color(0, 0, 0, gamma_here[0])); //black

}

void reset()
{
  //  solidColor(Light_Ring.Color(0, 0, 0, gamma_here[0]), 0); //Black
  Light_Ring.ColorSet(Light_Ring.Color(0, 0, 0, gamma_here[0])); //black
}


// Fill the dots one after the other with a color
void solidColor(uint32_t c, uint8_t wait) {

  Light_Ring.setBrightness(brightness);
  Light_Ring.ColorSet(c);

  delay(wait);
}


void load_config_from_eeprom()
{

  Serial.println("Loading Values for Storage: ");
  EEPROM.begin(512);

  Serial.print("EEPROM Compatibility: ");
  Serial.println(EEPROM.read(eeprom_address_compatibility));
  if (EEPROM.read(eeprom_address_compatibility) != eeprom_compatibility)
  {
    Serial.println("EEPROM Corrupt, Sending to Wizard to overwrite");
    initialConfig = true;
  }
  else
  {
    //eeprom ok, overwrite defaults with read values

    for (int i = 0; i < 40; i++)
    {
      weather_location_string[i] = EEPROM.read(eeprom_address_weather_location + i);
      //Serial.print(eeprom_address_weather_location + i);
      //Serial.print("\t");
      //Serial.print(weather_location_string[i]);
      //Serial.println();
    }

    weather_location_string[40] = 0;
    Serial.print("Weather location from Memory: ");
    Serial.println(weather_location_string);

    //long utc_offset = EEPROM.read(eeprom_address_utc_offset_string);
    float utc_offset = (float)EEPROMReadDouble(eeprom_address_utc_offset_string) / 100;

    Serial.print("UTC Offset from Memory: ");
    //Serial.println(utc_offset);

    //set new offset
    utc_global_float = utc_offset;
    //    timeClient.setOffset(utc_offset);

    //Serial.print("Float to String Convert: ");
    Serial.println(String(utc_global_float, 0));

    //set new offset
    timeClient.setOffset(utc_global_float);

  }

  Serial.println("");
}

void EEPROMWritelong(int address, long value)
{
  //Decomposition from a long to 4 bytes by using bitshift.
  //One = Most significant -> Four = Least significant byte
  byte four = (value & 0xFF);
  byte three = ((value >> 8) & 0xFF);
  byte two = ((value >> 16) & 0xFF);
  byte one = ((value >> 24) & 0xFF);

  //Write the 4 bytes into the eeprom memory.
  EEPROM.write(address, four);
  EEPROM.write(address + 1, three);
  EEPROM.write(address + 2, two);
  EEPROM.write(address + 3, one);
}

float EEPROMReadDouble(int p_address)
{
  byte Byte1 = EEPROM.read(p_address);
  byte Byte2 = EEPROM.read(p_address + 1);
  byte Byte3 = EEPROM.read(p_address + 2);
  byte Byte4 = EEPROM.read(p_address + 3);

  long firstTwoBytes = ((Byte1 << 0) & 0xFF) + ((Byte2 << 8) & 0xFF00);
  long secondTwoBytes = (((Byte3 << 0) & 0xFF) + ((Byte4 << 8) & 0xFF00));
  secondTwoBytes *= 65536; // multiply by 2 to power 16 - bit shift 24 to the left

  return (firstTwoBytes + secondTwoBytes);
}


void setReadyForWeatherUpdate() {
  //this interrupt is called by ticker

  //Serial.println("Setting readyForUpdate to true");
  readyForWeatherUpdate = true;
}


int update_weather()
{
  int return_val = 0;
  
  if (readyForWeatherUpdate)
  {
    return_val = 1;

    // wait for WiFi connection
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Waiting for Wifi Connection");
      delay(1000);
    }
    else
    {
      Serial.println("Updating time...");
      timeClient.updateTime();

      Serial.print("Updating conditions for ");
      Serial.println(weather_location_string);
      wunderground.updateConditionsFromZipCode(WUNDERGRROUND_API_KEY, weather_location_string);
      Serial.println("Updating forecasts...");
      wunderground.updateForecastFromZipCode(WUNDERGRROUND_API_KEY, weather_location_string);

      readyForWeatherUpdate = false;
      weatherUpdateTemp = true; //tells the lights to print temps to serial
      delay(100);

      Serial.print("Date & Time: ");
      Serial.print(wunderground.getDate());
      Serial.print(" ");
      Serial.println(timeClient.getFormattedTime());
      Serial.print("Current Weather: ");
      Serial.println(wunderground.getWeatherText());
      Serial.print("Current Temp: ");
      Serial.println(wunderground.getCurrentTemp());
      Serial.print("Icon1: ");
      Serial.println(wunderground.getTodayIcon());

      Serial.println("");

      for (int i = 0; i < 5; i++)
      {
        if (i % 2 == 0) //daytime prediction contains high lows including night
        {
          Serial.print(wunderground.getForecastTitle(i) + " forcasted Low Temp for ");
          Serial.print(wunderground.getForecastLowTemp(i));
          Serial.print(", Forcasted High Temp: ");
          Serial.println(wunderground.getForecastHighTemp(i));
        }

        Serial.print(wunderground.getForecastTitle(i));
        Serial.print(" prediction is: ");
        Serial.println(wunderground.getForecastIcon(i));
      }

    }//end wifi connected

    //update lights for temperature->color and time->brightness
    update_light_for_temperature();

  }

  return return_val;
}

uint32 convert_temp_to_color(int temperature)
{
  uint32 color_return;
  const int brightess_div = 2;

  if (temperature <= 30)
  {
    //Color clamped to blue
    if (weatherUpdateTemp) {
      Serial.println("Temp = Blue Min");
    }
    color_return = (Light_Ring.Color(0,
                                     0,
                                     255,
                                     gamma_here[0])); //Blue Hue
  }
  else if (temperature <= 50)
  {
    //Blue Hue
    if (weatherUpdateTemp) {
      Serial.println("Temp = Blue Hue");
    }
    color_return = (Light_Ring.Color(map(temperature, 30, 50, 0,   112),
                                     map(temperature, 30, 50, 0,   48),
                                     map(temperature, 30, 50, 255, 160),
                                     gamma_here[brightness / brightess_div]));
  }
  else if (temperature <= 60)
  {
    //white hue
    if (weatherUpdateTemp) {
      Serial.println("Temp = White Hue");
    }
    color_return = (Light_Ring.Color(map(temperature, 50, 60, 112, 255),
                                     map(temperature, 50, 60, 48,  255),
                                     map(temperature, 50, 60, 160, 255),
                                     gamma_here[brightness / brightess_div]));
  }
  else if (temperature <= 70)
  {
    //yellow hue
    if (weatherUpdateTemp) {
      Serial.println("Temp = Yellow Hue");
    }
    color_return = (Light_Ring.Color(255,
                                     255,
                                     map(temperature, 60, 70, 255, 0),
                                     gamma_here[brightness / brightess_div]));
  }
  else if (temperature <= 80)
  {
    //orange hue
    if (weatherUpdateTemp) {
      Serial.println("Temp = Orange Hue");
    }
    color_return = (Light_Ring.Color(255,
                                     map(temperature, 70, 80, 255, 192),
                                     0,
                                     gamma_here[brightness / brightess_div]));
  }
  else if (temperature < 100)
  {
    //red hue
    if (weatherUpdateTemp) {
      Serial.println("Temp = Red Hue");
    }
    color_return = (Light_Ring.Color(255,
                                     map(temperature, 80, 100, 192, 2),
                                     0,
                                     gamma_here[brightness / brightess_div]));
  }
  else //temperature > 100)
  {
    //lock color to red above 100
    if (weatherUpdateTemp) {
      Serial.println("Temp = Red Max");
    }
    color_return = (Light_Ring.Color(255, 0, 0, gamma_here[0]));
  }
}

void update_light_for_temperature()
{
  period_e time_interval = determine_time_and_brightness();
  int current_temp, forcast_temp;
  uint32 current_temp_color, forcast_temp_color;

  //displays a clean curve of temps -> colors

  /* Repeat forcast for selected interval */
  if (weatherUpdateTemp) {
    Serial.println("");
  }
  if (weatherUpdateTemp) {
    Serial.println("Updating Light Color based on forcast interval");
  }
  if (weatherUpdateTemp) {
    Serial.println(wunderground.getForecastTitle(time_interval));
  }


  if (weatherUpdateTemp) {
    current_weather = sort_forcast(wunderground.getTodayIcon());
    forcast_weather = sort_forcast(wunderground.getForecastIcon(time_interval));

    Serial.print("Current Weather: ");
    switch (current_weather)
    {
      case RAIN: Serial.println("RAIN"); break;
      case SUNNY: Serial.println("SUNNY"); break;
      case FOG: Serial.println("FOG"); break;
      case TSTORM: Serial.println("TSTORM"); break;
      case CLOUDY: Serial.println("CLOUDY"); break;
      default: Serial.println("UNKNOWN"); break;
    }

    Serial.print("Forcast Weather: ");
    switch (forcast_weather)
    {
      case RAIN: Serial.println("RAIN"); break;
      case SUNNY: Serial.println("SUNNY"); break;
      case FOG: Serial.println("FOG"); break;
      case TSTORM: Serial.println("TSTORM"); break;
      case CLOUDY: Serial.println("CLOUDY"); break;
      default: Serial.println("UNKNOWN"); break;
    }

    Serial.print("Using ");
    Serial.print(wunderground.getForecastTitle(time_interval));
  }
  /* Find the appropriate temp forcast */
  if (time_interval == TODAY)
  {
    //get high temp
    if (weatherUpdateTemp) {
      Serial.println("'s High Temp Predication");
    }
    forcast_temp = wunderground.getForecastHighTemp(time_interval).toInt();
  }
  else if (time_interval == TONIGHT)
  {
    //get low temp
    if (weatherUpdateTemp) {
      Serial.println("'s Low Temp Prediction");
    }
    forcast_temp = wunderground.getForecastLowTemp(TODAY).toInt();
  }
  else if (time_interval == TOMORROW)
  {
    //get high temp
    if (weatherUpdateTemp) {
      Serial.println("'s High Temp Prediction");
    }
    forcast_temp = wunderground.getForecastHighTemp(time_interval).toInt();
  }
  else
  {
    if (weatherUpdateTemp) {
      Serial.println("");
      Serial.println("Unknown Interval: Using Current Temp");
    }
    forcast_temp = wunderground.getCurrentTemp().toInt();
  }

  if (weatherUpdateTemp) {
    Serial.println("");
  }
  current_temp = wunderground.getCurrentTemp().toInt();
  if (weatherUpdateTemp) {
    Serial.print("Current Temp: ");
  }
  if (weatherUpdateTemp) {
    Serial.println(current_temp);
  }

  if (weatherUpdateTemp) {
    Serial.print("Forcast Temperature: ");
  }
  if (weatherUpdateTemp) {
    Serial.println(forcast_temp);
  }

  /* Update Display */

  if (time_interval == DISPLAY_OFF)
  {
    if (weatherUpdateTemp) {
      Serial.println("It is late at night, Display Off");
    }
    solidColor(Light_Ring.Color(0, 0, 0, gamma_here[0]), 0); //Black
  }
  else //Lights On
  {

    if (weatherUpdateTemp) {
      Serial.print("Current ");
    }
    current_temp_color = convert_temp_to_color(current_temp);
    if (weatherUpdateTemp) {
      Serial.print("Forcast ");
    }
    forcast_temp_color = convert_temp_to_color(forcast_temp);

    //fade between the two temperature colors
    Light_Ring.setBrightness(brightness);
    Light_Ring.Fade(current_temp_color, forcast_temp_color, 100, FADE_INTERVAL);

    //test fade to black
    //Light_Ring.Fade(Light_Ring.Color(0, 0, 0, gamma_here[0]), convert_temp_to_color(forcast_temp), 100, 5);
  }

  //only print to serial after weather is updated.
  weatherUpdateTemp = false;

}

period_e determine_time_and_brightness()
{
  int hour_in_24 = timeClient.getHoursInt();
  period_e index = TODAY;

  //if time is between 6am and 4pm use TODAY
  //if time is before 9pm, use TONIGHT, dim lights
  //if time is before 11pm, use TOMORROW, dim lights more
  //if time is after 11pm, turn off

  if ( hour_in_24 < 5)
  {
    //turn off lights, no animation
    brightness = 0;
    index = TODAY;
  }
  else if ( hour_in_24 < 7)
  {
    brightness = 20;
    index = TODAY;
  }
  else if ( hour_in_24 < 16) //6am - 4pm
  {
    index = TODAY;
    brightness = 100;
  }
  else if ( hour_in_24 < 20) //4pm - 8pm
  {
    index = TONIGHT;
    brightness = 80;
  }
  else if ( hour_in_24 < 21) //4pm - 9pm
  {
    index = TONIGHT;
    brightness = 10;
  }
  else
  {
    //index = DISPLAY_OFF;
    //brightness = 0;
    index = TOMORROW;
    brightness = 10;
  }

  if (weatherUpdateTemp) {
    Serial.println("");


    Serial.print("Time of day: ");
    Serial.print(hour_in_24);
    Serial.println(" hours");

    Serial.print("Forcast Index (Dec): ");
    Serial.println(index);
    Serial.print("Period Index (Enum): ");
    switch (index)
    {
      case TODAY: Serial.println("TODAY"); break;
      case TONIGHT: Serial.println("TONIGHT"); break;
      case TOMORROW: Serial.println("TOMORROW"); break;
      case NEXT_DAY: Serial.println("NEXT_DAY"); break;
      case NEXT_DAY_NIGHT: Serial.println("NEXT_DAY_NIGHT"); break;
      case DISPLAY_OFF: Serial.println("DISPLAY_OFF"); break;
      default: Serial.println("UNKNOWN"); break;
    }
    Serial.print("Time -> Brightness: ");
    Serial.println(brightness);
  }

  return index;
}

void Light_RingComplete()
{
  //This is what makes the light color pulse
  //short color fase is the forcast temp
  //long duration color is the current temp

  //FORWARD FADE -> REVERSE FADE -> Wait for next FADE -> FORWARD FADE

  if (Light_Ring.ActivePattern == PATTERN_DELAY)
  {
    //reset temperature animation
    update_light_for_temperature();
    //Light_Ring.Interval = 2;
    Light_Ring.ActivePattern = FADE;
    //Serial.println("Reset Animation");
  }
  else //if (Light_Ring.ActivePattern == FADE)
  {
    if (Light_Ring.Direction == FORWARD)
    {
      //reverse temperature animation
      Light_Ring.Reverse();
      //Light_Ring.Interval = 2;
      Light_Ring.ActivePattern = FADE;
      //Serial.println("Reverse Animation");
    }
    else //REVERSE
    {
      //delay before next temperature animation
      Light_Ring.SetDelay(FADE_DELAY, 1000); //approx 15 seconds
      //Serial.println("Delay Animation");
    }
  }
}

weather_e sort_forcast(String iconfound)
{

  if (iconfound == "chanceflurries")  return RAIN;
  if (iconfound == "chancerain")      return RAIN;
  if (iconfound == "chancesleet")     return RAIN;
  if (iconfound == "chancesnow")      return RAIN;
  if (iconfound == "chancetstorms")   return TSTORM;
  if (iconfound == "clear")           return SUNNY;
  if (iconfound == "cloudy")          return CLOUDY;
  if (iconfound == "flurries")        return RAIN;
  if (iconfound == "fog")             return FOG;
  if (iconfound == "hazy")            return FOG;
  if (iconfound == "mostlycloudy")    return CLOUDY;
  if (iconfound == "mostlysunny")     return SUNNY;
  if (iconfound == "partlycloudy")    return CLOUDY;
  if (iconfound == "partlysunny")     return SUNNY;
  if (iconfound == "sleet")           return RAIN;
  if (iconfound == "rain")            return RAIN;
  if (iconfound == "snow")            return RAIN;
  if (iconfound == "sunny")           return SUNNY;
  if (iconfound == "tstorms")         return TSTORM;

  if (iconfound == "nt_chanceflurries") return RAIN;
  if (iconfound == "nt_chancerain") return RAIN;
  if (iconfound == "nt_chancesleet") return RAIN;
  if (iconfound == "nt_chancesnow") return RAIN;
  if (iconfound == "nt_chancetstorms") return TSTORM;
  if (iconfound == "nt_clear") return SUNNY;
  if (iconfound == "nt_cloudy") return CLOUDY;
  if (iconfound == "nt_flurries") return RAIN;
  if (iconfound == "nt_fog") return FOG;
  if (iconfound == "nt_hazy") return FOG;
  if (iconfound == "nt_mostlycloudy") return CLOUDY;
  if (iconfound == "nt_mostlysunny") return SUNNY;
  if (iconfound == "nt_partlycloudy") return CLOUDY;
  if (iconfound == "nt_partlysunny") return SUNNY;
  if (iconfound == "nt_sleet") return RAIN;
  if (iconfound == "nt_rain") return RAIN;
  if (iconfound == "nt_snow") return RAIN;
  if (iconfound == "nt_sunny") return SUNNY;
  if (iconfound == "nt_tstorms") return TSTORM;

}
