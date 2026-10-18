#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <LittleFS.h>
#include <ArduinoJson.h>

const char* ssid = "E************";
const char* password = "Emmanu*********8";
const char* apiKey = "***********";//GEMINI API kEY HERE

const char* host = "generativelanguage.googleapis.com";
const char* modelEndpoint = "/v1beta/models/gemini-3.8-flash:generateContent?key=";

long delayInterval=0, oldtime=0;
bool isDone=false;
String mainResponse ="";

void processResponse(const String &result){//this method helps breakdown incoming json string response from gemini
    
    //Let's remove contnet length/end code to have our main jstring
    int jsonStart = result.indexOf('{');
    int jsonEnd = result.lastIndexOf('}');
    
    if (jsonStart != -1 && jsonEnd != -1 && jsonEnd > jsonStart) {
      mainResponse = result.substring(jsonStart, jsonEnd + 1);
    }
    else {
      Serial.println("CRITICAL ERROR: Response does not contain a valid JSON object!");
      return;
    }

    JsonDocument doc;

    DeserializationError error = deserializeJson(doc, mainResponse);

    if (error) {
        Serial.print("JSON parsing failed: ");
        Serial.println(error.c_str());
        return;
    }

    if (doc["error"]["code"].is<int>()) {
        int responseCode = doc["error"]["code"].as<int>();
        const char* responseText= doc["error"]["message"].as<const char*>();

        if(responseCode==503){
          Serial.println(responseText);
          delayInterval>=10000? isDone=true:delayInterval+=2000;
          isDone? printf("Gemini is too busy so am done trying...try again in a later time\n"):printf("Gemini is busy....Retrying in %d sec\n", delayInterval/1000);
        
        }
        else{
          Serial.println(responseText);
          isDone=true;
        }

        Serial.println(responseCode);
        return; 
    }
    else if (doc["candidates"][0]["content"]["parts"][0]["text"].is<const char*>()) {
        const char* mainResponsee =doc["candidates"][0]["content"]["parts"][0]["text"];
        mainResponse=mainResponsee;
        Serial.printf("GEMINI says:%s\n", mainResponsee);
        isDone=true;
    }
}

void analyzeImageWithGemini(String filename, String textPrompt) {
  if (!LittleFS.exists(filename)) {
    Serial.println("Error: Image file not found!");
    return;
  }

  File file = LittleFS.open(filename, "r");
  if (!file) {
    Serial.println("Error: Failed to open image.");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure(); // Skip certificate verification to save RAM

  // === CRITICAL FIX: Limit SSL RX/TX internal buffer sizes to stop memory exhaustion crashes ===
 // client.setBufferSizes(1024, 1024); 

  Serial.println("Connecting to Google Gemini API...");
  if (!client.connect(host, 443)) {
    Serial.println("Connection to Gemini failed!");
    file.close();
    delayInterval>=4000? isDone=true:delayInterval+=2000; //max retry count is five
    isDone? printf("Done retyring conection to Gemini domain...check code\n"):printf("Connection to Gemini failed!....Retrying in %d sec\n", delayInterval/1000);
    return;
  }
  
  Serial.printf("Successfully connected to: %s\n", host);

  // Calculate strict payload dimensions
  size_t originalSize = file.size();
  size_t base64Size = ((originalSize + 2) / 3) * 4; 

  String jsonHeader = "{\"contents\":[{\"parts\":[{\"text\":\"" + textPrompt + "\"},{\"inlineData\":{\"mimeType\":\"image/jpeg\",\"data\":\"";
  String jsonFooter = "\"}}]}]}";
  size_t totalContentLength = jsonHeader.length() + base64Size + jsonFooter.length();

  // Send exact, clean HTTP headers
  client.print("POST ");
  client.print(modelEndpoint);
  client.print(apiKey);
  client.println(" HTTP/1.1");
  client.print("Host: ");
  client.println(host);
  client.println("Content-Type: application/json");
  client.print("Content-Length: ");
  client.println(totalContentLength);
  client.println("Connection: close");
  client.println(); // Header spacing requirement

  // Stream structural JSON header 
  client.print(jsonHeader);

  // Here i stream & encode image bytes in 3-byte block sequences straight out of LittleFS
  const int inputChunkSize = 3;
  uint8_t inputBuffer[inputChunkSize];
  const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  while (file.available()) {
    int bytesRead = file.read(inputBuffer, inputChunkSize);
    if (bytesRead == 0) break;

    uint32_t b24 = 0;
    for (int i = 0; i < bytesRead; i++) {
      b24 |= inputBuffer[i] << (16 - (i * 8));
    }

    client.print(b64_table[(b24 >> 18) & 0x3F]);
    client.print(b64_table[(b24 >> 12) & 0x3F]);
    client.print(bytesRead > 1 ? b64_table[(b24 >> 6) & 0x3F] : '=');
    client.print(bytesRead > 2 ? b64_table[b24 & 0x3F] : '=');
  }
  file.close();

  // Stream structural JSON footer
  client.print(jsonFooter);

  // Read Response with safety timeout checks
  Serial.println("Payload deployed. Waiting for response...");
  
  uint32_t startTime = millis();
  while (client.connected() && !client.available()) {
    if (millis() - startTime > 10000) { // 10 second timeout protection
      Serial.println("Error: API Response Timeout.");
      client.stop();
      return;
    }
    delay(10);
  }

  // Skip HTTP response headers safely
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line == "\r") {
      break; 
    }
  }

  // Parse out response string blocks safely
  String responseText = "";
  while (client.available()) {
    responseText += (char)client.read();
  }
  client.stop();
  
  Serial.println("--- Gemini Response ---");
  Serial.println(responseText);
  Serial.println("Processed response below");
  processResponse(responseText);

}


void setup() {
  Serial.begin(115200);
  LittleFS.begin(true);
  WiFi.disconnect(true, false, 100);
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Connected!");

  // === CRITICAL SYNC STEP ===
  // Configures network time protocol using public pools. 
  // 0 offset, 0 daylight saving (UTC target).
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  
  Serial.print("Synchronizing time...");
  time_t now = time(nullptr);
  while (now < 8 * 3600 * 2) { // Loop until epoch updates to actual date
    delay(500);
    Serial.print(".");
    now = time(nullptr);
  }
  Serial.println("\nTime Synchronized successfully!");
}


void loop() {
  if (!isDone){
    if((millis()-oldtime)>=delayInterval){
      analyzeImageWithGemini("/start_image.jpg", "hey gemini, what do you see in less than 20 words?");
      oldtime=millis();
    }
  }
  //if(isDone) delayInterval=0;
  // Empty
}
