#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <Update.h>

#define WIFI_SSID "antivirus 4G"
#define WIFI_PASSWORD "yellowpages40"
#define FIRMWARE_URL "http://192.168.29.87:8080/firmware.bin"

void performOTAUpdate(const char *url);

void setup() {
    Serial.begin(115200);
    while (!Serial) {
        delay(100);
    }
    Serial.println("Starting OTA Update over HTTP...");
    
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
    Serial.println("\nWiFi Connected!");

    performOTAUpdate(FIRMWARE_URL);
}

void performOTAUpdate(const char *url) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(5000);
    
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("HTTP Error: %d\n", httpCode);
        http.end();
        return;
    }

    int contentLength = http.getSize();
    if (contentLength <= 0) {
        Serial.println("Invalid firmware file!");
        http.end();
        return;
    }

    Serial.printf("Firmware size: %d bytes\n", contentLength);

    // Start update
    if (!Update.begin(contentLength)) {
        Serial.println("Not enough space for OTA update.");
        http.end();
        Update.printError(Serial);
        return;
    }

    // Read the file in chunks
    Stream &stream = http.getStream();
    size_t written = Update.writeStream(stream);
    if (written == contentLength) {
        Serial.println("Firmware downloaded successfully.");
    } else {
        Serial.printf("Download error: %d/%d bytes written.\n", written, contentLength);
    }

    // Finalize update
    if (Update.end()) {
        Serial.println("OTA update completed!");
        if (Update.isFinished()) {
            Serial.println("Firmware updated successfully. Rebooting...");
            // delay(1000);
            // ESP.restart();
        } else {
            Serial.println("Update failed! OTA not finished.");
        }
    }

    Update.printError(Serial);

    http.end();
}

void loop() {
    Serial.println("Hello, World! v1.1.0");
    delay(2000);
    Update.size();
}
