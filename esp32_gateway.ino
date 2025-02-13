#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <BLEClient.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// Provide Firebase token generation process info
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// Firebase Configuration
#define WIFI_SSID "Faiz"
#define WIFI_PASSWORD "28022002"
#define API_KEY "AIzaSyA_BtOObGIMaRUTS37UMgBc7dmc3wsiVmA"
#define DATABASE_URL "https://esp8266-test-84f1d-default-rtdb.asia-southeast1.firebasedatabase.app/"

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
bool signupOK = false;
unsigned long lastUpdateTime = 0;

unsigned long previousMillisFirebase = 0;
unsigned long previousMillisBLE = 0;
const long firebaseInterval = 100; // Read Firebase every 100ms
const long bleInterval = 100;      // Send BLE every 100ms

// BLE Configuration
const char* targetDeviceName = "HMSoft";
static BLEUUID serviceUUID("0000FFE0-0000-1000-8000-00805F9B34FB");
static BLEUUID charUUID("0000FFE1-0000-1000-8000-00805F9B34FB");

BLEClient* pClient;
BLERemoteCharacteristic* pRemoteCharacteristic;
bool deviceConnected = false;
bool doConnect = false;
BLEAdvertisedDevice* myDevice;

bool newDataAvailable = false;
float temperature = 0.0;
float humidity = 0.0;
int ledValue = 0;  // Shared LED value

// BLE Notification Callback
void notifyCallback(BLERemoteCharacteristic* pBLERemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
    String receivedData = String((char*)pData).substring(0, length);
    receivedData.trim();

    Serial.print("DATA RECEIVED FROM HM-10 BLE: ");
    Serial.println(receivedData);

    int commaIndex = receivedData.indexOf(',');
    if (commaIndex != -1) {
        temperature = receivedData.substring(0, commaIndex).toFloat();
        humidity = receivedData.substring(commaIndex + 1).toFloat();
        newDataAvailable = true;

        Serial.print("Decoded Temperature: ");
        Serial.print(temperature);
        Serial.println("°C");

        Serial.print("Decoded Humidity: ");
        Serial.print(humidity);
        Serial.println("% RH");
    } else {
        Serial.println("Invalid BLE data format!");
    }
}

// Firebase Update Function
void updateFirebaseData() {
    if (!newDataAvailable || !Firebase.ready() || !signupOK) return;
    
    if (millis() - lastUpdateTime < 1000) return; // Send data every 1 second max

    newDataAvailable = false;
    lastUpdateTime = millis();

    Serial.println("Sending Data to Firebase...");

    if (Firebase.RTDB.setFloat(&fbdo, "test/temp", temperature)) {
        Serial.println("Temperature Sent to Firebase");
    } else {
        Serial.println("Temperature FAILED!");
        Serial.println("REASON: " + fbdo.errorReason());
    }

    if (Firebase.RTDB.setFloat(&fbdo, "test/humid", humidity)) {
        Serial.println("Humidity Sent to Firebase");
    } else {
        Serial.println("Humidity FAILED!");
        Serial.println("REASON: " + fbdo.errorReason());
    }
}

// BLE Send Function
void sendBLEData() {
    unsigned long currentMillis = millis();
    if (deviceConnected && (currentMillis - previousMillisBLE >= bleInterval)) {
        previousMillisBLE = currentMillis;
        String ledString = String(ledValue);
        Serial.print("Sending LED State over BLE: ");
        Serial.println(ledString);
        pRemoteCharacteristic->writeValue(ledString.c_str(), ledString.length());
    }
}

// Firebase LED State Retrieval
void updateLEDStateFromFirebase() {
    unsigned long currentMillis = millis();
    if (Firebase.ready() && signupOK && (currentMillis - previousMillisFirebase >= firebaseInterval)) {
        previousMillisFirebase = currentMillis;

        if (Firebase.RTDB.getInt(&fbdo, "/test/led")) {
            if (fbdo.dataType() == "int") {
                ledValue = fbdo.intData();
                Serial.print("Updated LED State from Firebase: ");
                Serial.println(ledValue);
            }
        } else {
            Serial.print("Failed to read /test/led: ");
            Serial.println(fbdo.errorReason());
        }
    }
}

// BLE Scan Callback to Find HM-10
class MyAdvertisedDeviceCallbacks : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
        Serial.print("Found Device: ");
        Serial.println(advertisedDevice.getName().c_str());

        if (advertisedDevice.getName() == targetDeviceName) {
            Serial.println("Found Target HM-10 Module!");
            advertisedDevice.getScan()->stop();
            myDevice = new BLEAdvertisedDevice(advertisedDevice);
            doConnect = true;
        }
    }
};

// Function to Connect to HM-10
void connectToServer() {
    Serial.println("Connecting to HM-10...");
    pClient = BLEDevice::createClient();
    pClient->connect(myDevice);
    Serial.println("Connected to HM-10!");

    BLERemoteService* pRemoteService = pClient->getService(serviceUUID);
    if (!pRemoteService) {
        Serial.println("Failed to find service UUID.");
        pClient->disconnect();
        return;
    }

    pRemoteCharacteristic = pRemoteService->getCharacteristic(charUUID);
    if (!pRemoteCharacteristic) {
        Serial.println("Failed to find characteristic UUID.");
        pClient->disconnect();
        return;
    }

    Serial.println("Subscribed to HM-10 notifications.");
    pRemoteCharacteristic->registerForNotify(notifyCallback);
    deviceConnected = true;
}

void setup() {
    Serial.begin(115200);
    
    // Connect to WiFi
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to Wi-Fi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(300);
    }
    Serial.println();
    Serial.print("Connected with IP: ");
    Serial.println(WiFi.localIP());

    // Firebase Setup
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;

    if (Firebase.signUp(&config, &auth, "", "")) {
        Serial.println("Firebase Sign-Up OK");
        signupOK = true;
    } else {
        Serial.printf("Firebase Error: %s\n", config.signer.signupError.message.c_str());
    }

    config.token_status_callback = tokenStatusCallback;
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);

    // BLE Setup
    BLEDevice::init("ESP32_BLE_Client");
    BLEScan* pBLEScan = BLEDevice::getScan();
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
    pBLEScan->setInterval(100);
    pBLEScan->setWindow(99);
    pBLEScan->setActiveScan(true);
    pBLEScan->start(60, false);
}

void loop() {
    unsigned long currentMillis = millis();
    
    if (doConnect) {
        connectToServer();
        doConnect = false;
    }

    sendBLEData();  // Send BLE data
    updateFirebaseData();  // Update Firebase
    updateLEDStateFromFirebase();  // Update LED state from Firebase
}
