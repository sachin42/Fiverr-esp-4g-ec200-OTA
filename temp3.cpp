#include <Arduino.h>
#include <Update.h>
#include <ArduinoHttpClient.h>

#define SerialAT Serial1  // Use Hardware Serial1 (GPIO16 = RX, GPIO17 = TX)
#define RX_PIN 16
#define TX_PIN 17
#define BAUD_RATE 115200

#define APN "your_apn"    // Replace with your SIM card's APN
#define FIRMWARE_URL "http://192.168.29.87:8080/firmware.bin"

void performOTAUpdate(const char *url);
bool sendATCommand(const char *cmd, const char *expectedResponse, uint16_t timeout);
void setupEC200U();

void setup() {
    Serial.begin(115200);
    SerialAT.begin(BAUD_RATE, SERIAL_8N1, RX_PIN, TX_PIN);
    delay(3000);
    
    Serial.println("Initializing EC200U and connecting to 4G...");
    setupEC200U();  // Setup and connect EC200U

    performOTAUpdate(FIRMWARE_URL);
}

void performOTAUpdate(const char *url) {
    Serial.println("Starting OTA Update over HTTP...");

    // Set the URL
    SerialAT.println("AT+QHTTPURL=60,60");
    delay(100);
    SerialAT.println(url);
    delay(1000);

    // Send HTTP GET request
    if (!sendATCommand("AT+QHTTPGET=80", "+QHTTPGET: 0,", 10000)) {
        Serial.println("HTTP GET request failed!");
        return;
    }

    // Read HTTP Headers
    SerialAT.println("AT+QHTTPREAD=80");
    if (!sendATCommand("AT+QHTTPREAD=80", "CONNECT", 5000)) {
        Serial.println("Failed to read HTTP response!");
        return;
    }

    Serial.println("Reading HTTP Headers...");
    String headers = "";
    while (SerialAT.available()) {
        char c = SerialAT.read();
        headers += c;
        if (headers.endsWith("\r\n\r\n")) break;  // End of headers
    }

    // Extract Content-Length and Content-Type
    int contentLength = 0;
    bool isOctetStream = false;
    
    if (headers.indexOf("Content-Length:") != -1) {
        int start = headers.indexOf("Content-Length:") + 15;
        int end = headers.indexOf("\r\n", start);
        contentLength = headers.substring(start, end).toInt();
    }

    if (headers.indexOf("Content-Type: application/octet-stream") != -1) {
        isOctetStream = true;
    }

    Serial.printf("Firmware Size: %d bytes\n", contentLength);
    Serial.printf("Is Octet-Stream: %s\n", isOctetStream ? "YES" : "NO");

    // Check for valid firmware file
    if (contentLength <= 0 || !isOctetStream) {
        Serial.println("Invalid firmware file!");
        return;
    }

    // Begin OTA update
    if (!Update.begin(contentLength)) {
        Serial.println("Not enough space for OTA update.");
        Update.printError(Serial);
        return;
    }

    Serial.println("Starting firmware download...");
    while (SerialAT.available()) {
        uint8_t buffer[128];
        int bytesRead = SerialAT.readBytes(buffer, sizeof(buffer));
        if (bytesRead > 0) {
            if (Update.write(buffer, bytesRead) != bytesRead) {
                Serial.println("Error writing firmware!");
                Update.printError(Serial);
                return;
            }
        }
    }

    // Finalize update
    if (Update.end()) {
        Serial.println("OTA update completed!");
        if (Update.isFinished()) {
            Serial.println("Firmware updated successfully. Rebooting...");
            delay(1000);
            ESP.restart();
        } else {
            Serial.println("Update failed! OTA not finished.");
        }
    }

    Update.printError(Serial);
}

void setupEC200U() {
    sendATCommand("AT+CFUN=1,1", "OK", 5000);
    delay(5000);

    if (!sendATCommand("AT", "OK", 2000)) {
        Serial.println("EC200U not responding!");
        return;
    }

    String setAPN = "AT+QICSGP=1,1,\"" + String(APN) + "\",\"\",\"\",1";
    sendATCommand(setAPN.c_str(), "OK", 3000);
    
    sendATCommand("AT+QIACT=1", "OK", 5000);  
    sendATCommand("AT+QIACT?", "+QIACT: 1,", 2000);  

    Serial.println("EC200U connected to 4G!");
}

bool sendATCommand(const char *cmd, const char *expectedResponse, uint16_t timeout) {
    SerialAT.println(cmd);
    uint32_t timeStart = millis();
    String response = "";
    
    while ((millis() - timeStart) < timeout) {
        while (SerialAT.available()) {
            response += (char)SerialAT.read();
        }
        if (response.indexOf(expectedResponse) != -1) {
            return true;
        }
    }
    
    Serial.println("AT command failed: " + String(cmd));
    return false;
}

void loop() {
    Serial.println("Running...");
    delay(5000);
}