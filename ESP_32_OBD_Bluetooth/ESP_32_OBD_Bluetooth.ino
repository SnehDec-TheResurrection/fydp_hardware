#include <BLEDevice.h>
// #include <BLEUtils.h>
#include <BLEServer.h>
// #include <BLEAdvertisedDevice.h>
#include <BLEScan.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "time.h"

const char* ssid = "myhOtspOt";
const char* password = "BlueDeww1";
String serverUrl = "http://driving-buddy-public-44aec6b920a3.herokuapp.com:80/esp32";

bool setup_success = false;
volatile bool received_response_data = false;
volatile bool received_lane_deviation = false;
String lane_deviation = "";
String* volatile lane_deviation_info = &lane_deviation;

String device_name = "VEEPEAK";

const char* serviceUUID = "0000fff0-0000-1000-8000-00805f9b34fb";
const char* serverCharacteristicUUID = "0000fff2-0200-1010-8009-00803f9b34fb";
const char* readCharacteristicUUID = "0000fff1-0000-1000-8000-00805f9b34fb";
const char* writeCharacteristicUUID = "0000fff2-0000-1000-8000-00805f9b34fb";

//characteristics to read from and write to obd2 scanner
BLERemoteCharacteristic* pMyRemoteReadCharacteristic = nullptr;
BLERemoteCharacteristic* pMyRemoteWriteCharacteristic = nullptr;

constexpr uint8_t delayms = 250;

struct tm timeinfo;
//OBD reading data set...=
String rx_data = "";

constexpr int success_status_LED = 13;
constexpr int lane_deviation_BLE_LED = 26;
constexpr int send_to_data_base_LED = 32;

//connecting to wifi
bool connectToWifi(wifi_mode_t mode,  const char* ssid, const char* password, int num_tries, int timeOutMs) {
    int connection_status = 0;
    int trial_number = 0;

    WiFi.mode(mode);

    connection_status = WiFi.waitForConnectResult(timeOutMs);
    
    while (trial_number < num_tries) {
      WiFi.begin(ssid, password);
      connection_status = WiFi.waitForConnectResult(timeOutMs);
      if (connection_status == WL_CONNECTED) {
        break;
      }
      trial_number++;
      Serial.print("Retrying connection....\n");
    }

    if (connection_status != WL_CONNECTED) {
      return false;
    }

    Serial.println("Connection to WiFi Successful\n");
    return true;
}


//sending data to server
bool sendHTTPData(String message, String serverURL) {
  HTTPClient http;
  http.begin(serverUrl);
  http.addHeader("Content-Type", "text/plain");
  
  int httpResponseCode = http.POST(message);
  if (httpResponseCode != 200) {
    return false;
  } 

  http.end();

  return true;
}

class MyCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    *lane_deviation_info = pCharacteristic->getValue();
    digitalWrite(lane_deviation_BLE_LED, HIGH);
    received_lane_deviation = true;
  }
};

//Callback to retrieve data.
void notifyCallback(
  BLERemoteCharacteristic* pBLERemoteCharacteristic,
  uint8_t* pData,
  size_t length,
  bool isNotify) {
  
  for (int i = 0; i < length; i++) {
    rx_data += (char)pData[i];
  }
  received_response_data = true;
}


bool initOBD() {
  String init_command[6] = {"ATZ\r", "ATE0\r", "ATL0\r", "ATS0\r", "ATSP0\r", "ATST100"};
  for (int command_idx = 0; command_idx < 5; ++command_idx) {
    pMyRemoteWriteCharacteristic->writeValue(init_command[command_idx]);
    delay(700);

    if (!received_response_data) {
      Serial.print("Initialization failure: The current index is ");
      Serial.println(command_idx);
      return false;
    }
    received_response_data = false;
  }
  return true;
}

String parseRPM(String command, String response) {
  int skipPos = response.indexOf(command) + command.length();
  String spaceless_string;

  if (skipPos == -1) {
      return "Failure";
  } else {
    String obd_information = response.substring(skipPos);
    for (char c : obd_information) {
        if (c != ' ' && c != '\r' && c != '>') {
            spaceless_string += c;
        }
    }
  }
  return spaceless_string;
}

String parse(String command, String response) {
    uint8_t pos = response.indexOf(command);
    if (pos == -1 || response.length() < pos + command.length() + 2) {
        return "Failure";
    }
    // Extract the next two bytes after the command
    String value = response.substring(pos + command.length(), pos + command.length() + 2);

    // Clean spaces if any
    value.trim();
    return value;
}



int fromChartoInt(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  } else if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  } else if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  } else {
    return -1;
  }
}

void setup() {
  Serial.begin(9600);
  if (!connectToWifi(WIFI_STA, ssid, password, 3, 5000)) {
    Serial.println("Connection to Wifi failed");
    return;
  }

  pinMode(success_status_LED, OUTPUT);
  pinMode(lane_deviation_BLE_LED, OUTPUT);
  pinMode(send_to_data_base_LED, OUTPUT);
  digitalWrite(success_status_LED, LOW);
  digitalWrite(lane_deviation_BLE_LED, LOW);
  digitalWrite(send_to_data_base_LED, LOW);

  const char* ntpServer = "pool.ntp.org";
  const long  gmtOffset_sec = -4 * 3600; // EDT (UTC-4)
  const int   daylightOffset_sec = 3600; // DST offset

  configTime(gmtOffset_sec, 0, ntpServer);

  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }

  Serial.println("Starting BLE work!");

  // Initialize BLE with device name
  BLEDevice::init("Veepeak_Connect");
  BLEAddress obd_address("8c:de:52:de:a2:6f");

  //Create the server
  BLEServer* pMyServer = BLEDevice::createServer();
  BLEService* pMyService = pMyServer->createService(serviceUUID);   //create the service
  BLECharacteristic *pESP32Characteristics = pMyService->createCharacteristic(serverCharacteristicUUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE);
  if (pESP32Characteristics == nullptr) { //if we can't get the characteristics...
    Serial.println("No server characteristics");
    return;
  }

  pESP32Characteristics->setCallbacks(new MyCallbacks());
  pMyService->start();  //start your service
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(serviceUUID);                // 👈 include service UUID
  BLEDevice::startAdvertising();



  //Create client. ESP32 is the client
  BLEClient* pMyClient = BLEDevice::createClient();
  if (!pMyClient->connect(obd_address)) { //connect to device
    Serial.println("Failed to connect");
  } else {  //If we can connect to the scanner...
    char const* initialization_message = "start_of_trip";
    if (!sendHTTPData(initialization_message, serverUrl)) {
      Serial.println("Failed to start trip");
      return;
    }

    digitalWrite(send_to_data_base_LED, HIGH);
    delay(100);
    digitalWrite(send_to_data_base_LED, LOW);
    
    

    //send
    BLERemoteService* pMyRemoteService = pMyClient->getService(serviceUUID);
    if (pMyRemoteService == nullptr) {
      Serial.println("No service");
      return;
    }
    pMyRemoteReadCharacteristic = pMyRemoteService->getCharacteristic(readCharacteristicUUID);
    pMyRemoteWriteCharacteristic = pMyRemoteService->getCharacteristic(writeCharacteristicUUID);

    Serial.println("Successful connection to Veepeak");
    if (pMyRemoteReadCharacteristic == nullptr || pMyRemoteWriteCharacteristic == nullptr) {
      Serial.println("Failed to find characteristics.");
      return;
    }

    //register the callback function.
    pMyRemoteReadCharacteristic->registerForNotify(notifyCallback);

    bool init_success = initOBD();
    if (init_success) {
      Serial.println("Setup Success");
      setup_success = true;
      digitalWrite(success_status_LED, HIGH);
      Serial.flush();
    } else {
      Serial.println("Setup failure");
    }
  }


  //Use a while loop instead of a void loop.
  while (1) {
    unsigned long start = millis();
    unsigned long end = 0;
    uint8_t speed_one = 0;
    uint8_t speed_two = 0;
    float speed_one_mps = 0;
    float speed_two_mps = 0;
    float acceleration = 0;
    // Adding the extra PIDs required for acceleration pedal %. 
    //We combine all of these and then multiply by 100/255 on the software side.
    // To reduce time on the hardware side, we only fetch them in this code. 
    // They are all 1 byte fields, like speed. 
    float pid_47 = 0;
    float pid_48 = 0;
    float pid_49 = 0;
    float pid_4A = 0;
    float pid_4B = 0;
    float pid_4C = 0;
    String message = "";

    if (setup_success) {
      if (!pMyClient->isConnected()) {
        message += "end_of_trip";
        sendHTTPData(message, serverUrl);
        Serial.println(message);
        digitalWrite(success_status_LED, LOW);
        digitalWrite(lane_deviation_BLE_LED, LOW);
        digitalWrite(send_to_data_base_LED, LOW);
        return;
      }


      if (!getLocalTime(&timeinfo)) {
        return;
      }
      rx_data.clear();
      Serial.print(timeinfo.tm_hour);
      message += timeinfo.tm_hour;
      Serial.print(":");
      message += ":";
      if (timeinfo.tm_min < 10) {
        Serial.print(0);
        message += 0;
      }

      message += timeinfo.tm_min;
      Serial.print(timeinfo.tm_min);
      Serial.print(":");
      message += ":";
      if (timeinfo.tm_sec < 10) {
        Serial.print(0);
        message += 0;
      }
      Serial.print(timeinfo.tm_sec);
      message += timeinfo.tm_sec;
      Serial.print(", ");
      message += ", ";
  

      // timeinfo
      //request speed
      pMyRemoteWriteCharacteristic->writeValue("010D\r");
      delay(delayms);
      if (received_response_data) {
        String spaceless_string = parse("410D", rx_data);
        rx_data.clear();

        speed_one = strtol(spaceless_string.c_str(), NULL, 16);
        speed_one_mps = (float)speed_one / 3.6;


        Serial.print(speed_one_mps);
        message += String(speed_one_mps);

        received_response_data = false; //reset flag.

      } else {
        Serial.print("EMPTY");
        rx_data.clear();
      }

      
      Serial.print(", ");
      message += ", ";

      //second speed request for acceleration.
      pMyRemoteWriteCharacteristic->writeValue("010D\r");
      delay(delayms);
      if (received_response_data) { //if you receive a response, parse the data, clear the rx buffer and grab data.
        String spaceless_string = parse("410D", rx_data);
        rx_data.clear();

        speed_two = strtol(spaceless_string.c_str(), NULL, 16);
        speed_two_mps = (float)speed_two / 3.6;
        end = millis();

        acceleration = (speed_two_mps - speed_one_mps) / ((float)(end - start) / 1000.0);
        Serial.print(acceleration);
        message += String(acceleration);

        received_response_data = false; //reset flag.
      } else {
        Serial.print("EMPTY");
      }

      Serial.print(", ");
      message += ", ";
      rx_data.clear();

    // request the acceleration pedal parameters, one by one. 
    //Starting with PID 47.
      pMyRemoteWriteCharacteristic->writeValue("0147\r");
      delay(delayms);
      if (received_response_data) {
        String spaceless_string = parse("4147", rx_data);
        rx_data.clear();

        pid_47 = strtol(spaceless_string.c_str(), NULL, 16);


        Serial.print(pid_47);
        message += String(pid_47);

        received_response_data = false; //reset flag.

      } else {
        Serial.print("EMPTY");
        rx_data.clear();
      }

      
      Serial.print(", ");
      message += ", ";

       //PID 48 
      pMyRemoteWriteCharacteristic->writeValue("0148\r");
      delay(delayms);
      if (received_response_data) {
        String spaceless_string = parse("4148", rx_data);
        rx_data.clear();

        pid_48= strtol(spaceless_string.c_str(), NULL, 16);


        Serial.print(pid_48);
        message += String(pid_48);

        received_response_data = false; //reset flag.

      } else {
        Serial.print("EMPTY");
        rx_data.clear();
      }

      
      Serial.print(", ");
      message += ", ";

      // PID 49
      pMyRemoteWriteCharacteristic->writeValue("0149\r");
      delay(delayms);
      if (received_response_data) {
        String spaceless_string = parse("4149", rx_data);
        rx_data.clear();

        pid_49 = strtol(spaceless_string.c_str(), NULL, 16);


        Serial.print(pid_49);
        message += String(pid_49);

        received_response_data = false; //reset flag.

      } else {
        Serial.print("EMPTY");
        rx_data.clear();
      }

      
      Serial.print(", ");
      message += ", ";

      // PID 4A
      pMyRemoteWriteCharacteristic->writeValue("014A\r");
      delay(delayms);
      if (received_response_data) {
        String spaceless_string = parse("414A", rx_data);
        rx_data.clear();

        pid_4A = strtol(spaceless_string.c_str(), NULL, 16);


        Serial.print(pid_4A);
        message += String(pid_4A);

        received_response_data = false; //reset flag.

      } else {
        Serial.print("EMPTY");
        rx_data.clear();
      }

      
      Serial.print(", ");
      message += ", ";

      // PID 4B
      pMyRemoteWriteCharacteristic->writeValue("014B\r");
      delay(delayms);
      if (received_response_data) {
        String spaceless_string = parse("414B", rx_data);
        rx_data.clear();

        pid_4B = strtol(spaceless_string.c_str(), NULL, 16);


        Serial.print(pid_4B);
        message += String(pid_4B);

        received_response_data = false; //reset flag.

      } else {
        Serial.print("EMPTY");
        rx_data.clear();
      }

      
      Serial.print(", ");
      message += ", ";

      // PID 4C
      pMyRemoteWriteCharacteristic->writeValue("014C\r");
      delay(delayms);
      if (received_response_data) {
        String spaceless_string = parse("414C", rx_data);
        rx_data.clear();

        pid_4C = strtol(spaceless_string.c_str(), NULL, 16);


        Serial.print(pid_4C);
        message += String(pid_4C);

        received_response_data = false; //reset flag.

      } else {
        Serial.print("EMPTY");
        rx_data.clear();
      }

      
      Serial.print(", ");
      message += ", ";
        
      //request engine RPM
      pMyRemoteWriteCharacteristic->writeValue("010C\r");
      delay(delayms);
      if (received_response_data) {
        String spaceless_string = parseRPM("410C", rx_data);

        rx_data.clear();

        int rpm = 0;
        if (spaceless_string.length() < 4) {
            // Serial.print("EH");
      
        } else {
          int A = fromChartoInt(spaceless_string[0]) * 16 + fromChartoInt(spaceless_string[1]);
          int B = fromChartoInt(spaceless_string[2]) * 16 + fromChartoInt(spaceless_string[3]);
          rpm = ((A * 256) + B) / 4;
          Serial.print(rpm);
          message += String(rpm);
        }
        

        received_response_data = false; //reset flag.
      } else {
        Serial.print("EMPTY");
      }

      Serial.print(", ");
      message += ", ";
      rx_data.clear();
      //request engine load
      pMyRemoteWriteCharacteristic->writeValue("0104\r");
      delay(delayms);
      if (received_response_data) {
        String spaceless_string = parse("4104", rx_data);
        rx_data.clear();

        float engine_load = (100.0 / 255) * (float)strtol(spaceless_string.c_str(), NULL, 16);
        Serial.print(engine_load);
        message += String(engine_load);

        received_response_data = false; //reset flag.
      } else {
        Serial.print("EMPTY");
      }

      Serial.print(", ");
      message += ",";
      //if we have received lane deviation info, then add it to the message.
      if (received_lane_deviation) {
        message += *lane_deviation_info;
        Serial.print(*lane_deviation_info);
      } else {
        Serial.print("EMPTY");
      }

      //set status LED.
      if (!sendHTTPData(message, serverUrl)) {
        digitalWrite(send_to_data_base_LED, LOW);
  
      } else {  //Successful message_sending attempt.
        digitalWrite(send_to_data_base_LED, HIGH);
      }

      Serial.println("\n");
    }
  }
}

void loop() {
  
  
}
