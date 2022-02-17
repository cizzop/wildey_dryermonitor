//include library
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <DallasTemperature.h>
#include <PubSubClient.h>

// ROLLING AVERAGE STUFF
// Define the number of samples to keep track of. The higher the number, the
// more the readings will be smoothed, but the slower the output will respond to
// the input. Using a constant rather than a normal variable lets us use this
// value to determine the size of the readings array.
//  http://www.arduino.cc/en/Tutorial/Smoothing
const int numReadings = 12;     // take 12 readings, 5 seconds apart
int readings[numReadings];      // the readings from the analog input
int readIndex = 0;              // the index of the current reading
int total = 68*12;                  // the running total
int average = 68;                // the average

//DALLAS DS18B20 STUFF
// Data wire is plugged into pin D1 on the ESP8266 12-E - GPIO 5
#define ONE_WIRE_BUS 5
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature DS18B20(&oneWire);
char temperatureCString[6];
char temperatureFString[6];
float current_temperature = 0;

//DRYER STATUS STUFF
int on_threshold = 87;
bool dryer_on = false;
bool dryer_on_last_cycle = false;
unsigned long time_on = 0;
unsigned long time_now = 0;
unsigned long runtime = 0;
float max_temperature = 0;
int duration_seconds = 0;
int duration_minutes = 0;

//WIFI STUFF
const char* ssid = "private";
const char* password = "private";
WiFiClient espClient;

//MQTT STUFF
#define mqtt_server "private"
#define current_temperature_topic "DRYER_MONITOR/currenttemperature"
#define max_temperature_topic "DRYER_MONITOR/maxtemperature"
#define status_topic "DRYER_MONITOR/status"
#define runminutes_topic "DRYER_MONITOR/runtime"
PubSubClient client(espClient);

//DEBUG
bool use_serial_debug = false;

void setup() {

  // IC Default 9 bit. If you have troubles consider upping it 12. Ups the delay giving the IC more time to process the temperature measurement
  DS18B20.begin();

  //Set hostname
  WiFi.hostname("DRYER_MONITOR");

  // Connect to WiFi network
  WiFi.begin(ssid, password);

  if (use_serial_debug)
  {
    Serial.begin(115200);
  }

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.print("Connected to ");
  Serial.println(ssid);

  //set mqtt server
  client.setServer(mqtt_server, 1883);

  // initialize all the readings to 68 for the rolling average
  for (int thisReading = 0; thisReading < numReadings; thisReading++) {
    readings[thisReading] = 68;
  }
}

float ReadTemp() //reads temperature of sensor
{
  float tempF;
  float tempC;
  do {
    DS18B20.requestTemperatures();
    tempC = DS18B20.getTempCByIndex(0);
    dtostrf(tempC, 2, 2, temperatureCString);
    tempF = DS18B20.getTempFByIndex(0);
    dtostrf(tempF, 3, 2, temperatureFString);
    delay(100);
    return tempF;
  } while (tempC == 85.0 || tempC == (-127.0));
}

void WiFi_Connect()
{
  WiFi.disconnect();
  delay(1000);
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }
}

void loop() {

  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi_Connect();
  }
  else
  {
    delay(5000); //take a reading every 5 seconds
    current_temperature = ReadTemp();

    //just in case we get a bad reading, use the last reading instead
    if (isnan(current_temperature))
    {
      if (readIndex > 0){
        current_temperature = readings[readIndex - 1];
      }
      else {
        current_temperature = readings[numReadings];
      }
    }

    // subtract the last reading:
    total = total - readings[readIndex];
    // read from the sensor:
    readings[readIndex] = current_temperature;
    // add the reading to the total:
    total = total + readings[readIndex];
    // advance to the next position in the array:
    readIndex = readIndex + 1;

    // if we're at the end of the array...
    if (readIndex >= numReadings) {
      // ...wrap around to the beginning:
      readIndex = 0;
    }

    // calculate the average:
    average = total / numReadings;

    //is the dryer on?
    if (average > on_threshold) {
      dryer_on = true;
    }
    else {
      dryer_on = false;
    }

    //dryer just turned on, init timer and vars
    if (!dryer_on_last_cycle && dryer_on) {
      time_on = millis();
      max_temperature = 0;
    }

    //how long has the dryer been running?
    if (dryer_on) {
      time_now = millis();
      runtime = time_now - time_on;
      duration_seconds = (runtime / 1000)%60;
      duration_minutes = (runtime / 60000);
    }

    //what is the max observed temp
    if (dryer_on) {
      if (current_temperature > max_temperature) {
        max_temperature = current_temperature;
      }
    }

    //dryer just turned off, do stuff
    if (!dryer_on && dryer_on_last_cycle) {

    }

    //send mqtt updates while running
    if (dryer_on) {
      client.connect("DRYER_MONITOR", "private","private");
      Serial.println(client.state());
      client.publish(current_temperature_topic, String(current_temperature).c_str(), true);
      client.publish(max_temperature_topic, String(max_temperature).c_str(), true);
      client.publish(status_topic, String(dryer_on).c_str(), true);
      client.publish(runminutes_topic, String(duration_minutes).c_str(), true);
    }

    //send only the off status once
    if (!dryer_on && dryer_on_last_cycle) {
      client.connect("DRYER_MONITOR", "private","private");
      Serial.println(client.state());
      client.publish(status_topic, String(dryer_on).c_str(), true);
    }

    //send the current temperature all the time but once per average
    if (((numReadings - 1) == readIndex) && !dryer_on) {
      client.connect("DRYER_MONITOR", "private","private");
      Serial.println(client.state());
      client.publish(current_temperature_topic, String(current_temperature).c_str(), true);
    }

    //set this for the next cycle of the jawn
    dryer_on_last_cycle = dryer_on;
  }
}
