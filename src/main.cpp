#define TINY_GSM_MODEM_EC200U

#define MODEM_TX_PIN (17)
#define MODEM_RX_PIN (16)
#define SerialAT Serial1
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb
#define DUMP_AT_COMMANDS

#define FIRMWARE "1.2.6"

#include <TinyGsmClient.h>
#include <Update.h>
#include <ArduinoHttpClient.h>

const char *server_url = "protocol.electrocus.com"; // Extract host from URL
const int server_port = 8000;
const char *firmware_path = "/firmware.bin"; // Extract file path

#ifdef DUMP_AT_COMMANDS // if enabled it requires the streamDebugger lib
#include <StreamDebugger.h>
StreamDebugger debugger(SerialAT, Serial);
TinyGsm modem(debugger);
#else
TinyGsm modem(SerialAT);
#endif

#define APN "airtelgprs.com" // Replace with your APN
#define user ""              // Replace with your APN username (if any)
#define pass ""              // Replace with your APN password (if any)

bool otaBegin = false;
TaskHandle_t ota_task_handle = NULL;

void blink_task(void *pv)
{
    pinMode(2, OUTPUT);
    while (true)
    {
        digitalWrite(2, HIGH);
        delay(100);
        digitalWrite(2, LOW);
        delay(100);
    }
    vTaskDelete(NULL);
}

static void __attribute__((noreturn)) task_fatal_error(void)
{
    Serial.println("Exiting task due to fatal error...");
    ota_task_handle = NULL;
    vTaskDelete(NULL);
    while (1) {}
}

bool isFirmwareUpdateAvailable(const String &header, const String &currentVersion)
{
    String newVersion = "";
    int index = header.indexOf("X-Firmware-Version: ");
    if (index != -1)
    {
        index += 20;
        int endIndex = header.indexOf("\r\n", index);
        if (endIndex == -1)
            endIndex = header.length();
        newVersion = header.substring(index, endIndex);
    }

    if (newVersion.length() == 0)
    {
        Serial.println("No firmware version found in header.");
        return false;
    }

    Serial.print("Current Firmware: ");
    Serial.println(currentVersion);
    Serial.print("Available Firmware: ");
    Serial.println(newVersion);

    return newVersion > currentVersion;
}

void ota_task(void *pv)
{
    Serial.println("Connecting to GPRS...");
    if (!modem.gprsConnect(APN, user, pass))
    {
        Serial.println("Failed to connect to GPRS!");
        task_fatal_error();
    }
    Serial.println("Connected to GPRS!");

    TinyGsmClient client(modem);
    HttpClient http(client, server_url, server_port);

    Serial.println("Sending GET request...");
    http.get(firmware_path);

    int httpCode = http.responseStatusCode();
    if (httpCode != 200)
    {
        Serial.print("HTTP GET failed! Error code = ");
        Serial.println(httpCode);
        modem.gprsDisconnect();
        task_fatal_error();
    }

    String headers = http.readString();
    if (!isFirmwareUpdateAvailable(headers, FIRMWARE))
    {
        modem.gprsDisconnect();
        task_fatal_error();
    }

    size_t firmware_size = http.contentLength();
    Serial.print("Firmware size: ");
    Serial.print(firmware_size / 1024);
    Serial.println(" KB");

    Serial.println("Starting firmware update...");
    if (!Update.begin(firmware_size))
    {
        Serial.println("Not enough space for OTA!");
        modem.gprsDisconnect();
        task_fatal_error();
    }

    uint8_t buffer[1024];
    int total = 0, progress = 0;

    while (total < firmware_size)
    {
        int len = http.readBytes(buffer, sizeof(buffer));
        if (len <= 0)
            break;

        int written = Update.write(buffer, len);
        if (written != len)
        {
            Serial.println("Write error! Retry?");
        }

        total += written;
        int newProgress = (total * 100) / firmware_size;
        if (newProgress - progress >= 5 || newProgress == 100)
        {
            progress = newProgress;
            Serial.print(String("\r ") + progress + "%\n");
        }
    }

    if (!Update.end() || !Update.isFinished())
    {
        Serial.println("Update failed!");
        modem.gprsDisconnect();
        task_fatal_error();
    }

    Serial.println("Update completed! Rebooting...");
    delay(1500);
    modem.gprsDisconnect();
    ota_task_handle = NULL;
    ESP.restart();
    vTaskDelete(NULL);
}

void IRAM_ATTR handleInterrupt()
{
    otaBegin = true;
}

void setup()
{
    Serial.begin(115200);
    Serial.println("Firmware version: " + String(FIRMWARE));
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    int retry = 0;
    while (!modem.testAT(1000))
    {
        Serial.print(".");
        if (retry++ > 10)
        {
            delay(1000);
            retry = 0;
        }
    }
    Serial.println();

    // Check SIM status
    while (modem.getSimStatus() != SIM_READY)
    {
        Serial.println("Waiting for SIM...");
        delay(1000);
    }

    // Configure APN
    modem.sendAT(GF("AT+QICSGP=1,1,\"" APN "\",\"" user "\",\"" pass "\",1"));
    modem.waitResponse();

    // Network registration
    Serial.println("Waiting for network...");
    while (modem.getRegistrationStatus() <= REG_SEARCHING)
    {
        Serial.printf("Signal Quality: %d\n", modem.getSignalQuality());
        delay(1000);
    }

    Serial.println("Network ready!");
    Serial.println("IP Address: " + modem.getLocalIP());

    xTaskCreate(blink_task, "blink_task", 1024, NULL, 5, NULL);
    pinMode(23, INPUT_PULLUP);
    attachInterrupt(23, handleInterrupt, FALLING);
}

void loop()
{
    if (otaBegin && ota_task_handle == NULL)
    {
        xTaskCreatePinnedToCore(ota_task, "ota_task", 1024 * 8, NULL, 5, &ota_task_handle, 1);
        otaBegin = false;
    }
}
