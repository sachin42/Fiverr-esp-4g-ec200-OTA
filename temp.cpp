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




void ota_task()
{
    const char firmware_url[] = "http://protocol.electrocus.com:7000/firmware.bin";
    const size_t chunk_size = 1024;          // Small chunks to avoid buffer overrun
    const unsigned long read_timeout = 180; // Timeout for large firmware

    SerialMon.println("Starting OTA via EC200U HTTP AT Commands...");

    // 1. Configure PDP Context (already activated in setup, assumed)
    modem.sendAT("+QHTTPCFG=\"contextid\",1");
    if (modem.waitResponse() != 1)
    {
        SerialMon.println("Failed to configure PDP context");
        return;
    }

    // 2. Enable Response Headers (optional, useful for version check)
    modem.sendAT("+QHTTPCFG=\"responseheader\",0");
    modem.waitResponse();

    // 3. Set URL Length and Timeout (60s timeout for slow network)
    modem.sendAT("+QHTTPURL=", strlen(firmware_url), ",60");
    if (modem.waitResponse("CONNECT") != 1)
    {
        SerialMon.println("Failed to set HTTP URL");
        return;
    }
    delay(100);
    modem.stream.print(firmware_url);
    if (modem.waitResponse() != 1)
    {
        SerialMon.println("Failed to send HTTP URL");
        return;
    }

    modem.sendAT("+QHTTPURL?");
    if (modem.waitResponse("+QHTTPURL:") != 1)
    {
        SerialMon.println("Failed to GET HTTP URL");
        return;
    }

    delay(1000);

    // 4. Send GET Request (180s timeout to handle slow downloads)
    modem.sendAT("+QHTTPGET=", read_timeout);
    if (modem.waitResponse() != 1)
    {
        SerialMon.println("HTTP GET failed to initiate");
        return;
    }

    // 5. Wait for HTTP GET result URC
    if (modem.waitResponse("+QHTTPGET: ") != 1)
    {
        SerialMon.println("No response from HTTP GET");
        return;
    }

    int err, httpCode;
    size_t contentLength;

    if (modem.stream.available())
    {
        err = modem.stream.parseInt(); // Reads until the first comma
        modem.stream.read();           // Skip comma
        httpCode = modem.stream.parseInt();
        modem.stream.read(); // Skip comma
        contentLength = modem.stream.parseInt();
    }
    else
    {
        SerialMon.println("Failed to parse HTTP GET response");
        return;
    }

    SerialMon.print("HTTP Response Code: ");
    SerialMon.println(httpCode);
    SerialMon.print("Firmware Size: ");
    SerialMon.print(contentLength / 1024);
    SerialMon.println(" kb");

    if (err != 0 || httpCode != 200 || contentLength <= 0)
    {
        SerialMon.println("HTTP GET failed or invalid content length");
        return;
    }

    // // 6. Begin OTA Update
    // if (!Update.begin(contentLength))
    // {
    //     SerialMon.println("Not enough space for OTA");
    //     return;
    // }

    // 7. Read Firmware Data in Chunks
    modem.sendAT("+QHTTPREAD=", read_timeout);
    if (modem.waitResponse(read_timeout, "CONNECT") != 1)
    {
        SerialMon.println("Failed to start HTTP READ");
        // Update.abort();
        return;
    }
    modem.streamSkipUntil('\n');
    while (1)
    {
        if(modem.stream.available() > 0)
        // Serial.println(modem.stream.readStringUntil('\n'));
        Serial.write(modem.stream.read());
    }

    // Update.onProgress();

    // Stream &stream = modem.stream;
    // size_t written = Update.writeStream(stream);
    // if (written == contentLength) {
    //     Serial.println("Firmware downloaded successfully.");
    // } else {
    //     Serial.printf("Download error: %d/%d bytes written.\n", written, contentLength);
    // }

    // uint8_t buffer[chunk_size];
    // size_t totalBytes = 0;
    // int progress = 0;
    // unsigned long timeoutStart = millis();
    // const unsigned long networkTimeout = 60000;

    // while (totalBytes < contentLength)
    // {
    //     if (modem.stream.available())
    //     {
    //         int len = modem.stream.readBytes(buffer, chunk_size);

    //         if (len > 0)
    //         {
    //             int written = Update.write(buffer, len);
    //             if (written != len)
    //             {
    //                 SerialMon.print("Write error during OTA! Error: ");
    //                 SerialMon.println(Update.getError());
    //                 Update.abort();
    //                 return;
    //             }

    //             totalBytes += written;
    //             int newProgress = (totalBytes * 100) / contentLength;
    //             if (newProgress - progress >= 5 || newProgress == 100)
    //             {
    //                 progress = newProgress;
    //                 SerialMon.printf("Progress: %d%%\n", progress);
    //             }
    //             SerialMon.print("total: ");
    //             SerialMon.print(totalBytes);
    //             SerialMon.print("\tFull: ");
    //             SerialMon.println(contentLength);

    //             timeoutStart = millis();
    //         }

    //         if (totalBytes >= contentLength)
    //         {
    //             SerialMon.println("Firmware download completed.");
    //             break;
    //         }
    //     }
    //     else
    //     {
    //         if (millis() - timeoutStart > networkTimeout)
    //         {
    //             SerialMon.println("Network timeout during OTA");
    //             Update.abort();
    //             return;
    //         }
    //     }
    // }

    // 7. Finalize OTA
    // if (!Update.end() || !Update.isFinished())
    // {
    //     SerialMon.print("OTA update failed! Error: ");
    //     SerialMon.println(Update.getError());
    //     return;
    // }

    // SerialMon.println("Update completed successfully! Rebooting...");
    // delay(2000);
    // ESP.restart();
}