/*
 * Project AquaMaster
 * Description: Watering program for the back deck
 * Author: Chip McClelland
 * Date: 7/18/17
 */
 // Wiring for Chirp (Board/Assign/Cable) - Red/Vcc/Orange, Black/GND/Green, Blue/SCL/Green&White, Yellow/SDA/Orange&White

STARTUP(WiFi.selectAntenna(ANT_AUTO)); // continually switches at high speed between antennas
SYSTEM_THREAD(ENABLED);

// Software Release lets me know what version the Particle is running
#define SOFTWARERELEASENUMBER "0.6"

// Included Libraries
#include <I2CSoilMoistureSensor.h>   // Apollon77's Chirp Library: https://github.com/Apollon77/I2CSoilMoistureSensor

// Function Prototypes
I2CSoilMoistureSensor sensor;

// Constants for Pins
const int solenoidPin = D2;
const int blueLED = D7;
const int donePin = D6;              // Pin the Electron uses to "pet" the watchdog
const int wakeUpPin = A7;            // This is the Particle Electron WKP pin

// Program variables
// Watering Variables
unsigned long oneMinuteMillis = 60000;    // For Testing the system and smaller adjustments
int shortWaterMinutes = 1;                // Short watering cycle
int longWaterMinutes = 5;                 // Long watering cycle - must be shorter than watchdog interval!
int wateringMinutes = 0;                  // How long will we water based on time or Moisture
int startWaterHour = 5;                  // When can we start watering
int stopWaterHour = 8;                   // When do we stop for the day
int wateringNow = 0;                      // Status - watering?
int waterEnabled = 1;                     // Allows you to disable watering from the app or Ubidots
float expectedRainfallToday = 0;        // From Weather Underground Simple Forecast qpf_allday

// Measurement Variables
char Signal[17];                        // Used to communicate Wireless RSSI and Description
char Rainfall[5];                       // Report Rainfall preduction
int capValue = 0;                       // This is where we store the soil moisture sensor raw data
int soilTemp = 0;                       // Soil Temp is measured 3" deep
char capDescription[12];                // Characterize using a map function
char Moisture[15];                      // Combines description and capValue

// Time Period Variables
int currentPeriod = 0;                  // Change length of period for testing 2 places in main loop
int lastWateredPeriod = 0;              // So we only wanter once an hour
int lastWateredDay = 0;                 // Need to reset the last watered period each day
int currentDay = 0;                     // Updated so we can tell which day we last watered

// Control Variables
const char* releaseNumber = SOFTWARERELEASENUMBER;  // Displays the release on the menu
volatile bool doneEnabled = true;       // This enables petting the watchdog
int lastWateredPeriodAddr = 0;          // Where I store the last watered period in EEPROM
int lastWateredDayAddr = 0;
char wateringContext[25];               // Why did we water or not

void setup() {
  pinMode(donePin,OUTPUT);       // Allows us to pet the watchdog
  digitalWrite(donePin, HIGH);  // Pet now while we are getting set up
  digitalWrite(donePin, LOW);
  attachInterrupt(wakeUpPin, watchdogISR, RISING);   // The watchdog timer will signal us and we have to respond

  Particle.variable("Watering", wateringNow);       // These variables are used to monitor the device
  Particle.variable("WiFiStrength", Signal);
  Particle.variable("Moisture", Moisture);
  Particle.variable("Enabled", waterEnabled);
  Particle.variable("Release",releaseNumber);
  Particle.variable("LastWater",lastWateredPeriod);
  Particle.variable("RainFcst", Rainfall);
  Particle.function("start-stop", startStop);       // Here are two functions for easy control
  Particle.function("Enabled", wateringEnabled);
  Particle.function("Measure", takeMeasurements);     // If we want to see Temp / Moisture values updated
  Particle.subscribe("hook-response/Ubidots_hook", UbidotsHandler, MY_DEVICES);      // Subscribe to the integration response event
  Particle.subscribe("hook-response/weatherU_hook", weatherHandler,MY_DEVICES); // Subscribe to weather response

  Time.zone(-4);    // Raleigh DST (watering is for the summer)

  Wire.begin();                       // Begin to initialize the libraries and devices
  Serial.begin(9600);
  sensor.begin();                     // reset sensor
  NonBlockingDelay(2000);

  pinMode(solenoidPin,OUTPUT);
  digitalWrite(solenoidPin, LOW);
  pinMode(blueLED,OUTPUT);
  pinMode(wakeUpPin,INPUT_PULLDOWN);                      // This pin is active HIGH

  EEPROM.get(lastWateredPeriodAddr,lastWateredPeriod);    // Load the last watered period from EEPROM
  EEPROM.get(lastWateredDayAddr,lastWateredDay);          // Load the last watered day from EEPROM
}


void loop() {
  if (Time.hour() != currentPeriod)         // Spring into action each hour on the hour
  {
    Particle.publish("weatherU_hook");      // Get the weather forcast
    NonBlockingDelay(5000);                 // Give the Weather Underground time to respond
    takeMeasurements("take");               // Take measurements - use the Particle.function here
    currentPeriod = Time.hour();            // Set the new current period
    currentDay = Time.day();                // Sets the current Day
    if (waterEnabled)
    {
      if (currentPeriod >= startWaterHour && currentPeriod <= stopWaterHour)
      {
        if ((strcmp(capDescription,"Very Dry") == 0) || (strcmp(capDescription,"Dry") == 0) || (strcmp(capDescription,"Normal") == 0))
        {
          if (currentPeriod != lastWateredPeriod || currentDay != lastWateredDay)
          {
            if (expectedRainfallToday <= 0.4)
            {
              strcpy(wateringContext,"Watering");
              if (currentPeriod == startWaterHour) wateringMinutes = longWaterMinutes;
              else wateringMinutes = shortWaterMinutes;
            }
            else strcpy(wateringContext,"Heavy Rain Expected");
          }
          else strcpy(wateringContext,"Already Watered");
        }
        else strcpy(wateringContext,"Not Needed");
      }
      else strcpy(wateringContext,"Not Time");
    }
    else strcpy(wateringContext,"Not Enabled");
    Particle.publish("Watering", wateringContext);
    sendToUbidots();      // Let Ubidots know what we are doing
  }
  if (wateringMinutes)      // This IF statement waits for permission to water
  {
    unsigned long waterTime = wateringMinutes * oneMinuteMillis;    // Set the watering duration
    wateringMinutes = 0;  // Reset wateringMinutes for next time
    turnOnWater(waterTime);   // Starts the watering function
  }
}

void turnOnWater(unsigned long duration)    // Where we water the plants - critical function completes
{
  // We are going to use the watchdog timer to ensure this function completes successfully
  // Need a watchdog interval that is slighly longer than the longest watering cycle
  // We will pet the dog then disable petting until the end of the function
  // That way, if the Particle freezes while the water is on, it will be reset by the watchdog
  // Upon reset, the water will be turned off averting disaster
  digitalWrite(donePin, HIGH);  // We will pet the dog now so we have the full interval to water
  digitalWrite(donePin, LOW);   // We set the delay resistor to 50k or 7 mins so that is the longest watering duration
  // Uncomment this next line only after you are sure your watchdog timer interval is greater than watering period
  doneEnabled = false;          // Will suspend watchdog petting until water is turned off
  Particle.publish("Watering","Watering");
  digitalWrite(blueLED, HIGH);
  digitalWrite(solenoidPin, HIGH);
  wateringNow = 1;
  NonBlockingDelay(duration);
  digitalWrite(blueLED, LOW);
  digitalWrite(solenoidPin, LOW);
  wateringNow = 0;
  doneEnabled = true;         // Successful response - can pet the dog again
  digitalWrite(donePin, HIGH);  // If an interrupt came in while petting disabled, we missed it so...
  digitalWrite(donePin, LOW);   // will pet the fdog just to be safe
  lastWateredDay = currentDay;
  lastWateredPeriod = currentPeriod;
  EEPROM.put(lastWateredPeriodAddr,currentPeriod);  // Sets the last watered period to the current one
  EEPROM.put(lastWateredDayAddr,currentDay);            // Stored in EEPROM since this issue only comes in case of a reset
  Particle.publish("Watering","Done");
}

void sendToUbidots()      // Houly update to Ubidots for serial data logging and analysis
{
  char data[256];
  snprintf(data, sizeof(data), "{\"Moisture\":%i, \"Watering\":%i, \"key1\":\"%s\", \"SoilTemp\":%i}",capValue, wateringMinutes, wateringContext, soilTemp);
  Particle.publish("Ubidots_hook", data , PRIVATE);
  NonBlockingDelay(1000);             // Makes sure we are spacing out our Particle.publish requests
}

int startStop(String command)         // So we can manually turn on the water for testing and setup
{
  if (command == "start")
  {
    wateringMinutes = shortWaterMinutes;
    sendToUbidots();
    return 1;
  }
  else if (command == "stop")
  {
    digitalWrite(blueLED, LOW);
    digitalWrite(solenoidPin, LOW);
    wateringNow = 0;
    Particle.publish("Watering","Done");
    return 1;
  }
  else
  {
    Serial.print("Got here but did not work: ");
    Serial.println(command);
    return 0;
  }
}

int wateringEnabled(String command)       // If I sense something is amiss, I wanted to be able to easily disable watering
  {
    if (command == "enabled")
    {
      waterEnabled = 1;
      return 1;
    }
    else if (command == "not")
    {
      waterEnabled = 0;
      return 1;
    }
    else
    {
      waterEnabled = 0;
      return 0;
    }
  }

int takeMeasurements(String command)       // If I sense something is amiss, I wanted to be able to easily disable watering
  {
    if (command == "take")
    {
      getMoisture();                          // Test soil Moisture
      getWiFiStrength();                      // Get the WiFi Signal strength
      soilTemp = int(sensor.getTemperature()/(float)10);    // Get the Soil temperature
      return 1;
    }
    else
    {
      return 0;
    }
  }

int getWiFiStrength()       // Measure and describe wireless signal strength
{
  int wifiRSSI = WiFi.RSSI();
  char RSSIdescription[12];
  if (wifiRSSI >= 0)
  {
    strcpy(Signal,"Error");
    return 0;
  }
  int strength = map(wifiRSSI, -127, -1, 0, 5);
  switch (strength)
  {
    case 0:
      strcpy(RSSIdescription,"Poor");
      break;
    case 1:
      strcpy(RSSIdescription, "Low");
      break;
    case 2:
      strcpy(RSSIdescription,"Medium");
      break;
    case 3:
      strcpy(RSSIdescription,"Good");
      break;
    case 4:
      strcpy(RSSIdescription,"Very Good");
      break;
    case 5:
      strcpy(RSSIdescription,"Great");
      break;
  }
  strcpy(Signal,RSSIdescription);
  strcat(Signal,": ");
  snprintf(Signal,sizeof(Signal),"%d",wifiRSSI);
  // strcat(Signal,String(wifiRSSI));            // Signal is both description and RSSI for Particle variable
  return 1;
}


void getMoisture()          // Here we get the soil moisture and characterize it to see if watering is needed
{
  capValue = sensor.getCapacitance();
  char capValueString[4];
  int strength = map(capValue, 300, 600, 0, 5);
  switch (strength)
  {
    case 0:
      strcpy(capDescription,"Very Dry");
      break;
    case 1:
      strcpy(capDescription,"Dry");
      break;
    case 2:
      strcpy(capDescription,"Normal");
      break;
    case 3:
      strcpy(capDescription,"Wet");
      break;
    case 4:
      strcpy(capDescription,"Very Wet");
      break;
    case 5:
      strcpy(capDescription,"Waterlogged");
      break;
  }
  strcpy(Moisture,capDescription);
  strcat(Moisture,": ");
  snprintf(capValueString,sizeof(capValueString),"%d",capValue);
  strcat(Moisture,capValueString);
  Particle.publish("Moisture Level", Moisture);
}

void NonBlockingDelay(int millisDelay)  // Used for a non-blocking delay
{
  unsigned long commandTime = millis();
  while (millis() <= millisDelay + commandTime)
  {
    Particle.process();
  }
  return;
}

void weatherHandler(const char *event, const char *data)    // Extracts the expected rainfall for today from webhook response
{
  // Uses forecast JSON for Raleigh-Durham Airport
  // Response template gets current date and qpf_allday
  // Only look at the current day
  // JSON payload - http://api.wunderground.com/api/(my key)/forecast/q/nc/raleigh-durham.json
  if (!data) {              // First check to see if there is any data
    Particle.publish("Weather", "No Data");
    return;
  }
  //String str = String(data);
  char strBuffer[30] = "";
  //str.toCharArray(strBuffer, 30); // example: "\"2~0~3~0~4~0~5~0.07~\""
  //int forecastDay = atoi(strtok(strBuffer, "\"~"));
  strcpy(strBuffer,data);
  int forecastDay = atoi(strtok(strBuffer, "\"~"));
  expectedRainfallToday = atof(strtok(NULL, "~"));
  snprintf(Rainfall,sizeof(Rainfall),"%4.2f",expectedRainfallToday);
  //strcpy(Rainfall,String(expectedRainfallToday,2));
}

void UbidotsHandler(const char *event, const char *data)   // Looks at the response from Ubidots - not acting on this now
{
  if (!data) {              // First check to see if there is any data
    Particle.publish("UbidotsResp", "No Data");
    return;
  }
  int responseCode = atoi(data);
  if ((responseCode == 200) || (responseCode == 201))
  {
    Particle.publish("UbidotsHook","Success");
    doneEnabled = true;   // Successful response - can pet the dog again
    digitalWrite(donePin, HIGH);  // If an interrupt came in while petting disabled, we missed it so...
    digitalWrite(donePin, LOW);   // will pet the fdog just to be safe
  }
  else Particle.publish("UbidotsHook", data);
}

void watchdogISR()    // Will pet the dog ... if petting is allowed
{
  if (doneEnabled)    // This allows us to ensure we don't have a system panic while the water is running
  {
    digitalWrite(donePin, HIGH);
    digitalWrite(donePin, LOW);
  }
}
