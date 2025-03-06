#define TINY_GSM_MODEM_EC200U
#define SerialMon Serial
#define SerialAT Serial1

#if !defined(TINY_GSM_RX_BUFFER)
#define TINY_GSM_RX_BUFFER 1024
#endif
#define BOARD_PWRKEY_PIN 7
#define BOARD_RESET_PIN 6
#define GSM_RX 18
#define GSM_TX 17

#define GSM_BAUD 115200
#define GSM_PIN "" // Sim Unlock Pin

// Your GPRS credentials, if any
const char apn[] = "airteliot.com";
// const char apn[] = "airtelgprs.com";
const char user[] = "";
const char pass[] = "";

// Server details
const char *server_url = "protocol.electrocus.com"; // Extract host from URL
const int server_port = 7000;
const char *firmware_path = "/firmware.bin"; // Extract file path

// const char *server_url = "www.abcd.com"; // Extract host from URL  http://www.abcd.com/xyz/filename.bin
// const int server_port = 80;
// const char *firmware_path = "/xyz/filename.bin"; // Extract file path

#include <TinyGsmClient.h>
#include <ArduinoHttpClient.h>
#include <Update.h>


#include <HTTPClient.h>
HTTPClient http;

TinyGsm modem(SerialAT);

const int kNetworkTimeout = 30 * 1000; // Number of milliseconds to wait without receiving any data before we give up
const int kNetworkDelay = 1000;        // Number of milliseconds to wait if no data is available before trying again

bool powerOn()
{
    digitalWrite(BOARD_PWRKEY_PIN, LOW);
    delay(500);
    digitalWrite(BOARD_PWRKEY_PIN, HIGH);

    delay(6000);
    if (!modem.init())
    {
        Serial.println("Modem not responding check uart connections");
        return false;
    }
    return true;
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

    // Read firmware size from Content-Length
    size_t firmware_size = http.contentLength();

    if (firmware_size <= 0)
    {
        Serial.println("Invalid firmware size!");
        http.stop();
        return;
    }

    Serial.print("Firmware size: ");
    Serial.print(firmware_size / 1024);
    Serial.println(" KB");

    // Prepare for OTA update
    Serial.println("Starting firmware update...");
    if (!Update.begin(firmware_size))
    {
        Serial.println("Not enough space for OTA!");
        http.stop();
        return;
    }

    uint8_t buffer[512]; // GSM is slow, so 512 bytes is better
    size_t totalBytes = 0;
    int progress = 0;

    unsigned long lastDataMillis = millis();

    while ((http.connected() || http.available()) && totalBytes < firmware_size)
    {
        int len = 0;

        // Read available data into buffer
        while (http.available() && len < sizeof(buffer))
        {
            buffer[len++] = http.read();
            lastDataMillis = millis();
        }

        if (len > 0)
        {
            size_t written = Update.write(buffer, len);
            if (written != len)
            {
                Serial.println("Write error during OTA update!");
                Update.abort();
                http.stop();
                return;
            }

            totalBytes += written;

            // Progress indicator every 5%
            int newProgress = (totalBytes * 100) / firmware_size;
            if (newProgress - progress >= 5 || totalBytes == firmware_size)
            {
                progress = newProgress;
                Serial.print("\rOTA Progress: ");
                Serial.print(progress);
                Serial.println("%");
            }
        }

        // Timeout if no data is received for a long time
        if ((millis() - lastDataMillis) > kNetworkTimeout)
        {
            Serial.println("Network timeout during OTA update.");
            Update.abort();
            http.stop();
            return;
        }

        // Avoid busy-wait loops when data is slow
        if (!http.available())
        {
            delay(kNetworkDelay);
        }
    }

    // Check if download was completed
    if (totalBytes != firmware_size)
    {
        Serial.println("Incomplete firmware received!");
        Update.abort();
        http.stop();
        return;
    }

    // Finalize OTA update
    if (!Update.end() || !Update.isFinished())
    {
        Serial.println("OTA update failed!");
        Update.printError(Serial);
        http.stop();
        return;
    }

    Serial.println("OTA update completed successfully! Rebooting...");
    http.stop();
    modem.gprsDisconnect();

    delay(1500);
    ESP.restart();
}

void setup()
{
    SerialMon.begin(115200);
    SerialAT.begin(GSM_BAUD, SERIAL_8N1, GSM_RX, GSM_TX);
    pinMode(BOARD_RESET_PIN, OUTPUT);
    pinMode(BOARD_PWRKEY_PIN, OUTPUT);

    delay(10);
    digitalWrite(BOARD_RESET_PIN, LOW);
    digitalWrite(BOARD_PWRKEY_PIN, LOW);

    SerialMon.println("Initializing modem...");
    if (!modem.restart())
    {
        if (!powerOn())
            return;
    }

    // Unlock your SIM card with a PIN if needed
    if (GSM_PIN && modem.getSimStatus() != 3)
        modem.simUnlock(GSM_PIN);
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

    Serial.println("OTA 8");
    delay(5000);
    ota_task();
}

void loop()
{
}
