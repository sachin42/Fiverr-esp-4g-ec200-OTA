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
    if (!modem.isGprsConnected())
    {
        Serial.println("Failed to connect to GPRS!");
        return;
    }
    Serial.println("Connected to GPRS!");

    TinyGsmClient client(modem);
    HttpClient http(client, server_url, server_port);

    Serial.println("Sending GET request...");
    if (http.get(firmware_path) != 0)
    {
        Serial.println("Connection Failed");
        http.stop();
        return;
    }
    int httpCode = http.responseStatusCode();
    if (httpCode != 200)
    {
        Serial.print("HTTP GET failed! Error code = ");
        Serial.println(httpCode);
        http.stop();
        return;
    }

    size_t firmware_size = http.contentLength();
    Serial.print("Firmware size: ");
    Serial.print(firmware_size / 1024);
    Serial.println(" KB");

    Serial.println("Starting firmware update...");
    if (!Update.begin(firmware_size))
    {
        Serial.println("Not enough space for OTA!");
        http.stop();
        return;
    }

    // size_t written = Update.writeStream(http);
    // if (written == firmware_size)
    // {
    //     Serial.println("Firmware downloaded successfully.");
    // }
    // else
    // {
    //     Serial.printf("Download error: %d/%d bytes written.\n", written, firmware_size);
    // }

    // uint8_t buffer[1024];
    // int total = 0, progress = 0;

    // while (total < firmware_size)
    // {
    //     int len = http.readBytes(buffer, sizeof(buffer));
    //     if (len <= 0)
    //         break;

    //     int written = Update.write(buffer, len);
    //     if (written != len)
    //     {
    //         Serial.println("Write error! Retry?");
    //     }

    //     // Serial.printf(" %d / %d", len, written);
    //     total += written;
    //     // int newProgress = (total * 100) / firmware_size;
    //     // if (newProgress - progress >= 5 || newProgress == 100)
    //     // {
    //     progress = (total * 100) / firmware_size;
    //     Serial.printf(" %d / %d", total, firmware_size);
    //     Serial.print(String("\r ") + progress + "%\n");
    // }
    // // }

    uint8_t buffer[512]; // Chunk buffer
    size_t totalBytes = 0;
    int progress = 0;

    unsigned long timeoutStart;
    timeoutStart = millis();
    while ((http.connected() || http.available()) && totalBytes < firmware_size)
    {
        int len = 0;

        // If data is available, read into the buffer in chunks
        while (http.available() && len < sizeof(buffer))
        {
            buffer[len++] = http.read();
            timeoutStart = millis(); // Reset timeout on every byte
        }

        if (len > 0)
        {
            int written = Update.write(buffer, len);
            if (written != len)
            {
                SerialMon.println("Write error!");
                Update.abort();
                http.stop();
                return;
            }

            totalBytes += written;
            int newProgress = (totalBytes * 100) / firmware_size;
            if (newProgress - progress >= 5 || newProgress == 100)
            {
                progress = newProgress;
                SerialMon.print("\r ");
                SerialMon.print(progress);
                SerialMon.println("%");
            }
        }

        // Timeout if no data for a while
        if ((millis() - timeoutStart) > kNetworkTimeout)
        {
            SerialMon.println("Network timeout. Aborting OTA.");
            Update.abort();
            http.stop();
            return;
        }

        // Delay if nothing is available to avoid tight looping
        if (!http.available())
        {
            delay(kNetworkDelay);
        }
    }

    if (totalBytes < firmware_size)
    {
        SerialMon.println("Incomplete firmware received!");
        Update.abort();
        http.stop();
        return;
    }

    if (!Update.end() || !Update.isFinished())
    {
        Serial.println("Update failed!");
        Update.printError(Serial);
        http.stop();
        return;
    }

    Serial.println("Update completed! Rebooting...");
    http.stop();
    modem.gprsDisconnect();
    delay(1500);
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
