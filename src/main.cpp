#define TINY_GSM_MODEM_EC200U
// #define TINY_GSM_MODEM_A7670

#define MODEM_TX_PIN (17)
#define MODEM_RX_PIN (16)
#define SerialAT Serial1
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb
#define DUMP_AT_COMMANDS

#define FIRMWARE "1.2.6"

#include <TinyGsmClient.h>
#include <Update.h>

const char *server_url = "http://protocol.electrocus.com:8000/firmware.bin";

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
    (void)vTaskDelete(NULL);

    while (1)
    {
        ;
    }
}

bool isFirmwareUpdateAvailable(const String &header, const String &currentVersion)
{
    String newVersion = "";

    // Find "X-Firmware-Version: " in the header
    int index = header.indexOf("X-Firmware-Version: ");
    if (index != -1)
    {
        index += 20;                                  // Move index past "X-Firmware-Version: "
        int endIndex = header.indexOf("\r\n", index); // End of the version line
        if (endIndex == -1)
            endIndex = header.length(); // If no newline, take till end
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

    // Compare version numbers (e.g., "1.2.1" vs "1.3.5")
    if (newVersion > currentVersion)
    {
        Serial.println("Firmware update available!");
        return true;
    }
    else
    {
        Serial.println("Firmware is up to date.");
        return false;
    }
}

void ota_task(void *pv)
{
    // Initialize HTTPS
    Serial.println("Initialize HTTPS");
    modem.https_begin();

    // Set OTA Server URL
    if (!modem.https_set_url(server_url))
    {
        Serial.println("Failed to set the URL. Please check the validity of the URL!");
        modem.https_end();
        task_fatal_error();
    }

    // Send GET request
    int httpCode = 0;
    Serial.println("Get firmware form HTTPS");
    httpCode = modem.https_get();
    if (httpCode != 200)
    {
        Serial.print("HTTP get failed ! error code = ");
        Serial.println(httpCode);
        modem.https_end();
        task_fatal_error();
    }

    String headers = modem.https_header();
    if (isFirmwareUpdateAvailable(headers, FIRMWARE) == false)
    {
        modem.https_end();
        task_fatal_error();
    }

    // Serial.println("Headers :");
    // Serial.println(headers);

    // Get firmware size
    size_t firmware_size = modem.https_get_size();
    Serial.print("Firmware size : ");
    Serial.print(firmware_size / 1024);
    Serial.println("Kb");

    // Begin Update firmware
    Serial.println("Start upgrade firmware ...");
    if (!Update.begin(firmware_size))
    {
        Serial.println("Not enough space to begin OTA");
        modem.https_end();
        task_fatal_error();
    }

    uint8_t buffer[1024];
    int written = 0;
    int progress = 0;
    int total = 0;

    while (1)
    {
        // Read firmware form modem buffer
        int len = modem.https_body(buffer, 1024);
        if (len == 0)
            break;

        written = Update.write(buffer, len);
        if (written != len)
        {
            Serial.println("Written only : " + String(written) + "/" + String(len) + ". Retry?");
        }
        total += written;
        int newProgress = (total * 100) / firmware_size;
        if (newProgress - progress >= 5 || newProgress == 100)
        {
            progress = newProgress;
            Serial.print(String("\r ") + progress + "%\n");
        }
    }
    if (!Update.end())
    {
        Serial.println("Update not finished? Something went wrong!");
        Serial.println("Error Occurred. Error #: " + String(Update.getError()));
        modem.https_end();
        task_fatal_error();
    }

    Serial.println();

    if (!Update.isFinished())
    {
        Serial.println("Update successfully completed.");
    }

    Serial.println("=== Update successfully completed. Rebooting.");

    delay(1500);

    modem.https_end();
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
    Serial.begin(115200); // Set console baud rate
    Serial.println("Firmware version : " + String(FIRMWARE));
    SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

    int retry = 0;
    while (!modem.testAT(1000))
    {
        Serial.println(".");
        if (retry++ > 10)
        {
            delay(1000);
            retry = 0;
        }
    }
    Serial.println();

    // Check if SIM card is online
    SimStatus sim = SIM_ERROR;
    while (sim != SIM_READY)
    {
        sim = modem.getSimStatus();
        switch (sim)
        {
        case SIM_READY:
            // Serial.println("SIM card online");
            break;
        case SIM_LOCKED:
            Serial.println("The SIM card is locked. Please unlock the SIM card first.");
            // const char *SIMCARD_PIN_CODE = "123456";
            // modem.simUnlock(SIMCARD_PIN_CODE);
            break;
        default:
            break;
        }
        delay(1000);
    }

#ifdef APN
    // Serial.printf("Set network apn : %s\n", APN);
    modem.sendAT(GF("AT+QICSGP=1,1,\"" APN "\",\"" user "\",\"" pass "\",1"));
    if (modem.waitResponse() != 1)
    {
        Serial.printf("Set network apn error : %s\n", APN);
    }
#endif

    // Check network registration status and network signal status
    int16_t sq;
    Serial.println("Wait for the modem to register with the network.");
    RegStatus status = REG_NO_RESULT;
    while (status == REG_NO_RESULT || status == REG_SEARCHING || status == REG_UNREGISTERED)
    {
        status = modem.getRegistrationStatus();
        switch (status)
        {
        case REG_UNREGISTERED:
        case REG_SEARCHING:
            sq = modem.getSignalQuality();
            Serial.printf("[%lu] Signal Quality:%d\n", millis() / 1000, sq);
            delay(1000);
            break;
        case REG_DENIED:
            Serial.println("Network registration was rejected, please check if the APN is correct");
            // return;
            break;
        case REG_OK_HOME:
            Serial.println("Online registration successful");
            break;
        case REG_OK_ROAMING:
            Serial.println("Network registration successful, currently in roaming mode");
            break;
        default:
            Serial.printf("Registration Status:%d\n", status);
            delay(1000);
            break;
        }
    }
    Serial.println();

    String ueInfo;
    if (modem.getSystemInformation(ueInfo))
    {
        Serial.print("Inquiring UE system information:");
        Serial.println(ueInfo);
    }

    delay(5000);

    String ipAddress = modem.getLocalIP();
    Serial.print("Network IP:");
    Serial.println(ipAddress);

    xTaskCreate(blink_task, "blink_task", 1024, NULL, 5, NULL);
    pinMode(23, INPUT_PULLUP);
    attachInterrupt(23, handleInterrupt, FALLING);
}

void loop()
{
    if (otaBegin)
    {
        if (ota_task_handle == NULL)
        {
            xTaskCreatePinnedToCore(ota_task, "ota_task", 1024 * 8, NULL, 5, &ota_task_handle, 1);
        }
        otaBegin = false;
    }
}
