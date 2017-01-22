#include <Arduino.h>
#include <ESP8266WiFi.h>        // https://github.com/esp8266/Arduino
#include <WiFiClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <FS.h>

#include <ESP8266WebServer.h>   // https://github.com/esp8266/Arduino
#include <Wire.h>               // https://github.com/esp8266/Arduino
#include <Adafruit_ADS1015.h>   // https://github.com/adafruit/Adafruit_ADS1X15/

#include <WiFiManager.h>        // https://github.com/tzapu/WiFiManager
#include <SimpleTimer.h>        // https://github.com/jfturcot/SimpleTimer

#include <ArduinoJson.h>        // https://github.com/bblanchon/ArduinoJson

// declare timer
SimpleTimer timer;

// Create an ESP8266 WiFiClient class to connect to the MQTT server.
WiFiClient espclient;
// or... use WiFiFlientSecure for SSL
//WiFiClientSecure client;

// declare MDNS name for OTA
String Hostname = "espgrowstation";

// declare webserver to listen on port 80
ESP8266WebServer server(80);
File fsUploadFile;

// declare ADS
Adafruit_ADS1015 ads;

int soil[4] = {0, 0, 0, 0};

//format bytes
String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

String getContentType(String filename){
  if(server.hasArg("download")) return "application/octet-stream";
  else if(filename.endsWith(".htm")) return "text/html";
  else if(filename.endsWith(".html")) return "text/html";
  else if(filename.endsWith(".css")) return "text/css";
  else if(filename.endsWith(".js")) return "application/javascript";
  else if(filename.endsWith(".png")) return "image/png";
  else if(filename.endsWith(".gif")) return "image/gif";
  else if(filename.endsWith(".jpg")) return "image/jpeg";
  else if(filename.endsWith(".ico")) return "image/x-icon";
  else if(filename.endsWith(".xml")) return "text/xml";
  else if(filename.endsWith(".pdf")) return "application/x-pdf";
  else if(filename.endsWith(".zip")) return "application/x-zip";
  else if(filename.endsWith(".gz")) return "application/x-gzip";
  return "text/plain";
}

bool handleFileRead(String path){
  Serial.println("handleFileRead: " + path);
  if(path.endsWith("/")) path += "index.htm";
  String contentType = getContentType(path);
  String pathWithGz = path + ".gz";
  if(SPIFFS.exists(pathWithGz) || SPIFFS.exists(path)){
    if(SPIFFS.exists(pathWithGz))
      path += ".gz";
    File file = SPIFFS.open(path, "r");
    size_t sent = server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

void handleFileUpload(){
  if(server.uri() != "/edit") return;
  HTTPUpload& upload = server.upload();
  if(upload.status == UPLOAD_FILE_START){
    String filename = upload.filename;
    if(!filename.startsWith("/")) filename = "/"+filename;
    Serial.print("handleFileUpload Name: "); Serial.println(filename);
    fsUploadFile = SPIFFS.open(filename, "w");
    filename = String();
  } else if(upload.status == UPLOAD_FILE_WRITE){
    //Serial.print("handleFileUpload Data: "); Serial.println(upload.currentSize);
    if(fsUploadFile)
      fsUploadFile.write(upload.buf, upload.currentSize);
  } else if(upload.status == UPLOAD_FILE_END){
    if(fsUploadFile)
      fsUploadFile.close();
    Serial.print("handleFileUpload Size: "); Serial.println(upload.totalSize);
  }
}

void handleFileDelete(){
  if(server.args() == 0) return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileDelete: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(!SPIFFS.exists(path))
    return server.send(404, "text/plain", "FileNotFound");
  SPIFFS.remove(path);
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileCreate(){
  if(server.args() == 0)
    return server.send(500, "text/plain", "BAD ARGS");
  String path = server.arg(0);
  Serial.println("handleFileCreate: " + path);
  if(path == "/")
    return server.send(500, "text/plain", "BAD PATH");
  if(SPIFFS.exists(path))
    return server.send(500, "text/plain", "FILE EXISTS");
  File file = SPIFFS.open(path, "w");
  if(file)
    file.close();
  else
    return server.send(500, "text/plain", "CREATE FAILED");
  server.send(200, "text/plain", "");
  path = String();
}

void handleFileList() {
  if(!server.hasArg("dir")) {server.send(500, "text/plain", "BAD ARGS"); return;}

  String path = server.arg("dir");
  Serial.println("handleFileList: " + path);
  Dir dir = SPIFFS.openDir(path);
  path = String();

  String output = "[";
  while(dir.next()){
    File entry = dir.openFile("r");
    if (output != "[") output += ',';
    bool isDir = false;
    output += "{\"type\":\"";
    output += (isDir)?"dir":"file";
    output += "\",\"name\":\"";
    output += String(entry.name()).substring(1);
    output += "\"}";
    entry.close();
  }

  output += "]";
  server.send(200, "text/json", output);
}

/////////////////////////////////////////////////////////////////////////
void configModeCallback (WiFiManager *myWiFiManager) {
    Serial.println("Entered config mode");
    Serial.println(WiFi.softAPIP());
    Serial.println(myWiFiManager->getConfigPortalSSID());
}

void readADC() {
  int16_t adc0, adc1, adc2, adc3;
  adc0 = ads.readADC_SingleEnded(0);
  adc1 = ads.readADC_SingleEnded(1);
  adc2 = ads.readADC_SingleEnded(2);
  adc3 = ads.readADC_SingleEnded(3);

  Serial.print("AIN0: "); Serial.println(adc0);
  Serial.print("AIN1: "); Serial.println(adc1);
  Serial.print("AIN2: "); Serial.println(adc2);
  Serial.print("AIN3: "); Serial.println(adc3);

  // 1400 Dry
  // 700 Wet

  adc0 = constrain(adc0, 700, 1400);
  adc1 = constrain(adc1, 700, 1400);
  adc2 = constrain(adc2, 700, 1400);
  adc3 = constrain(adc3, 700, 1400);

  soil[0] = map(adc0, 700, 1400, 100, 0);
  soil[1] = map(adc1, 700, 1400, 100, 0);
  soil[2] = map(adc2, 700, 1400, 100, 0);
  soil[3] = map(adc3, 700, 1400, 100, 0);

  Serial.print("SOIL0: "); Serial.print(soil[0]); Serial.println("%");
  Serial.print("SOIL1: "); Serial.print(soil[1]); Serial.println("%");
  Serial.print("SOIL2: "); Serial.print(soil[2]); Serial.println("%");
  Serial.print("SOIL3: "); Serial.print(soil[3]); Serial.println("%");
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(74880);

    Serial.println("Booting up...");

    WiFi.hostname(Hostname);

    //WiFiManager
    //Local intialization. Once its business is done, there is no need to keep it around
    WiFiManager wifiManager;

    //reset saved settings
    //wifiManager.resetSettings();
    wifiManager.setDebugOutput(true);
    wifiManager.setAPCallback(configModeCallback);

    char tmpHostname[Hostname.length()];
    Hostname.toCharArray(tmpHostname, Hostname.length());
    wifiManager.autoConnect(tmpHostname);

    Serial.println("connected!");


    // Set up mDNS responder:
    // - first argument is the domain name, in this example
    //   the fully-qualified domain name is "esp8266.local"
    // - second argument is the IP address to advertise
    //   we send our IP address on the WiFi network
    if (!MDNS.begin(tmpHostname)) {
        Serial.println("Error setting up MDNS responder!");
        while(1) {
            delay(1000);
        }
    }
    Serial.println("mDNS responder started");

    // Initialize ArduinoOTA
    // Set password for Arduino OTA upload
    //ArduinoOTA.setPassword((const char *)"123");

    ArduinoOTA.onStart([]() {
      Serial.println("Start");
    });

    ArduinoOTA.onEnd([]() {
      Serial.println("\nEnd");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    });

    ArduinoOTA.onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed");
    });

    ArduinoOTA.begin();

    // Initialize SPIFFS
    SPIFFS.begin();
    {
        Dir dir = SPIFFS.openDir("/");
        while (dir.next()) {
            String fileName = dir.fileName();
            size_t fileSize = dir.fileSize();
            Serial.printf("FS File: %s, size: %s\r\n", fileName.c_str(), formatBytes(fileSize).c_str());
        }
    }

    Serial.print("Open http://");
    Serial.print(Hostname);
    Serial.println("/edit to see the file browser");


    //SERVER INIT
    //list directory
    server.on("/list", HTTP_GET, handleFileList);

    //load editor
    server.on("/edit", HTTP_GET, [](){
        if(!handleFileRead("/edit.htm")) server.send(404, "text/plain", "FileNotFound");
    });

    //create file
    server.on("/edit", HTTP_PUT, handleFileCreate);

    //delete file
    server.on("/edit", HTTP_DELETE, handleFileDelete);

    //first callback is called after the request has ended with all parsed arguments
    //second callback handles file uploads at that location
    server.on("/edit", HTTP_POST, [](){ server.send(200, "text/plain", ""); }, handleFileUpload);

    server.on("/config", HTTP_GET, []() {
        server.send(200, "text/json", "config");
    });

    //called when the url is not defined here
    //use it to load content from SPIFFS
    server.onNotFound([](){
        if(!handleFileRead(server.uri()))
            server.send(404, "text/plain", "FileNotFound");
    });

    //get heap status, analog input value and all GPIO statuses in one json call
    server.on("/all", HTTP_GET, [](){
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();

        root["heap"] = ESP.getFreeHeap();
        root["analog"] = analogRead(A0);
        root["gpio"] = (uint32_t)(((GPI | GPO) & 0xFFFF) | ((GP16I & 0x01) << 16));

        size_t size = root.measureLength() + 1;
        char json[size];
        root.printTo(json, size);

        server.send(200, "text/json", json);
    });


    //get heap status, analog input value and all GPIO statuses in one json call
    server.on("/data", HTTP_GET, [](){
        DynamicJsonBuffer jsonBuffer;
        JsonObject& root = jsonBuffer.createObject();

        root["soil0"] = soil[0];
        root["soil1"] = soil[1];
        root["soil2"] = soil[2];
        root["soil3"] = soil[3];

        size_t size = root.measureLength() + 1;
        char json[size];
        root.printTo(json, size);

        server.send(200, "text/json", json);
    });


    server.begin();
    Serial.println("HTTP server started");
    MDNS.addService("http", "tcp", 80);

    ads.setGain(GAIN_ONE);
    ads.begin();

    // Starting timer jobs
    timer.setInterval(1000, readADC);   // Update display
}

void loop() {
    ArduinoOTA.handle();
    server.handleClient();
    timer.run();
}
