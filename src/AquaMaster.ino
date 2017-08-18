/*
 * Project AquaMaster
 * Description: Watering program for the back deck
 * Author: Chip McClelland
 * Date: 7/18/17
 */
 // Wiring for Chirp (Board/Assign/Cable) - Red/Vcc/Orange, Black/GND/Green, Blue/SCL/Green&White, Yellow/SDA/Orange&White

STARTUP(WiFi.selectAntenna(ANT_AUTO));      // continually switches at high speed between antennas
SYSTEM_THREAD(ENABLED);

// Software Release lets me know what version the Particle is running
#define SOFTWARERELEASENUMBER "0.62"

// Included Libraries
#include <I2CSoilMoistureSensor.h>          // Apollon77's Chirp Library: https://github.com/Apollon77/I2CSoilMoistureSensor

// Function Prototypes
I2CSoilMoistureSensor sensor;               // For the Chirp sensor

// Constants for Pins
const int solenoidPin = D2;                 // Pin that controls the MOSFET that turn on the water
const int blueLED = D7;                     // Used for debugging, can see when water is ON
const int donePin = D6;                     // Pin the Electron uses to "pet" the watchdog
const int wakeUpPin = A7;                   // This is the Particle Electron WKP pin

// Watering Variables
unsigned long oneMinuteMillis = 60000;      // For Testing the system and smaller adjustments
int shortWaterMinutes = 1;                  // Short watering cycle
int longWaterMinutes = 5;                   // Long watering cycle - must be shorter than watchdog interval!
int wateringMinutes = 0;                    // How long will we water based on time or Moisture
int startWaterHour = 5;                     // When can we start watering
int stopWaterHour = 8;                      // When do we stop for the day
int wateringNow = 0;                        // Status - watering?
int waterEnabled = 1;                       // Allows you to disable watering from the app or Ubidots
float expectedRainfallToday = 0;            // From Weather Underground Simple Forecast qpf_allday

// Measurement Variables
char Signal[17];                            // Used to communicate Wireless RSSI and Description
char Rainfall[5];                           // Report Rainfall preduction
int capValue = 0;                           // This is where we store the soil moisture sensor raw data
int soilTemp = 0;                           // Soil Temp is measured 3" deep
char capDescription[13];                    // Characterize using a map function
char Moisture[15];                          // Combines description and capValue

// Time Period Variables
int currentPeriod = 0;                      // Change length of period for testing 2 places in main loop
int lastWateredPeriod = 0;                  // So we only wanter once an hour
int lastWateredDay = 0;                     // Need to reset the last watered period each day
int currentDay = 0;                         // Updated so we can tell which day we last watered

// Control Variables
const char* releaseNumber = SOFTWARERELEASENUMBER;  // Displays the release on the menu
volatile bool doneEnabled = true;           // This enables petting the watchdog
int lastWateredPeriodAddr = 0;              // Where I store the last watered period in EEPROM
int lastWateredDayAddr = 0;
char wateringContext[25];                   // Why did we water or not sent to Ubidots for context
float rainThreshold = 0.4;                  // Expected rainfall in inches which would cause us not to water

void setup() {
  pinMode(donePin,OUTPUT);                  // Allows us to pet the watchdog
  digitalWrite(donePin, HIGH);              // Pet now while we are getting set up
  digitalWrite(donePin, LOW);
  pinMode(solenoidPin,OUTPUT);              // Pin to turn on the water
  digitalWrite(solenoidPin, LOW);           // Make sure it is off
  pinMode(blueLED,OUTPUT);                  // Pin to see whether water should be on
  pinMode(wakeUpPin,INPUT_PULLDOWN);        // The signal from the watchdog is active HIGH
  attachInterrupt(wakeUpPin, watchdogISR, RISING);   // The watchdog timer will signal us and we have to respond

  Particle.variable("Watering", wateringNow);       // These variables are used to monitor the device will reduce them over time
  Particle.variable("WiFiStrength", Signal);
  Particle.variable("Moisture", Moisture);
  Particle.variable("Enabled", waterEnabled);
  Particle.variable("Release",releaseNumber);
  Particle.variable("LastWater",lastWateredPeriod);
  Particle.variable("RainFcst", Rainfall);
  Particle.function("start-stop", startStop);       // Here are thre functions for easy control
  Particle.function("Enabled", wateringEnabled);    // I can disable watering simply here
  Particle.function("Measure", takeMeasurements);   // If we want to see Temp / Moisture values updated
  Particle.subscribe("hook-response/Ubidots_hook", UbidotsHandler, MY_DEVICES);       // Subscribe to the integration response event
  Particle.subscribe("hook-response/weatherU_hook", weatherHandler,MY_DEVICES);       // Subscribe to weather response

  Time.zone(-4);                            // Raleigh DST (watering is for the summer)

  Wire.begin();                             // Begin to initialize the libraries and devices
  Serial.begin(9600);
  sensor.begin();                           // reset the Chrip sensor
  NonBlockingDelay(2000);                   // The sensor needs 2 seconds after reset to stabilize

  EEPROM.get(lastWateredPeriodAddr,lastWateredPeriod);    // Load the last watered period from EEPROM
  EEPROM.get(lastWateredDayAddr,lastWateredDay);          // Load the last watered day from EEPROM
}


void loop() {
  if (Time.hour() != currentPeriod)                       // Spring into action each hour on the hour
  {
    Particle.publish("weatherU_hook");                    // Get the weather forcast
    NonBlockingDelay(5000);                               // Give the Weather Underground time to respond
    takeMeasurements("1");                                // Take measurements
    currentPeriod = Time.hour();                          // Set the new current period
    currentDay = Time.day();                              // Sets the current Day
    if (waterEnabled)
    {
      if (currentPeriod >= startWaterHour && currentPeriod <= stopWaterHour)
      {
        if ((strcmp(capDescription,"Very Dry") == 0) || (strcmp(capDescription,"Dry") == 0) || (strcmp(capDescription,"Normal") == 0))
        {
          if (currentPeriod != lastWateredPeriod || currentDay != lastWateredDay)
          {
            if (expectedRainfallToday <= rainThreshold)
            {
              strcpy(wateringContext,"Watering");
              if (currentPeriod == startWaterHour) wateringMinutes = longWaterMinutes;  // So, the first watering is long
              else wateringMinutes = shortWaterMinutes;                                 // Subsequent are short - fine tuning
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
    Particle.publish("Watering", wateringContext);        // Update console on what we are doing
    sendToUbidots();                                      // Let Ubidots know what we are doing
  }
  if (wateringMinutes)                                    // This IF statement waits for permission to water
  {
    unsigned long waterTime = wateringMinutes * oneMinuteMillis;    // Set the watering duration
    wateringMinutes = 0;                                  // Reset wateringMinutes for next time
    turnOnWater(waterTime);                               // Starts the watering function
  }
}

void turnOnWater(unsigned long duration)                  // Where we water the plants - critical function completes
{
  // We are going to use the watchdog timer to ensure this function completes successfully
  // Need a watchdog interval that is slighly longer than the longest watering cycle
  // We will pet the dog then disable petting until the end of the function
  // That way, if the Particle freezes while the water is on, it will be reset by the watchdog
  // Upon reset, the water will be turned off averting disaster
  digitalWrite(donePin, HIGH);                            // We will pet the dog now so we have the full interval to water
  digitalWrite(donePin, LOW);                             // We set the delay resistor to 50k or 7 mins so that is the longest watering duration
  // Uncomment this next line only after you are sure your watchdog timer interval is greater than watering period
  doneEnabled = false;                                    // Will suspend watchdog petting until water is turned off
  // If anything in this section hangs, the watchdog will reset the Photon
  digitalWrite(blueLED, HIGH);                            // Light on for watering
  digitalWrite(solenoidPin, HIGH);                        // Turn on the water
  wateringNow = 1;                                        // This is a Particle.variable so you can see from the app
  NonBlockingDelay(duration);                             // Delay for the watering period
  digitalWrite(blueLED, LOW);                             // Turn everything off
  digitalWrite(solenoidPin, LOW);
  wateringNow = 0;
  // End mission - critical session
  doneEnabled = true;                                     // Successful response - can pet the dog again
  digitalWrite(donePin, HIGH);                            // If an interrupt came in while petting disabled, we missed it so...
  digitalWrite(donePin, LOW);                             // will pet the fdog just to be safe
  lastWateredDay = currentDay;
  lastWateredPeriod = currentPeriod;
  EEPROM.put(lastWateredPeriodAddr,currentPeriod);        // Sets the last watered period to the current one
  EEPROM.put(lastWateredDayAddr,currentDay);              // Stored in EEPROM since this issue only comes in case of a reset
  Particle.publish("Watering","Done");
}

void sendToUbidots()                                      // Houly update to Ubidots for serial data logging and analysis
{
  digitalWrite(donePin, HIGH);                            // We will pet the dog now so we have the full interval to water
  digitalWrite(donePin, LOW);                             // We set the delay resistor to 50k or 7 mins so that is the longest watering duration
  // Uncomment this next line only after you are sure your watchdog timer interval is greater than the Ubidots response period (about 5 secs)
  doneEnabled = false;                                    // Turns off watchdog petting - only a successful response will re-enable
  char data[256];                                         // Store the date in this character array - not global
  snprintf(data, sizeof(data), "{\"Moisture\":%i, \"Watering\":%i, \"key1\":\"%s\", \"SoilTemp\":%i}",capValue, wateringMinutes, wateringContext, soilTemp);
  Particle.publish("Ubidots_hook", data , PRIVATE);
  NonBlockingDelay(1000);                                 // Makes sure we are spacing out our Particle.publish requests
}

int startStop(String command)                             // So we can manually turn on the water for testing and setup
{
  if (command == "1")
  {
    wateringMinutes = shortWaterMinutes;                  // Manual waterings are short
    strcpy(wateringContext,"User Initiated");             // Add the right context for publishing
    Particle.publish("Watering", wateringContext);        // Update console on what we are doing
    sendToUbidots();                                      // Let Ubidots know what we are doing
    return 1;
  }
  else if (command == "0")                                // This allows us to turn off the water at any time
  {
    digitalWrite(blueLED, LOW);                           // Turn off light
    digitalWrite(solenoidPin, LOW);                       // Turn off water
    wateringNow = 0;                                      // Update Particle.variable
    Particle.publish("Watering","Done");                  // publish
    return 1;
  }
  else
  {
    Serial.print("Got here but did not work: ");          // Just in case - never get here
    Serial.println(command);
    return 0;
  }
}

int wateringEnabled(String command)                       // If I sense something is amiss, I can easily disable watering
  {
    if (command == "1")                                   // Default - enabled
    {
      waterEnabled = 1;
      return 1;
    }
    else if (command == "0")                              // Ensures no watering will occur
    {
      waterEnabled = 0;
      return 1;
    }
    else                                                  // Never get here but if we do, let's be safe and disable
    {
      waterEnabled = 0;
      return 0;
    }
  }

int takeMeasurements(String command)                      // Allows me to monitor variables between normal hourly updates
  {
    if (command == "1")                                   // Take a set of measurements
    {
      getMoisture();                                      // Test soil Moisture
      getWiFiStrength();                                  // Get the WiFi Signal strength
      soilTemp = int(sensor.getTemperature()/(float)10);  // Get the Soil temperature
      return 1;
    }
    else                                                  // In case something other than "1" is entered
    {
      return 0;
    }
  }

int getWiFiStrength()                                     // Measure and describe wireless signal strength
{
  int wifiRSSI = WiFi.RSSI();                             // Get the signed integer for RSSI
  char RSSIString[4];                                     // Need to create a char array so we can combine later
  snprintf(RSSIString,sizeof(RSSIString),"%d",wifiRSSI);  // Put string value into array
  char RSSIdescription[12];                               // Want to put the words with it as well
  if (wifiRSSI >= 0)                                      // Greater than zero is not possible
  {
    strcpy(Signal,"Error");
    return 0;
  }
  int strength = map(wifiRSSI, -127, -1, 0, 5);           // Map the RSSI values to words - valid RSSI falls in this range
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
  strcpy(Signal,RSSIdescription);                         // Combine signal description and value
  strcat(Signal,": ");
  strcat(Signal,RSSIString);                              // Value from above
  return 1;
}


void getMoisture()                                        // Here we get the soil moisture and characterize it to see if watering is needed
{
  capValue = sensor.getCapacitance();                     // capValue is typically between 300 and 700
  char capValueString[4];                                 // Create a char arrray and load with the capValue for later string formation
  snprintf(capValueString,sizeof(capValueString),"%d",capValue);
  int strength = map(capValue, 300, 500, 0, 5);           // Map - these values to cases that will use words that are easier to understand
  switch (strength)                                       // Main loop watering logic is based on capDescription not the capValue
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
    default:
      strcpy(capDescription,"Out of Range");              // I tweak the range to match normal values - need to see if not in range
      break;
  }
  strcpy(Moisture,capDescription);
  strcat(Moisture,": ");
  strcat(Moisture,capValueString);                        // Assemble the string that will be published
  Particle.publish("Moisture Level", Moisture);
}

void NonBlockingDelay(int millisDelay)                    // Used for a non-blocking delay - will allow for interrrupts and Particle calls
{
  unsigned long commandTime = millis();
  while (millis() <= millisDelay + commandTime)
  {
    Particle.process();                                   // This ensures that we can still service Particle processes
  }
  return;
}

void weatherHandler(const char *event, const char *data)  // Extracts the expected rainfall for today from webhook response
{
  // Uses forecast JSON for Raleigh-Durham Airport
  // Response template gets current date and qpf_allday
  // Only look at the current day
  // JSON payload - http://api.wunderground.com/api/(my key)/forecast/q/nc/raleigh-durham.json
  // Response Template: "{{#forecast}}{{#simpleforecast}}{{#forecastday}}{{date.day}}~{{qpf_allday.in}}~{{/forecastday}}{{/simpleforecast}}{{/forecast}}"
  if (!data) {                                            // First check to see if there is any data
    Particle.publish("Weather", "No Data");
    return;
  }
  char strBuffer[30] = "";                                // Create character array to hold response
  strcpy(strBuffer,data);                                 // Copy into the array
  int forecastDay = atoi(strtok(strBuffer, "\"~"));       // Use the delimiter to find today's date and expected Rainfall
  expectedRainfallToday = atof(strtok(NULL, "~"));
  snprintf(Rainfall,sizeof(Rainfall),"%4.2f",expectedRainfallToday);
}

void UbidotsHandler(const char *event, const char *data)  // Looks at the response from Ubidots - Will reset Photon if no successful response
{
  // Response Template: "{{watering.0.status_code}}"
  if (!data) {                                            // First check to see if there is any data
    Particle.publish("UbidotsResp", "No Data");
    return;
  }
  int responseCode = atoi(data);                          // Response is only a single number thanks to Template
  if ((responseCode == 200) || (responseCode == 201))
  {
    Particle.publish("UbidotsHook","Success");
    doneEnabled = true;                                   // Successful response - can pet the dog again
    digitalWrite(donePin, HIGH);                          // If an interrupt came in while petting disabled, we missed it so...
    digitalWrite(donePin, LOW);                           // will pet the dog just to be safe
  }
  else Particle.publish("UbidotsHook", data);             // Publish the response code
}

void watchdogISR()                                        // Will pet the dog ... if petting is allowed
{
  if (doneEnabled)                                        // the doneEnabled is used for Ubidots and Watering to prevent lockups
  {
    digitalWrite(donePin, HIGH);                          // This is all you need to do to pet the dog low to high transition
    digitalWrite(donePin, LOW);
  }
}
