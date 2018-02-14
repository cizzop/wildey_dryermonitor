// Distributed with a free-will license.
// Use it any way you want, profit or free, provided it fits in the licenses of its associated works.
// Clothes Dryer Monitoring with ESP8266 & Dallas DS18B20

//include library
#include <ESP8266WiFi.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BlynkSimpleEsp8266.h>
#include <PubSubClient.h>

//blynk stuff
char auth[] = ""; //private
WidgetLCD lcd(V3);

//mqtt shit
#define mqtt_server ""//private
#define temperature_topic "/appliance/dryer/temperature"
#define status_topic "/appliance/dryer/status"
#define runminutes_topic "/appliance/dryer/runtime"
float oldmqtttemp = 0;
float diff = 1; //wont update mqtt unless difference is 1 degrees

//should we update twitter?
bool update_twitter = true;

//mqtt start
WiFiClient espClient;
PubSubClient client(espClient);

//Stuff for Dallas DS18B20
// Data wire is plugged into pin D1 on the ESP8266 12-E - GPIO 5
#define ONE_WIRE_BUS 5
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature.
DallasTemperature DS18B20(&oneWire);
char temperatureCString[6];
char temperatureFString[6];

//should we ouput serial?
bool use_serial = false;

//wifi credentials
const char* ssid = "";//private
const char* password = "";//private

float temperature = 0;
float maxtemp = 0;
unsigned long timeon = 0;
unsigned long dryduration = 0;
int durationseconds = 0;
int durationminutes = 0;
bool updatemqtt = 1;
bool dryer_running = 0;
bool cooldown_running = 0;
unsigned long runtime = 0;
unsigned long timenow = 0;
int debug = 1;

//main setup - runs once
void setup()
{
  // IC Default 9 bit. If you have troubles consider upping it 12. Ups the delay giving the IC more time to process the temperature measurement
  DS18B20.begin();

  // Connect to WiFi network
  WiFi.begin(ssid, password);

  if (use_serial)
  {
    Serial.begin(115200);
  }

  delay(10);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);

  // Get the IP address of ESP8266
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  //set mqtt
  client.setServer(mqtt_server, 1883);

  //start blynk
  Blynk.config(auth);
  Blynk.connect();
  while (Blynk.connect() == false) {
    // Wait until connected
  }
}

float ReadTemp() //reads temperature of sensor
{
  float tempF;
  float tempC;
  //float temperature;
  do {
    DS18B20.requestTemperatures();
    tempC = DS18B20.getTempCByIndex(0);
    dtostrf(tempC, 2, 2, temperatureCString);
    tempF = DS18B20.getTempFByIndex(0);
    dtostrf(tempF, 3, 2, temperatureFString);
    delay(100);
    temperature = tempF;
    return temperature;
  } while (tempC == 85.0 || tempC == (-127.0));
}

void monitorDryer()
{
  //get the temperature.

  temperature = ReadTemp();

  //update mqtt if we need to
  if (abs(oldmqtttemp - temperature) > diff)
  {
    //Serial.println(abs(oldmqtttemp - ));
    updatemqtt = 1;
    oldmqtttemp = temperature;
  }

    // Output data to serial monitor and blynk
  if isnan(temperature)
  {
    return;
  }
  else
  {
    Blynk.virtualWrite(V1, temperature);

    if (temperature > 87 && dryer_running == 0 && cooldown_running == 0)
    {
      //dryer just turned on. this runs once per cycle.
      debug = 1;
      dryer_running = 1;
      timeon = millis();
      runtime = 0;
      durationseconds = 0;
      durationminutes = 0;
      maxtemp = temperature;
    }

    if (dryer_running)
    {
      //dryer is running. runs constantly while drying
      debug = 2;
      timenow = millis();
      runtime = timenow - timeon;
      durationseconds = (runtime / 1000)%60;
      durationminutes = (runtime / 60000);
      lcd.print(0,0,String(durationminutes) + " min  " + String(durationseconds) + " sec");

      //Serial.println(runtime);

      if (temperature > maxtemp)
      {
        maxtemp = temperature;
      }

      if (durationminutes >= 3 && temperature < 95)
      {
        //dryer just finished. runs once per cycle.
        debug = 3;
        Serial.println("Dryer Finished. Entering Cooldown");
        dryer_running = 0;
        cooldown_running = 1;
        sendBlynkNotifications();
      }
    }
    else if ((temperature < 86 && cooldown_running) || (temperature > 97 && cooldown_running))
      {
        //runs until dryer has cooled down or restarted. reset everything.
        debug = 4;
        Serial.println("Reached 86 degrees. Cooldown Over.");
        cooldown_running = 0;
      }
      else
      {
        debug = 0;
      }
  }
}
    ///////////////////////////////////////////////////////////////////


void sendBlynkNotifications()
{
  if (update_twitter)
  {
    Blynk.notify("DRYER DONE! Max Temp: " + String(maxtemp) + " Duration: " + String(durationminutes) + ":" + String(durationseconds));
    Blynk.tweet("DRYER DONE! Max Temp: " + String(maxtemp) + " Duration: " + String(durationminutes) + ":" + String(durationseconds));
  }
  else
  {
    Serial.println("DRYER DONE, twatter would be updated here");
  }
}

void WiFi_Connect()
{
  Serial.println("RECONNECTING");
  WiFi.disconnect();
  delay(1000);
  WiFi.begin(ssid, password);

  // Wait for connection
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);

  // Get the IP address of ESP8266
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void loop()
{
  if (WiFi.status() != WL_CONNECTED)
  {
    WiFi_Connect();
  }
  else
  {
    Blynk.run();
    monitorDryer();

    if (use_serial)
    {
      Serial.print("debug: ");
      Serial.print(debug);
      Serial.print("   dryer_running: ");
      Serial.print(dryer_running);
      Serial.print("   cooldown_running: ");
      Serial.print(cooldown_running);
      Serial.print("   updatemqtt: ");
      Serial.print(updatemqtt);
      Serial.print("   runtime: ");
      Serial.println(runtime);
    }

    //Serial.println(temperature);

    //send mqtt update if we got a good reading
    if (!isnan(temperature))
    {
      if (updatemqtt == 1)
      {
        updatemqtt = 0;
        client.connect("HASS");
        client.publish(temperature_topic, String(temperature).c_str(), true);
        client.publish(status_topic, String(dryer_running).c_str(), true);
        client.publish(runminutes_topic, String(durationminutes).c_str(), true);
      }
    }

  }
}
