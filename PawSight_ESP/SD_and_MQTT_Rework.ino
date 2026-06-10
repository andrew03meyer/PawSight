#include <SPI.h>
#include <SD.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <DHT11.h>

// SD CARD STUFF
#define SD_CS 2
#define SD_SCK 18
#define SD_MOSI 23
#define SD_MISO 19
#define TS_DOUT 14

// SLEEP STUFF
#define uS_TO_S_FACTOR 1000000ULL

File tempsFile;
File gpsFile;
File appendFile;

// SD STUFF
SPIClass spi = SPIClass(VSPI);

//GPS STUFF
#define RX 16 // Connect to GPS Module's TX pin
#define TX 17 // Connect to GPS Module's RX pin
HardwareSerial gpsSerial(2);

// WIFI STUFF
#define EAP_ANONYMOUS_IDENTITY ""  // leave it as anonymous
#define EAP_IDENTITY ""            
#define EAP_USERNAME ""            
char* ssid = "eduroam";                       // eduroam SSID
WiFiClient wifiClient;
// char password[64];

// MQTT STUFF
#define MQTT_BROKER "broker.hivemq.com"
#define MQTT_PORT (1883)
#define MQTT_PUBLISH_TEMPS ""  // Use your own ID here
#define MQTT_PUBLISH_GPS ""  // 
#define MQTT_SUBSCRIBE_TOPIC ""   // Use your own ID here
#define MQTT_ID ""  // Use your own ID here
PubSubClient client(wifiClient);

// SEMAPHOR STUFF
SemaphoreHandle_t sdMutex;

// HEAT SENSOR STUFF
DHT11 dht11(TS_DOUT);

// TICK STUFF
RTC_DATA_ATTR int tickCount = 1;


// ----------------------------------------------------------------------------------
//                             Helper Functions                                     |
// ----------------------------------------------------------------------------------
String getField(String data, int index) {
  int start = 0;
  for (int i = 0; i < index; i++) {
    start = data.indexOf(',', start) + 1;
    if (start == 0) return "";
  }
  int end = data.indexOf(',', start);
  if (end == -1) end = data.length();
  return data.substring(start, end);
}

float DdmToLatLong(float Ddm){
    int degrees = (int)(Ddm / 100);
    float minutes = Ddm - (degrees * 100);
    return degrees + (minutes / 60.0);
}

// ----------------------------------------------------------------------------------
//                                     WIFI                                         |
// ----------------------------------------------------------------------------------
// void readPassword() {
//   Serial.println();
//   Serial.print("Password: ");
//   // Read string
//   while (Serial.available() == 0) {}  // Wait for input
//   String line = Serial.readString();
//   line.trim();
//   strcpy(password, line.c_str());
//   Serial.println();
// }

void EduroamSetup(String password) {
  Serial.print("Connecting to network:");
  Serial.print(ssid);
  // readPassword();
  // Disconnect first if there was another connection
  WiFi.disconnect(true);

  // Try connecting to Eduroam - for 10s
  WiFi.begin(ssid, WPA2_AUTH_PEAP, EAP_IDENTITY, EAP_USERNAME, password);  // without CERTIFICATE, RADIUS server EXCEPTION ”for old devices” required
  int tick = 0;
  while (WiFi.status() != WL_CONNECTED && tick < 10000) {
    // vTaskDelay(pdMS_TO_TICKS(500));
    delay(500);
    Serial.print(F("."));
    tick += 500;
  }
  
  if(WiFi.status() == WL_CONNECTED){
    Serial.println("");
    Serial.println(F("Eduroam is connected!"));
    Serial.println(F("IP address set: "));
    Serial.println(WiFi.localIP());  //print LAN IP
  }
  else {
    Serial.println("Not connected to Eduroam");
    // WiFi.disconnect();
  }
}

void WifiSetup() {

  // Does wifi file exist on SD card
  File wifiFile;
  String path = "/wifi.txt";
  if (!SD.exists(path)) {
    Serial.println(String(path) + " doesn't exist. Please create.");

  } else {
    int tick = 0;

    // Get passwords for eduroam and user wifi
    xSemaphoreTake(sdMutex, portMAX_DELAY);
    wifiFile = SD.open(path, FILE_READ);
    String eduroamPass = wifiFile.readStringUntil('\n');
    String userWifi = wifiFile.readStringUntil('\n');
    eduroamPass.trim();
    userWifi.trim();
    wifiFile.close();
    xSemaphoreGive(sdMutex);

    // Try setting up eduroam
    EduroamSetup(eduroamPass);

    // If not succesfull, try setting up user wifi
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect(true);
      delay(100);
      Serial.println("Eduroam failed, trying User WiFi...");
      // Serial.println("Pass: " + userWifi);

      // Try establishing a connection - for 10s
      String ssid = getField(userWifi, 0);
      String pass = getField(userWifi, 1);
      ssid.trim();
      pass.trim();
      WiFi.begin(ssid, pass);
      int tick = 0;
      while (WiFi.status() != WL_CONNECTED && tick < 10000) {
          delay(500);
          Serial.print(".");
          tick += 500;
      }
    }
  }


}

// ----------------------------------------------------------------------------------
//                                     CLOCK                                         |
// ----------------------------------------------------------------------------------

void clockSetup() {
  // Configure time to use two NTP servers
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  setenv("TZ", "GMT0BST,M3.5.0/1,M10.5.0", 1);
  tzset();
  Serial.print(F("Waiting for NTP time sync:"));
  time_t nowSecs = time(0);  // Get seconds since epoch
  while (nowSecs < 100) {
    // vTaskDelay(pdMS_TO_TICKS(500));
    delay(500);
    nowSecs = time(0);  // Get seconds since epoch until we have jumped to current time
    Serial.print(".");
  }
}

void printTime() {
  time_t nowSecs = time(0);  // Get seconds since epoch
  struct tm timeinfo;        // structure tm holds details about current date/time
  // Print GMT time
  localtime_r(&nowSecs, &timeinfo);  // Convert raw seconds into GMT time
  Serial.print(F("Current time (GMT): "));
  Serial.print(asctime(&timeinfo));  // Print GMT time
}

// ----------------------------------------------------------------------------------
//                                     MQTT                                         |
// ----------------------------------------------------------------------------------

void mqttConnect() {
  // Loop until we’re reconnected
  int attempts = 0;
  while (!client.connected() && attempts < 5) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect((String(MQTT_ID) + (millis() % 1000)).c_str())) {
      Serial.println("connected");
      client.subscribe(MQTT_SUBSCRIBE_TOPIC);
      client.setBufferSize(4096);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // vTaskDelay(pdMS_TO_TICKS(5000));
      attempts++;
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();
}

// ----------------------------------------------------------------------------------
//                                     SD                                           |
// ----------------------------------------------------------------------------------

void sd_setup() {
  spi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);

  while (!SD.begin(SD_CS, spi, 80000000)) {
    Serial.println("Mount failed — check wiring/CS pin");
  }

  Serial.println("SD OK");
}

void appendToFile(const char* path, const char* message) {
  // Serial.println("Appending to " + String(path));
  xSemaphoreTake(sdMutex, portMAX_DELAY);

  if (!SD.exists(path)) {
    // Serial.println(String(path) + " doesn't exist. Creating...");
    appendFile = SD.open(path, FILE_WRITE);
    appendFile.close();
  }

  appendFile = SD.open(path, FILE_APPEND);

  if (appendFile) {
    uint64_t ms = esp_timer_get_time() / 1000;
    String line = String(message) + "," + String(ms);
    // Serial.println(line);
    appendFile.println(line);
    appendFile.close();
    // Serial.println("Append OK");
  } else {
    Serial.println("Append failed");
  }
  xSemaphoreGive(sdMutex);
}

String getLastLine(File& f) {
  String lastLine = "";
  f.seek(0);
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() > 0) lastLine = line;
  }
  return lastLine;
}

void appendToTempsFile() {
  // https://github.com/dhrubasaha08/DHT11/blob/main/examples/ReadTemperature/ReadTemperature.ino
  int temperature = dht11.readTemperature();
  if (temperature != DHT11::ERROR_CHECKSUM && temperature != DHT11::ERROR_TIMEOUT) {
    appendToFile("/temps.csv", String(temperature).c_str());
  } else {
    // Print error message based on the error code.
    Serial.println(DHT11::getErrorString(temperature));
  }
}

void appendToGpsFile() {
  // If sensor is available
  if (gpsSerial.available() > 0) {
    bool lineFound = false;
    // While it's available and a value hasn't been found
    while (gpsSerial.available() > 0 && !lineFound)
      {
        // Read a byte of data from the buffer
        String line = gpsSerial.readStringUntil('\n');
        line.trim();

        // Check if it's the correct data line
        if(line.startsWith("$GPGGA")){
          Serial.println(line);
          // Remove unwanted data
          String shortenedLine = String(DdmToLatLong(getField(line, 2).toFloat()), 6) + "," + getField(line, 3) + "," 
            + String(DdmToLatLong(getField(line, 4).toFloat()), 6) + "," + getField(line, 5);

          // Add to CSV
          appendToFile("/gps.csv", shortenedLine.c_str());
          lineFound = true;
        }
    }
  } else {
    Serial.println("GPS not found");
  }
}


// ----------------------------------------------------------------------------------
//                           RTOS Methods                                           |
// ----------------------------------------------------------------------------------

void MqttTransfer() {
  if (!client.connected()) {
    mqttConnect();
  }
  client.loop();  // PubSub checking for incomming messages

  int publishCount = 0;
  int failedCount = 0;

  //Send temperature data
  xSemaphoreTake(sdMutex, portMAX_DELAY);
  tempsFile = SD.open("/temps.csv", FILE_READ);
  StaticJsonDocument<256> doc;

  if (tempsFile) {
    // Serial.println("temps file!");
    String lastLine = getLastLine(tempsFile);
    tempsFile.seek(0);
    int lastComma = lastLine.indexOf(',');

    while (tempsFile.available()) {
      String line = tempsFile.readStringUntil('\n');
      line.trim();
      // if (line.length() == 0) {continue;}

      int commaPlace = line.indexOf(',');
      
      doc["value"] = line.substring(0, commaPlace);

      // if device time is > 1st jan 2025: today - (latest timestamp - row timestamp)
      // else: push timestamp
      if (time(0) > 1735689600) {
        uint64_t latestTs = (uint64_t)lastLine.substring(lastComma + 1).toDouble();
        uint64_t rowTs = (uint64_t)line.substring(commaPlace + 1).toDouble();
        uint64_t diffTs = latestTs - rowTs;
        doc["timestamp"] = (long)(time(0) - (diffTs / 1000));
      } else {
        doc["timestamp"] = (uint32_t)line.substring(commaPlace + 1).toDouble();
      }

      char payload[100];
      // Take doc (JSON document), serialize it and save it to payload
      serializeJson(doc, payload);

      if (client.publish(MQTT_PUBLISH_TEMPS, payload)) {
        publishCount++;
      } else {
        failedCount++;
      }

      // Allow watchdog to reset
      // vTaskDelay(1);
      doc.clear();
    }

    Serial.println("Publish count: " + String(publishCount));
    Serial.println("Failed packets: " + String(failedCount));
    tempsFile.close();

    // Delete temps file if transfer was successfull
    if (publishCount > 0) {
      SD.remove("/temps.csv");
    }
  }
  

  gpsFile = SD.open("/gps.csv", FILE_READ);
  publishCount = 0;
  
  if(gpsFile){
    // Serial.println("GPS file!");
    String lastLine = getLastLine(gpsFile);
    gpsFile.seek(0);
    int lastComma = lastLine.indexOf(',');

    while (gpsFile.available()) {
      String line = gpsFile.readStringUntil('\n');
      line.trim();
      // if (line.length() == 0) {continue;}

      int commaPlace = line.indexOf(',');
      
      doc["latitude"] = getField(line, 0);
      doc["longitude"] = getField(line, 2);
      doc["gps_accuracy"] = 5;

      // if device time is > 1st jan 2025: today - (latest timestamp - row timestamp)
      // else: push timestamp
      // if (time(0) > 1735689600) {
      //   uint64_t latestTs = (uint64_t)lastLine.substring(lastComma + 1).toDouble();
      //   uint64_t rowTs = (uint64_t)line.substring(commaPlace + 1).toDouble();
      //   uint64_t diffTs = latestTs - rowTs;
      //   doc["timestamp"] = (long)(time(0) - (diffTs / 1000));
      // } else {
      //   doc["timestamp"] = (uint32_t)line.substring(commaPlace + 1).toDouble();
      // }

      char payload[200];
      // Take doc (JSON document), serialize it and save it to payload
      serializeJson(doc, payload);

      if (client.publish(MQTT_PUBLISH_GPS, payload)) {
        publishCount++;
      } else {
        failedCount++;
      }
      doc.clear();
    }
    
    // Delete file if transfer was successfull
    if (publishCount > 0) {
      SD.remove("/gps.csv");
    }
  }
  
  xSemaphoreGive(sdMutex);
}



void setup() {
  esp_sleep_enable_timer_wakeup(2 * uS_TO_S_FACTOR);
  Serial.begin(115200);
  sd_setup();

  //GPS Serial Connection
  gpsSerial.begin(9600, SERIAL_8N1, 16, 17);  // baud, config, RX pin, TX pin
  delay(500);

  sdMutex = xSemaphoreCreateMutex();
}

void loop() {
  appendToGpsFile();
  if (tickCount % 30 == 0){
    appendToTempsFile();
  }

  if (tickCount % 60 == 0) {
      if (WiFi.status() != WL_CONNECTED) {
          WifiSetup();
          clockSetup();
          printTime();
          client.setServer(MQTT_BROKER, MQTT_PORT);
          client.setCallback(mqttCallback);
      } 
      if (WiFi.status() == WL_CONNECTED) {
        MqttTransfer();
      }
      tickCount = 0;
  }
  tickCount++;
  esp_deep_sleep_start();
}