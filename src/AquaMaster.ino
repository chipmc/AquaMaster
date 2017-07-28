/*
 * Project AquaMaster
 * Description: Watering program for the back deck
 * Author: Chip McClelland
 * Date: 7/18/17
 */

 STARTUP(WiFi.selectAntenna(ANT_AUTO)); // continually switches at high speed between antennas
 SYSTEM_THREAD(ENABLED);

 // Finally, here are the variables I want to change often and pull them all together here
 #define SOFTWARERELEASENUMBER "0.27"

 #include <I2CSoilMoistureSensor.h>

 I2CSoilMoistureSensor sensor;

 const int solenoidPin = D2;
 const int blueLED = D7;
 const int donePin = D6;              // Pin the Electron uses to "pet" the watchdog
 const int wakeUpPin = A7;            // This is the Particle Electron WKP pin


 unsigned long waterTimeShort = 60000;    // For Testing the system and smaller adjustments
 unsigned long waterTimeLong = 300000;    // The longest watering period
 unsigned long waterTime = 0;             // How long with the watering go in the main loop
 int startWaterHour = 6;                  // When can we start watering
 int stopWaterHour = 8;                   // When do we stop for the day
 char RSSIdescription[12];
 char Signal[17];
 char capDescription[12];
 char Moisture[15];
 int capValue = 0;      // This is where we store the soil moisture sensor raw data
 int wateringNow = 0;
 int waterEnabled = 1;
 int currentPeriod = 0;  // Change length of period for testing 2 times in main loop
 int lastWateredPeriod = 0; // So we only wanter once an hour
 const char* releaseNumber = SOFTWARERELEASENUMBER;  // Displays the release on the menu
 volatile bool doneEnabled = true;    // This enables petting the watchdog
 volatile bool watchdogPet = false;

void setup() {
  pinMode(donePin,OUTPUT);       // Allows us to pet the watchdog
  digitalWrite(donePin, HIGH);  // Pet now while we are getting set up
  digitalWrite(donePin, LOW);

  Particle.variable("Watering", wateringNow);
  Particle.variable("WiFiStrength", Signal);
  Particle.variable("Moisture", Moisture);
  Particle.variable("Enabled", waterEnabled);
  Particle.variable("Release",releaseNumber);
  Particle.function("start-stop", startStop);
  Particle.function("Enabled", wateringEnabled);
  Particle.subscribe("hook-response/soilMoisture", myHandler, MY_DEVICES);      // Subscribe to the integration response event
  //Particle.subscribe("hook-response/watering", myHandler, MY_DEVICES);      // Subscribe to the integration response event


  Wire.begin();
  Serial.begin(9600);
  sensor.begin(); // reset sensor
  NonBlockingDelay(2000);

  pinMode(donePin,OUTPUT);       // Allows us to pet the watchdog
  attachInterrupt(wakeUpPin, watchdogISR, RISING);   // The watchdog timer will signal us and we have to respond
  pinMode(solenoidPin,OUTPUT);
  digitalWrite(solenoidPin, LOW);
  pinMode(blueLED,OUTPUT);
  pinMode(wakeUpPin,INPUT_PULLDOWN);   // This pin is active HIGH

  Particle.connect();    // <-- now connect to the cloud, which ties up the system thread


  Serial.println("");                 // Header information
  Serial.print(F("AquaMaster - release "));
  Serial.println(releaseNumber);

  pinMode(solenoidPin,OUTPUT);
  digitalWrite(solenoidPin, LOW);
  pinMode(blueLED,OUTPUT);

  Time.zone(-4);    // Raleigh DST (watering is for the summer)

  Serial.print("I2C Soil Moisture Sensor Address: ");
  Serial.println(sensor.getAddress(),HEX);
  Serial.print("Sensor Firmware version: ");
  Serial.println(sensor.getVersion(),HEX);
  Serial.println();
}


void loop() {
  if (Time.hour() != currentPeriod)
  {
    getMoisture();
    Particle.publish("Soil Temp C",String(sensor.getTemperature()/(float)10),3); //temperature register
    getWiFiStrength();
    currentPeriod = Time.hour();
    if (waterEnabled)
    {
      if (currentPeriod >= startWaterHour && currentPeriod <= stopWaterHour)
      {
        if ((strcmp(capDescription,"Very Dry") == 0) || (strcmp(capDescription,"Dry") == 0) || (strcmp(capDescription,"Normal") == 0))
        {
          if (currentPeriod != lastWateredPeriod)
          {
            lastWateredPeriod = currentPeriod; // Only want to water once an hour
            if (currentPeriod == startWaterHour) waterTime = waterTimeLong;
            else waterTime = waterTimeShort;
            turnOnWater(waterTime);
          }
        }
        else Particle.publish("Watering","Not Needed");
      }
      else Particle.publish("Watering","Not Time");
    }
    else Particle.publish("Watering","Not Enabled");
  }
}

void turnOnWater(unsigned long duration)
{
  Particle.publish("Watering","Watering");
  digitalWrite(blueLED, HIGH);
  digitalWrite(solenoidPin, HIGH);
  wateringNow = 1;
  NonBlockingDelay(duration);
  digitalWrite(blueLED, LOW);
  digitalWrite(solenoidPin, LOW);
  wateringNow = 0;
  Particle.publish("Watering","Done");
}

int startStop(String command)   // Will reset the local counts
{
  if (command == "start")
  {
    turnOnWater(waterTimeShort);
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

int wateringEnabled(String command)
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

int getWiFiStrength()
{
  int wifiRSSI = WiFi.RSSI();
  if (wifiRSSI >= 0)
  {
    strcpy(RSSIdescription,"Error");
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
  strcat(Signal,String(wifiRSSI));
}


void getMoisture()
{
  capValue = sensor.getCapacitance();
  String data = String(90);
  Particle.publish("soilMoisture", data ,PRIVATE);
  NonBlockingDelay(3000);
  if ((capValue >= 650) || (capValue <=300))
  {
    strcpy(capDescription,"Error");
  }
  else
  {
    int strength = map(capValue, 300, 650, 0, 5);
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
  }
  strcpy(Moisture,capDescription);
  strcat(Moisture,": ");
  strcat(Moisture,String(capValue));
  Particle.publish("Moisture Level", Moisture);
}

void NonBlockingDelay(int millisDelay)  // Used for a non-blocking delay
{
    unsigned long commandTime = millis();
    while (millis() <= millisDelay + commandTime) { }
    return;
}

void myHandler(const char *event, const char *data)
{
  if (!data) {              // First check to see if there is any data
    Particle.publish("WebHook", "No Data");
    return;
  }
  String response = data;   // If there is data - copy it into a String variable
  int datainResponse = response.indexOf("moisture") + 24; // Find the "hourly" field and add 24 to get to the value
  String responseCodeString = response.substring(datainResponse,datainResponse+3);  // Trim all but the value
  int responseCode = responseCodeString.toInt();  // Put this into an int for comparisons
  switch (responseCode) {   // From the Ubidots API refernce https://ubidots.com/docs/api/#response-codes
    case 200:
      Serial.println("Request successfully completed");
      doneEnabled = true;   // Successful response - can pet the dog again
      digitalWrite(donePin, HIGH);  // If an interrupt came in while petting disabled, we missed it so...
      digitalWrite(donePin, LOW);   // will pet the fdog just to be safe
      break;
    case 201:
      Serial.println("Successful request - new data point created");
      doneEnabled = true;   // Successful response - can pet the dog again
      digitalWrite(donePin, HIGH);  // If an interrupt came in while petting disabled, we missed it so...
      digitalWrite(donePin, LOW);   // will pet the fdog just to be safe
      break;
    case 400:
      Serial.println("Bad request - check JSON body");
      break;
    case 403:
      Serial.println("Forbidden token not valid");
      break;
    case 404:
      Serial.println("Not found - verify variable and device ID");
      break;
    case 405:
      Serial.println("Method not allowed for API endpoint chosen");
      break;
    case 501:
      Serial.println("Internal error");
      break;
    default:
      Serial.print("Ubidots Response Code: ");    // Non-listed code - generic response
      Serial.println(responseCode);
      break;
  }
}

void watchdogISR()
{
  if (doneEnabled)
  {
    digitalWrite(donePin, HIGH);
    digitalWrite(donePin, LOW);
    watchdogPet = true;
  }
}
