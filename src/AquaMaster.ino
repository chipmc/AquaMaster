/*
 * Project AquaMaster
 * Description: Watering program for the back deck
 * Author: Chip McClelland
 * Date: 7/18/17
 */

 STARTUP(WiFi.selectAntenna(ANT_AUTO)); // continually switches at high speed between antennas
 SYSTEM_THREAD(ENABLED);

 #include <I2CSoilMoistureSensor.h>

 I2CSoilMoistureSensor sensor;

 const int solenoidPin = D2;
 const int resetChirp = D3;
 const int blueLED = D7;

 unsigned long waterTime = 60000;
 String RSSIdescription = "";
 String RSSIstring = "";
 String capDescription = "";
 int capValue = 0;
 int wateringNow = 0;
 int waterEnabled = 0;
 int currentPeriod = 0;  // Change length of period for testing 2 times in main loop


void setup() {
  Wire.begin();
  Serial.begin(9600);
  pinMode(solenoidPin,OUTPUT);
  digitalWrite(solenoidPin, LOW);
  pinMode(blueLED,OUTPUT);
  pinMode(resetChirp,OUTPUT);

  Time.zone(-4);    // Raleigh DST (watering is for the summer)

  Particle.variable("Watering", wateringNow);
  Particle.variable("WiFiStrength", RSSIdescription);
  Particle.variable("RSSI",RSSIstring);
  Particle.variable("Moisture", capDescription);
  Particle.variable("capValue", capValue);
  Particle.variable("Enabled", waterEnabled);
  Particle.function("start-stop", startStop);
  Particle.function("Enabled", wateringEnabled);


  sensor.begin(); // reset sensor
  delay(1000); // give some time to boot up
  Serial.print("I2C Soil Moisture Sensor Address: ");
  Serial.println(sensor.getAddress(),HEX);
  Serial.print("Sensor Firmware version: ");
  Serial.println(sensor.getVersion(),HEX);
  Serial.println();

}


void loop() {
  if (Time.minute() != currentPeriod)
  {
    capValue = sensor.getCapacitance();
    Particle.publish("Soil Moisture", String(capValue));
    getMoisture(capValue);
    Serial.print(capValue); //read capacitance register
    //Serial.print(", Temperature: ");
    //Serial.println(sensor.getTemperature()/(float)10); //temperature register
    getWiFiStrength();
    currentPeriod = Time.minute();
    if (waterEnabled)
    {
      if (currentPeriod >= 5 && currentPeriod <= 8)
      {
        if ((capDescription == "Very Dry") || (capDescription == "Dry") || (capDescription == "Normal"))
        {
        Particle.publish("Watering","Watering");
        digitalWrite(solenoidPin, HIGH);
        delay(waterTime);
        digitalWrite(solenoidPin,LOW);
        Particle.publish("Watering","Done");
        }
        else Particle.publish("Watering","Not Needed");
      }
      else Particle.publish("Watering","Not Time");
    }
  }
}

void turnOnWater(unsigned long duration)
{
  digitalWrite(blueLED, HIGH);
  digitalWrite(solenoidPin, HIGH);
  wateringNow = 1;
  delay(duration);
  digitalWrite(blueLED, LOW);
  digitalWrite(solenoidPin, LOW);
  wateringNow = 0;
}

int startStop(String command)   // Will reset the local counts
{
  if (command == "start")
  {
    turnOnWater(waterTime);
    return 1;
  }
  else if (command == "stop")
  {
    digitalWrite(blueLED, LOW);
    digitalWrite(solenoidPin, LOW);
    wateringNow = 0;
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
  RSSIstring = String(wifiRSSI);
  if (wifiRSSI >= 0)
  {
    Serial.println("WiFi RSSI Error");
    RSSIdescription = "error";
    return 0;
  }
  Serial.print("WiFi RSSI:");
  Serial.println(wifiRSSI);
  int strength = map(wifiRSSI, -127, -1, 0, 5);
  Serial.print("The signal strength is: ");
  switch (strength)
  {
    case 0:
      RSSIdescription = "Poor Signal";
      break;
    case 1:
      RSSIdescription = "Low Signal";
      break;
    case 2:
      RSSIdescription = "Medium Signal";
      break;
    case 3:
      RSSIdescription = "Good Signal";
      break;
    case 4:
      RSSIdescription = "Very Good Signal";
      break;
    case 5:
      RSSIdescription = "Great Signal";
      break;
  }
  Serial.println(RSSIdescription);
}


void getMoisture(int value)
{
  if ((value >= 500) || (value <=300))
  {
    capDescription = "error";
  }
  else
  {
    int strength = map(value, 300, 500, 0, 5);
    switch (strength)
    {
      case 0:
        capDescription = "Very Dry";
        break;
      case 1:
        capDescription = "Dry";
        break;
      case 2:
        capDescription = "Normal";
        break;
      case 3:
        capDescription = "Wet";
        break;
      case 4:
        capDescription = "Very Wet";
        break;
      case 5:
        capDescription = "Waterlogged";
        break;
    }
  }
  Particle.publish("Moisture Level", capDescription);
}
