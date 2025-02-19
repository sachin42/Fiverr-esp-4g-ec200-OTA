#define TINY_GSM_MODEM_SIM7600
// #define TINY_GSM_MODEM_EC200U
#define SerialMon Serial
#define SerialAT Serial1

#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 1024
#endif

// See all AT commands, if wanted
// #define DUMP_AT_COMMANDS

#define TINY_GSM_DEBUG SerialMon
// #define LOGGING  // <- Logging is for the HTTP library
#define GSM_BAUD 115200

// Add a reception delay, if needed.
// This may be needed for a fast processor at a slow baud rate.
// #define TINY_GSM_YIELD() \
//     {                    \
//         delay(2);        \
//     }
// Define how you're planning to connect to the internet
// These defines are only for this example; they are not needed in other code.
#define TINY_GSM_USE_GPRS true

// set GSM PIN, if any
#define GSM_PIN ""

// Your GPRS credentials, if any
// const char apn[] = "airteliot.com";
const char apn[] = "airtelgprs.com";
const char user[] = "";
const char pass[] = "";

// Server details
const char *server_url = "protocol.electrocus.com"; // Extract host from URL
const int server_port = 8000;
const char *firmware_path = "/firmware.bin"; // Extract file path

#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <Update.h>

#ifdef DUMP_AT_COMMANDS
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, SerialMon);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

// Number of milliseconds to wait without receiving any data before we give up
const int kNetworkTimeout = 30 * 1000;
// Number of milliseconds to wait if no data is available before trying again
const int kNetworkDelay = 1000;

void powerOn()
{
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);
    delay(300); // Need delay
    digitalWrite(4, LOW);
}

void ota_task()
{
    const char *firmware_url = "http://protocol.electrocus.com:8000/firmware.bin";
    const size_t chunk_size = 512; // Small chunks to avoid buffer overrun
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
    modem.sendAT("+QHTTPCFG=\"responseheader\",1");
    modem.waitResponse();

    // 3. Set URL Length and Timeout (60s timeout for slow network)
    modem.sendAT("+QHTTPURL=", strlen(firmware_url), ",60");
    if (modem.waitResponse("CONNECT") != 1)
    {
        SerialMon.println("Failed to set HTTP URL");
        return;
    }
    modem.stream.print(firmware_url);
    modem.stream.write(0x1A); // SEND CTRL+Z to terminate URL input
    if (modem.waitResponse() != 1)
    {
        SerialMon.println("Failed to send HTTP URL");
        return;
    }
    
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

    int err = modem.streamGetIntBefore(',');
    int httpCode = modem.streamGetIntBefore(',');
    int contentLength = modem.streamGetIntBefore('\r');

    SerialMon.print("HTTP Response Code: ");
    SerialMon.println(httpCode);
    SerialMon.print("Firmware Size: ");
    SerialMon.println(contentLength);

    if (err != 0 || httpCode != 200 || contentLength <= 0)
    {
        SerialMon.println("HTTP GET failed or invalid content length");
        return;
    }

    // 6. Begin OTA Update
    if (!Update.begin(contentLength))
    {
        SerialMon.println("Not enough space for OTA");
        return;
    }

    // 7. Read Firmware Data in Chunks
    modem.sendAT("+QHTTPREAD=", read_timeout);
    if (modem.waitResponse("CONNECT") != 1)
    {
        SerialMon.println("Failed to start HTTP READ");
        Update.abort();
        return;
    }

    uint8_t buffer[chunk_size];
    size_t totalBytes = 0;
    int progress = 0;

    unsigned long timeoutStart = millis();
    const unsigned long networkTimeout = 60000; // 60s timeout
    while (totalBytes < contentLength)
    {
        if (modem.stream.available())
        {
            int bytesRead = modem.stream.readBytes(buffer, min(chunk_size, contentLength - totalBytes));
            if (bytesRead <= 0)
            {
                SerialMon.println("Read error during OTA");
                Update.abort();
                return;
            }

            int written = Update.write(buffer, bytesRead);
            if (written != bytesRead)
            {
                SerialMon.println("Write error during OTA");
                Update.abort();
                return;
            }

            totalBytes += written;
            timeoutStart = millis();

            int newProgress = (totalBytes * 100) / contentLength;
            if (newProgress - progress >= 5 || newProgress == 100)
            {
                progress = newProgress;
                SerialMon.printf("\rProgress: %d%%\n", progress);
            }
        }
        else
        {
            if (millis() - timeoutStart > networkTimeout)
            {
                SerialMon.println("Network timeout during OTA");
                Update.abort();
                return;
            }
            delay(50); // Yield to prevent tight loop
        }
    }

    // 8. Finalize OTA
    if (!Update.end() || !Update.isFinished())
    {
        SerialMon.println("Update failed or not finished");
        return;
    }

    SerialMon.println("Update completed successfully! Rebooting...");
    delay(2000);
    ESP.restart();
}


void setup()
{
    SerialMon.begin(115200);
    SerialAT.begin(115200, SERIAL_8N1, 26, 27);
    powerOn();
    delay(6000);
    SerialMon.println("Initializing modem...");
    modem.init();
    // Unlock your SIM card with a PIN if needed
    if (GSM_PIN && modem.getSimStatus() != 3)
        modem.simUnlock(GSM_PIN);
#ifdef TINY_GSM_MODEM_SIM7600
    modem.setNetworkMode(13);
#endif
    SerialMon.print("Waiting for network...");
    if (!modem.waitForNetwork())
    {
        SerialMon.println(" fail");
        delay(10000);
        return;
    }
    if (modem.isNetworkConnected())
    {
        SerialMon.println("Network connected");
    }
    if (!modem.gprsConnect(apn, user, pass))
    {
        SerialMon.println(" fail");
        delay(10000);
        return;
    }
    if (modem.isGprsConnected())
    {
        SerialMon.println("GPRS connected");
    }

    Serial.println("OTA 4");
    pinMode(34, INPUT);
}

void loop()
{
    if (digitalRead(34) == LOW)
    {
        delay(500);
        Serial.println("OTA starting ");
        ota_task();
    }
}
