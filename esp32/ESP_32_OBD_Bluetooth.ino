#include "BLEDevice.h"
#include "BLEUtils.h"
#include "BLEServer.h"
 
#define SERVICE_UUID        "efa01cde-af04-4562-8a93-5e3bcf61ae94"
#define CHARACTERISTIC_UUID "c84bdef1-cf13-4ad9-87a8-a36a9e66b7bb"
#define bleServerName "VEEPEAK"

static BLEAddress *pServerAddress;
BLEAdvertisedDevice* myDevice = nullptr;
BLEClient* pClient;
bool dev_connected = false;             //determines if you have connnected to your Bluetooth device

void setup() {
  Serial.begin(9600);           // Start serial communication
  BLEDevice::init("BLE Connection started") BLEinst;

  if (!BLEinst.begin("ESP32_OBD", true)) {
    Serial.println("Init failed!");
    return;
  } else {
    Serial.println("Init succeeded");
  }

  dev_connected = BTinst.connect(veepeak_obd_address); //connect to the OBD address via bluetooth.
  if (dev_connected == false) {
    Serial.println("Connect fail");
    return;
  } else {
    Serial.println("HELLOOOOOOOOO!");
  }
  initializeOBD(); //Initialize OBD 
  delay(10);              
}

//Sending data.
void sendOBD(String cmd) {
  Serial.print("Sending: "); 
  Serial.println(cmd);
  BTinst.print(cmd + "\r");   //send OBD-II command.
  delay(100);
}

//Function for reading OBD response bytes.
void readOBD(String &responseBytes) {
  unsigned long start = millis();
  while (millis() - start < 2000) {  // 2-second timeout
    while (BTinst.available()) {
      char c = BTinst.read();
      if (c == '>') { // End of response
        Serial.println(responseBytes);
        return;
      }
      if (c != '\r' && c != '\n') { // Skip CR/LF
        responseBytes += c;
      }
    }
  }
  Serial.println("Timeout waiting for OBD response");
}

int getSpeed() {
  String response;
  getOBDdata("010D");
  readOBD(response);

  if (response.length() < 6) 
    return -1; // Response too short

  // Response should now look like: "410D3C"
  String speedHex = response.substring(response.length() - 2); // Last 2 chars
  return strtol(speedHex.c_str(), NULL, 16);
}

void initializeOBD() {
  sendOBD("ATZ"); //reset
  delay(1000);
  sendOBD("ATE0");  
  delay(200);  // Echo Off
  sendOBD("ATL0");  
  delay(200);  // Linefeeds Off
  sendOBD("ATS0");  
  delay(200);  // Spaces Off
  sendOBD("ATH0");  
  delay(200);  // Headers Off
  sendOBD("ATPS0");
  delay(1000);
}

void getOBDdata(String pid) {
  sendOBD(pid);  // e.g., "010D" = Speed PID
}

void loop() {
  // Serial.println("The loop is running");
  String received_data;
  int speed_data = 0;
  if (dev_connected) {  //send and receive data.
    Serial.println("Device connected");
    speed_data = getSpeed();
    if (speed_data == -1) {
      Serial.println("Invalid OBD response");
    } else {
      Serial.println(speed_data);
    }
  }
}
