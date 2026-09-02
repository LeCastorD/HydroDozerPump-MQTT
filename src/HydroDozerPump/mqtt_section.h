static void formatIpAddress(const IPAddress& ip, char* out, size_t outLen)
{
    if (outLen == 0)
        return;
    snprintf(out, outLen, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
}

void setupMQTT() // MQTT Publish
{
    mqttClient.setServer(mqttBroker, mqttPort);
    mqttClient.setKeepAlive(60);
    mqttClient.setCallback(mqttCallback);
}

void ensureMQTT() // MQTT Connect
{
    if (mqttClient.connected())
        return;

    unsigned long now = millis();

    if (now - lastMQTTAttempt < mqttInterval)
        return;

    lastMQTTAttempt = now;

    Serial.println("[MQTT] Attempting connection...");

    if (mqttClient.connect(
        mqttClientId,
        mqttUser,
        mqttPass,
        MQTT_TOPIC_AVAIL,
        1,
        true,
        "offline")) // Last Will
    {
        Serial.println("[MQTT] Connected!");
        mqttReconnectCount++;

        mqttClient.publish(MQTT_TOPIC_AVAIL, "online", true);

        mqttClient.subscribe(MQTT_TOPIC_CMD);
        mqttClient.subscribe(MQTT_TOPIC_HA_CMD);
        mqttClient.subscribe(MQTT_TOPIC_HA_STATUS);
        publishSystemState();
        requestHADiscoveryPublish(250);
        requestAutoStatePublish(500);
        requestAllRetainedStatePublish();
        printHealth();

        Serial.println("[MQTT] Subscribed to CMD topic");

    }
    else
    {
        Serial.print("[MQTT] Failed, rc=");
        Serial.println(mqttClient.state());
    }
}

void printHealth() // MQTT refresh
{
    uint32_t heap = ESP.getFreeHeap();
    uint8_t frag = ESP.getHeapFragmentation();
    uint32_t maxBlock = ESP.getMaxFreeBlockSize();
    if (heap < minFreeHeap)
        minFreeHeap = heap;

    Serial.print("[HEALTH] Free heap: ");
    Serial.println(heap);

    if (mqttClient.connected())
    {
        StaticJsonDocument<320> doc;
        char ipBuf[16];
        formatIpAddress(WiFi.localIP(), ipBuf, sizeof(ipBuf));
        doc["uptime_s"] = millis() / 1000UL;
        doc["wifi"] = WiFi.isConnected();
        doc["ip"] = ipBuf;
        doc["rssi"] = WiFi.RSSI();
        doc["heap"] = heap;
        doc["min_heap"] = minFreeHeap;
        doc["heap_frag"] = frag;
        doc["max_free_block"] = maxBlock;
        doc["mqtt_reconnects"] = mqttReconnectCount;

        char payload[320];
        serializeJson(doc, payload, sizeof(payload));
        mqttClient.publish(MQTT_TOPIC_HEALTH, payload, true);
    }
}

// MQTT mapping (HA bottle state source for capacity/name/percent displays)
// State topic (retained): hydrodozerpump/bottle/<bottleId>/state
// Published JSON: {"id":X,"name":"...","remaining_ml":...,"capacity_ml":...,"percent":...}
// Related commands consumed in mqttCallback():
// - hydrodozerpump/ha/cmd: {"ha":"set_capacity","bottle":<id>,"ml":<float>}
// - hydrodozerpump/cmd: {"bottle":<id>,"refill":true}
void publishBottleState(uint8_t bottleId)
{
    if (!mqttClient.connected() || bottleId < 1 || bottleId > 2)
        return;

    float percent = 0.0f;
    if (bottles[bottleId].capacity_ml > 0.0f)
        percent = (bottles[bottleId].remaining_ml / bottles[bottleId].capacity_ml) * 100.0f;

    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    char topic[64];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_BOTTLE_STATE_FMT, bottleId);

    StaticJsonDocument<192> doc;
    doc["id"] = bottleId;
    doc["name"] = bottleLabelById(bottleId);
    doc["remaining_ml"] = bottles[bottleId].remaining_ml;
    doc["capacity_ml"] = bottles[bottleId].capacity_ml;
    doc["percent"] = percent;

    char payload[192];
    serializeJson(doc, payload, sizeof(payload));
    mqttClient.publish(topic, payload, true);
}

void publishAllBottleStates()
{
    publishBottleState(1);
    publishBottleState(2);
}

// MQTT mapping (HA Number: "Pump X Dose mL")
// State topic (retained): hydrodozerpump/pump/<pumpId>/dose_ml
// Published payload: decimal string in mL (example: "25.00")
// Related command consumed in mqttCallback():
// - hydrodozerpump/ha/cmd: {"ha":"set_dose_ml","pump":<id>,"ml":<float>}
void publishPumpDoseMlState(uint8_t pumpId)
{
    if (!mqttClient.connected() || pumpId < 1 || pumpId > 2)
        return;

    char topic[64];
    snprintf(topic, sizeof(topic), MQTT_TOPIC_PUMP_DOSE_ML_FMT, pumpId);

    char payload[16];
    dtostrf(haDoseMl[pumpId], 0, 2, payload);
    mqttClient.publish(topic, payload, true);
}

void publishAllPumpDoseMlStates()
{
    publishPumpDoseMlState(1);
    publishPumpDoseMlState(2);
}

void publishLowBottleAlarmState()
{
    if (!mqttClient.connected())
        return;

    char payload[8];
    snprintf(payload, sizeof(payload), "%u", (unsigned int)(lowBottleAlarmPct + 0.5f));
    mqttClient.publish(MQTT_TOPIC_LOW_BOTTLE_ALARM_PCT, payload, true);
}

// MQTT mapping (HA Number controls in Scheduller section)
// State topics (retained):
// - hydrodozerpump/scheduller/total_ml
// - hydrodozerpump/scheduller/part_a_pct
// - hydrodozerpump/scheduller/part_b_pct
// - hydrodozerpump/scheduller/pause
// Related commands consumed in mqttCallback():
// - hydrodozerpump/ha/cmd: {"ha":"set_scheduller_total_ml","ml":<float>}
// - hydrodozerpump/ha/cmd: {"ha":"set_scheduller_part_a_pct","pct":<float>}
// - hydrodozerpump/ha/cmd: {"ha":"set_scheduller_part_b_pct","pct":<float>}
// - hydrodozerpump/ha/cmd: {"ha":"set_scheduller_pause","value":0|1}
void publishSchedullerState(uint8_t fieldId)
{
    if (!mqttClient.connected())
        return;

    const char* topic = nullptr;
    char payload[20];

    if (fieldId == 1)
    {
        topic = MQTT_TOPIC_SCHEDULLER_TOTAL_ML;
        dtostrf(schedullerTotalMl, 0, 2, payload);
    }
    else if (fieldId == 2)
    {
        topic = MQTT_TOPIC_SCHEDULLER_PART_A_PCT;
        dtostrf(schedullerPartAPct, 0, 1, payload);
    }
    else if (fieldId == 3)
    {
        topic = MQTT_TOPIC_SCHEDULLER_PART_B_PCT;
        dtostrf(schedullerPartBPct, 0, 1, payload);
    }
    else if (fieldId == 4)
    {
        topic = MQTT_TOPIC_SCHEDULLER_PAUSE;
        snprintf(payload, sizeof(payload), "%u", schedullerPaused ? 1U : 0U);
    }
    else
    {
        return;
    }

    mqttClient.publish(topic, payload, true);
}

void publishAllSchedullerStates()
{
    publishSchedullerState(1);
    publishSchedullerState(2);
    publishSchedullerState(3);
    publishSchedullerState(4);
}

// MQTT mapping (runtime permission feedback for dosing requests)
// Topics (non-retained):
// - hydrodozerpump/pump/<pumpId>/permission
// - hydrodozerpump/bottle/<bottleId>/permission (when bottle assigned)
// Published JSON: {"pump":...,"bottle":...,"ok":...,"requested_ml":...,"remaining_ml":...,"percent":...,"reason":"..."}
// Emitted when dosing is accepted/rejected before pump start.
void publishDosePermission(uint8_t pumpId, float requestedMl, bool allowed, const char* reason)
{
    if (!mqttClient.connected() || pumpId < 1 || pumpId > 2)
        return;

    Bottle* b = getBottleForPump(pumpId);
    uint8_t bottleId = bottleIdForPump(pumpId);
    float percent = 0.0f;
    float remainingMl = 0.0f;
    if (b)
    {
        remainingMl = b->remaining_ml;
    }
    if (b && b->capacity_ml > 0.0f)
        percent = (b->remaining_ml / b->capacity_ml) * 100.0f;

    if (percent < 0.0f) percent = 0.0f;
    if (percent > 100.0f) percent = 100.0f;

    StaticJsonDocument<256> doc;
    doc["pump"] = pumpId;
    doc["bottle"] = (b ? bottleId : 0);
    doc["ok"] = allowed;
    doc["requested_ml"] = requestedMl;
    doc["remaining_ml"] = remainingMl;
    doc["percent"] = percent;
    doc["reason"] = reason;

    char payload[256];
    serializeJson(doc, payload, sizeof(payload));

    char topicPump[64];
    snprintf(topicPump, sizeof(topicPump), MQTT_TOPIC_PUMP_PERMISSION_FMT, pumpId);
    mqttClient.publish(topicPump, payload, false);

    if (b && bottleId >= 1 && bottleId <= 2)
    {
        char topicBottle[64];
        snprintf(topicBottle, sizeof(topicBottle), MQTT_TOPIC_BOTTLE_PERMISSION_FMT, bottleId);
        mqttClient.publish(topicBottle, payload, false);
    }
}

// MQTT mapping (generic command acknowledgment)
// Topic (non-retained): hydrodozerpump/cmd/ack
// Published JSON: {"ok":bool,"type":"...","detail":"...","uptime_s":...}
// Used for both hydrodozerpump/cmd and hydrodozerpump/ha/cmd handlers.
void publishCmdAck(bool ok, const char* type, const char* detail)
{
    if (!mqttClient.connected())
        return;

    StaticJsonDocument<160> doc;
    doc["ok"] = ok;
    doc["type"] = type ? type : "";
    doc["detail"] = detail ? detail : "";
    doc["uptime_s"] = millis() / 1000UL;

    char payload[160];
    serializeJson(doc, payload, sizeof(payload));
    mqttClient.publish(MQTT_TOPIC_CMD_ACK, payload, false);
}

// MQTT mapping (static device/network/system configuration snapshot)
// Topic (retained): hydrodozerpump/system/state
// Published JSON includes network, MQTT, and OTA settings metadata.
void publishSystemState()
{
    if (!mqttClient.connected())
        return;

    StaticJsonDocument<1024> doc;
    JsonObject network = doc.createNestedObject("network");
    network["hostname"] = deviceName;
    network["mode"] = networkUseDhcp ? "dhcp" : "manual";
    network["ip"] = networkIp;
    network["gateway"] = networkGateway;
    network["netmask"] = networkNetmask;
    network["dns"] = networkDns;
    network["timezone"] = timeZoneSpec;

    JsonObject mqtt = doc.createNestedObject("mqtt");
    mqtt["broker"] = mqttBroker;
    mqtt["port"] = mqttPort;
    mqtt["client_id"] = mqttClientId;
    mqtt["user"] = mqttUser;
    mqtt["pass_set"] = (mqttPass[0] != '\0');

    JsonObject ota = doc.createNestedObject("ota");
    ota["user"] = otaUser;
    ota["pass_set"] = (otaPass[0] != '\0');

    char payload[1024];
    serializeJson(doc, payload, sizeof(payload));
    mqttClient.publish(MQTT_TOPIC_SYSTEM_STATE, payload, true);
}

// MQTT mapping (primary HA state aggregation payload)
// Topic (retained): hydrodozerpump/auto/state
// Published JSON includes pump states, bottle metrics/names, and network diagnostics.
// Most HA sensor/text entities use value templates against this JSON.
void publishAutoState()
{
    if (!mqttClient.connected())
        return;

    StaticJsonDocument<1024> doc;
    doc["all_pumps_stopped"] = !pump1.running && !pump2.running;

    JsonObject pumps = doc.createNestedObject("pumps");
    pumps["1"] = pump1.running ? "running" : "stopped";
    pumps["2"] = pump2.running ? "running" : "stopped";

    JsonObject bottlesObj = doc.createNestedObject("bottles");
    for (uint8_t i = 1; i <= 2; i++)
    {
        const char* bottleKey = (i == 1) ? "1" : "2";
        JsonObject b = bottlesObj.createNestedObject(bottleKey);
        b["capacity_ml"] = bottles[i].capacity_ml;
        b["remaining_ml"] = bottles[i].remaining_ml;
        b["name"] = bottleLabelById(i);

        float percent = 0.0f;
        if (bottles[i].capacity_ml > 0.0f)
            percent = (bottles[i].remaining_ml / bottles[i].capacity_ml) * 100.0f;
        if (percent < 0.0f) percent = 0.0f;
        if (percent > 100.0f) percent = 100.0f;
        b["percent"] = percent;
    }

    JsonObject network = doc.createNestedObject("network");
    char ipBuf[16];
    formatIpAddress(WiFi.localIP(), ipBuf, sizeof(ipBuf));
    network["mode"] = networkUseDhcp ? "dhcp" : "manual";
    network["wifi_connected"] = WiFi.isConnected();
    network["wifi_rssi"] = WiFi.RSSI();
    network["ip"] = ipBuf;
    network["timezone"] = timeZoneSpec;

    char mdnsHost[DEVICE_NAME_LEN + 7];
    snprintf(mdnsHost, sizeof(mdnsHost), "%s.local", deviceName);
    network["mdns"] = mdnsHost;

    JsonObject pumpNames = doc.createNestedObject("pump_names");
    pumpNames["1"] = pumpLabelById(1);
    pumpNames["2"] = pumpLabelById(2);

    JsonObject scheduller = doc.createNestedObject("scheduller");
    scheduller["total_ml"] = schedullerTotalMl;
    scheduller["part_a_pct"] = schedullerPartAPct;
    scheduller["part_b_pct"] = schedullerPartBPct;
    scheduller["pause"] = schedullerPaused ? 1 : 0;
    scheduller["dose_each_ml"] = schedullerTotalMl / 3.0f;
    scheduller["part_a_ml"] = (schedullerTotalMl * schedullerPartAPct) / 300.0f;
    scheduller["part_b_ml"] = (schedullerTotalMl * schedullerPartBPct) / 300.0f;
    scheduller["hour_1"] = schedullerHour1;
    scheduller["hour_2"] = schedullerHour2;
    scheduller["hour_3"] = schedullerHour3;

    JsonObject alarms = doc.createNestedObject("alarms");
    alarms["low_bottle_pct"] = lowBottleAlarmPct;

    doc["mqtt_reconnects"] = mqttReconnectCount;

    char payload[1024];
    serializeJson(doc, payload, sizeof(payload));
    mqttClient.publish(MQTT_TOPIC_AUTO_STATE, payload, true);
    lastAutoStatePublishedAt = millis();
}

void requestAutoStatePublish(unsigned long delayMs)
{
    unsigned long due = millis() + delayMs;
    if (!autoStatePublishPending || (long)(due - autoStateDueAt) < 0)
        autoStateDueAt = due;
    autoStatePublishPending = true;
}

void requestHADiscoveryPublish(unsigned long delayMs)
{
    unsigned long due = millis() + delayMs;
    if (!haDiscoveryPublishPending || (long)(due - haDiscoveryDueAt) < 0)
        haDiscoveryDueAt = due;
    haDiscoveryPublishPending = true;
    // Discovery is emitted in staged steps from loop() to keep MQTT/heap/WDT load bounded.
    haDiscoveryStep = 0;
}

void requestConfigSave(unsigned long delayMs)
{
    unsigned long due = millis() + delayMs;
    if (!configSavePending || (long)(due - configSaveDueAt) < 0)
        configSaveDueAt = due;
    configSavePending = true;
}

void requestBottleStatePublish(uint8_t bottleId)
{
    if (bottleId >= 1 && bottleId <= 2)
        bottleStatePublishMask |= (1U << bottleId);
}

void requestPumpDoseMlPublish(uint8_t pumpId)
{
    if (pumpId >= 1 && pumpId <= 2)
        pumpDoseMlPublishMask |= (1U << pumpId);
}

void requestSchedullerStatePublish(uint8_t fieldId)
{
    if (fieldId >= 1 && fieldId <= 4)
        schedullerStatePublishMask |= (1U << fieldId);
}

void requestLowBottleAlarmPublish()
{
    lowBottleAlarmPublishPending = true;
}

void requestAllRetainedStatePublish()
{
    bottleStatePublishMask |= (1U << 1) | (1U << 2);
    pumpDoseMlPublishMask |= (1U << 1) | (1U << 2);
    schedullerStatePublishMask |= (1U << 1) | (1U << 2) | (1U << 3) | (1U << 4);
    lowBottleAlarmPublishPending = true;
}

void processDeferredPublishes()
{
    unsigned long now = millis();

    if (configSavePending && (long)(now - configSaveDueAt) >= 0)
    {
        saveConfig();
        configSavePending = false;
        return;
    }

    if (!mqttClient.connected())
        return;

    for (uint8_t id = 1; id <= 2; id++)
    {
        uint8_t bit = (1U << id);
        if (bottleStatePublishMask & bit)
        {
            bottleStatePublishMask &= ~bit;
            publishBottleState(id);
            return;
        }
        if (pumpDoseMlPublishMask & bit)
        {
            pumpDoseMlPublishMask &= ~bit;
            publishPumpDoseMlState(id);
            return;
        }
    }

    for (uint8_t id = 1; id <= 4; id++)
    {
        uint8_t bit = (1U << id);
        if (schedullerStatePublishMask & bit)
        {
            schedullerStatePublishMask &= ~bit;
            publishSchedullerState(id);
            return;
        }
    }

    if (lowBottleAlarmPublishPending)
    {
        lowBottleAlarmPublishPending = false;
        publishLowBottleAlarmState();
        return;
    }

    if (haDiscoveryPublishPending && (long)(now - haDiscoveryDueAt) >= 0)
    {
        if (publishHADiscovery())
        {
            haDiscoveryPublishPending = false;
            haDiscoveryStep = 0;
        }
        return;
    }

    if (autoStatePublishPending && (long)(now - autoStateDueAt) >= 0 &&
        (long)(now - lastAutoStatePublishedAt) >= (long)autoStateMinIntervalMs)
    {
        publishAutoState();
        autoStatePublishPending = false;
        return;
    }
}

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
    const char* payloadOff)
{
    if (!mqttClient.connected() || !component || !objectId || !name || !valueTemplate)
        return;

    const char* haDeviceDisplayName = getHADeviceDisplayName();

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config", component, deviceName, objectId);

    char uniqId[80];
    snprintf(uniqId, sizeof(uniqId), "%s_%s", deviceName, objectId);

    StaticJsonDocument<768> doc;
    doc["name"] = name;
    doc["uniq_id"] = uniqId;
    doc["stat_t"] = MQTT_TOPIC_AUTO_STATE;
    doc["val_tpl"] = valueTemplate;
    doc["avty_t"] = MQTT_TOPIC_AVAIL;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    if (unit && unit[0] != '\0')
        doc["unit_of_meas"] = unit;
    if (deviceClass && deviceClass[0] != '\0')
        doc["dev_cla"] = deviceClass;
    if (icon && icon[0] != '\0')
        doc["ic"] = icon;
    if (entityCategory && entityCategory[0] != '\0')
        doc["ent_cat"] = entityCategory;
    if (payloadOn && payloadOn[0] != '\0')
        doc["pl_on"] = payloadOn;
    if (payloadOff && payloadOff[0] != '\0')
        doc["pl_off"] = payloadOff;

    JsonObject dev = doc.createNestedObject("dev");
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(deviceName);
    dev["name"] = haDeviceDisplayName;
    dev["mdl"] = "HydroDozerPump";
    dev["mf"] = "HydroDozer";

    char payload[768];
    size_t neededLen = measureJson(doc);
    if (neededLen == 0 || neededLen >= sizeof(payload))
    {
        appendDebugLog("HA DISC sensor %s overflow len=%u cap=%u", objectId, (unsigned)neededLen, (unsigned)sizeof(payload));
        return;
    }
    size_t payloadLen = serializeJson(doc, payload, sizeof(payload));
    bool ok = mqttClient.publish(topic, (const uint8_t*)payload, (unsigned int)payloadLen, true);
    appendDebugLog("HA DISC sensor %s len=%u -> %s", objectId, (unsigned)payloadLen, ok ? "ok" : "failed");
}

void publishHAButtonDiscovery(
    const char* objectId,
    const char* name,
    const char* commandTopic,
    const char* payloadPress,
    const char* icon,
    const char* entityCategory)
{
    if (!mqttClient.connected() || !objectId || !name || !payloadPress || !commandTopic)
        return;

    const char* haDeviceDisplayName = getHADeviceDisplayName();

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/button/%s/%s/config", deviceName, objectId);

    char uniqId[80];
    snprintf(uniqId, sizeof(uniqId), "%s_%s", deviceName, objectId);

    StaticJsonDocument<512> doc;
    doc["name"] = name;
    doc["uniq_id"] = uniqId;
    // Buttons can target different topics (ex: stop_all -> MQTT_TOPIC_CMD,
    // dose_now -> MQTT_TOPIC_HA_CMD).
    doc["cmd_t"] = commandTopic;
    doc["payload_press"] = payloadPress;
    doc["avty_t"] = MQTT_TOPIC_AVAIL;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    if (icon && icon[0] != '\0')
        doc["ic"] = icon;
    if (entityCategory && entityCategory[0] != '\0')
        doc["ent_cat"] = entityCategory;

    JsonObject dev = doc.createNestedObject("dev");
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(deviceName);
    dev["name"] = haDeviceDisplayName;
    dev["mdl"] = "HydroDozerPump";
    dev["mf"] = "HydroDozer";

    char payload[512];
    size_t neededLen = measureJson(doc);
    if (neededLen == 0 || neededLen >= sizeof(payload))
    {
        appendDebugLog("HA DISC button %s overflow len=%u cap=%u", objectId, (unsigned)neededLen, (unsigned)sizeof(payload));
        return;
    }
    size_t payloadLen = serializeJson(doc, payload, sizeof(payload));
    bool ok = mqttClient.publish(topic, (const uint8_t*)payload, (unsigned int)payloadLen, true);
    appendDebugLog("HA DISC button %s len=%u -> %s", objectId, (unsigned)payloadLen, ok ? "ok" : "failed");
}

void publishHATextDiscovery(
    const char* objectId,
    const char* name,
    const char* stateTopic,
    const char* valueTemplate,
    const char* commandTopic,
    const char* commandTemplate,
    const char* icon,
    const char* entityCategory)
{
    if (!mqttClient.connected() || !objectId || !name || !stateTopic || !valueTemplate ||
        !commandTopic || !commandTemplate)
        return;

    const char* haDeviceDisplayName = getHADeviceDisplayName();

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/text/%s/%s/config", deviceName, objectId);

    char uniqId[80];
    snprintf(uniqId, sizeof(uniqId), "%s_%s", deviceName, objectId);

    StaticJsonDocument<640> doc;
    doc["name"] = name;
    doc["uniq_id"] = uniqId;
    doc["stat_t"] = stateTopic;
    doc["val_tpl"] = valueTemplate;
    doc["cmd_t"] = commandTopic;
    doc["cmd_tpl"] = commandTemplate;
    doc["avty_t"] = MQTT_TOPIC_AVAIL;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    if (icon && icon[0] != '\0')
        doc["ic"] = icon;
    if (entityCategory && entityCategory[0] != '\0')
        doc["ent_cat"] = entityCategory;

    JsonObject dev = doc.createNestedObject("dev");
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(deviceName);
    dev["name"] = haDeviceDisplayName;
    dev["mdl"] = "HydroDozerPump";
    dev["mf"] = "HydroDozer";

    char payload[640];
    size_t neededLen = measureJson(doc);
    if (neededLen == 0 || neededLen >= sizeof(payload))
    {
        appendDebugLog("HA DISC text %s overflow len=%u cap=%u", objectId, (unsigned)neededLen, (unsigned)sizeof(payload));
        return;
    }
    size_t payloadLen = serializeJson(doc, payload, sizeof(payload));
    bool ok = mqttClient.publish(topic, (const uint8_t*)payload, (unsigned int)payloadLen, true);
    appendDebugLog("HA DISC text %s len=%u -> %s", objectId, (unsigned)payloadLen, ok ? "ok" : "failed");
}

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
    const char* entityCategory)
{
    if (!mqttClient.connected() || !objectId || !name || !stateTopic || !valueTemplate || !commandTemplate)
        return;

    const char* haDeviceDisplayName = getHADeviceDisplayName();

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/number/%s/%s/config", deviceName, objectId);

    char uniqId[80];
    snprintf(uniqId, sizeof(uniqId), "%s_%s", deviceName, objectId);

    StaticJsonDocument<768> doc;
    doc["name"] = name;
    doc["uniq_id"] = uniqId;
    doc["stat_t"] = stateTopic;
    doc["val_tpl"] = valueTemplate;
    doc["cmd_t"] = MQTT_TOPIC_HA_CMD;
    doc["cmd_tpl"] = commandTemplate;
    doc["min"] = minValue;
    doc["max"] = maxValue;
    doc["step"] = stepValue;
    doc["mode"] = "box";
    doc["avty_t"] = MQTT_TOPIC_AVAIL;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    if (unit && unit[0] != '\0')
        doc["unit_of_meas"] = unit;
    if (icon && icon[0] != '\0')
        doc["ic"] = icon;
    if (entityCategory && entityCategory[0] != '\0')
        doc["ent_cat"] = entityCategory;

    JsonObject dev = doc.createNestedObject("dev");
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(deviceName);
    dev["name"] = haDeviceDisplayName;
    dev["mdl"] = "HydroDozerPump";
    dev["mf"] = "HydroDozer";

    char payload[768];
    size_t neededLen = measureJson(doc);
    if (neededLen == 0 || neededLen >= sizeof(payload))
    {
        appendDebugLog("HA DISC number %s overflow len=%u cap=%u", objectId, (unsigned)neededLen, (unsigned)sizeof(payload));
        return;
    }
    size_t payloadLen = serializeJson(doc, payload, sizeof(payload));
    bool ok = mqttClient.publish(topic, (const uint8_t*)payload, (unsigned int)payloadLen, true);
    appendDebugLog("HA DISC number %s len=%u -> %s", objectId, (unsigned)payloadLen, ok ? "ok" : "failed");
}

void publishHASelectDiscovery(
    const char* objectId,
    const char* name,
    const char* stateTopic,
    const char* commandTemplate,
    const char* optionA,
    const char* optionB,
    const char* optionC,
    const char* icon,
    const char* entityCategory)
{
    if (!mqttClient.connected() || !objectId || !name || !stateTopic || !commandTemplate ||
        !optionA || !optionB || !optionC)
        return;

    const char* haDeviceDisplayName = getHADeviceDisplayName();

    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/select/%s/%s/config", deviceName, objectId);

    char uniqId[80];
    snprintf(uniqId, sizeof(uniqId), "%s_%s", deviceName, objectId);

    StaticJsonDocument<768> doc;
    doc["name"] = name;
    doc["uniq_id"] = uniqId;
    doc["stat_t"] = stateTopic;
    doc["cmd_t"] = MQTT_TOPIC_HA_CMD;
    doc["cmd_tpl"] = commandTemplate;
    doc["avty_t"] = MQTT_TOPIC_AVAIL;
    doc["pl_avail"] = "online";
    doc["pl_not_avail"] = "offline";

    JsonArray options = doc.createNestedArray("options");
    options.add(optionA);
    options.add(optionB);
    options.add(optionC);

    if (icon && icon[0] != '\0')
        doc["ic"] = icon;
    if (entityCategory && entityCategory[0] != '\0')
        doc["ent_cat"] = entityCategory;

    JsonObject dev = doc.createNestedObject("dev");
    JsonArray ids = dev.createNestedArray("ids");
    ids.add(deviceName);
    dev["name"] = haDeviceDisplayName;
    dev["mdl"] = "HydroDozerPump";
    dev["mf"] = "HydroDozer";

    char payload[768];
    size_t neededLen = measureJson(doc);
    if (neededLen == 0 || neededLen >= sizeof(payload))
    {
        appendDebugLog("HA DISC select %s overflow len=%u cap=%u", objectId, (unsigned)neededLen, (unsigned)sizeof(payload));
        return;
    }
    size_t payloadLen = serializeJson(doc, payload, sizeof(payload));
    bool ok = mqttClient.publish(topic, (const uint8_t*)payload, (unsigned int)payloadLen, true);
    appendDebugLog("HA DISC select %s len=%u -> %s", objectId, (unsigned)payloadLen, ok ? "ok" : "failed");
}

bool publishHADiscovery()
{
    if (!mqttClient.connected())
        return true;

    static char bottle1CapName[64];
    static char bottle1ReservePctName[64];
    static char bottle1ReserveMlName[64];
    static char bottle2CapName[64];
    static char bottle2ReservePctName[64];
    static char bottle2ReserveMlName[64];
    snprintf(bottle1CapName, sizeof(bottle1CapName), "%s Capacity", bottleLabelById(1));
    snprintf(bottle1ReservePctName, sizeof(bottle1ReservePctName), "%s Reserve %%", bottleLabelById(1));
    snprintf(bottle1ReserveMlName, sizeof(bottle1ReserveMlName), "%s Reserve mL", bottleLabelById(1));
    snprintf(bottle2CapName, sizeof(bottle2CapName), "%s Capacity", bottleLabelById(2));
    snprintf(bottle2ReservePctName, sizeof(bottle2ReservePctName), "%s Reserve %%", bottleLabelById(2));
    snprintf(bottle2ReserveMlName, sizeof(bottle2ReserveMlName), "%s Reserve mL", bottleLabelById(2));

    static char bottleStateTopic1[64];
    static char bottleStateTopic2[64];
    static char pumpDoseMlTopic1[64];
    static char pumpDoseMlTopic2[64];
    snprintf(bottleStateTopic1, sizeof(bottleStateTopic1), MQTT_TOPIC_BOTTLE_STATE_FMT, 1);
    snprintf(bottleStateTopic2, sizeof(bottleStateTopic2), MQTT_TOPIC_BOTTLE_STATE_FMT, 2);
    snprintf(pumpDoseMlTopic1, sizeof(pumpDoseMlTopic1), MQTT_TOPIC_PUMP_DOSE_ML_FMT, 1);
    snprintf(pumpDoseMlTopic2, sizeof(pumpDoseMlTopic2), MQTT_TOPIC_PUMP_DOSE_ML_FMT, 2);

    static char pump1DoseInputName[64];
    static char pump2DoseInputName[64];
    static char pump1DoseNowName[64];
    static char pump2DoseNowName[64];
    snprintf(pump1DoseInputName, sizeof(pump1DoseInputName), "%s Dose mL", pumpLabelById(1));
    snprintf(pump2DoseInputName, sizeof(pump2DoseInputName), "%s Dose mL", pumpLabelById(2));
    snprintf(pump1DoseNowName, sizeof(pump1DoseNowName), "%s Dose Now", pumpLabelById(1));
    snprintf(pump2DoseNowName, sizeof(pump2DoseNowName), "%s Dose Now", pumpLabelById(2));
    float pump1MaxDoseMl = computePumpHardMaxDoseMl(1);
    float pump2MaxDoseMl = computePumpHardMaxDoseMl(2);
    float schedullerMaxTotalMl = computeSchedullerTotalMlMax();
    if (pump1MaxDoseMl < 0.1f) pump1MaxDoseMl = 0.1f;
    if (pump2MaxDoseMl < 0.1f) pump2MaxDoseMl = 0.1f;
    if (schedullerMaxTotalMl < 0.1f) schedullerMaxTotalMl = 0.1f;

    switch (haDiscoveryStep)
    {
        case 0:
        {
            char legacyTopic[160];
            snprintf(legacyTopic, sizeof(legacyTopic), "homeassistant/binary_sensor/%s/all_pumps_stopped/config", deviceName);
            mqttClient.publish(legacyTopic, "", true);
            break;
        }
        case 1:
            publishHAButtonDiscovery("stop_all_pumps", "Stop All Pumps", MQTT_TOPIC_CMD, "{\"action\":\"stop_all\"}", "mdi:pump-off", "");
            break;
        case 2:
            publishHANumberDiscovery("pump_1_dose_ml_ctrl", pump1DoseInputName, pumpDoseMlTopic1, "{{ value | float(5) }}", "{\"ha\":\"set_dose_ml\",\"pump\":1,\"ml\":{{ value }}}", 0.1f, pump1MaxDoseMl, 0.1f, "mL", "mdi:cup-water", "");
            break;
        case 3:
            publishHANumberDiscovery("pump_2_dose_ml_ctrl", pump2DoseInputName, pumpDoseMlTopic2, "{{ value | float(5) }}", "{\"ha\":\"set_dose_ml\",\"pump\":2,\"ml\":{{ value }}}", 0.1f, pump2MaxDoseMl, 0.1f, "mL", "mdi:cup-water", "");
            break;
        case 4:
            publishHANumberDiscovery("scheduller_total_ml_ctrl", "Scheduller Total mL", MQTT_TOPIC_AUTO_STATE, "{{ value_json.scheduller.total_ml | default(100) | float(100) }}", "{\"ha\":\"set_scheduller_total_ml\",\"ml\":{{ value }}}", 0.1f, schedullerMaxTotalMl, 0.1f, "mL", "mdi:beaker-outline", "");
            break;
        case 5:
            publishHANumberDiscovery("scheduller_part_a_pct_ctrl", "Scheduller % Part A", MQTT_TOPIC_AUTO_STATE, "{{ value_json.scheduller.part_a_pct | default(50) | float(50) }}", "{\"ha\":\"set_scheduller_part_a_pct\",\"pct\":{{ value }}}", 0.0f, 100.0f, 1.0f, "%", "mdi:percent-outline", "");
            break;
        case 6:
            publishHANumberDiscovery("scheduller_part_b_pct_ctrl", "Scheduller % Part B", MQTT_TOPIC_AUTO_STATE, "{{ value_json.scheduller.part_b_pct | default(50) | float(50) }}", "{\"ha\":\"set_scheduller_part_b_pct\",\"pct\":{{ value }}}", 0.0f, 100.0f, 1.0f, "%", "mdi:percent-outline", "");
            break;
        case 7:
            publishHANumberDiscovery("scheduller_pause_ctrl", "Scheduller Pause", MQTT_TOPIC_AUTO_STATE, "{{ value_json.scheduller.pause | default(0) | int(0) }}", "{\"ha\":\"set_scheduller_pause\",\"value\":{{ value | int }}}", 0.0f, 1.0f, 1.0f, "", "mdi:pause-circle-outline", "");
            break;
        case 8:
            publishHANumberDiscovery("low_bottle_alarm_pct_ctrl", "Low Bottle Alarm", MQTT_TOPIC_AUTO_STATE, "{{ value_json.alarms.low_bottle_pct | default(10) | float(10) | round(0) | int(10) }}", "{\"ha\":\"set_low_bottle_alarm_pct\",\"pct\":{{ value | int }}}", 0.0f, 100.0f, 1.0f, "%", "mdi:alert-outline", "");
            break;
        case 9:
            publishHANumberDiscovery("scheduller_hour_1_ctrl", "Scheduller Hour 1", MQTT_TOPIC_AUTO_STATE, "{{ value_json.scheduller.hour_1 | default(8) | int(8) }}", "{\"ha\":\"set_scheduller_hour\",\"slot\":1,\"hour\":{{ value | int }}}", 0.0f, 23.0f, 1.0f, "", "mdi:clock-outline", "");
            break;
        case 10:
            publishHANumberDiscovery("scheduller_hour_2_ctrl", "Scheduller Hour 2", MQTT_TOPIC_AUTO_STATE, "{{ value_json.scheduller.hour_2 | default(14) | int(14) }}", "{\"ha\":\"set_scheduller_hour\",\"slot\":2,\"hour\":{{ value | int }}}", 0.0f, 23.0f, 1.0f, "", "mdi:clock-outline", "");
            break;
        case 11:
            publishHANumberDiscovery("scheduller_hour_3_ctrl", "Scheduller Hour 3", MQTT_TOPIC_AUTO_STATE, "{{ value_json.scheduller.hour_3 | default(20) | int(20) }}", "{\"ha\":\"set_scheduller_hour\",\"slot\":3,\"hour\":{{ value | int }}}", 0.0f, 23.0f, 1.0f, "", "mdi:clock-outline", "");
            break;
        case 12:
            publishHANumberDiscovery("bottle_1_capacity_cfg", bottle1CapName, bottleStateTopic1, "{{ value_json.capacity_ml | default(1000, true) | float(1000) }}", "{\"ha\":\"set_capacity\",\"bottle\":1,\"ml\":{{ value }}}", 1.0f, 10000.0f, 1.0f, "mL", "mdi:cup-water", "config");
            break;
        case 13:
            publishHANumberDiscovery("bottle_2_capacity_cfg", bottle2CapName, bottleStateTopic2, "{{ value_json.capacity_ml | default(1000, true) | float(1000) }}", "{\"ha\":\"set_capacity\",\"bottle\":2,\"ml\":{{ value }}}", 1.0f, 10000.0f, 1.0f, "mL", "mdi:cup-water", "config");
            break;
        case 14:
            // Refill actions are operational controls, not configuration entities.
            publishHAButtonDiscovery("bottle_1_refill", "Refill Bottle 1", MQTT_TOPIC_CMD, "{\"bottle\":1,\"refill\":true}", "mdi:cup-water", "");
            break;
        case 15:
            publishHAButtonDiscovery("bottle_2_refill", "Refill Bottle 2", MQTT_TOPIC_CMD, "{\"bottle\":2,\"refill\":true}", "mdi:cup-water", "");
            break;
        case 16:
            publishHAButtonDiscovery("pump_1_dose_now", pump1DoseNowName, MQTT_TOPIC_HA_CMD, "{\"ha\":\"dose_now\",\"pump\":1}", "mdi:pump", "");
            break;
        case 17:
            publishHAButtonDiscovery("pump_2_dose_now", pump2DoseNowName, MQTT_TOPIC_HA_CMD, "{\"ha\":\"dose_now\",\"pump\":2}", "mdi:pump", "");
            break;
        case 18:
            publishHASensorDiscovery("sensor", "bottle_1_capacity_ml", bottle1CapName, "{{ value_json.bottles['1'].capacity_ml | default(0, true) | float(0) | round(0) | int }}", "mL", "", "mdi:cup-water", "", "", "");
            break;
        case 19:
            publishHASensorDiscovery("sensor", "bottle_1_reserve_pct", bottle1ReservePctName, "{{ value_json.bottles['1'].percent | default(0, true) | float(0) | round(0) | int }}", "%", "", "mdi:water-percent", "", "", "");
            break;
        case 20:
            publishHASensorDiscovery("sensor", "bottle_2_capacity_ml", bottle2CapName, "{{ value_json.bottles['2'].capacity_ml | default(0, true) | float(0) | round(0) | int }}", "mL", "", "mdi:cup-water", "", "", "");
            break;
        case 21:
            publishHASensorDiscovery("sensor", "bottle_1_reserve_ml", bottle1ReserveMlName, "{{ value_json.bottles['1'].remaining_ml | default(0, true) | float(0) | round(0) | int }}", "mL", "", "mdi:cup-water", "", "", "");
            break;
        case 22:
            publishHASensorDiscovery("sensor", "bottle_2_reserve_pct", bottle2ReservePctName, "{{ value_json.bottles['2'].percent | default(0, true) | float(0) | round(0) | int }}", "%", "", "mdi:water-percent", "", "", "");
            break;
        case 23:
            publishHASensorDiscovery("sensor", "bottle_2_reserve_ml", bottle2ReserveMlName, "{{ value_json.bottles['2'].remaining_ml | default(0, true) | float(0) | round(0) | int }}", "mL", "", "mdi:cup-water", "", "", "");
            break;
        case 24:
            publishHASensorDiscovery("sensor", "network_mode", "Network Mode", "{{ value_json.network.mode | default('unknown', true) }}", "", "", "mdi:lan", "diagnostic", "", "");
            break;
        case 25:
            publishHASensorDiscovery("sensor", "wifi_signal", "WiFi Signal", "{{ value_json.network.wifi_rssi | default(-127, true) | int(-127) }}", "dBm", "signal_strength", "mdi:wifi", "diagnostic", "", "");
            break;
        case 26:
            publishHASensorDiscovery("sensor", "ip_address", "IP Address", "{{ value_json.network.ip | default('0.0.0.0', true) }}", "", "", "mdi:ip", "diagnostic", "", "");
            break;
        case 27:
            publishHASensorDiscovery("sensor", "mdns_host", "mDNS Host", "{{ value_json.network.mdns | default('unknown.local', true) }}", "", "", "mdi:access-point-network", "diagnostic", "", "");
            break;
        case 28:
            publishHASensorDiscovery("sensor", "mqtt_reconnects", "MQTT Reconnects", "{{ value_json.mqtt_reconnects | default(0, true) | int(0) }}", "", "", "mdi:connection", "diagnostic", "", "");
            break;
        case 29:
        {
            char textTopic[160];
            snprintf(textTopic, sizeof(textTopic), "homeassistant/text/%s/bottle_1_name/config", deviceName);
            mqttClient.publish(textTopic, "", true);
            break;
        }
        case 30:
        {
            char textTopic[160];
            snprintf(textTopic, sizeof(textTopic), "homeassistant/text/%s/bottle_2_name/config", deviceName);
            mqttClient.publish(textTopic, "", true);
            break;
        }
        case 31:
        {
            char textTopic[160];
            snprintf(textTopic, sizeof(textTopic), "homeassistant/text/%s/pump_1_name/config", deviceName);
            mqttClient.publish(textTopic, "", true);
            break;
        }
        case 32:
        {
            char textTopic[160];
            snprintf(textTopic, sizeof(textTopic), "homeassistant/text/%s/pump_2_name/config", deviceName);
            mqttClient.publish(textTopic, "", true);
            break;
        }
        default:
            haDiscoveryStep = 0;
            return true;
    }

    ESP.wdtFeed();
    haDiscoveryStep++;
    return (haDiscoveryStep > 32);
}

void resetHADiscovery()
{
    if (!mqttClient.connected())
        return;
    appendDebugLog("HA RESET start");

    const char* sensorIds[] = {
        "bottle_1_capacity_ml",
        "bottle_1_reserve_pct",
        "bottle_1_reserve_ml",
        "bottle_1_percent",
        "bottle_2_capacity_ml",
        "bottle_2_reserve_pct",
        "bottle_2_reserve_ml",
        "bottle_2_percent",
        "network_mode",
        "wifi_signal",
        "ip_address",
        "mdns_host",
        "mqtt_reconnects"
    };

    const char* textIds[] = {
        "bottle_1_name",
        "bottle_2_name",
        "pump_1_name",
        "pump_2_name"
    };

    const char* buttonIds[] = {
        "stop_all_pumps",
        "bottle_1_refill",
        "bottle_2_refill",
        "pump_1_dose_now",
        "pump_2_dose_now"
    };

    const char* numberIds[] = {
        "pump_1_dose_ml",
        "pump_2_dose_ml",
        "pump_1_dose_ml_ctrl",
        "pump_2_dose_ml_ctrl",
        "scheduller_total_ml_ctrl",
        "scheduller_part_a_pct_ctrl",
        "scheduller_part_b_pct_ctrl",
        "low_bottle_alarm_pct_ctrl",
        "scheduller_pause_ctrl",
        "scheduller_hour_1_ctrl",
        "scheduller_hour_2_ctrl",
        "scheduller_hour_3_ctrl",
        "bottle_1_capacity_cfg",
        "bottle_2_capacity_cfg"
    };

    char topic[160];
    for (size_t i = 0; i < sizeof(sensorIds) / sizeof(sensorIds[0]); i++)
    {
        snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", deviceName, sensorIds[i]);
        bool ok = mqttClient.publish(topic, "", true);
        appendDebugLog("HA RESET clear sensor %s -> %s", sensorIds[i], ok ? "ok" : "failed");
        mqttClient.loop();
        delay(1);
    }
    for (size_t i = 0; i < sizeof(textIds) / sizeof(textIds[0]); i++)
    {
        snprintf(topic, sizeof(topic), "homeassistant/text/%s/%s/config", deviceName, textIds[i]);
        bool ok = mqttClient.publish(topic, "", true);
        appendDebugLog("HA RESET clear text %s -> %s", textIds[i], ok ? "ok" : "failed");
        mqttClient.loop();
        delay(1);
    }
    for (size_t i = 0; i < sizeof(buttonIds) / sizeof(buttonIds[0]); i++)
    {
        snprintf(topic, sizeof(topic), "homeassistant/button/%s/%s/config", deviceName, buttonIds[i]);
        bool ok = mqttClient.publish(topic, "", true);
        appendDebugLog("HA RESET clear button %s -> %s", buttonIds[i], ok ? "ok" : "failed");
        mqttClient.loop();
        delay(1);
    }
    for (size_t i = 0; i < sizeof(numberIds) / sizeof(numberIds[0]); i++)
    {
        snprintf(topic, sizeof(topic), "homeassistant/number/%s/%s/config", deviceName, numberIds[i]);
        bool ok = mqttClient.publish(topic, "", true);
        appendDebugLog("HA RESET clear number %s -> %s", numberIds[i], ok ? "ok" : "failed");
        mqttClient.loop();
        delay(1);
    }
    // Clear known legacy entity; no automatic republish here.
    snprintf(topic, sizeof(topic), "homeassistant/binary_sensor/%s/all_pumps_stopped/config", deviceName);
    bool legacyOk = mqttClient.publish(topic, "", true);
    appendDebugLog("HA RESET clear legacy all_pumps_stopped -> %s", legacyOk ? "ok" : "failed");
    haDiscoveryRepublishPending = false;
    appendDebugLog("HA RESET clear done (no republish)");
}

void processPendingHADiscoveryRepublish()
{
    // Kept for backward compatibility with existing call sites.
    // Discovery republish is now handled via requestHADiscoveryPublish().
    haDiscoveryRepublishPending = false;
}

bool computeDoseDurationMs(uint8_t pumpId, float ml, unsigned long* durationMsOut, const char** reasonOut)
{
    if (!durationMsOut || !reasonOut)
        return false;

    if (pumpId < 1 || pumpId > 2)
    {
        *reasonOut = "invalid_pump";
        return false;
    }

    if (ml <= 0.0f)
    {
        *reasonOut = "invalid_ml";
        return false;
    }

    float flow = pumpConfig[pumpId].flow_ml_per_sec;
    if (flow < CAL_MIN_FLOW_ML_PER_SEC || flow > CAL_MAX_FLOW_ML_PER_SEC)
    {
        *reasonOut = "invalid_flow";
        return false;
    }

    unsigned long duration = (unsigned long)((ml / flow) * 1000.0f + 0.5f);
    if (duration == 0 || duration > PUMP_HARD_MAX_MS)
    {
        *reasonOut = "duration_limit";
        return false;
    }

    *durationMsOut = duration;
    *reasonOut = "ok";
    return true;
}

float computePumpHardMaxDoseMl(uint8_t pumpId)
{
    if (pumpId < 1 || pumpId > 2)
        return 0.0f;

    float flow = pumpConfig[pumpId].flow_ml_per_sec;
    if (flow < CAL_MIN_FLOW_ML_PER_SEC || flow > CAL_MAX_FLOW_ML_PER_SEC)
        return 0.0f;

    return flow * ((float)PUMP_HARD_MAX_MS / 1000.0f);
}

float computeSchedullerTotalMlMax()
{
    // Dynamic cap based on the current A/B split.
    // For each active part, keep the per-slot requested volume within that pump's
    // hard single-run limit, then take the most restrictive result.
    float p1 = computePumpHardMaxDoseMl(1);
    float p2 = computePumpHardMaxDoseMl(2);
    bool hasLimit = false;
    float maxTotalMl = 0.0f;

    if (schedullerPartAPct > 0.0f)
    {
        if (p1 <= 0.0f)
            return 0.0f;

        float limitA = (p1 * 300.0f) / schedullerPartAPct;
        maxTotalMl = limitA;
        hasLimit = true;
    }

    if (schedullerPartBPct > 0.0f)
    {
        if (p2 <= 0.0f)
            return 0.0f;

        float limitB = (p2 * 300.0f) / schedullerPartBPct;
        if (!hasLimit || limitB < maxTotalMl)
            maxTotalMl = limitB;
        hasLimit = true;
    }

    return hasLimit ? maxTotalMl : 0.0f;
}

bool isPumpRunning(uint8_t pumpId)
{
    if (pumpId == 1) return pump1.running;
    if (pumpId == 2) return pump2.running;
    return false;
}

bool checkPumpRunPermission(uint8_t pumpId, unsigned long durationMs, float* requestedMlOut, const char** reasonOut)
{
    if (!reasonOut)
        return false;

    if (pumpId < 1 || pumpId > 2)
    {
        *reasonOut = "invalid_pump";
        return false;
    }

    if (durationMs == 0 || durationMs > PUMP_HARD_MAX_MS)
    {
        *reasonOut = "duration_limit";
        return false;
    }

    float flow = pumpConfig[pumpId].flow_ml_per_sec;
    if (flow < CAL_MIN_FLOW_ML_PER_SEC || flow > CAL_MAX_FLOW_ML_PER_SEC)
    {
        *reasonOut = "invalid_flow";
        return false;
    }

    float requestedMl = (durationMs / 1000.0f) * flow;
    if (requestedMlOut)
        *requestedMlOut = requestedMl;

    Bottle* b = getBottleForPump(pumpId);
    if (!b)
    {
        *reasonOut = "no_bottle";
        return false;
    }

    if (b->remaining_ml < requestedMl)
    {
        *reasonOut = "not_enough_liquid";
        return false;
    }

    *reasonOut = "ok";
    return true;
}

bool canExecuteDangerousMqttCommand()
{
    return millis() >= MQTT_DANGEROUS_CMD_GUARD_MS;
}

// MQTT mapping (command ingress)
// Subscribed topics:
// - hydrodozerpump/ha/cmd : HA-focused controls (ha field required)
// - hydrodozerpump/cmd    : legacy/general controls and direct actions
// - homeassistant/status  : HA online trigger for state/discovery sync
void mqttCallback(char* topic, byte* payload, unsigned int length)
{
    bool isHaStatusTopic = (strcmp(topic, MQTT_TOPIC_HA_STATUS) == 0);
    if (isHaStatusTopic)
    {
        char msg[32];
        if (length >= sizeof(msg))
            length = sizeof(msg) - 1;
        memcpy(msg, payload, length);
        msg[length] = '\0';

        if (strcmp(msg, "online") == 0)
        {
            appendDebugLog("HA status online -> schedule discovery/state sync");
            requestAllRetainedStatePublish();
            requestHADiscoveryPublish(250);
            requestAutoStatePublish(500);
        }
        return;
    }

    bool isCmdTopic = (strcmp(topic, MQTT_TOPIC_CMD) == 0);
    bool isHaCmdTopic = (strcmp(topic, MQTT_TOPIC_HA_CMD) == 0);
    if (!isCmdTopic && !isHaCmdTopic)
        return;

    Serial.print("[MQTT] Message received: ");

    char msg[128];
    if (length >= sizeof(msg))
        length = sizeof(msg) - 1;

    memcpy(msg, payload, length);
    msg[length] = '\0';

    Serial.println(msg);

    // -------- Try JSON first --------
    StaticJsonDocument<256> doc;
    DeserializationError err = deserializeJson(doc, msg);
    bool looksJson = (msg[0] == '{' || msg[0] == '[');

    if (err && looksJson)
    {
        Serial.println("[MQTT] Invalid JSON payload");
        publishCmdAck(false, "json", "invalid_json");
        return;
    }

    if (!err)
    {
        if (isHaCmdTopic && doc.containsKey("ha"))
        {
            // Dedicated HA control path to keep UI-focused controls separate
            // from the legacy/generic command parser.
            if (!doc["ha"].is<const char*>())
            {
                publishCmdAck(false, "ha", "invalid_type");
                return;
            }

            const char* haCmd = doc["ha"];
            int pump = doc["pump"] | 0;

            // HA Number controls:
            // {"ha":"set_dose_ml","pump":1|2,"ml":<float>}
            if (strcmp(haCmd, "set_dose_ml") == 0)
            {
                if (pump < 1 || pump > 2 || !(doc["ml"].is<float>() || doc["ml"].is<int>()))
                {
                    publishCmdAck(false, "ha_set_dose_ml", "invalid_payload");
                    return;
                }

                float ml = doc["ml"].as<float>();
                float maxMl = computePumpHardMaxDoseMl((uint8_t)pump);
                if (maxMl <= 0.0f)
                {
                    publishCmdAck(false, "ha_set_dose_ml", "invalid_flow");
                    return;
                }
                if (ml <= 0.0f || ml > maxMl)
                {
                    publishCmdAck(false, "ha_set_dose_ml", "invalid_ml");
                    return;
                }

                haDoseMl[pump] = ml;
                requestPumpDoseMlPublish((uint8_t)pump);
                publishCmdAck(true, "ha_set_dose_ml", "ok");
                return;
            }

            // HA Number controls (Scheduller):
            // {"ha":"set_scheduller_total_ml","ml":<float>}
            if (strcmp(haCmd, "set_scheduller_total_ml") == 0 ||
                strcmp(haCmd, "set_scheduler_total_ml") == 0)
            {
                if (!(doc["ml"].is<float>() || doc["ml"].is<int>()))
                {
                    publishCmdAck(false, "ha_set_scheduller_total_ml", "invalid_payload");
                    return;
                }

                float ml = doc["ml"].as<float>();
                float maxTotalMl = computeSchedullerTotalMlMax();
                if (maxTotalMl <= 0.0f || ml <= 0.0f || ml > maxTotalMl)
                {
                    publishCmdAck(false, "ha_set_scheduller_total_ml", "invalid_ml");
                    return;
                }

                schedullerTotalMl = ml;
                requestConfigSave(100);
                requestSchedullerStatePublish(1);
                requestAutoStatePublish(100);
                publishCmdAck(true, "ha_set_scheduller_total_ml", "updated");
                return;
            }

            // HA Number controls (Scheduller):
            // {"ha":"set_scheduller_part_a_pct","pct":<float>}
            if (strcmp(haCmd, "set_scheduller_part_a_pct") == 0 ||
                strcmp(haCmd, "set_scheduler_part_a_pct") == 0)
            {
                if (!(doc["pct"].is<float>() || doc["pct"].is<int>()))
                {
                    publishCmdAck(false, "ha_set_scheduller_part_a_pct", "invalid_payload");
                    return;
                }

                setSchedullerPartA(doc["pct"].as<float>());
                bool totalClamped = clampSchedullerTotalMlToCurrentMax();
                requestConfigSave(100);
                if (totalClamped)
                    requestSchedullerStatePublish(1);
                requestSchedullerStatePublish(2);
                requestSchedullerStatePublish(3);
                requestHADiscoveryPublish(250);
                requestAutoStatePublish(100);
                publishCmdAck(true, "ha_set_scheduller_part_a_pct", "updated");
                return;
            }

            // HA Number controls (Scheduller):
            // {"ha":"set_scheduller_part_b_pct","pct":<float>}
            if (strcmp(haCmd, "set_scheduller_part_b_pct") == 0 ||
                strcmp(haCmd, "set_scheduler_part_b_pct") == 0)
            {
                if (!(doc["pct"].is<float>() || doc["pct"].is<int>()))
                {
                    publishCmdAck(false, "ha_set_scheduller_part_b_pct", "invalid_payload");
                    return;
                }

                setSchedullerPartB(doc["pct"].as<float>());
                bool totalClamped = clampSchedullerTotalMlToCurrentMax();
                requestConfigSave(100);
                if (totalClamped)
                    requestSchedullerStatePublish(1);
                requestSchedullerStatePublish(2);
                requestSchedullerStatePublish(3);
                requestHADiscoveryPublish(250);
                requestAutoStatePublish(100);
                publishCmdAck(true, "ha_set_scheduller_part_b_pct", "updated");
                return;
            }

            // HA Number control (Scheduller Pause):
            // {"ha":"set_scheduller_pause","value":0|1}
            if (strcmp(haCmd, "set_scheduller_pause") == 0 ||
                strcmp(haCmd, "set_scheduler_pause") == 0)
            {
                if (!(doc["value"].is<int>() || doc["value"].is<float>()))
                {
                    publishCmdAck(false, "ha_set_scheduller_pause", "invalid_payload");
                    return;
                }

                int value = (int)(doc["value"].as<float>() + 0.5f);
                if (value < 0 || value > 1)
                {
                    publishCmdAck(false, "ha_set_scheduller_pause", "invalid_value");
                    return;
                }

                schedullerPaused = (value != 0);
                requestConfigSave(100);
                requestSchedullerStatePublish(4);
                requestAutoStatePublish(100);
                publishCmdAck(true, "ha_set_scheduller_pause", "updated");
                return;
            }

            if (strcmp(haCmd, "set_low_bottle_alarm_pct") == 0 ||
                strcmp(haCmd, "set_low_bottle_alarm") == 0)
            {
                if (!(doc["pct"].is<int>() || doc["pct"].is<float>()))
                {
                    publishCmdAck(false, "ha_set_low_bottle_alarm_pct", "invalid_payload");
                    return;
                }

                lowBottleAlarmPct = clampPercent(doc["pct"].as<float>());
                requestConfigSave(100);
                requestLowBottleAlarmPublish();
                requestAutoStatePublish(100);
                publishCmdAck(true, "ha_set_low_bottle_alarm_pct", "updated");
                return;
            }

            // HA Number controls (Scheduller):
            // {"ha":"set_scheduller_hour","slot":1|2|3,"hour":0..23}
            if (strcmp(haCmd, "set_scheduller_hour") == 0 ||
                strcmp(haCmd, "set_scheduler_hour") == 0)
            {
                int slot = doc["slot"] | 0;
                if (slot < 1 || slot > 3 || !(doc["hour"].is<int>() || doc["hour"].is<float>()))
                {
                    publishCmdAck(false, "ha_set_scheduller_hour", "invalid_payload");
                    return;
                }

                int hour = (int)(doc["hour"].as<float>() + 0.5f);
                if (!isValidSchedullerHour(hour))
                {
                    publishCmdAck(false, "ha_set_scheduller_hour", "invalid_hour");
                    return;
                }

                setSchedullerHourBySlot((uint8_t)slot, hour);
                requestConfigSave(100);
                requestAutoStatePublish(100);
                publishCmdAck(true, "ha_set_scheduller_hour", "updated");
                return;
            }

            // HA Button controls:
            // {"ha":"dose_now","pump":1|2}
            if (strcmp(haCmd, "dose_now") == 0)
            {
                if (pump < 1 || pump > 2)
                {
                    publishCmdAck(false, "ha_dose_now", "invalid_pump");
                    return;
                }

                float ml = haDoseMl[pump];
                unsigned long duration = 0;
                const char* reason = "invalid";
                if (!computeDoseDurationMs((uint8_t)pump, ml, &duration, &reason))
                {
                    publishDosePermission((uint8_t)pump, ml, false, reason);
                    publishCmdAck(false, "ha_dose_now", reason);
                    return;
                }

                float requestedMl = ml;
                if (!checkPumpRunPermission((uint8_t)pump, duration, &requestedMl, &reason))
                {
                    publishDosePermission((uint8_t)pump, requestedMl, false, reason);
                    publishCmdAck(false, "ha_dose_now", reason);
                    return;
                }

                pumpStart((uint8_t)pump, duration);
                if (isPumpRunning((uint8_t)pump))
                {
                    publishDosePermission((uint8_t)pump, requestedMl, true, "ok");
                    publishCmdAck(true, "ha_dose_now", "started");
                }
                else
                {
                    publishDosePermission((uint8_t)pump, requestedMl, false, "start_failed");
                    publishCmdAck(false, "ha_dose_now", "start_failed");
                }
                return;
            }

            // HA Text controls:
            // {"ha":"set_bottle_name","bottle":1|2,"name":"..."}
            if (strcmp(haCmd, "set_bottle_name") == 0)
            {
                publishCmdAck(false, "ha_set_bottle_name", "customization_locked");
                return;
            }

            // HA Number controls:
            // {"ha":"set_capacity","bottle":1|2,"ml":<float>}
            if (strcmp(haCmd, "set_capacity") == 0)
            {
                int bottle = doc["bottle"] | 0;
                if (bottle < 1 || bottle > 2 || !(doc["ml"].is<float>() || doc["ml"].is<int>()))
                {
                    publishCmdAck(false, "ha_set_capacity", "invalid_payload");
                    return;
                }

                float cap = doc["ml"].as<float>();
                if (cap <= 0.0f || cap > 10000.0f)
                {
                    publishCmdAck(false, "ha_set_capacity", "invalid_capacity");
                    return;
                }

                bottles[bottle].capacity_ml = cap;
                if (bottles[bottle].remaining_ml > bottles[bottle].capacity_ml)
                    bottles[bottle].remaining_ml = bottles[bottle].capacity_ml;

                requestConfigSave(100);
                requestBottleStatePublish((uint8_t)bottle);
                requestAutoStatePublish(100);
                publishCmdAck(true, "ha_set_capacity", "updated");
                return;
            }

            publishCmdAck(false, "ha", "unknown");
            return;
        }

        if (isHaCmdTopic)
        {
            publishCmdAck(false, "ha", "missing_or_invalid_ha_command");
            return;
        }

        bool isBottleCfg = doc.containsKey("bottle") &&
            (doc.containsKey("set_name") || doc.containsKey("set_capacity") || doc.containsKey("refill"));
        bool isPumpCfg = doc.containsKey("pump") &&
            (doc.containsKey("set_name") || doc.containsKey("set_bottle_id") || doc.containsKey("set_flow_60s"));

        // -------- Bottle configuration --------
        // Legacy/global command examples on hydrodozerpump/cmd:
        // {"bottle":1,"set_name":"..."} {"bottle":1,"set_capacity":1500} {"bottle":1,"refill":true}
        if (isBottleCfg)
        {
            if (!doc["bottle"].is<int>())
            {
                Serial.println("[MQTT] Invalid bottle id type");
                publishCmdAck(false, "bottle_cfg", "invalid_bottle_type");
                return;
            }

            int id = doc["bottle"];
            if (id < 1 || id > 2)
            {
                Serial.println("[MQTT] Invalid bottle id range");
                publishCmdAck(false, "bottle_cfg", "invalid_bottle_range");
                return;
            }

            bool changed = false;
            bool nameChangeRequested = false;

            if (doc.containsKey("set_name"))
            {
                nameChangeRequested = true;
            }

            if (doc.containsKey("set_capacity"))
            {
                if (doc["set_capacity"].is<float>() || doc["set_capacity"].is<int>())
                {
                    float cap = doc["set_capacity"].as<float>();
                    if (cap > 0.0f)
                    {
                        bottles[id].capacity_ml = cap;
                        if (bottles[id].remaining_ml > bottles[id].capacity_ml)
                            bottles[id].remaining_ml = bottles[id].capacity_ml;
                        changed = true;
                    }
                    else
                    {
                        Serial.println("[MQTT] Invalid set_capacity value");
                    }
                }
                else
                {
                    Serial.println("[MQTT] Invalid set_capacity type");
                }
            }

            // Refill only when explicit boolean true.
            if (doc.containsKey("refill"))
            {
                if (doc["refill"].is<bool>() && doc["refill"].as<bool>())
                {
                    bottles[id].remaining_ml = bottles[id].capacity_ml;
                    changed = true;
                }
                else if (!doc["refill"].is<bool>())
                {
                    Serial.println("[MQTT] Invalid refill type");
                }
            }

            if (changed)
            {
                requestConfigSave(100);
                requestBottleStatePublish((uint8_t)id);
                requestAutoStatePublish(100);
                publishCmdAck(true, "bottle_cfg", "updated");
            }
            else if (nameChangeRequested)
            {
                publishCmdAck(false, "bottle_cfg", "name_locked");
            }
            else
            {
                publishCmdAck(true, "bottle_cfg", "no_change");
            }
            return;
        }

        // -------- Pump configuration --------
        // Legacy/global command examples on hydrodozerpump/cmd:
        // {"pump":1,"set_name":"..."} {"pump":1,"set_bottle_id":2} {"pump":1,"set_flow_60s":56}
        if (isPumpCfg)
        {
            if (!doc["pump"].is<int>())
            {
                Serial.println("[MQTT] Invalid pump id type");
                publishCmdAck(false, "pump_cfg", "invalid_pump_type");
                return;
            }

            int id = doc["pump"];
            if (id < 1 || id > 2)
            {
                Serial.println("[MQTT] Invalid pump id range");
                publishCmdAck(false, "pump_cfg", "invalid_pump_range");
                return;
            }

            bool changed = false;
            bool personalizationChangeRequested = false;

            if (doc.containsKey("set_name"))
            {
                personalizationChangeRequested = true;
            }

            if (doc.containsKey("set_bottle_id"))
            {
                personalizationChangeRequested = true;
            }

            if (doc.containsKey("set_flow_60s"))
            {
                if (doc["set_flow_60s"].is<float>() || doc["set_flow_60s"].is<int>())
                {
                    float flow60 = doc["set_flow_60s"].as<float>();
                    if (flow60 > 0.0f)
                    {
                        pumpConfig[id].flow_ml_per_sec = flow60 / 60.0f;
                        changed = true;
                    }
                    else
                    {
                        Serial.println("[MQTT] Invalid set_flow_60s value");
                    }
                }
                else
                {
                    Serial.println("[MQTT] Invalid set_flow_60s type");
                }
            }

            if (changed)
            {
                bool totalClamped = clampSchedullerTotalMlToCurrentMax();
                requestConfigSave(100);
                if (totalClamped)
                    requestSchedullerStatePublish(1);
                requestBottleStatePublish(1);
                requestBottleStatePublish(2);
                requestHADiscoveryPublish(250);
                requestAutoStatePublish(100);
                publishCmdAck(true, "pump_cfg", "updated");
            }
            else if (personalizationChangeRequested)
            {
                publishCmdAck(false, "pump_cfg", "customization_locked");
            }
            else
            {
                publishCmdAck(true, "pump_cfg", "no_change");
            }
            return;
        }
    }

    if (!err && doc.containsKey("cmd"))
    {
        if (!doc["cmd"].is<const char*>())
        {
            Serial.println("[MQTT] Invalid cmd type");
            publishCmdAck(false, "cmd", "invalid_cmd_type");
            return;
        }

        const char* cmd = doc["cmd"];

        if (strcmp(cmd, "calibrate") == 0)
        {
            if (!doc["pump"].is<int>())
            {
                Serial.println("[MQTT] Invalid calibrate pump");
                publishCmdAck(false, "calibrate", "invalid_pump_type");
                return;
            }

            int pump = doc["pump"];
            if (pump < 1 || pump > 2)
            {
                Serial.println("[MQTT] Invalid calibrate pump range");
                publishCmdAck(false, "calibrate", "invalid_pump_range");
                return;
            }

            // Step 2: submit measured ml after the fixed 60s run.
            if (doc.containsKey("measured_ml"))
            {
                if (!(doc["measured_ml"].is<float>() || doc["measured_ml"].is<int>()))
                {
                    Serial.println("[MQTT] Invalid calibrate measured_ml type");
                    publishCmdAck(false, "calibrate", "invalid_measured_ml_type");
                    return;
                }
                if (!calibrationAwaitResult[pump])
                {
                    Serial.println("[MQTT] No pending calibration run for this pump");
                    publishCmdAck(false, "calibrate", "no_pending_run");
                    return;
                }
                if (isPumpRunning((uint8_t)pump))
                {
                    Serial.println("[MQTT] Calibration run still active");
                    publishCmdAck(false, "calibrate", "run_still_active");
                    return;
                }

                float measuredMl = doc["measured_ml"].as<float>();
                if (measuredMl <= 0.0f)
                {
                    Serial.println("[MQTT] Invalid calibrate measured_ml");
                    publishCmdAck(false, "calibrate", "invalid_measured_ml");
                    return;
                }

                float flow = measuredMl / (CAL_RUN_DURATION_MS / 1000.0f);
                if (flow < CAL_MIN_FLOW_ML_PER_SEC || flow > CAL_MAX_FLOW_ML_PER_SEC)
                {
                    Serial.println("[MQTT] Calibrate flow out of range");
                    publishCmdAck(false, "calibrate", "flow_out_of_range");
                    return;
                }

                pumpConfig[pump].flow_ml_per_sec = flow;
                calibrationAwaitResult[pump] = false;
                requestConfigSave(100);

                Serial.print("[CAL] Pump ");
                Serial.print(pump);
                Serial.print(" calibrated: ");
                Serial.print(flow * 60.0f, 3);
                Serial.println(" ml/min");
                publishCmdAck(true, "calibrate", "applied");
                return;
            }

            // Step 1: start fixed 60s calibration run.
            if (isPumpRunning((uint8_t)pump))
            {
                Serial.println("[MQTT] Pump already running; calibration start rejected");
                publishCmdAck(false, "calibrate", "pump_running");
                return;
            }

            pumpStart((uint8_t)pump, CAL_RUN_DURATION_MS);
            if (isPumpRunning((uint8_t)pump))
            {
                calibrationAwaitResult[pump] = true;
                Serial.print("[CAL] Pump ");
                Serial.print(pump);
                Serial.println(" calibration run started (60s). Send measured_ml when complete.");
                publishCmdAck(true, "calibrate", "run_started");
            }
            else
            {
                Serial.println("[CAL] Calibration run did not start (check bottle/flow/safety)");
                publishCmdAck(false, "calibrate", "run_not_started");
            }
            return;
        }

        if (strcmp(cmd, "dose") == 0)
        {
            if (!doc["pump"].is<int>() || !(doc["ml"].is<float>() || doc["ml"].is<int>()))
            {
                Serial.println("[MQTT] Invalid dose payload");
                publishCmdAck(false, "dose", "invalid_payload");
                return;
            }

            int pump = doc["pump"];
            if (pump < 1 || pump > 2)
            {
                Serial.println("[MQTT] Invalid dose pump range");
                publishCmdAck(false, "dose", "invalid_pump_range");
                return;
            }

            float ml = doc["ml"] | 0.0f;
            haDoseMl[pump] = ml;
            publishPumpDoseMlState((uint8_t)pump);
            unsigned long duration = 0;
            const char* reason = "invalid";
            if (!computeDoseDurationMs((uint8_t)pump, ml, &duration, &reason))
            {
                publishDosePermission((uint8_t)pump, ml, false, reason);
                publishCmdAck(false, "dose", reason);
                return;
            }

            float requestedMl = ml;
            if (!checkPumpRunPermission((uint8_t)pump, duration, &requestedMl, &reason))
            {
                publishDosePermission((uint8_t)pump, requestedMl, false, reason);
                publishCmdAck(false, "dose", reason);
                return;
            }

            pumpStart((uint8_t)pump, duration);
            if (isPumpRunning((uint8_t)pump))
            {
                publishDosePermission((uint8_t)pump, requestedMl, true, "ok");
                publishCmdAck(true, "dose", "started");
            }
            else
            {
                publishDosePermission((uint8_t)pump, requestedMl, false, "start_failed");
                publishCmdAck(false, "dose", "start_failed");
            }
            return;
        }

        publishCmdAck(false, "cmd", "unknown_cmd");
        return;
    }

    if (!err && doc.containsKey("action"))
    {
        if (!doc["action"].is<const char*>())
        {
            Serial.println("[MQTT] Invalid action type");
            publishCmdAck(false, "action", "invalid_action_type");
            return;
        }

        const char* action = doc["action"];
        int pump = doc["pump"] | 0;
        unsigned long duration = doc["duration"] | PUMP_HARD_MAX_MS;

        if (strcmp(action, "start") == 0)
        {
            float requestedMl = 0.0f;
            const char* reason = "invalid";
            if (!checkPumpRunPermission((uint8_t)pump, duration, &requestedMl, &reason))
            {
                publishDosePermission((uint8_t)pump, requestedMl, false, reason);
                publishCmdAck(false, "action_start", reason);
                return;
            }
            pumpStart(pump, duration);
            if (isPumpRunning((uint8_t)pump))
            {
                publishDosePermission((uint8_t)pump, requestedMl, true, "ok");
                publishCmdAck(true, "action_start", "started");
            }
            else
            {
                publishDosePermission((uint8_t)pump, requestedMl, false, "start_failed");
                publishCmdAck(false, "action_start", "start_failed");
            }
            return;
        }

        if (strcmp(action, "stop") == 0)
        {
            if (pump < 1 || pump > 2)
            {
                publishCmdAck(false, "action_stop", "invalid_pump");
                return;
            }
            pumpStop(pump);
            publishCmdAck(true, "action_stop", "stopped");
            return;
        }

        if (strcmp(action, "stop_all") == 0)
        {
            pumpsStopAll();
            publishCmdAck(true, "action_stop_all", "ok");
            return;
        }

        if (strcmp(action, "reboot") == 0)
        {
            if (!canExecuteDangerousMqttCommand())
            {
                Serial.println("[MQTT] Reboot command blocked by boot guard");
                publishCmdAck(false, "action_reboot", "boot_guard");
                return;
            }
            publishCmdAck(true, "action_reboot", "ok");
            ESP.restart();
            return;
        }

        if (strcmp(action, "factory_reset") == 0)
        {
            if (!canExecuteDangerousMqttCommand())
            {
                Serial.println("[MQTT] Factory reset command blocked by boot guard");
                publishCmdAck(false, "action_factory_reset", "boot_guard");
                return;
            }
            publishCmdAck(true, "action_factory_reset", "ok");
            performFactoryReset(true);
            return;
        }

        publishCmdAck(false, "action", "unknown_action");
        return;
    }




    // -------- Legacy string commands still supported --------

    if (strcmp(msg, "reboot") == 0)
    {
        if (!canExecuteDangerousMqttCommand())
        {
            Serial.println("[MQTT] Legacy reboot blocked by boot guard");
            publishCmdAck(false, "legacy_reboot", "boot_guard");
            return;
        }
        publishCmdAck(true, "legacy_reboot", "ok");
        ESP.restart();
    }
    else if (strcmp(msg, "factory_reset") == 0)
    {
        if (!canExecuteDangerousMqttCommand())
        {
            Serial.println("[MQTT] Legacy factory reset blocked by boot guard");
            publishCmdAck(false, "legacy_factory_reset", "boot_guard");
            return;
        }
        publishCmdAck(true, "legacy_factory_reset", "ok");
        performFactoryReset(true);
    }
    else if (strcmp(msg, "pump1_on") == 0) {
        float requestedMl = 0.0f;
        const char* reason = "invalid";
        if (checkPumpRunPermission(1, PUMP_HARD_MAX_MS, &requestedMl, &reason))
        {
            pumpStart(1, PUMP_HARD_MAX_MS);
            if (isPumpRunning(1))
            {
                publishDosePermission(1, requestedMl, true, "ok");
                publishCmdAck(true, "legacy_pump1_on", "started");
            }
            else
            {
                publishDosePermission(1, requestedMl, false, "start_failed");
                publishCmdAck(false, "legacy_pump1_on", "start_failed");
            }
        }
        else
        {
            publishDosePermission(1, requestedMl, false, reason);
            publishCmdAck(false, "legacy_pump1_on", reason);
        }
    }
    else if (strcmp(msg, "pump1_off") == 0) {
        pumpStop(1);
        publishCmdAck(true, "legacy_pump1_off", "stopped");
    }
    else if (strcmp(msg, "pump2_on") == 0) {
        float requestedMl = 0.0f;
        const char* reason = "invalid";
        if (checkPumpRunPermission(2, PUMP_HARD_MAX_MS, &requestedMl, &reason))
        {
            pumpStart(2, PUMP_HARD_MAX_MS);
            if (isPumpRunning(2))
            {
                publishDosePermission(2, requestedMl, true, "ok");
                publishCmdAck(true, "legacy_pump2_on", "started");
            }
            else
            {
                publishDosePermission(2, requestedMl, false, "start_failed");
                publishCmdAck(false, "legacy_pump2_on", "start_failed");
            }
        }
        else
        {
            publishDosePermission(2, requestedMl, false, reason);
            publishCmdAck(false, "legacy_pump2_on", reason);
        }
    }
    else if (strcmp(msg, "pump2_off") == 0) {
        pumpStop(2);
        publishCmdAck(true, "legacy_pump2_off", "stopped");
    }
    else if (strcmp(msg, "stop_all") == 0) {
        pumpsStopAll();
        publishCmdAck(true, "legacy_stop_all", "ok");
    }
    else
    {
        publishCmdAck(false, "legacy", "unknown_command");
    }
}
