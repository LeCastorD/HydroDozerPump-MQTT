#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>
#include <ESP8266WebServer.h>
#include <ElegantOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include "fw_version_auto.h"

#ifndef FW_VERSION
#define FW_VERSION "0.0.0-dev"
#endif

#ifndef FW_BUILD_STAMP
#define FW_BUILD_STAMP "unknown"
#endif

#ifndef ENABLE_DEBUG_LOG
#define ENABLE_DEBUG_LOG 0
#endif

#ifndef DEBUG_LOG_BUFFER_SIZE
#define DEBUG_LOG_BUFFER_SIZE 1024
#endif

// --------- bottle concept ------------//

#define DEVICE_NAME_LEN 32
#define MQTT_HOST_LEN 40
#define MQTT_CLIENT_ID_LEN 32
#define MQTT_USER_LEN 24
#define MQTT_PASS_LEN 32
#define OTA_USER_LEN 24
#define OTA_PASS_LEN 24
#define WEB_UI_SESSION_TOKEN_LEN 33
#define IPV4_STR_LEN 16
#define TIMEZONE_LEN 64
#define MQTT_TOPIC_SEGMENT_LEN 32
#define MQTT_TOPIC_BASE_LEN 64
#define MQTT_TOPIC_LEN 96

const char* DEFAULT_DEVICE_NAME_BASE = "hydrodozerpump";
const char* DEFAULT_MQTT_CLIENT_ID_BASE = "HydroDozerPump";

    const char* WEB_UI_BASE_CSS =
        "div,fieldset,input,select{padding:5px;font-size:1em;}"
        ".main{text-align:left;display:inline-block;width:340px;max-width:92vw;color:#eaeaea;box-sizing:border-box;}"
        "fieldset{background-color:#4f4f4f;}p{margin:.5em 0;}"
        "input[type=checkbox],input[type=radio]{width:1em;margin-right:6px;vertical-align:-1px;}"
        "input[type=range]{width:99%;}"
        "input:not([type]),input[type=password],input[type=number]{width:100%;box-sizing:border-box;background:#dddddd;color:#000000;}"
        "select{width:100%;background:#dddddd;color:#000000;block-size:40px;}"
        "textarea{resize:none;width:98%;height:318px;padding:5px;overflow:auto;background:#1f1f1f;color:#65c115;}"
        "body{text-align:center;font-family:verdana,sans-serif;background:#252525;}td{padding:0;}"
        "button,a.button{display:inline-block;text-align:center;border:0;border-radius:.3rem;background:#7bcf6a;color:#faffff;line-height:2.4rem;font-size:1.2rem;width:100%;transition-duration:.4s;cursor:pointer;}"
        "button:hover,a.button:hover{background:#4ea94a;}.bred{background-color:#d43535;}.bred:hover{background-color:#931f1f;}"
        ".bgrn{background-color:#47c266;}.bgrn:hover{background-color:#5aaf6f;}a{text-decoration:none;color:#1fa3ec;}"
        ".p{float:left;text-align:left;}.q{float:right;text-align:right;}.r{border-radius:.3em;padding:2px;margin:6px 2px;}"
        "span{display:inline-block;}h2,h3{text-align:center;}";

    struct Bottle
    {
        float capacity_ml;
        float remaining_ml;
    };

    // Bottles (index 0 unused)
    Bottle bottles[3] = {
        { 0, 0 },
        { 1000.0, 1000.0 },
        { 1000.0, 1000.0 }
    };

    // Pump config: flow only (identity and assignment are fixed by ID).
    struct PumpConfig
    {
        float flow_ml_per_sec;
    };

    PumpConfig pumpConfig[3] = {
        { 0 },
        { 0.9 },
        { 0.9 }
    };

    const char* bottleLabelById(uint8_t bottleId) {
        switch (bottleId) {
            case 1: return "Bottle A";
            case 2: return "Bottle B";
            default: return "Bottle";
        }
    }

    const char* pumpLabelById(uint8_t pumpId) {
        switch (pumpId) {
            case 1: return "Pump A";
            case 2: return "Pump B";
            default: return "Pump";
        }
    }

    uint8_t bottleIdForPump(uint8_t pumpId) {
        if (pumpId >= 1 && pumpId <= 2) return pumpId;
        return 0;
    }

    inline Bottle* getBottleForPump(uint8_t pumpId) {
        uint8_t bid = bottleIdForPump(pumpId);
        if (bid < 1 || bid > 2) return nullptr;
        return &bottles[bid];
    }


// --------- Global constants ------------

    // Device name

    char deviceName[DEVICE_NAME_LEN] = "hydrodozerpump";

    // MQTT Constants

    char mqttBroker[MQTT_HOST_LEN] = "127.0.0.1";
    uint16_t mqttPort = 1883;
    char mqttClientId[MQTT_CLIENT_ID_LEN] = "HydroDozerPump";
    char mqttTopicNode[MQTT_TOPIC_SEGMENT_LEN] = "hydrodozerpump";
    char MQTT_TOPIC_BASE[MQTT_TOPIC_BASE_LEN] = "hydrodozerpump/hydrodozerpump";
    char MQTT_TOPIC_AVAIL[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/status";
    char MQTT_TOPIC_HEALTH[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/health";
    char MQTT_TOPIC_AUTO_STATE[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/auto/state";
    unsigned long lastMQTTAttempt = 0;
    const unsigned long mqttInterval = 5000; // retry every 5 seconds
    const unsigned long MQTT_DANGEROUS_CMD_GUARD_MS = 60000; // ignore reboot/reset commands during first minute after boot
    const unsigned long autoPublishInterval = 60000; // periodic state refresh (60s)
    const unsigned long TIME_SYNC_INTERVAL_MS = 12UL * 60UL * 60UL * 1000UL; // 12h
    const unsigned long TIME_SYNC_RETRY_MS = 60000UL; // retry after 60s when offline
    const time_t TIME_VALID_AFTER_EPOCH = 1704067200; // 2024-01-01 UTC
    const char* NTP_SERVER_PRIMARY = "pool.ntp.org";
    const char* NTP_SERVER_SECONDARY = "time.nist.gov";
    char timeZoneSpec[TIMEZONE_LEN] = "EST5EDT,M3.2.0/2,M11.1.0/2";
    char mqttUser[MQTT_USER_LEN] = "homeassistant";
    char mqttPass[MQTT_PASS_LEN] = "homeassistantpass";
    char otaUser[OTA_USER_LEN] = "admin";
    char otaPass[OTA_PASS_LEN] = "adminpass";
    bool webUiLoginRequired = true;
    char webUiSessionToken[WEB_UI_SESSION_TOKEN_LEN] = "";
    bool networkUseDhcp = true;
    char networkIp[IPV4_STR_LEN] = "";
    char networkGateway[IPV4_STR_LEN] = "";
    char networkNetmask[IPV4_STR_LEN] = "";
    char networkDns[IPV4_STR_LEN] = "";
    // General command topic (legacy + JSON action/cmd).
    char MQTT_TOPIC_CMD[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/cmd";
    // HA-specific control topic used by discovery number/button entities
    // for dosing workflows.
    char MQTT_TOPIC_HA_CMD[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/ha/cmd";
    const char* MQTT_TOPIC_HA_STATUS = "homeassistant/status";
    char MQTT_TOPIC_CMD_ACK[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/cmd/ack";
    char MQTT_TOPIC_SYSTEM_STATE[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/system/state";
    char MQTT_TOPIC_BOTTLE_STATE_FMT[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/bottle/%u/state";
    // Dedicated retained topics keep HA text entities in sync independently
    // from larger JSON payloads.
    char MQTT_TOPIC_PUMP_DOSE_ML_FMT[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/pump/%u/dose_ml";
    char MQTT_TOPIC_BOTTLE_PERMISSION_FMT[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/bottle/%u/permission";
    char MQTT_TOPIC_PUMP_PERMISSION_FMT[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/pump/%u/permission";
    char MQTT_TOPIC_SCHEDULLER_TOTAL_ML[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/scheduller/total_ml";
    char MQTT_TOPIC_SCHEDULLER_PART_A_PCT[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/scheduller/part_a_pct";
    char MQTT_TOPIC_SCHEDULLER_PART_B_PCT[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/scheduller/part_b_pct";
    char MQTT_TOPIC_SCHEDULLER_PAUSE[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/scheduller/pause";
    char MQTT_TOPIC_LOW_BOTTLE_ALARM_PCT[MQTT_TOPIC_LEN] = "hydrodozerpump/hydrodozerpump/alarm/low_bottle_pct";
    const char* PUMP_RUN_LOG_CSV_PATH = "/pump_runs.csv";
    const size_t PUMP_RUN_LOG_MAX_BYTES = 102400; // 100 KB

    // ---------- Status LEDs ----------
    const uint8_t STATUS_LED_GREEN_PIN = D6; // GPIO12
    const uint8_t STATUS_LED_RED_PIN = D7;   // GPIO13
    const bool STATUS_LED_ACTIVE_HIGH = true;
    const unsigned long STATUS_LED_BLINK_INTERVAL_MS = 350UL;
    const unsigned long STATUS_LED_PULSE_PERIOD_MS = 1600UL;
    const uint16_t STATUS_LED_PWM_MAX = 1023U;
    const uint16_t STATUS_LED_PWM_MIN = 160U;

    // ---------- Pump Pins (ESP8266 D pins) ----------
    const uint8_t PUMP1_PIN = D1; // GPIO5
    const uint8_t PUMP2_PIN = D2; // GPIO4
    const uint8_t FACTORY_RESET_PIN = D5; // GPIO14, short to GND at boot to trigger reset
    // If ULN2003 turns ON when GPIO is HIGH, keep this true.
    // If your wiring is inverted, set to false.
    const bool PUMP_ACTIVE_HIGH = true;
    // Hard safety cutoff (milliseconds)
    const unsigned long PUMP_HARD_MAX_MS = 120000; // 120s
    const unsigned long CAL_RUN_DURATION_MS = 60000; // fixed 60s calibration run
    const unsigned long FACTORY_RESET_HOLD_MS = 3000; // hold jumper for 3s
    const float CAL_MIN_FLOW_ML_PER_SEC = 0.001f;
    const float CAL_MAX_FLOW_ML_PER_SEC = 50.0f;
    struct PumpState
        {
        bool running;
        unsigned long startedAt;
        unsigned long maxRunMs;
        };
    PumpState pump1 = {false, 0, 0};
    PumpState pump2 = {false, 0, 0};
    bool calibrationAwaitResult[3] = {false, false, false}; // index 1..2 used
    float haDoseMl[3] = {0.0f, 5.0f, 5.0f}; // index 1..2 used for HA dose controls
    // Scheduller controls (kept independent from immediate dose_now execution).
    float schedullerTotalMl = 100.0f;
    float schedullerPartAPct = 50.0f;
    float schedullerPartBPct = 50.0f;
    bool schedullerPaused = false;
    float lowBottleAlarmPct = 10.0f;
    int schedullerHour1 = 8;
    int schedullerHour2 = 14;
    int schedullerHour3 = 20;
    // Per-job run history (slot 1..3, pump 1..2 => 6 jobs total).
    // Stored as (year, yday) to survive reboot and skip same-day duplicate runs.
    int schedullerLastRunYear[4][3] = {
        {-1, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1}
    };
    int schedullerLastRunYDay[4][3] = {
        {-1, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1},
        {-1, -1, -1}
    };
    time_t schedullerLastAttemptEpoch[4] = {0, 0, 0, 0}; // index 1..3 used
    const unsigned long SCHEDULLER_PUMP_SWITCH_GAP_MS = 10000UL; // 10s gap between Pump 1 and Pump 2
    struct SchedullerPump2Sequence
    {
        bool pending;
        bool gapStarted;
        uint8_t slot;
        unsigned long durationMs;
        float requestedMl;
        unsigned long gapStartedAtMs;
        int runYear;
        int runYDay;
    };
    SchedullerPump2Sequence schedullerPump2Sequence = {false, false, 0, 0, 0.0f, 0, -1, -1};




// ---------- Global objects ----------
WiFiManager wm;
ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);


// ---------- Function Prototypes ----------
void printBootInfo();
void printHealth();
void setupWiFi();
void setupWebServer();
void setupOTA();
void ensureMQTT();
void setupMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int length);
void setupTimeSync();
void processTimeSync();
void requestTimeSync(const char* reason);
bool hasValidSystemTime();
void applyTimeZone();
void applyDefaultIdentityFromMac();
void rebuildMqttTopics();
bool isValidTimeZoneSpec(const String& tzSpec);
bool clampSchedullerTotalMlToCurrentMax();
float bottlePercent(uint8_t bottleId);
bool isBottleEmpty(uint8_t bottleId);
bool isBottleLowAlarm(uint8_t bottleId);
bool isAnyBottleEmpty();
bool isAnyBottleLowAlarm();
bool isValidSchedullerHour(int hour);
int getSchedullerHourBySlot(uint8_t slot);
void setSchedullerHourBySlot(uint8_t slot, int hour);
void clearSchedullerExecutionHistory();
void appendPumpRunCsv(uint8_t pumpId, float volumeMl, const char* note);
void appendPumpLogEventCsv(const char* note);
bool hasSchedullerJobRunToday(uint8_t slot, uint8_t pumpId, int year, int yday);
void markSchedullerJobRunToday(uint8_t slot, uint8_t pumpId, int year, int yday);
bool runSchedullerDoseForSlot(uint8_t slot, int year, int yday);
void processSchedullerPump2Sequence();
void processScheduller();
void publishBottleState(uint8_t bottleId);
void publishAllBottleStates();
void publishPumpDoseMlState(uint8_t pumpId);
void publishAllPumpDoseMlStates();
void publishSchedullerState(uint8_t fieldId);
void publishAllSchedullerStates();
void publishLowBottleAlarmState();
void publishDosePermission(uint8_t pumpId, float requestedMl, bool allowed, const char* reason);
void publishCmdAck(bool ok, const char* type, const char* detail);
void publishSystemState();
void publishAutoState();
const char* getHADeviceDisplayName();
bool publishHADiscovery();
void resetHADiscovery();
void processPendingHADiscoveryRepublish();
void requestAutoStatePublish(unsigned long delayMs);
void requestHADiscoveryPublish(unsigned long delayMs);
void requestConfigSave(unsigned long delayMs);
void requestBottleStatePublish(uint8_t bottleId);
void requestPumpDoseMlPublish(uint8_t pumpId);
void requestSchedullerStatePublish(uint8_t fieldId);
void requestLowBottleAlarmPublish();
void requestAllRetainedStatePublish();
void processDeferredPublishes();
void appendDebugLog(const char* fmt, ...);
void clearDebugLog();
void publishHASensorDiscovery(
    const char* component,
    const char* objectId,
    const char* name,
    const char* valueTemplate,
    const char* unit,
    const char* deviceClass,
    const char* icon,
    const char* entityCategory,
    const char* payloadOn,
    const char* payloadOff);
void publishHAButtonDiscovery(
    const char* objectId,
    const char* name,
    const char* commandTopic,
    const char* payloadPress,
    const char* icon,
    const char* entityCategory);
void publishHATextDiscovery(
    const char* objectId,
    const char* name,
    const char* stateTopic,
    const char* valueTemplate,
    const char* commandTopic,
    const char* commandTemplate,
    const char* icon,
    const char* entityCategory);
void publishHANumberDiscovery(
    const char* objectId,
    const char* name,
    const char* stateTopic,
    const char* valueTemplate,
    const char* commandTemplate,
    float minValue,
    float maxValue,
    float stepValue,
    const char* unit,
    const char* icon,
    const char* entityCategory);
void publishHASelectDiscovery(
    const char* objectId,
    const char* name,
    const char* stateTopic,
    const char* commandTemplate,
    const char* optionA,
    const char* optionB,
    const char* optionC,
    const char* icon,
    const char* entityCategory);
bool canExecuteDangerousMqttCommand();
bool bottleCanDispense(uint8_t pumpId, unsigned long durationMs);
float computePumpHardMaxDoseMl(uint8_t pumpId);
float computeSchedullerTotalMlMax();
bool computeDoseDurationMs(uint8_t pumpId, float ml, unsigned long* durationMsOut, const char** reasonOut);
bool isPumpRunning(uint8_t pumpId);
bool checkPumpRunPermission(uint8_t pumpId, unsigned long durationMs, float* requestedMlOut, const char** reasonOut);
void appendWebUiPageStart(String& html, const char* title, const char* heading);
void appendWebUiPageEnd(String& html);
void sendWebUiPageStartChunked(int code, const char* title, const char* heading);
void sendWebUiPageEndChunked();
String normalizeWebUiPath(const String& rawPath);
String buildWebUiSessionCookie();
String generateWebUiSessionToken();
void clearWebUiSession();
bool hasValidWebUiSession();
bool ensureWebUiAuth();
const char* webMimeTypeFromPath(const String& path);
bool sendLittleFSFile(const String& path, bool noCache);
String formatUptimeCompact();
void performFactoryReset(bool wipeFileSystem);
void checkHardwareFactoryReset();

    // -------------- Pump Control
    void setupPumps();
    void setupStatusLeds();
    void pumpStart(uint8_t pumpId, unsigned long maxMs);
    void pumpStop(uint8_t pumpId);
    void pumpsStopAll();
    void pumpsUpdateSafety();
    void updateStatusLeds();



// ---------- Timing ----------
unsigned long lastTelemetryTick = 0;
const unsigned long telemetryInterval = 60000; // 60 seconds (health telemetry)
unsigned long lastAutoPublish = 0;
unsigned long lastAutoStatePublishedAt = 0;
unsigned long autoStateDueAt = 0;
unsigned long haDiscoveryDueAt = 0;
unsigned long configSaveDueAt = 0;
bool lowBottleAlarmPublishPending = false;
const unsigned long autoStateMinIntervalMs = 1000;
unsigned long nextTimeSyncAt = 0;
bool timeSyncHadValidTime = false;
bool bootLogPending = true;
uint32_t minFreeHeap = 0xFFFFFFFFUL;
uint32_t mqttReconnectCount = 0;
bool haDiscoveryRepublishPending = false;
bool autoStatePublishPending = false;
bool haDiscoveryPublishPending = false;
bool configSavePending = false;
uint8_t haDiscoveryStep = 0;
uint8_t bottleStatePublishMask = 0;
uint8_t pumpDoseMlPublishMask = 0;
uint8_t schedullerStatePublishMask = 0;
#if ENABLE_DEBUG_LOG
char debugLogBuf[DEBUG_LOG_BUFFER_SIZE];
size_t debugLogLen = 0;
#endif


// ---------- Boot Info ----------
void applyDefaultIdentityFromMac()
{
    WiFi.mode(WIFI_STA);

    uint8_t mac[6] = {0, 0, 0, 0, 0, 0};
    WiFi.macAddress(mac);

    char macSuffix[5];
    snprintf(macSuffix, sizeof(macSuffix), "%02x%02x", mac[4], mac[5]);

    if (deviceName[0] == '\0' || strcmp(deviceName, DEFAULT_DEVICE_NAME_BASE) == 0)
        snprintf(deviceName, sizeof(deviceName), "%s-%s", DEFAULT_DEVICE_NAME_BASE, macSuffix);

    if (mqttClientId[0] == '\0' || strcmp(mqttClientId, DEFAULT_MQTT_CLIENT_ID_BASE) == 0)
        snprintf(mqttClientId, sizeof(mqttClientId), "%s-%s", DEFAULT_MQTT_CLIENT_ID_BASE, macSuffix);
}

void rebuildMqttTopics()
{
    size_t out = 0;
    const char* src = (deviceName[0] != '\0') ? deviceName : DEFAULT_DEVICE_NAME_BASE;

    for (size_t i = 0; src[i] != '\0' && out + 1 < sizeof(mqttTopicNode); i++)
    {
        unsigned char ch = (unsigned char)src[i];
        if (isalnum(ch))
            mqttTopicNode[out++] = (char)tolower(ch);
        else if (ch == '-' || ch == '_')
            mqttTopicNode[out++] = (char)ch;
        else
            mqttTopicNode[out++] = '-';
    }

    if (out == 0)
    {
        strncpy(mqttTopicNode, DEFAULT_DEVICE_NAME_BASE, sizeof(mqttTopicNode) - 1);
        mqttTopicNode[sizeof(mqttTopicNode) - 1] = '\0';
    }
    else
    {
        mqttTopicNode[out] = '\0';
    }

    snprintf(MQTT_TOPIC_BASE, sizeof(MQTT_TOPIC_BASE), "hydrodozerpump/%s", mqttTopicNode);
    snprintf(MQTT_TOPIC_AVAIL, sizeof(MQTT_TOPIC_AVAIL), "%s/status", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_HEALTH, sizeof(MQTT_TOPIC_HEALTH), "%s/health", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_AUTO_STATE, sizeof(MQTT_TOPIC_AUTO_STATE), "%s/auto/state", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_CMD, sizeof(MQTT_TOPIC_CMD), "%s/cmd", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_HA_CMD, sizeof(MQTT_TOPIC_HA_CMD), "%s/ha/cmd", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_CMD_ACK, sizeof(MQTT_TOPIC_CMD_ACK), "%s/cmd/ack", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_SYSTEM_STATE, sizeof(MQTT_TOPIC_SYSTEM_STATE), "%s/system/state", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_BOTTLE_STATE_FMT, sizeof(MQTT_TOPIC_BOTTLE_STATE_FMT), "%s/bottle/%%u/state", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_PUMP_DOSE_ML_FMT, sizeof(MQTT_TOPIC_PUMP_DOSE_ML_FMT), "%s/pump/%%u/dose_ml", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_BOTTLE_PERMISSION_FMT, sizeof(MQTT_TOPIC_BOTTLE_PERMISSION_FMT), "%s/bottle/%%u/permission", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_PUMP_PERMISSION_FMT, sizeof(MQTT_TOPIC_PUMP_PERMISSION_FMT), "%s/pump/%%u/permission", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_SCHEDULLER_TOTAL_ML, sizeof(MQTT_TOPIC_SCHEDULLER_TOTAL_ML), "%s/scheduller/total_ml", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_SCHEDULLER_PART_A_PCT, sizeof(MQTT_TOPIC_SCHEDULLER_PART_A_PCT), "%s/scheduller/part_a_pct", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_SCHEDULLER_PART_B_PCT, sizeof(MQTT_TOPIC_SCHEDULLER_PART_B_PCT), "%s/scheduller/part_b_pct", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_SCHEDULLER_PAUSE, sizeof(MQTT_TOPIC_SCHEDULLER_PAUSE), "%s/scheduller/pause", MQTT_TOPIC_BASE);
    snprintf(MQTT_TOPIC_LOW_BOTTLE_ALARM_PCT, sizeof(MQTT_TOPIC_LOW_BOTTLE_ALARM_PCT), "%s/alarm/low_bottle_pct", MQTT_TOPIC_BASE);
}

const char* getHADeviceDisplayName()
{
    if (mqttClientId[0] != '\0')
        return mqttClientId;
    return deviceName;
}

void printBootInfo()
{
    Serial.println();
    Serial.println("========== HydroDozerPump ==========");

    // Reset reason
    Serial.print("Reset reason: ");
    Serial.println(ESP.getResetReason());

    // Boot mode
    Serial.print("Boot mode: ");
    Serial.println(ESP.getBootMode());

    // CPU frequency
    Serial.print("CPU freq: ");
    Serial.print(ESP.getCpuFreqMHz());
    Serial.println(" MHz");

    // Free heap
    Serial.print("Free heap: ");
    Serial.print(ESP.getFreeHeap());
    Serial.println(" bytes");

    Serial.println("====================================");
}

void appendDebugLog(const char* fmt, ...)
{
#if !ENABLE_DEBUG_LOG
    (void)fmt;
    return;
#else
    if (!fmt)
        return;

    char line[320];
    int off = snprintf(line, sizeof(line), "[%lu] ", millis());
    if (off < 0) off = 0;
    if (off >= (int)sizeof(line)) off = (int)sizeof(line) - 1;

    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(line + off, sizeof(line) - off, fmt, args);
    va_end(args);

    if (n < 0)
        return;

    size_t lineLen = strnlen(line, sizeof(line));
    if (lineLen + 1 >= sizeof(line))
        lineLen = sizeof(line) - 2;
    line[lineLen++] = '\n';
    line[lineLen] = '\0';

    if (lineLen >= sizeof(debugLogBuf))
    {
        size_t start = lineLen - (sizeof(debugLogBuf) - 1);
        memcpy(debugLogBuf, line + start, sizeof(debugLogBuf) - 1);
        debugLogBuf[sizeof(debugLogBuf) - 1] = '\0';
        debugLogLen = sizeof(debugLogBuf) - 1;
        return;
    }

    if (debugLogLen + lineLen >= sizeof(debugLogBuf))
    {
        size_t drop = (debugLogLen + lineLen) - (sizeof(debugLogBuf) - 1);
        if (drop > debugLogLen)
            drop = debugLogLen;
        memmove(debugLogBuf, debugLogBuf + drop, debugLogLen - drop);
        debugLogLen -= drop;
        debugLogBuf[debugLogLen] = '\0';
    }

    memcpy(debugLogBuf + debugLogLen, line, lineLen);
    debugLogLen += lineLen;
    debugLogBuf[debugLogLen] = '\0';
#endif
}

void clearDebugLog()
{
#if ENABLE_DEBUG_LOG
    debugLogLen = 0;
    debugLogBuf[0] = '\0';
#endif
}

void performFactoryReset(bool wipeFileSystem)
{
    Serial.println("[RESET] Factory reset requested");
    wm.resetSettings();     // clear WiFiManager credentials
    ESP.eraseConfig();      // clear SDK WiFi/system config sectors

    if (wipeFileSystem)
    {
        // Flash erase sets bytes to 0xFF (not 0x00). Full format removes all files.
        if (LittleFS.format())
        {
            Serial.println("[RESET] LittleFS formatted (all files erased)");
        }
        else
        {
            Serial.println("[RESET] LittleFS format failed");
        }
    }

    delay(500);
    ESP.restart();
}

void checkHardwareFactoryReset()
{
    // Do not evaluate jumper on software-initiated restarts (web/MQTT reboot),
    // otherwise a fixed-low input can cause repeated reset loops.
    const rst_info* rst = ESP.getResetInfoPtr();
    if (rst && rst->reason == REASON_SOFT_RESTART)
    {
        Serial.println("[RESET] Soft restart detected, skipping hardware reset jumper check");
        return;
    }

    pinMode(FACTORY_RESET_PIN, INPUT_PULLUP);
    if (digitalRead(FACTORY_RESET_PIN) == HIGH)
        return;

    Serial.println("[RESET] Hardware reset jumper detected. Hold for 3 seconds...");
    unsigned long t0 = millis();
    while (millis() - t0 < FACTORY_RESET_HOLD_MS)
    {
        if (digitalRead(FACTORY_RESET_PIN) == HIGH)
        {
            Serial.println("[RESET] Jumper released before timeout, reset canceled");
            return;
        }
        delay(25);
    }

    Serial.println("[RESET] Hardware reset confirmed");
    performFactoryReset(true);
}

float clampPercent(float v)
{
    if (v < 0.0f) return 0.0f;
    if (v > 100.0f) return 100.0f;
    return v;
}

float bottlePercent(uint8_t bottleId)
{
    if (bottleId < 1 || bottleId > 2)
        return 0.0f;

    if (bottles[bottleId].capacity_ml <= 0.0f)
        return 0.0f;

    float percent = (bottles[bottleId].remaining_ml / bottles[bottleId].capacity_ml) * 100.0f;
    if (percent < 0.0f) return 0.0f;
    if (percent > 100.0f) return 100.0f;
    return percent;
}

bool isBottleEmpty(uint8_t bottleId)
{
    if (bottleId < 1 || bottleId > 2)
        return false;
    return (bottles[bottleId].remaining_ml <= 0.0f);
}

bool isBottleLowAlarm(uint8_t bottleId)
{
    if (bottleId < 1 || bottleId > 2)
        return false;
    if (isBottleEmpty(bottleId))
        return false;
    return (bottlePercent(bottleId) <= lowBottleAlarmPct);
}

bool isAnyBottleEmpty()
{
    return isBottleEmpty(1) || isBottleEmpty(2);
}

bool isAnyBottleLowAlarm()
{
    return isBottleLowAlarm(1) || isBottleLowAlarm(2);
}

void setSchedullerPartA(float pctA)
{
    schedullerPartAPct = clampPercent(pctA);
    schedullerPartBPct = 100.0f - schedullerPartAPct;
}

void setSchedullerPartB(float pctB)
{
    schedullerPartBPct = clampPercent(pctB);
    schedullerPartAPct = 100.0f - schedullerPartBPct;
}

bool clampSchedullerTotalMlToCurrentMax()
{
    float maxTotalMl = computeSchedullerTotalMlMax();
    if (maxTotalMl <= 0.0f || schedullerTotalMl <= 0.0f)
        return false;

    if (schedullerTotalMl > maxTotalMl)
    {
        schedullerTotalMl = maxTotalMl;
        return true;
    }

    return false;
}

bool isValidSchedullerHour(int hour)
{
    return (hour >= 0 && hour <= 23);
}

int getSchedullerHourBySlot(uint8_t slot)
{
    if (slot == 1) return schedullerHour1;
    if (slot == 2) return schedullerHour2;
    if (slot == 3) return schedullerHour3;
    return -1;
}

void setSchedullerHourBySlot(uint8_t slot, int hour)
{
    if (!isValidSchedullerHour(hour))
        return;

    if (slot == 1) schedullerHour1 = hour;
    else if (slot == 2) schedullerHour2 = hour;
    else if (slot == 3) schedullerHour3 = hour;
}

void clearSchedullerExecutionHistory()
{
    for (uint8_t slot = 1; slot <= 3; slot++)
    {
        for (uint8_t pumpId = 1; pumpId <= 2; pumpId++)
        {
            schedullerLastRunYear[slot][pumpId] = -1;
            schedullerLastRunYDay[slot][pumpId] = -1;
        }
        schedullerLastAttemptEpoch[slot] = 0;
    }
    schedullerPump2Sequence.pending = false;
    schedullerPump2Sequence.gapStarted = false;
    schedullerPump2Sequence.slot = 0;
    schedullerPump2Sequence.durationMs = 0;
    schedullerPump2Sequence.requestedMl = 0.0f;
    schedullerPump2Sequence.gapStartedAtMs = 0;
    schedullerPump2Sequence.runYear = -1;
    schedullerPump2Sequence.runYDay = -1;
    // Persist clear action so reboot does not restore previously completed jobs.
    requestConfigSave(100);
}

bool hasSchedullerJobRunToday(uint8_t slot, uint8_t pumpId, int year, int yday)
{
    if (slot < 1 || slot > 3 || pumpId < 1 || pumpId > 2)
        return false;
    return (schedullerLastRunYear[slot][pumpId] == year &&
            schedullerLastRunYDay[slot][pumpId] == yday);
}

void markSchedullerJobRunToday(uint8_t slot, uint8_t pumpId, int year, int yday)
{
    if (slot < 1 || slot > 3 || pumpId < 1 || pumpId > 2)
        return;
    schedullerLastRunYear[slot][pumpId] = year;
    schedullerLastRunYDay[slot][pumpId] = yday;
    requestConfigSave(100);
}

void appendPumpRunCsv(uint8_t pumpId, float volumeMl, const char* note)
{
    if (pumpId < 1 || pumpId > 2 || volumeMl <= 0.0f)
        return;

    bool shouldWriteHeader = false;
    if (LittleFS.exists(PUMP_RUN_LOG_CSV_PATH))
    {
        File fCheck = LittleFS.open(PUMP_RUN_LOG_CSV_PATH, "r");
        if (fCheck)
        {
            size_t currentSize = fCheck.size();
            fCheck.close();
            if (currentSize >= PUMP_RUN_LOG_MAX_BYTES)
                LittleFS.remove(PUMP_RUN_LOG_CSV_PATH);
            else if (currentSize == 0)
                shouldWriteHeader = true;
        }
    }
    else
    {
        shouldWriteHeader = true;
    }

    if (!LittleFS.exists(PUMP_RUN_LOG_CSV_PATH))
        shouldWriteHeader = true;

    File f = LittleFS.open(PUMP_RUN_LOG_CSV_PATH, "a");
    if (!f)
        return;

    if (shouldWriteHeader)
    {
        static const char* kPumpLogHeader = "Date, Pump ID, Volume in ML, Note\n";
        f.write((const uint8_t*)kPumpLogHeader, strlen(kPumpLogHeader));
    }

    char timestamp[24];
    if (hasValidSystemTime())
    {
        time_t nowTs = time(nullptr);
        struct tm localTm;
        localtime_r(&nowTs, &localTm);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTm);
    }
    else
    {
        unsigned long totalSec = millis() / 1000UL;
        unsigned long hh = (totalSec / 3600UL) % 24UL;
        unsigned long mm = (totalSec / 60UL) % 60UL;
        unsigned long ss = totalSec % 60UL;
        snprintf(timestamp, sizeof(timestamp), "0000-00-00 %02lu:%02lu:%02lu", hh, mm, ss);
    }

    char sanitizedNote[48];
    sanitizedNote[0] = '\0';
    if (note)
    {
        size_t out = 0;
        for (size_t i = 0; note[i] != '\0' && out + 1 < sizeof(sanitizedNote); i++)
        {
            char c = note[i];
            if (c == '"' || c == '\r' || c == '\n')
                c = ' ';
            sanitizedNote[out++] = c;
        }
        sanitizedNote[out] = '\0';
    }

    char line[128];
    int n = snprintf(line, sizeof(line), "%s,%u,%.2f,\"%s\"\n", timestamp, pumpId, volumeMl, sanitizedNote);
    if (n > 0)
        f.write((const uint8_t*)line, (size_t)n);
    f.close();
}

void appendPumpLogEventCsv(const char* note)
{
    bool shouldWriteHeader = false;
    if (LittleFS.exists(PUMP_RUN_LOG_CSV_PATH))
    {
        File fCheck = LittleFS.open(PUMP_RUN_LOG_CSV_PATH, "r");
        if (fCheck)
        {
            size_t currentSize = fCheck.size();
            fCheck.close();
            if (currentSize >= PUMP_RUN_LOG_MAX_BYTES)
                LittleFS.remove(PUMP_RUN_LOG_CSV_PATH);
            else if (currentSize == 0)
                shouldWriteHeader = true;
        }
    }
    else
    {
        shouldWriteHeader = true;
    }

    if (!LittleFS.exists(PUMP_RUN_LOG_CSV_PATH))
        shouldWriteHeader = true;

    File f = LittleFS.open(PUMP_RUN_LOG_CSV_PATH, "a");
    if (!f)
        return;

    if (shouldWriteHeader)
    {
        static const char* kPumpLogHeader = "Date, Pump ID, Volume in ML, Note\n";
        f.write((const uint8_t*)kPumpLogHeader, strlen(kPumpLogHeader));
    }

    char timestamp[24];
    if (hasValidSystemTime())
    {
        time_t nowTs = time(nullptr);
        struct tm localTm;
        localtime_r(&nowTs, &localTm);
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &localTm);
    }
    else
    {
        unsigned long totalSec = millis() / 1000UL;
        unsigned long hh = (totalSec / 3600UL) % 24UL;
        unsigned long mm = (totalSec / 60UL) % 60UL;
        unsigned long ss = totalSec % 60UL;
        snprintf(timestamp, sizeof(timestamp), "0000-00-00 %02lu:%02lu:%02lu", hh, mm, ss);
    }

    char sanitizedNote[48];
    sanitizedNote[0] = '\0';
    if (note)
    {
        size_t out = 0;
        for (size_t i = 0; note[i] != '\0' && out + 1 < sizeof(sanitizedNote); i++)
        {
            char c = note[i];
            if (c == '"' || c == '\r' || c == '\n')
                c = ' ';
            sanitizedNote[out++] = c;
        }
        sanitizedNote[out] = '\0';
    }

    char line[128];
    int n = snprintf(line, sizeof(line), "%s,,,\"%s\"\n", timestamp, sanitizedNote);
    if (n > 0)
        f.write((const uint8_t*)line, (size_t)n);
    f.close();
}

static const char* CONFIG_SECRET_PREFIX = "obf1:";
static const char* CONFIG_SECRET_SALT = "HydroDozerPumpCfgSaltV1";

uint8_t configSecretKeyByte(size_t index)
{
    size_t saltLen = strlen(CONFIG_SECRET_SALT);
    uint8_t saltByte = (uint8_t)CONFIG_SECRET_SALT[index % saltLen];
    return (uint8_t)(saltByte ^ (uint8_t)(0x5A + ((index * 17U) & 0xFFU)));
}

int hexNibbleFromChar(char c)
{
    if (c >= '0' && c <= '9') return (int)(c - '0');
    if (c >= 'a' && c <= 'f') return 10 + (int)(c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (int)(c - 'A');
    return -1;
}

String obfuscateConfigSecret(const char* plain)
{
    if (!plain || plain[0] == '\0')
        return String("");

    size_t len = strlen(plain);
    String out;
    out.reserve(strlen(CONFIG_SECRET_PREFIX) + (len * 2));
    out += CONFIG_SECRET_PREFIX;
    static const char kHex[] = "0123456789ABCDEF";

    for (size_t i = 0; i < len; i++)
    {
        uint8_t x = ((uint8_t)plain[i]) ^ configSecretKeyByte(i);
        out += kHex[(x >> 4) & 0x0F];
        out += kHex[x & 0x0F];
    }

    return out;
}

bool deobfuscateConfigSecret(const char* stored, char* out, size_t outLen)
{
    if (!stored || !out || outLen == 0)
        return false;

    size_t prefixLen = strlen(CONFIG_SECRET_PREFIX);
    if (strncmp(stored, CONFIG_SECRET_PREFIX, prefixLen) != 0)
    {
        strncpy(out, stored, outLen - 1);
        out[outLen - 1] = '\0';
        return true;
    }

    const char* hex = stored + prefixLen;
    size_t hexLen = strlen(hex);
    if ((hexLen % 2) != 0)
        return false;

    size_t plainLen = hexLen / 2;
    if (plainLen >= outLen)
        return false;

    for (size_t i = 0; i < plainLen; i++)
    {
        int hi = hexNibbleFromChar(hex[i * 2]);
        int lo = hexNibbleFromChar(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0)
            return false;
        uint8_t x = (uint8_t)((hi << 4) | lo);
        out[i] = (char)(x ^ configSecretKeyByte(i));
    }
    out[plainLen] = '\0';
    return true;
}

void saveConfig()
{
    // Keep large JSON buffers scoped so stack is released before MQTT publish.
    {
        StaticJsonDocument<2048> doc;

        JsonObject bottlesObj = doc.createNestedObject("bottles");
        for (int i = 1; i <= 2; i++)
        {
            JsonObject o = bottlesObj.createNestedObject(String(i));
            o["capacity"] = bottles[i].capacity_ml;
            o["remaining"] = bottles[i].remaining_ml;
        }

        JsonObject pumpsObj = doc.createNestedObject("pumps");
        for (int i = 1; i <= 2; i++)
        {
            JsonObject o = pumpsObj.createNestedObject(String(i));
            o["flow"] = pumpConfig[i].flow_ml_per_sec;
        }

        JsonObject schedullerObj = doc.createNestedObject("scheduller");
        schedullerObj["total_ml"] = schedullerTotalMl;
        schedullerObj["part_a_pct"] = schedullerPartAPct;
        schedullerObj["part_b_pct"] = schedullerPartBPct;
        schedullerObj["pause"] = schedullerPaused ? 1 : 0;
        schedullerObj["hour_1"] = schedullerHour1;
        schedullerObj["hour_2"] = schedullerHour2;
        schedullerObj["hour_3"] = schedullerHour3;
        schedullerObj["last_run_s1_p1_year"] = schedullerLastRunYear[1][1];
        schedullerObj["last_run_s1_p1_yday"] = schedullerLastRunYDay[1][1];
        schedullerObj["last_run_s1_p2_year"] = schedullerLastRunYear[1][2];
        schedullerObj["last_run_s1_p2_yday"] = schedullerLastRunYDay[1][2];
        schedullerObj["last_run_s2_p1_year"] = schedullerLastRunYear[2][1];
        schedullerObj["last_run_s2_p1_yday"] = schedullerLastRunYDay[2][1];
        schedullerObj["last_run_s2_p2_year"] = schedullerLastRunYear[2][2];
        schedullerObj["last_run_s2_p2_yday"] = schedullerLastRunYDay[2][2];
        schedullerObj["last_run_s3_p1_year"] = schedullerLastRunYear[3][1];
        schedullerObj["last_run_s3_p1_yday"] = schedullerLastRunYDay[3][1];
        schedullerObj["last_run_s3_p2_year"] = schedullerLastRunYear[3][2];
        schedullerObj["last_run_s3_p2_yday"] = schedullerLastRunYDay[3][2];

        JsonObject alarmsObj = doc.createNestedObject("alarms");
        alarmsObj["low_bottle_pct"] = lowBottleAlarmPct;

        JsonObject systemObj = doc.createNestedObject("system");
        JsonObject networkObj = systemObj.createNestedObject("network");
        networkObj["hostname"] = deviceName;
        networkObj["mode"] = networkUseDhcp ? "dhcp" : "manual";
        networkObj["ip"] = networkIp;
        networkObj["gateway"] = networkGateway;
        networkObj["netmask"] = networkNetmask;
        networkObj["dns"] = networkDns;
        JsonObject mqttObj = systemObj.createNestedObject("mqtt");
        mqttObj["broker"] = mqttBroker;
        mqttObj["port"] = mqttPort;
        mqttObj["client_id"] = mqttClientId;
        mqttObj["user"] = mqttUser;
        String mqttPassStored = obfuscateConfigSecret(mqttPass);
        mqttObj["pass"] = mqttPassStored;
        JsonObject otaObj = systemObj.createNestedObject("ota");
        otaObj["user"] = otaUser;
        String otaPassStored = obfuscateConfigSecret(otaPass);
        otaObj["pass"] = otaPassStored;
        otaObj["web_login_required"] = webUiLoginRequired;
        JsonObject timeObj = systemObj.createNestedObject("time");
        timeObj["tz"] = timeZoneSpec;

        File f = LittleFS.open("/config.json", "w");
        if (!f) return;

        serializeJson(doc, f);
        f.close();
    }

    yield();

    publishSystemState();
}

void loadConfig()
{
    if (!LittleFS.exists("/config.json"))
        return;

    File f = LittleFS.open("/config.json", "r");
    if (!f) return;

    StaticJsonDocument<2048> doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return;

    if (doc.containsKey("bottles"))
    {
        JsonObject bottlesObj = doc["bottles"];
        for (int i = 1; i <= 2; i++)
        {
            if (!bottlesObj.containsKey(String(i))) continue;
            JsonObject o = bottlesObj[String(i)];
            if (o.containsKey("capacity"))
                bottles[i].capacity_ml = o["capacity"];
            if (o.containsKey("remaining"))
                bottles[i].remaining_ml = o["remaining"];
        }
    }

    if (doc.containsKey("pumps"))
    {
        JsonObject pumpsObj = doc["pumps"];
        for (int i = 1; i <= 2; i++)
        {
            if (!pumpsObj.containsKey(String(i))) continue;
            JsonObject o = pumpsObj[String(i)];
            if (o.containsKey("flow"))
                pumpConfig[i].flow_ml_per_sec = o["flow"];
        }
    }

    if (doc.containsKey("scheduller"))
    {
        JsonObject schedullerObj = doc["scheduller"];
        float savedTotalMl = schedullerObj["total_ml"] | schedullerTotalMl;

        bool hasA = schedullerObj.containsKey("part_a_pct");
        bool hasB = schedullerObj.containsKey("part_b_pct");
        if (hasA)
            setSchedullerPartA(schedullerObj["part_a_pct"] | schedullerPartAPct);
        else if (hasB)
            setSchedullerPartB(schedullerObj["part_b_pct"] | schedullerPartBPct);

        if (savedTotalMl > 0.0f)
        {
            schedullerTotalMl = savedTotalMl;
            clampSchedullerTotalMlToCurrentMax();
        }

        if (schedullerObj.containsKey("pause"))
            schedullerPaused = ((schedullerObj["pause"] | 0) != 0);

        if (schedullerObj.containsKey("hour_1"))
        {
            int h = schedullerObj["hour_1"] | schedullerHour1;
            if (isValidSchedullerHour(h))
                schedullerHour1 = h;
        }
        if (schedullerObj.containsKey("hour_2"))
        {
            int h = schedullerObj["hour_2"] | schedullerHour2;
            if (isValidSchedullerHour(h))
                schedullerHour2 = h;
        }
        if (schedullerObj.containsKey("hour_3"))
        {
            int h = schedullerObj["hour_3"] | schedullerHour3;
            if (isValidSchedullerHour(h))
            schedullerHour3 = h;
        }

        if (doc.containsKey("alarms"))
        {
            JsonObject alarmsObj = doc["alarms"];
            if (alarmsObj.containsKey("low_bottle_pct"))
                lowBottleAlarmPct = clampPercent(alarmsObj["low_bottle_pct"] | lowBottleAlarmPct);
        }

        if (schedullerObj.containsKey("last_run_s1_p1_year"))
            schedullerLastRunYear[1][1] = schedullerObj["last_run_s1_p1_year"] | schedullerLastRunYear[1][1];
        if (schedullerObj.containsKey("last_run_s1_p1_yday"))
            schedullerLastRunYDay[1][1] = schedullerObj["last_run_s1_p1_yday"] | schedullerLastRunYDay[1][1];
        if (schedullerObj.containsKey("last_run_s1_p2_year"))
            schedullerLastRunYear[1][2] = schedullerObj["last_run_s1_p2_year"] | schedullerLastRunYear[1][2];
        if (schedullerObj.containsKey("last_run_s1_p2_yday"))
            schedullerLastRunYDay[1][2] = schedullerObj["last_run_s1_p2_yday"] | schedullerLastRunYDay[1][2];

        if (schedullerObj.containsKey("last_run_s2_p1_year"))
            schedullerLastRunYear[2][1] = schedullerObj["last_run_s2_p1_year"] | schedullerLastRunYear[2][1];
        if (schedullerObj.containsKey("last_run_s2_p1_yday"))
            schedullerLastRunYDay[2][1] = schedullerObj["last_run_s2_p1_yday"] | schedullerLastRunYDay[2][1];
        if (schedullerObj.containsKey("last_run_s2_p2_year"))
            schedullerLastRunYear[2][2] = schedullerObj["last_run_s2_p2_year"] | schedullerLastRunYear[2][2];
        if (schedullerObj.containsKey("last_run_s2_p2_yday"))
            schedullerLastRunYDay[2][2] = schedullerObj["last_run_s2_p2_yday"] | schedullerLastRunYDay[2][2];

        if (schedullerObj.containsKey("last_run_s3_p1_year"))
            schedullerLastRunYear[3][1] = schedullerObj["last_run_s3_p1_year"] | schedullerLastRunYear[3][1];
        if (schedullerObj.containsKey("last_run_s3_p1_yday"))
            schedullerLastRunYDay[3][1] = schedullerObj["last_run_s3_p1_yday"] | schedullerLastRunYDay[3][1];
        if (schedullerObj.containsKey("last_run_s3_p2_year"))
            schedullerLastRunYear[3][2] = schedullerObj["last_run_s3_p2_year"] | schedullerLastRunYear[3][2];
        if (schedullerObj.containsKey("last_run_s3_p2_yday"))
            schedullerLastRunYDay[3][2] = schedullerObj["last_run_s3_p2_yday"] | schedullerLastRunYDay[3][2];
    }
    else if (doc.containsKey("1"))
    {
        for (int i = 1; i <= 2; i++)
        {
            if (!doc.containsKey(String(i))) continue;
            JsonObject o = doc[String(i)];
            if (o.containsKey("capacity"))
                bottles[i].capacity_ml = o["capacity"];
            if (o.containsKey("remaining"))
                bottles[i].remaining_ml = o["remaining"];
            if (o.containsKey("flow"))
                pumpConfig[i].flow_ml_per_sec = o["flow"];
        }
    }

    if (doc.containsKey("system"))
    {
        JsonObject systemObj = doc["system"];

        if (systemObj.containsKey("network"))
        {
            JsonObject networkObj = systemObj["network"];
            if (networkObj.containsKey("mode") && networkObj["mode"].is<const char*>())
            {
                const char* m = networkObj["mode"];
                networkUseDhcp = !(m && strcmp(m, "manual") == 0);
            }
            if (networkObj.containsKey("hostname") && networkObj["hostname"].is<const char*>())
            {
                const char* h = networkObj["hostname"];
                if (h && h[0] != '\0')
                {
                    strncpy(deviceName, h, DEVICE_NAME_LEN - 1);
                    deviceName[DEVICE_NAME_LEN - 1] = '\0';
                }
            }
            if (networkObj.containsKey("ip") && networkObj["ip"].is<const char*>())
            {
                const char* v = networkObj["ip"];
                if (v)
                {
                    strncpy(networkIp, v, IPV4_STR_LEN - 1);
                    networkIp[IPV4_STR_LEN - 1] = '\0';
                }
            }
            if (networkObj.containsKey("gateway") && networkObj["gateway"].is<const char*>())
            {
                const char* v = networkObj["gateway"];
                if (v)
                {
                    strncpy(networkGateway, v, IPV4_STR_LEN - 1);
                    networkGateway[IPV4_STR_LEN - 1] = '\0';
                }
            }
            if (networkObj.containsKey("netmask") && networkObj["netmask"].is<const char*>())
            {
                const char* v = networkObj["netmask"];
                if (v)
                {
                    strncpy(networkNetmask, v, IPV4_STR_LEN - 1);
                    networkNetmask[IPV4_STR_LEN - 1] = '\0';
                }
            }
            if (networkObj.containsKey("dns") && networkObj["dns"].is<const char*>())
            {
                const char* v = networkObj["dns"];
                if (v)
                {
                    strncpy(networkDns, v, IPV4_STR_LEN - 1);
                    networkDns[IPV4_STR_LEN - 1] = '\0';
                }
            }
        }

        if (systemObj.containsKey("mqtt"))
        {
            JsonObject mqttObj = systemObj["mqtt"];
            if (mqttObj.containsKey("broker") && mqttObj["broker"].is<const char*>())
            {
                const char* v = mqttObj["broker"];
                if (v && v[0] != '\0')
                {
                    strncpy(mqttBroker, v, MQTT_HOST_LEN - 1);
                    mqttBroker[MQTT_HOST_LEN - 1] = '\0';
                }
            }
            if (mqttObj.containsKey("port"))
            {
                int p = mqttObj["port"] | 0;
                if (p > 0 && p <= 65535)
                    mqttPort = (uint16_t)p;
            }
            if (mqttObj.containsKey("client_id") && mqttObj["client_id"].is<const char*>())
            {
                const char* v = mqttObj["client_id"];
                if (v && v[0] != '\0')
                {
                    strncpy(mqttClientId, v, MQTT_CLIENT_ID_LEN - 1);
                    mqttClientId[MQTT_CLIENT_ID_LEN - 1] = '\0';
                }
            }
            if (mqttObj.containsKey("user") && mqttObj["user"].is<const char*>())
            {
                const char* v = mqttObj["user"];
                if (v)
                {
                    strncpy(mqttUser, v, MQTT_USER_LEN - 1);
                    mqttUser[MQTT_USER_LEN - 1] = '\0';
                }
            }
            if (mqttObj.containsKey("pass") && mqttObj["pass"].is<const char*>())
            {
                const char* v = mqttObj["pass"];
                if (v)
                {
                    char decoded[MQTT_PASS_LEN];
                    if (deobfuscateConfigSecret(v, decoded, sizeof(decoded)))
                        strncpy(mqttPass, decoded, MQTT_PASS_LEN - 1);
                    else
                        strncpy(mqttPass, v, MQTT_PASS_LEN - 1);
                    mqttPass[MQTT_PASS_LEN - 1] = '\0';
                }
            }
        }

        if (systemObj.containsKey("ota"))
        {
            JsonObject otaObj = systemObj["ota"];
            if (otaObj.containsKey("user") && otaObj["user"].is<const char*>())
            {
                const char* v = otaObj["user"];
                if (v && v[0] != '\0')
                {
                    strncpy(otaUser, v, OTA_USER_LEN - 1);
                    otaUser[OTA_USER_LEN - 1] = '\0';
                }
            }
            if (otaObj.containsKey("pass") && otaObj["pass"].is<const char*>())
            {
                const char* v = otaObj["pass"];
                if (v && v[0] != '\0')
                {
                    char decoded[OTA_PASS_LEN];
                    if (deobfuscateConfigSecret(v, decoded, sizeof(decoded)))
                        strncpy(otaPass, decoded, OTA_PASS_LEN - 1);
                    else
                        strncpy(otaPass, v, OTA_PASS_LEN - 1);
                    otaPass[OTA_PASS_LEN - 1] = '\0';
                }
            }
            if (otaObj.containsKey("web_login_required"))
                webUiLoginRequired = ((otaObj["web_login_required"] | 1) != 0);
        }

        if (systemObj.containsKey("time"))
        {
            JsonObject timeObj = systemObj["time"];
            if (timeObj.containsKey("tz") && timeObj["tz"].is<const char*>())
            {
                const char* v = timeObj["tz"];
                if (v && v[0] != '\0')
                {
                    strncpy(timeZoneSpec, v, TIMEZONE_LEN - 1);
                    timeZoneSpec[TIMEZONE_LEN - 1] = '\0';
                }
            }
        }
    }
}

void setup()
{
    Serial.begin(115200);
    minFreeHeap = ESP.getFreeHeap();

    delay(1500);

    unsigned long start = millis();
    while (!Serial && millis() - start < 2000)
    {
        delay(10);
    }

    printBootInfo();
    LittleFS.begin();
    randomSeed(ESP.getChipId() ^ micros() ^ millis());
    checkHardwareFactoryReset();
    loadConfig();
    applyDefaultIdentityFromMac();
    rebuildMqttTopics();
    setupWiFi();
    setupTimeSync();

    setupWebServer();
    setupOTA();

    setupMQTT();

    setupPumps();
    setupStatusLeds();

}

// Interval helper that tolerates timestamp corruption/wrap edge cases.
bool isIntervalElapsed(unsigned long now, unsigned long* lastTs, unsigned long intervalMs)
{
    if (!lastTs || intervalMs == 0)
        return false;

    long elapsed = (long)(now - *lastTs);
    if (elapsed < 0)
    {
        // If a timestamp gets corrupted into the future, resync without flooding logs.
        *lastTs = now;
        return false;
    }

    return ((unsigned long)elapsed >= intervalMs);
}


void loop()
{
    unsigned long now = millis();
    uint32_t heapNow = ESP.getFreeHeap();
    if (heapNow < minFreeHeap)
        minFreeHeap = heapNow;

    if (isIntervalElapsed(now, &lastTelemetryTick, telemetryInterval))
    {
        lastTelemetryTick = now;
        printHealth();
    }

    if (isIntervalElapsed(now, &lastAutoPublish, autoPublishInterval))
    {
        lastAutoPublish = now;
        requestAutoStatePublish(0);
    }

    // pump loop
    pumpsUpdateSafety();
    updateStatusLeds();

    //MQTT Loop
    ensureMQTT();
    mqttClient.loop();
    processTimeSync();
    processSchedullerPump2Sequence();
    processScheduller();
    processPendingHADiscoveryRepublish();
    processDeferredPublishes();

    // Networking
    server.handleClient();      // <-- REQUIRED
    MDNS.update();              // <-- REQUIRED on ESP8266

}


// ---------- WiFi ----------
// WiFiManager wm; //reference moved at the top

void setupWiFi()
{

    WiFi.setSleepMode(WIFI_NONE_SLEEP);  // Do not sleep

    if (networkUseDhcp)
    {
        WiFi.config(0U, 0U, 0U);
        Serial.println("[WIFI] IP mode: DHCP");
    }
    else
    {
        IPAddress ip, gw, mask, dns;
        bool ok = ip.fromString(networkIp) &&
                  gw.fromString(networkGateway) &&
                  mask.fromString(networkNetmask) &&
                  dns.fromString(networkDns);
        if (ok)
        {
            WiFi.config(ip, gw, mask, dns);
            Serial.println("[WIFI] IP mode: Manual");
        }
        else
        {
            Serial.println("[WIFI] Invalid manual network settings. Falling back to DHCP.");
            WiFi.config(0U, 0U, 0U);
            networkUseDhcp = true;
        }
    }

    wm.setHostname(deviceName);
    WiFi.hostname(deviceName);

    Serial.println();
    Serial.println("[WIFI] Starting WiFiManager");

    // Keep portal simple and reliable
    wm.setConfigPortalTimeout(180);   // 3 minutes
    wm.setConnectTimeout(20);          // seconds

    // Option A behavior:
    // Try saved WiFi, otherwise start captive portal automatically
    bool connected = wm.autoConnect("HydroDozerPump-Setup");

    if (!connected)
    {
        Serial.println("[WIFI] Failed to connect, rebooting...");
        delay(2000);
        ESP.restart();
    }

    // Connected successfully
    Serial.println("[WIFI] Connected!");
    Serial.print("[WIFI] SSID: ");
    Serial.println(WiFi.SSID());

    Serial.print("[WIFI] IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("[WIFI] RSSI: ");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm");

    if (MDNS.begin(deviceName))
    {
    Serial.println("[MDNS] Responder started");
    Serial.print("[MDNS] http://");
    Serial.print(deviceName);
    Serial.println(".local");
    } else
    {
    Serial.println("[MDNS] Failed to start");
    }

}

bool hasValidSystemTime()
{
    time_t now = time(nullptr);
    return (now >= TIME_VALID_AFTER_EPOCH);
}

bool isValidTimeZoneSpec(const String& tzSpec)
{
    if (tzSpec.length() == 0 || tzSpec.length() >= TIMEZONE_LEN)
        return false;

    for (size_t i = 0; i < tzSpec.length(); i++)
    {
        char c = tzSpec.charAt(i);
        if (c < 32 || c > 126)
            return false;
    }

    return true;
}

void applyTimeZone()
{
    setenv("TZ", timeZoneSpec, 1);
    tzset();
    appendDebugLog("TIME zone applied tz=%s", timeZoneSpec);
}

void requestTimeSync(const char* reason)
{
    if (!WiFi.isConnected())
    {
        nextTimeSyncAt = millis() + TIME_SYNC_RETRY_MS;
        appendDebugLog("TIME sync deferred reason=%s (wifi disconnected)", reason ? reason : "unknown");
        return;
    }

    // Use TZ-aware API so localtime()/strftime(%Z) follows the configured POSIX TZ string.
    configTime(timeZoneSpec, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY);
    nextTimeSyncAt = millis() + TIME_SYNC_INTERVAL_MS;

    Serial.print("[TIME] Sync requested: ");
    Serial.println(reason ? reason : "periodic");
    appendDebugLog("TIME sync requested reason=%s next_in_ms=%lu", reason ? reason : "periodic", TIME_SYNC_INTERVAL_MS);
}

void setupTimeSync()
{
    applyTimeZone();
    nextTimeSyncAt = 0;
    requestTimeSync("boot");
}

void processTimeSync()
{
    unsigned long nowMs = millis();
    if ((long)(nowMs - nextTimeSyncAt) >= 0)
    {
        requestTimeSync("12h");
    }

    bool validNow = hasValidSystemTime();
    if (validNow && !timeSyncHadValidTime)
    {
        timeSyncHadValidTime = true;
        time_t now = time(nullptr);
        Serial.print("[TIME] Time acquired, epoch=");
        Serial.println((unsigned long)now);
        appendDebugLog("TIME acquired epoch=%lu", (unsigned long)now);
        if (bootLogPending)
        {
            appendPumpLogEventCsv("System boot");
            bootLogPending = false;
        }
    }
    else if (!validNow)
    {
        timeSyncHadValidTime = false;
    }
}

bool runSchedullerDoseForSlot(uint8_t slot, int year, int yday)
{
    if (slot < 1 || slot > 3)
        return false;

    float doseEachMl = schedullerTotalMl / 3.0f;
    if (doseEachMl <= 0.0f)
        return false;

    float dosePartAMl = (doseEachMl * schedullerPartAPct) / 100.0f;
    if (dosePartAMl < 0.0f) dosePartAMl = 0.0f;
    if (dosePartAMl > doseEachMl) dosePartAMl = doseEachMl;
    float dosePartBMl = doseEachMl - dosePartAMl;
    if (dosePartBMl < 0.0f) dosePartBMl = 0.0f;

    bool runPump1 = (dosePartAMl > 0.0f);
    bool runPump2 = (dosePartBMl > 0.0f);

    // Persisted per-job history prevents same-day duplicate runs after reboot.
    if (runPump1 && hasSchedullerJobRunToday(slot, 1, year, yday))
        runPump1 = false;
    if (runPump2 && hasSchedullerJobRunToday(slot, 2, year, yday))
        runPump2 = false;

    if (!runPump1 && !runPump2)
        return false;

    unsigned long duration1 = 0;
    unsigned long duration2 = 0;
    const char* reason = "invalid";

    if (runPump1 && !computeDoseDurationMs(1, dosePartAMl, &duration1, &reason))
    {
        publishDosePermission(1, dosePartAMl, false, reason);
        appendDebugLog("SCHED slot=%u pump=1 reject reason=%s", slot, reason);
        return false;
    }
    if (runPump2 && !computeDoseDurationMs(2, dosePartBMl, &duration2, &reason))
    {
        publishDosePermission(2, dosePartBMl, false, reason);
        appendDebugLog("SCHED slot=%u pump=2 reject reason=%s", slot, reason);
        return false;
    }

    if (runPump1)
    {
        float req1 = dosePartAMl;
        if (!checkPumpRunPermission(1, duration1, &req1, &reason))
        {
            publishDosePermission(1, req1, false, reason);
            appendDebugLog("SCHED slot=%u pump=1 blocked reason=%s", slot, reason);
            return false;
        }
    }
    if (runPump2)
    {
        float req2 = dosePartBMl;
        if (!checkPumpRunPermission(2, duration2, &req2, &reason))
        {
            publishDosePermission(2, req2, false, reason);
            appendDebugLog("SCHED slot=%u pump=2 blocked reason=%s", slot, reason);
            return false;
        }
    }

    if (runPump1)
    {
        pumpStart(1, duration1);
        if (!isPumpRunning(1))
        {
            publishDosePermission(1, dosePartAMl, false, "start_failed");
            appendDebugLog("SCHED slot=%u pump=1 start_failed", slot);
            return false;
        }
        publishDosePermission(1, dosePartAMl, true, "ok");
        markSchedullerJobRunToday(slot, 1, year, yday);
    }
    if (runPump2 && !runPump1)
    {
        pumpStart(2, duration2);
        if (!isPumpRunning(2))
        {
            publishDosePermission(2, dosePartBMl, false, "start_failed");
            appendDebugLog("SCHED slot=%u pump=2 start_failed", slot);
            return false;
        }
        publishDosePermission(2, dosePartBMl, true, "ok");
        markSchedullerJobRunToday(slot, 2, year, yday);
    }
    else if (runPump1 && runPump2)
    {
        // Scheduler path is sequential: Pump 1 runs first, then Pump 2 after a fixed gap.
        schedullerPump2Sequence.pending = true;
        schedullerPump2Sequence.gapStarted = false;
        schedullerPump2Sequence.slot = slot;
        schedullerPump2Sequence.durationMs = duration2;
        schedullerPump2Sequence.requestedMl = dosePartBMl;
        schedullerPump2Sequence.gapStartedAtMs = 0;
        schedullerPump2Sequence.runYear = year;
        schedullerPump2Sequence.runYDay = yday;
        publishDosePermission(2, dosePartBMl, true, "queued");
        appendDebugLog("SCHED slot=%u pump=2 queued after pump=1", slot);
    }

    requestAutoStatePublish(100);
    if (mqttClient.connected())
        publishCmdAck(true, "scheduller_dose", "started");

    Serial.print("[SCHED] Slot ");
    Serial.print(slot);
    Serial.print(" started: total=");
    Serial.print(doseEachMl, 2);
    Serial.print("ml A=");
    Serial.print(dosePartAMl, 2);
    Serial.print("ml B=");
    Serial.print(dosePartBMl, 2);
    Serial.println("ml");

    appendDebugLog("SCHED slot=%u started each=%.2f A=%.2f B=%.2f", slot, doseEachMl, dosePartAMl, dosePartBMl);
    return true;
}

void processSchedullerPump2Sequence()
{
    if (!schedullerPump2Sequence.pending)
        return;

    if (schedullerPaused)
    {
        appendDebugLog("SCHED slot=%u pump=2 canceled_pause", schedullerPump2Sequence.slot);
        schedullerPump2Sequence.pending = false;
        schedullerPump2Sequence.gapStarted = false;
        schedullerPump2Sequence.slot = 0;
        schedullerPump2Sequence.durationMs = 0;
        schedullerPump2Sequence.requestedMl = 0.0f;
        schedullerPump2Sequence.gapStartedAtMs = 0;
        schedullerPump2Sequence.runYear = -1;
        schedullerPump2Sequence.runYDay = -1;
        requestAutoStatePublish(100);
        return;
    }

    if (isPumpRunning(1))
        return;

    if (!schedullerPump2Sequence.gapStarted)
    {
        schedullerPump2Sequence.gapStarted = true;
        schedullerPump2Sequence.gapStartedAtMs = millis();
        Serial.print("[SCHED] Slot ");
        Serial.print(schedullerPump2Sequence.slot);
        Serial.println(" Pump 2 queued, waiting 10s gap");
        appendDebugLog("SCHED slot=%u pump=2 gap_start", schedullerPump2Sequence.slot);
        return;
    }

    if ((millis() - schedullerPump2Sequence.gapStartedAtMs) < SCHEDULLER_PUMP_SWITCH_GAP_MS)
        return;

    // Respect any currently running operation before starting the queued step.
    if (isPumpRunning(2))
        return;

    float requestedMl = schedullerPump2Sequence.requestedMl;
    const char* reason = "invalid";
    if (!checkPumpRunPermission(2, schedullerPump2Sequence.durationMs, &requestedMl, &reason))
    {
        publishDosePermission(2, requestedMl, false, reason);
        Serial.print("[SCHED] Slot ");
        Serial.print(schedullerPump2Sequence.slot);
        Serial.print(" Pump 2 blocked: ");
        Serial.println(reason);
        appendDebugLog("SCHED slot=%u pump=2 blocked_after_gap reason=%s", schedullerPump2Sequence.slot, reason);
        schedullerPump2Sequence.pending = false;
        schedullerPump2Sequence.gapStarted = false;
        schedullerPump2Sequence.slot = 0;
        schedullerPump2Sequence.durationMs = 0;
        schedullerPump2Sequence.requestedMl = 0.0f;
        schedullerPump2Sequence.gapStartedAtMs = 0;
        schedullerPump2Sequence.runYear = -1;
        schedullerPump2Sequence.runYDay = -1;
        requestAutoStatePublish(100);
        return;
    }

    pumpStart(2, schedullerPump2Sequence.durationMs);
    if (isPumpRunning(2))
    {
        publishDosePermission(2, requestedMl, true, "ok");
        Serial.print("[SCHED] Slot ");
        Serial.print(schedullerPump2Sequence.slot);
        Serial.println(" Pump 2 started");
        appendDebugLog("SCHED slot=%u pump=2 started_after_gap", schedullerPump2Sequence.slot);
        markSchedullerJobRunToday(
            schedullerPump2Sequence.slot,
            2,
            schedullerPump2Sequence.runYear,
            schedullerPump2Sequence.runYDay);
    }
    else
    {
        publishDosePermission(2, requestedMl, false, "start_failed");
        appendDebugLog("SCHED slot=%u pump=2 start_failed_after_gap", schedullerPump2Sequence.slot);
    }

    schedullerPump2Sequence.pending = false;
    schedullerPump2Sequence.gapStarted = false;
    schedullerPump2Sequence.slot = 0;
    schedullerPump2Sequence.durationMs = 0;
    schedullerPump2Sequence.requestedMl = 0.0f;
    schedullerPump2Sequence.gapStartedAtMs = 0;
    schedullerPump2Sequence.runYear = -1;
    schedullerPump2Sequence.runYDay = -1;
    requestAutoStatePublish(100);
}

void processScheduller()
{
    if (schedullerPaused)
        return;

    // If a queued Pump 2 scheduler step is pending, do not start a new scheduler slot.
    if (schedullerPump2Sequence.pending)
        return;

    if (!hasValidSystemTime())
        return;

    // Avoid launching a scheduled cycle while another pump cycle is active.
    if (isPumpRunning(1) || isPumpRunning(2))
        return;

    time_t nowTs = time(nullptr);
    struct tm localTm;
    localtime_r(&nowTs, &localTm);

    const int year = localTm.tm_year;
    const int yday = localTm.tm_yday;
    const int hour = localTm.tm_hour;

    for (uint8_t slot = 1; slot <= 3; slot++)
    {
        if (isPumpRunning(1) || isPumpRunning(2))
            return;

        const int slotHour = getSchedullerHourBySlot(slot);
        if (!isValidSchedullerHour(slotHour))
            continue;
        if (hour != slotHour)
            continue;
        bool slotPump1Done = hasSchedullerJobRunToday(slot, 1, year, yday);
        bool slotPump2Done = hasSchedullerJobRunToday(slot, 2, year, yday);
        if (slotPump1Done && slotPump2Done)
            continue;
        if ((nowTs - schedullerLastAttemptEpoch[slot]) < 60)
            continue;

        schedullerLastAttemptEpoch[slot] = nowTs;
        runSchedullerDoseForSlot(slot, year, yday);
    }
}

String normalizeWebUiPath(const String& rawPath)
{
    String path = rawPath;
    path.trim();
    if (path.length() == 0)
        return "/";
    if (!path.startsWith("/"))
        return "/";
    if (path.startsWith("//"))
        return "/";
    if (path.indexOf("..") >= 0 || path.indexOf('\\') >= 0)
        return "/";
    return path;
}

String buildWebUiSessionCookie()
{
    String cookie = "HDPSESSID=";
    cookie += webUiSessionToken;
    cookie += "; Path=/; HttpOnly; SameSite=Lax";
    return cookie;
}

String generateWebUiSessionToken()
{
    uint32_t a = ESP.getChipId();
    uint32_t b = micros();
    uint32_t c = millis();
    uint32_t d = (uint32_t)random(0x7FFFFFFF);
    char token[WEB_UI_SESSION_TOKEN_LEN];
    snprintf(token, sizeof(token), "%08lx%08lx%08lx%08lx",
             (unsigned long)a,
             (unsigned long)b,
             (unsigned long)c,
             (unsigned long)d);
    return String(token);
}

void clearWebUiSession()
{
    webUiSessionToken[0] = '\0';
}

bool hasValidWebUiSession()
{
    if (!webUiLoginRequired)
        return true;
    if (webUiSessionToken[0] == '\0')
        return false;
    if (!server.hasHeader("Cookie"))
        return false;

    String cookie = server.header("Cookie");
    int start = cookie.indexOf("HDPSESSID=");
    if (start < 0)
        return false;
    start += 10; // strlen("HDPSESSID=")
    int end = cookie.indexOf(';', start);
    String token = (end >= 0) ? cookie.substring(start, end) : cookie.substring(start);
    token.trim();
    return (token.length() > 0 && token == String(webUiSessionToken));
}

bool ensureWebUiAuth()
{
    if (!webUiLoginRequired)
        return true;
    if (hasValidWebUiSession())
        return true;

    String next = "/";
    if (server.method() == HTTP_GET)
        next = normalizeWebUiPath(server.uri());
    server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
    server.sendHeader("Pragma", "no-cache");
    server.sendHeader("Expires", "0");
    server.sendHeader("Location", "/login?next=" + next, true);
    server.send(303, "text/plain", "");
    return false;
}

const char* webMimeTypeFromPath(const String& path)
{
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".js")) return "application/javascript";
    if (path.endsWith(".json")) return "application/json";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".ico")) return "image/x-icon";
    if (path.endsWith(".txt")) return "text/plain";
    return "application/octet-stream";
}

bool sendLittleFSFile(const String& path, bool noCache)
{
    if (!LittleFS.exists(path))
        return false;

    File f = LittleFS.open(path, "r");
    if (!f)
        return false;

    if (noCache)
    {
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "0");
    }

    server.streamFile(f, webMimeTypeFromPath(path));
    f.close();
    return true;
}

String formatUptimeCompact()
{
    unsigned long totalSec = millis() / 1000UL;
    unsigned long days = totalSec / 86400UL;
    unsigned long hours = (totalSec % 86400UL) / 3600UL;
    unsigned long mins = (totalSec % 3600UL) / 60UL;
    unsigned long secs = totalSec % 60UL;

    String out;
    if (days > 0) { out += String(days); out += "d"; }
    if (hours > 0 || days > 0) { out += String(hours); out += "h"; }
    if (mins > 0 || hours > 0 || days > 0) { out += String(mins); out += "m"; }
    out += String(secs);
    out += "s";
    return out;
}

void appendWebUiPageStart(String& html, const char* title, const char* heading)
{
    html += "<!DOCTYPE html><html lang='en'><head>";
    html += "<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>";
    html += "<title>";
    html += (title && title[0] != '\0') ? title : "HydroDozerPump";
    html += "</title><style>";
    html += WEB_UI_BASE_CSS;
    html += "</style></head><body><div class='main'>";
    if (heading && heading[0] != '\0')
    {
        html += "<div style='text-align:center;'><h3>&#127811; ";
        html += heading;
        html += " &#127811;</h3></div>";
    }
}

void appendWebUiPageEnd(String& html)
{
    html += "<br><div style='text-align:right;font-size:10px;color:grey;'><hr>HydroDozerPump ";
    html += FW_VERSION;
    html += "</div></div></body></html>";
}

void sendWebUiPageStartChunked(int code, const char* title, const char* heading)
{
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(code, "text/html", "");
    server.sendContent("<!DOCTYPE html><html lang='en'><head>");
    server.sendContent("<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1,user-scalable=no'>");
    server.sendContent("<title>");
    server.sendContent((title && title[0] != '\0') ? title : "HydroDozerPump");
    server.sendContent("</title><style>");
    server.sendContent(WEB_UI_BASE_CSS);
    server.sendContent("</style></head><body><div class='main'>");
    if (heading && heading[0] != '\0')
    {
        server.sendContent("<div style='text-align:center;'><h3>&#127811; ");
        server.sendContent(heading);
        server.sendContent(" &#127811;</h3></div>");
    }
}

void sendWebUiPageEndChunked()
{
    server.sendContent("<br><div style='text-align:right;font-size:10px;color:grey;'><hr>HydroDozerPump ");
    server.sendContent(FW_VERSION);
    server.sendContent("</div></div></body></html>");
    // Terminates chunked response.
    server.sendContent("");
}

#include "web_ui_section.h"

void setupOTA()     // OTA plugin to update the ESP8266 via the web portal
{
    ElegantOTA.begin(&server, otaUser, otaPass);   // attaches to your existing web server
    Serial.println("[OTA] ElegantOTA ready");
    Serial.print("[OTA] Open http://");
    Serial.print(deviceName);
    Serial.println(".local/update");
}

#include "mqtt_section.h"

void setupPumps()
{
    pinMode(PUMP1_PIN, OUTPUT);
    pinMode(PUMP2_PIN, OUTPUT);

    // Boot-safe OFF
    digitalWrite(PUMP1_PIN, PUMP_ACTIVE_HIGH ? LOW : HIGH);
    digitalWrite(PUMP2_PIN, PUMP_ACTIVE_HIGH ? LOW : HIGH);

    Serial.println("[PUMP] Pins initialized, pumps OFF");
}

void ledApplyPwm(uint8_t pin, uint16_t level)
{
    uint16_t clamped = (level > STATUS_LED_PWM_MAX) ? STATUS_LED_PWM_MAX : level;
    if (STATUS_LED_ACTIVE_HIGH)
        analogWrite(pin, clamped);
    else
        analogWrite(pin, STATUS_LED_PWM_MAX - clamped);
}

void ledApplyDigital(uint8_t pin, bool on)
{
    ledApplyPwm(pin, on ? STATUS_LED_PWM_MAX : 0U);
}

void setupStatusLeds()
{
    pinMode(STATUS_LED_GREEN_PIN, OUTPUT);
    pinMode(STATUS_LED_RED_PIN, OUTPUT);
    analogWriteRange(STATUS_LED_PWM_MAX);
    ledApplyDigital(STATUS_LED_GREEN_PIN, true);
    ledApplyDigital(STATUS_LED_RED_PIN, false);
}

void updateStatusLeds()
{
    if (schedullerPaused || isAnyBottleEmpty())
    {
        bool lightsOn = ((millis() / STATUS_LED_BLINK_INTERVAL_MS) % 2UL) == 0UL;
        ledApplyDigital(STATUS_LED_GREEN_PIN, lightsOn);
        ledApplyDigital(STATUS_LED_RED_PIN, lightsOn);
        return;
    }

    bool anyPumpRunning = pump1.running || pump2.running;
    bool lowBottleAlarm = isAnyBottleLowAlarm();

    if (lowBottleAlarm)
    {
        ledApplyDigital(STATUS_LED_GREEN_PIN, true);
        ledApplyDigital(STATUS_LED_RED_PIN, true);
        return;
    }

    if (anyPumpRunning)
    {
        unsigned long phaseMs = millis() % STATUS_LED_PULSE_PERIOD_MS;
        unsigned long halfPeriod = STATUS_LED_PULSE_PERIOD_MS / 2UL;
        uint16_t level = 0;

        if (phaseMs < halfPeriod)
            level = STATUS_LED_PWM_MIN + (uint16_t)(((STATUS_LED_PWM_MAX - STATUS_LED_PWM_MIN) * phaseMs) / halfPeriod);
        else
            level = STATUS_LED_PWM_MAX - (uint16_t)(((STATUS_LED_PWM_MAX - STATUS_LED_PWM_MIN) * (phaseMs - halfPeriod)) / halfPeriod);

        ledApplyPwm(STATUS_LED_GREEN_PIN, level);
    }
    else
    {
        ledApplyDigital(STATUS_LED_GREEN_PIN, true);
    }

    ledApplyDigital(STATUS_LED_RED_PIN, false);
}

void pumpApplyOutput(uint8_t pin, bool on)
{
    if (PUMP_ACTIVE_HIGH)
        digitalWrite(pin, on ? HIGH : LOW);
    else
        digitalWrite(pin, on ? LOW : HIGH);
}

PumpState* getPump(uint8_t pumpId, uint8_t* pinOut)
{
    if (pumpId == 1)
    {
        *pinOut = PUMP1_PIN;
        return &pump1;
    }
    if (pumpId == 2)
    {
        *pinOut = PUMP2_PIN;
        return &pump2;
    }
    return nullptr;
}


// -------------------  Validate if pump can dispense --------------------//


        bool bottleCanDispense(uint8_t pumpId, unsigned long durationMs)
        {
            Bottle* b = getBottleForPump(pumpId);
            if (!b) return false;

            if (b->remaining_ml <= 0) return false;  // explicit empty check

            float ml = (durationMs / 1000.0) * pumpConfig[pumpId].flow_ml_per_sec;
            return b->remaining_ml >= ml;
        }



void pumpStart(uint8_t pumpId, unsigned long maxMs)
{
    uint8_t pin = 0;
    PumpState* p = getPump(pumpId, &pin);
    if (!p)
        return;

    if (p->running)
    {
        Serial.print("[PUMP] Pump ");
        Serial.print(pumpId);
        Serial.println(" already running (ignored)");
        return;
    }

    // Clamp runtime to hard max
    if (maxMs == 0 || maxMs > PUMP_HARD_MAX_MS)
        maxMs = PUMP_HARD_MAX_MS;

    if (!bottleCanDispense(pumpId, maxMs))
    {
        Serial.println("[BOTTLE] Not enough liquid. Pump blocked.");
        return;
    }

    p->running = true;
    p->startedAt = millis();
    p->maxRunMs = maxMs;

    pumpApplyOutput(pin, true);

    Serial.print("[PUMP] Pump ");
    Serial.print(pumpId);
    Serial.print(" ON (");
    Serial.print(maxMs / 1000);
    Serial.println("s run time)");
    requestAutoStatePublish(100);
}

void pumpStop(uint8_t pumpId)
{
    uint8_t pin = 0;
    PumpState* p = getPump(pumpId, &pin);
    if (!p)
        return;

    if (!p->running)
        return;

    unsigned long runtime = millis() - p->startedAt;

    p->running = false;
    p->startedAt = 0;
    p->maxRunMs = 0;

    pumpApplyOutput(pin, false);

    Serial.print("[PUMP] Pump ");
    Serial.print(pumpId);
    Serial.println(" OFF");

    if (calibrationAwaitResult[pumpId] && runtime + 250UL < CAL_RUN_DURATION_MS)
    {
        calibrationAwaitResult[pumpId] = false;
        Serial.print("[CAL] Pump ");
        Serial.print(pumpId);
        Serial.println(" calibration run interrupted; discarded");
    }

    // ------- update bottle quantity -----------//

    Bottle* b = getBottleForPump(pumpId);
    if (b)
    {
        float ml = (runtime / 1000.0) * pumpConfig[pumpId].flow_ml_per_sec;

        b->remaining_ml -= ml;
        if (b->remaining_ml < 0) b->remaining_ml = 0;

        Serial.print("[BOTTLE] Remaining: ");
        Serial.print(b->remaining_ml);
        Serial.println(" ml");

        char note[16];
        snprintf(note, sizeof(note), "%.0f%%", bottlePercent(bottleIdForPump(pumpId)));
        appendPumpRunCsv(pumpId, ml, note);
        requestBottleStatePublish(bottleIdForPump(pumpId));
        requestConfigSave(100);
    }

    requestAutoStatePublish(100);


}

void pumpsStopAll()
{
    pumpStop(1);
    pumpStop(2);
    // Cancel pending scheduler sequencer step to avoid deferred restart after a manual stop-all.
    schedullerPump2Sequence.pending = false;
    schedullerPump2Sequence.gapStarted = false;
    schedullerPump2Sequence.slot = 0;
    schedullerPump2Sequence.durationMs = 0;
    schedullerPump2Sequence.requestedMl = 0.0f;
    schedullerPump2Sequence.gapStartedAtMs = 0;
    schedullerPump2Sequence.runYear = -1;
    schedullerPump2Sequence.runYDay = -1;
    Serial.println("[PUMP] All pumps OFF");
}

void pumpsUpdateSafety()
{
    unsigned long now = millis();

    if (pump1.running && (now - pump1.startedAt >= pump1.maxRunMs))
    {
        Serial.println("[PUMP] Pump 1 safety timeout -> OFF");
        pumpStop(1);
    }

    if (pump2.running && (now - pump2.startedAt >= pump2.maxRunMs))
    {
        Serial.println("[PUMP] Pump 2 safety timeout -> OFF");
        pumpStop(2);
    }
}
