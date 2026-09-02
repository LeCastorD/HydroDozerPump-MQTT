// ----------- Web server content
static File backupRestoreUploadFile;
static bool backupRestoreUploadFailed = false;
static char backupRestoreUploadTmpPath[32] = {0};
static char backupRestoreUploadError[96] = {0};

static void backupRestoreResetUploadState()
{
    if (backupRestoreUploadFile)
        backupRestoreUploadFile.close();
    backupRestoreUploadFailed = false;
    backupRestoreUploadTmpPath[0] = '\0';
    backupRestoreUploadError[0] = '\0';
}

static void backupRestoreSetUploadError(const char* msg)
{
    backupRestoreUploadFailed = true;
    if (msg && msg[0] != '\0')
    {
        strncpy(backupRestoreUploadError, msg, sizeof(backupRestoreUploadError) - 1);
        backupRestoreUploadError[sizeof(backupRestoreUploadError) - 1] = '\0';
    }
    else
    {
        backupRestoreUploadError[0] = '\0';
    }
}

static void backupRestoreAbortUpload(const char* msg)
{
    if (backupRestoreUploadFile)
        backupRestoreUploadFile.close();
    if (backupRestoreUploadTmpPath[0] != '\0' && LittleFS.exists(backupRestoreUploadTmpPath))
        LittleFS.remove(backupRestoreUploadTmpPath);
    backupRestoreSetUploadError(msg);
}

static void backupRestoreStartUpload(const char* tmpPath)
{
    backupRestoreResetUploadState();
    if (!tmpPath || tmpPath[0] == '\0')
    {
        backupRestoreSetUploadError("Invalid temporary upload path.");
        return;
    }
    strncpy(backupRestoreUploadTmpPath, tmpPath, sizeof(backupRestoreUploadTmpPath) - 1);
    backupRestoreUploadTmpPath[sizeof(backupRestoreUploadTmpPath) - 1] = '\0';
    if (LittleFS.exists(backupRestoreUploadTmpPath))
        LittleFS.remove(backupRestoreUploadTmpPath);
    backupRestoreUploadFile = LittleFS.open(backupRestoreUploadTmpPath, "w");
    if (!backupRestoreUploadFile)
        backupRestoreSetUploadError("Unable to create temporary upload file.");
}

static void backupRestoreWriteUploadChunk(const uint8_t* data, size_t len)
{
    if (backupRestoreUploadFailed || !backupRestoreUploadFile || !data || len == 0)
        return;

    size_t written = backupRestoreUploadFile.write(data, len);
    if (written != len)
        backupRestoreAbortUpload("File write failed.");
}

static bool backupRestoreFinishUpload(size_t totalSize)
{
    if (backupRestoreUploadFile)
        backupRestoreUploadFile.close();
    if (backupRestoreUploadFailed)
        return false;
    if (totalSize == 0)
    {
        backupRestoreAbortUpload("Uploaded file is empty.");
        return false;
    }
    if (backupRestoreUploadTmpPath[0] == '\0' || !LittleFS.exists(backupRestoreUploadTmpPath))
    {
        backupRestoreSetUploadError("Temporary upload file missing.");
        return false;
    }
    return true;
}

static bool backupRestoreValidateConfigJson(const char* path)
{
    if (!path || path[0] == '\0' || !LittleFS.exists(path))
    {
        backupRestoreSetUploadError("Configuration upload file missing.");
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f)
    {
        backupRestoreSetUploadError("Unable to open uploaded config file.");
        return false;
    }

    DynamicJsonDocument doc(3072);
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc.is<JsonObject>())
    {
        backupRestoreSetUploadError("Uploaded config is not valid JSON.");
        return false;
    }

    return true;
}

static bool backupRestoreValidatePumpLogCsv(const char* path)
{
    if (!path || path[0] == '\0' || !LittleFS.exists(path))
    {
        backupRestoreSetUploadError("Pump log upload file missing.");
        return false;
    }

    File f = LittleFS.open(path, "r");
    if (!f)
    {
        backupRestoreSetUploadError("Unable to open uploaded pump log file.");
        return false;
    }

    if (f.size() > PUMP_RUN_LOG_MAX_BYTES)
    {
        f.close();
        backupRestoreSetUploadError("Uploaded pump log exceeds configured max size.");
        return false;
    }

    char firstLine[64];
    size_t lineLen = f.readBytesUntil('\n', firstLine, sizeof(firstLine) - 1);
    f.close();
    firstLine[lineLen] = '\0';
    if (lineLen > 0 && firstLine[lineLen - 1] == '\r')
        firstLine[lineLen - 1] = '\0';
    if (strcmp(firstLine, "Date, Pump ID, Volume in ML, Note") != 0)
    {
        backupRestoreSetUploadError("CSV header mismatch.");
        return false;
    }

    return true;
}

static bool backupRestorePromoteUploadedFile(const char* tmpPath, const char* finalPath)
{
    if (!tmpPath || !finalPath || tmpPath[0] == '\0' || finalPath[0] == '\0')
    {
        backupRestoreSetUploadError("Invalid restore target path.");
        return false;
    }
    if (!LittleFS.exists(tmpPath))
    {
        backupRestoreSetUploadError("Temporary upload file missing.");
        return false;
    }
    if (LittleFS.exists(finalPath))
        LittleFS.remove(finalPath);
    if (!LittleFS.rename(tmpPath, finalPath))
    {
        backupRestoreSetUploadError("Failed to apply restored file.");
        return false;
    }
    return true;
}

void setupWebServer()

    {
    const char* headerKeys[] = { "Cookie" };
    server.collectHeaders(headerKeys, sizeof(headerKeys) / sizeof(headerKeys[0]));

#define REQUIRE_WEB_UI_AUTH() do { if (!ensureWebUiAuth()) return; } while (0)

    server.on("/login", HTTP_GET, []()
    {
        if (!webUiLoginRequired)
        {
            server.sendHeader("Location", "/", true);
            server.send(303, "text/plain", "");
            return;
        }

        if (hasValidWebUiSession())
        {
            String next = normalizeWebUiPath(server.arg("next"));
            server.sendHeader("Location", next, true);
            server.send(303, "text/plain", "");
            return;
        }

        String next = normalizeWebUiPath(server.arg("next"));
        bool invalidCreds = (server.hasArg("err") && server.arg("err") == "1");

        String html;
        html.reserve(2800);
        appendWebUiPageStart(html, "HydroDozerPump - Login", "Sign in");
        html += "<p>Use your OTA credentials to access the Web UI.</p>";
        if (invalidCreds)
            html += "<p><b>Invalid username or password.</b></p>";
        html += "<fieldset class='r'><form method='POST' action='/login'>";
        html += "<input type='hidden' name='next' value='";
        html += next;
        html += "'>";
        html += "Username:<br><input name='username' autocomplete='username' autocapitalize='none' autocorrect='off' spellcheck='false'><br><br>";
        html += "Password:<br><input name='password' type='password' autocomplete='current-password'><br><br>";
        html += "<button type='submit'>Sign in</button></form></fieldset>";
        appendWebUiPageEnd(html);
        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "0");
        server.send(200, "text/html", html);
    });

    server.on("/login", HTTP_POST, []()
    {
        String next = normalizeWebUiPath(server.arg("next"));
        if (!webUiLoginRequired)
        {
            server.sendHeader("Location", next, true);
            server.send(303, "text/plain", "");
            return;
        }

        String username = server.arg("username");
        String password = server.arg("password");
        if (username == String(otaUser) && password == String(otaPass))
        {
            String token = generateWebUiSessionToken();
            strncpy(webUiSessionToken, token.c_str(), WEB_UI_SESSION_TOKEN_LEN - 1);
            webUiSessionToken[WEB_UI_SESSION_TOKEN_LEN - 1] = '\0';

            server.sendHeader("Set-Cookie", buildWebUiSessionCookie());
            server.sendHeader("Location", next, true);
            server.send(303, "text/plain", "");
            return;
        }

        clearWebUiSession();
        server.sendHeader("Set-Cookie", "HDPSESSID=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
        server.sendHeader("Location", "/login?err=1&next=" + next, true);
        server.send(303, "text/plain", "");
    });

    server.on("/logout", HTTP_GET, []()
    {
        clearWebUiSession();
        server.sendHeader("Set-Cookie", "HDPSESSID=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
        server.sendHeader("Location", "/login", true);
        server.send(303, "text/plain", "");
    });

    server.on("/ui/app.css", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/app.css", true))
            return;
        server.send(404, "text/plain", "Not found");
    });

    server.on("/api/ui/home", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        StaticJsonDocument<512> doc;
        doc["fw_version"] = FW_VERSION;
        doc["fw_build_stamp"] = FW_BUILD_STAMP;
        doc["min_free_heap"] = minFreeHeap;
        doc["rssi"] = WiFi.RSSI();
        doc["uptime"] = formatUptimeCompact();
        doc["web_login_required"] = webUiLoginRequired;
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/ui/calibration", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        bool p1Running = isPumpRunning(1);
        bool p2Running = isPumpRunning(2);
        bool p1CanSubmit = calibrationAwaitResult[1] && !p1Running;
        bool p2CanSubmit = calibrationAwaitResult[2] && !p2Running;

        StaticJsonDocument<512> doc;
        doc["pump1_flow_ml_s"] = pumpConfig[1].flow_ml_per_sec;
        doc["pump2_flow_ml_s"] = pumpConfig[2].flow_ml_per_sec;
        doc["pump1_flow_ml_min"] = pumpConfig[1].flow_ml_per_sec * 60.0f;
        doc["pump2_flow_ml_min"] = pumpConfig[2].flow_ml_per_sec * 60.0f;
        doc["pump1_running"] = p1Running;
        doc["pump2_running"] = p2Running;
        doc["pump1_can_submit"] = p1CanSubmit;
        doc["pump2_can_submit"] = p2CanSubmit;
        if (server.hasArg("status"))
            doc["status"] = server.arg("status");
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/ui/settings/network", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        StaticJsonDocument<512> doc;
        doc["device_name"] = deviceName;
        doc["network_mode"] = networkUseDhcp ? "dhcp" : "manual";
        doc["ip"] = networkIp;
        doc["gateway"] = networkGateway;
        doc["netmask"] = networkNetmask;
        doc["dns"] = networkDns;
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/ui/settings/time", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        StaticJsonDocument<384> doc;
        doc["time_zone"] = timeZoneSpec;
        doc["has_valid_time"] = hasValidSystemTime();
        char nowBuf[48];
        if (hasValidSystemTime())
        {
            time_t nowTs = time(nullptr);
            struct tm localTm;
            localtime_r(&nowTs, &localTm);
            strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%d %H:%M:%S %Z", &localTm);
        }
        else
        {
            strncpy(nowBuf, "not synchronized yet", sizeof(nowBuf) - 1);
            nowBuf[sizeof(nowBuf) - 1] = '\0';
        }
        doc["current_time"] = nowBuf;
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/ui/settings/mqtt", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        StaticJsonDocument<512> doc;
        doc["broker"] = mqttBroker;
        doc["port"] = mqttPort;
        doc["client_id"] = mqttClientId;
        doc["user"] = mqttUser;
        doc["pass"] = mqttPass;
        doc["connected"] = mqttClient.connected();
        doc["reconnects"] = (unsigned long)mqttReconnectCount;
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/ui/settings/ota", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        StaticJsonDocument<384> doc;
        doc["user"] = otaUser;
        doc["pass"] = otaPass;
        doc["web_login_required"] = webUiLoginRequired;
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/ui/settings/backup_restore", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        StaticJsonDocument<384> doc;

        size_t configSize = 0;
        bool configExists = LittleFS.exists("/config.json");
        if (configExists)
        {
            File f = LittleFS.open("/config.json", "r");
            if (f)
            {
                configSize = f.size();
                f.close();
            }
        }

        size_t pumpLogSize = 0;
        bool pumpLogExists = LittleFS.exists(PUMP_RUN_LOG_CSV_PATH);
        if (pumpLogExists)
        {
            File f = LittleFS.open(PUMP_RUN_LOG_CSV_PATH, "r");
            if (f)
            {
                pumpLogSize = f.size();
                f.close();
            }
        }

        doc["config_exists"] = configExists;
        doc["config_size"] = (unsigned long)configSize;
        doc["pump_log_exists"] = pumpLogExists;
        doc["pump_log_size"] = (unsigned long)pumpLogSize;
        doc["pump_log_max_bytes"] = (unsigned long)PUMP_RUN_LOG_MAX_BYTES;

        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/api/ui/mqtt_defaults", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        StaticJsonDocument<512> doc;
        doc["sched_total_ml"] = schedullerTotalMl;
        doc["sched_part_a_pct"] = schedullerPartAPct;
        doc["sched_part_b_pct"] = schedullerPartBPct;
        doc["sched_pause"] = schedullerPaused ? 1 : 0;
        doc["low_bottle_alarm_pct"] = (int)(lowBottleAlarmPct + 0.5f);
        doc["sched_hour_1"] = schedullerHour1;
        doc["sched_hour_2"] = schedullerHour2;
        doc["sched_hour_3"] = schedullerHour3;
        doc["bottle_1_capacity"] = bottles[1].capacity_ml;
        doc["bottle_2_capacity"] = bottles[2].capacity_ml;
        String out;
        serializeJson(doc, out);
        server.send(200, "application/json", out);
    });

    server.on("/", HTTP_GET, []() {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/index.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/index.html");
    });

    {
    server.on("/calibration", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/calibration.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/calibration.html");
        return;
        bool p1Running = isPumpRunning(1);
        bool p2Running = isPumpRunning(2);
        bool anyRunning = (p1Running || p2Running);
        bool p1CanSubmit = calibrationAwaitResult[1] && !p1Running;
        bool p2CanSubmit = calibrationAwaitResult[2] && !p2Running;

        String html;
        html.reserve(6200);
        appendWebUiPageStart(html, "HydroDozerPump - Calibration", "Pump Calibration");
        html += "<p><b>Pump 1 :</b> ";
        html += String(pumpConfig[1].flow_ml_per_sec, 3);
        html += " mL/s (";
        html += String(pumpConfig[1].flow_ml_per_sec * 60.0f, 1);
        html += " mL/min)<br>";
        html += "<b>Pump 2 :</b> ";
        html += String(pumpConfig[2].flow_ml_per_sec, 3);
        html += " mL/s (";
        html += String(pumpConfig[2].flow_ml_per_sec * 60.0f, 1);
        html += " mL/min)</p>";
        if (anyRunning)
        {
            // Keep status auto-updating even if browser JS is unavailable.
            server.sendHeader("Refresh", "2");
            html += "<script>setTimeout(function(){window.location.reload();},2000);</script>";
        }
        html += "<p><a href='/'><button type='button'>Back</button></a></p>";

        if (server.hasArg("status"))
        {
            String st = server.arg("status");
            html += "<p><b>Status:</b> ";
            html += st;
            html += "</p>";
        }

        html += "<fieldset class='r'>";
        html += "<form method='POST' action='/calibration/start' style='margin:0 0 10px 0;'>";
        html += "<input type='hidden' name='pump' value='1'>";
        html += "<button type='submit'";
        if (p1Running || p1CanSubmit) html += " disabled";
        html += ">Calibration Pump A</button>";
        html += "</form>";
        html += "<h3>Pump A</h3>";

        html += "<form method='POST' action='/calibration/submit' style='margin:0;'>";
        html += "<input type='hidden' name='pump' value='1'>";
        html += "<input type='number' name='measured_ml' min='0.1' step='0.1' value='' placeholder='Measured mL after 60s'";
        if (p1CanSubmit) html += " style='background:#ffffff;color:#000000;'";
        else html += " style='background:#a0a0a0;color:#444444;'";
        if (!p1CanSubmit) html += " disabled";
        html += ">";
        html += "<button type='submit'";
        if (!p1CanSubmit) html += " disabled";
        html += ">Apply</button>";
        html += "</form>";

        html += "<p style='margin-top:8px;'>";
        if (p1Running)
            html += "Calibration running. Wait for auto-stop before entering measured mL.";
        else if (p1CanSubmit)
            html += "Run complete. Enter measured mL then press Apply.";
        else
            html += "Start calibration first. Input is disabled until a run is initiated.";
        html += "</p>";
        html += "</fieldset>";

        html += "<fieldset class='r'>";
        html += "<form method='POST' action='/calibration/start' style='margin:0 0 10px 0;'>";
        html += "<input type='hidden' name='pump' value='2'>";
        html += "<button type='submit'";
        if (p2Running || p2CanSubmit) html += " disabled";
        html += ">Calibration Pump B</button>";
        html += "</form>";
        html += "<h3>Pump B</h3>";

        html += "<form method='POST' action='/calibration/submit' style='margin:0;'>";
        html += "<input type='hidden' name='pump' value='2'>";
        html += "<input type='number' name='measured_ml' min='0.1' step='0.1' value='' placeholder='Measured mL after 60s'";
        if (p2CanSubmit) html += " style='background:#ffffff;color:#000000;'";
        else html += " style='background:#a0a0a0;color:#444444;'";
        if (!p2CanSubmit) html += " disabled";
        html += ">";
        html += "<button type='submit'";
        if (!p2CanSubmit) html += " disabled";
        html += ">Apply</button>";
        html += "</form>";

        html += "<p style='margin-top:8px;'>";
        if (p2Running)
            html += "Calibration running. Wait for auto-stop before entering measured mL.";
        else if (p2CanSubmit)
            html += "Run complete. Enter measured mL then press Apply.";
        else
            html += "Start calibration first. Input is disabled until a run is initiated.";
        html += "</p>";
        html += "</fieldset>";

        html += "<hr><p><b>Calibration process:</b><br>";
        html += "1) Press Calibration Pump A or Pump B (fixed 60s run).<br>";
        html += "2) Wait for pump to stop automatically.<br>";
        html += "3) Enter measured mL from your cylinder and press Apply.<br>";
        html += "4) Firmware computes flow as measured_mL / 60 seconds and saves it.</p>";
        html += "<p>Input fields stay disabled until a calibration run is initiated.</p>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
    });

    server.on("/calibration/start", HTTP_POST, []()
    {
        REQUIRE_WEB_UI_AUTH();
        int pump = server.arg("pump").toInt();
        if (pump < 1 || pump > 2)
        {
            server.sendHeader("Location", "/calibration?status=invalid_pump", true);
            server.send(303, "text/plain", "");
            return;
        }

        if (isPumpRunning((uint8_t)pump))
        {
            server.sendHeader("Location", "/calibration?status=pump_running", true);
            server.send(303, "text/plain", "");
            return;
        }

        pumpStart((uint8_t)pump, CAL_RUN_DURATION_MS);
        if (isPumpRunning((uint8_t)pump))
        {
            calibrationAwaitResult[pump] = true;
            requestAutoStatePublish(100);
            server.sendHeader("Location", "/calibration?status=run_started", true);
            server.send(303, "text/plain", "");
            return;
        }

        server.sendHeader("Location", "/calibration?status=start_failed", true);
        server.send(303, "text/plain", "");
    });

    server.on("/calibration/submit", HTTP_POST, []()
    {
        REQUIRE_WEB_UI_AUTH();
        int pump = server.arg("pump").toInt();
        if (pump < 1 || pump > 2)
        {
            server.sendHeader("Location", "/calibration?status=invalid_pump", true);
            server.send(303, "text/plain", "");
            return;
        }

        if (!calibrationAwaitResult[pump])
        {
            server.sendHeader("Location", "/calibration?status=no_pending_run", true);
            server.send(303, "text/plain", "");
            return;
        }

        if (isPumpRunning((uint8_t)pump))
        {
            server.sendHeader("Location", "/calibration?status=run_still_active", true);
            server.send(303, "text/plain", "");
            return;
        }

        String measuredS = server.arg("measured_ml");
        measuredS.trim();
        float measuredMl = measuredS.toFloat();
        if (!(measuredMl > 0.0f))
        {
            server.sendHeader("Location", "/calibration?status=invalid_measured_ml", true);
            server.send(303, "text/plain", "");
            return;
        }

        float flow = measuredMl / (CAL_RUN_DURATION_MS / 1000.0f);
        if (flow < CAL_MIN_FLOW_ML_PER_SEC || flow > CAL_MAX_FLOW_ML_PER_SEC)
        {
            server.sendHeader("Location", "/calibration?status=flow_out_of_range", true);
            server.send(303, "text/plain", "");
            return;
        }

        pumpConfig[pump].flow_ml_per_sec = flow;
        calibrationAwaitResult[pump] = false;
        bool totalClamped = clampSchedullerTotalMlToCurrentMax();
        requestConfigSave(100);
        if (totalClamped)
            requestSchedullerStatePublish(1);
        requestHADiscoveryPublish(250);
        requestAutoStatePublish(100);

        server.sendHeader("Location", "/calibration?status=calibration_applied", true);
        server.send(303, "text/plain", "");
    });
    }

    server.on("/mqtt_send", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/mqtt-send.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/mqtt-send.html");
        return;
        char schedTotalMl[16];
        char schedPartAPct[16];
        char schedPartBPct[16];
        char schedPauseStr[4];
        char lowBottleAlarmPctStr[8];
        char schedHour1Str[8];
        char schedHour2Str[8];
        char schedHour3Str[8];
        char bottle1CapMl[16];
        char bottle2CapMl[16];

        dtostrf(schedullerTotalMl, 0, 2, schedTotalMl);
        dtostrf(schedullerPartAPct, 0, 1, schedPartAPct);
        dtostrf(schedullerPartBPct, 0, 1, schedPartBPct);
        snprintf(schedPauseStr, sizeof(schedPauseStr), "%u", schedullerPaused ? 1U : 0U);
        snprintf(lowBottleAlarmPctStr, sizeof(lowBottleAlarmPctStr), "%u", (unsigned int)(lowBottleAlarmPct + 0.5f));
        snprintf(schedHour1Str, sizeof(schedHour1Str), "%d", schedullerHour1);
        snprintf(schedHour2Str, sizeof(schedHour2Str), "%d", schedullerHour2);
        snprintf(schedHour3Str, sizeof(schedHour3Str), "%d", schedullerHour3);
        dtostrf(bottles[1].capacity_ml, 0, 1, bottle1CapMl);
        dtostrf(bottles[2].capacity_ml, 0, 1, bottle2CapMl);

        sendWebUiPageStartChunked(200, "HydroDozerPump - Send MQTT command", "Send MQTT command");
        server.sendContent("<p><a href='/'><button type='button'>Back</button></a></p>");
        server.sendContent("<p>Select a safe command template, adjust values, then send to MQTT.</p>");
        server.sendContent("<form method='POST' action='/mqtt_send_do'>");
        server.sendContent("<label for='cmd_template'><b>Command template</b></label><br>");
        server.sendContent("<select id='cmd_template' name='cmd_template' onchange='applyTemplate()'>");
        server.sendContent("<option value='sched_total_ml'>Daily total mL (scheduler)</option>");
        server.sendContent("<option value='sched_part_a_pct'>Part A ratio (%)</option>");
        server.sendContent("<option value='sched_part_b_pct'>Part B ratio (%)</option>");
        server.sendContent("<option value='sched_pause'>Scheduler pause (0/1)</option>");
        server.sendContent("<option value='low_bottle_alarm_pct'>Low Bottle Alarm (%)</option>");
        server.sendContent("<option value='sched_hour_1' selected>Scheduler hour slot 1</option>");
        server.sendContent("<option value='sched_hour_2'>Scheduler hour slot 2</option>");
        server.sendContent("<option value='sched_hour_3'>Scheduler hour slot 3</option>");
        server.sendContent("<option value='bottle_1_capacity'>Bottle 1 capacity (mL)</option>");
        server.sendContent("<option value='bottle_2_capacity'>Bottle 2 capacity (mL)</option>");
        server.sendContent("<option value='bottle_1_refill'>Refill Bottle A</option>");
        server.sendContent("<option value='bottle_2_refill'>Refill Bottle B</option>");
        server.sendContent("</select><br><br>");
        server.sendContent("<p id='template_help'><b></b></p>");
        server.sendContent("<label for='payload'><b>MQTT payload (JSON)</b></label><br>");
        server.sendContent("<textarea id='payload' name='payload' style='height:130px;'></textarea><br><br>");
        server.sendContent("<button type='submit'>Send</button>");
        server.sendContent("</form>");

        server.sendContent(R"HTML(
<script>
const currentVals = {
  sched_total_ml: )HTML");
        server.sendContent(schedTotalMl);
        server.sendContent(R"HTML(,
  sched_part_a_pct: )HTML");
        server.sendContent(schedPartAPct);
        server.sendContent(R"HTML(,
  sched_part_b_pct: )HTML");
        server.sendContent(schedPartBPct);
        server.sendContent(R"HTML(,
  sched_pause: )HTML");
        server.sendContent(schedPauseStr);
        server.sendContent(R"HTML(,
  low_bottle_alarm_pct: )HTML");
        server.sendContent(lowBottleAlarmPctStr);
        server.sendContent(R"HTML(,
  sched_hour_1: )HTML");
        server.sendContent(schedHour1Str);
        server.sendContent(R"HTML(,
  sched_hour_2: )HTML");
        server.sendContent(schedHour2Str);
        server.sendContent(R"HTML(,
  sched_hour_3: )HTML");
        server.sendContent(schedHour3Str);
        server.sendContent(R"HTML(,
  bottle_1_capacity: )HTML");
        server.sendContent(bottle1CapMl);
        server.sendContent(R"HTML(,
  bottle_2_capacity: )HTML");
        server.sendContent(bottle2CapMl);
        server.sendContent(R"HTML(
};

const mqttTemplates = {
  sched_total_ml: {
    payload: '{"ha":"set_scheduller_total_ml","ml":' + currentVals.sched_total_ml + '}',
    help: 'Sets the total daily scheduler dose in mL (split across 3 slots).'
  },
  sched_part_a_pct: {
    payload: '{"ha":"set_scheduller_part_a_pct","pct":' + currentVals.sched_part_a_pct + '}',
    help: 'Sets Part A percentage (0..100). Part B is adjusted automatically.'
  },
  sched_part_b_pct: {
    payload: '{"ha":"set_scheduller_part_b_pct","pct":' + currentVals.sched_part_b_pct + '}',
    help: 'Sets Part B percentage (0..100). Part A is adjusted automatically.'
  },
  sched_pause: {
    payload: '{"ha":"set_scheduller_pause","value":' + currentVals.sched_pause + '}',
    help: 'Pauses scheduled dosing when set to 1. Set to 0 to resume normal scheduler execution.'
  },
  low_bottle_alarm_pct: {
    payload: '{"ha":"set_low_bottle_alarm_pct","pct":' + currentVals.low_bottle_alarm_pct + '}',
    help: 'Sets the low bottle alarm threshold in percent (0..100). Red LED turns on when a bottle is at or below this threshold.'
  },
  sched_hour_1: {
    payload: '{"ha":"set_scheduller_hour","slot":1,"hour":' + currentVals.sched_hour_1 + '}',
    help: 'Sets execution hour for scheduler slot 1 (0..23).'
  },
  sched_hour_2: {
    payload: '{"ha":"set_scheduller_hour","slot":2,"hour":' + currentVals.sched_hour_2 + '}',
    help: 'Sets execution hour for scheduler slot 2 (0..23).'
  },
  sched_hour_3: {
    payload: '{"ha":"set_scheduller_hour","slot":3,"hour":' + currentVals.sched_hour_3 + '}',
    help: 'Sets execution hour for scheduler slot 3 (0..23).'
  },
  bottle_1_capacity: {
    payload: '{"ha":"set_capacity","bottle":1,"ml":' + currentVals.bottle_1_capacity + '}',
    help: 'Sets Bottle 1 capacity in mL.'
  },
  bottle_2_capacity: {
    payload: '{"ha":"set_capacity","bottle":2,"ml":' + currentVals.bottle_2_capacity + '}',
    help: 'Sets Bottle 2 capacity in mL.'
  },
  bottle_1_refill: {
    payload: '{"bottle":1,"refill":true}',
    help: 'Refills Bottle A to full capacity using the current configured capacity.'
  },
  bottle_2_refill: {
    payload: '{"bottle":2,"refill":true}',
    help: 'Refills Bottle B to full capacity using the current configured capacity.'
  }
};

function applyTemplate() {
  const key = document.getElementById('cmd_template').value;
  const t = mqttTemplates[key];
  if (!t) return;
  document.getElementById('template_help').textContent = t.help;
  document.getElementById('payload').value = t.payload;
}

document.addEventListener('DOMContentLoaded', applyTemplate);
</script>
)HTML");
        sendWebUiPageEndChunked();
    });

    server.on("/mqtt_send_do", HTTP_POST, []()
    {
        REQUIRE_WEB_UI_AUTH();
        bool mqttConnected = mqttClient.connected();
        String html;
        html.reserve(3200);
        appendWebUiPageStart(html, "HydroDozerPump - Send MQTT command", "Send MQTT command");
        html += "<p><a href='/mqtt_send'><button type='button'>Back</button></a></p>";

        String payload = server.arg("payload");
        payload.trim();
        if (payload.length() == 0)
        {
            html += "<p><b>Validation failed:</b> Payload is empty.</p>";
            appendWebUiPageEnd(html);
            server.send(400, "text/html", html);
            return;
        }
        if (payload.length() >= 128)
        {
            html += "<p><b>Validation failed:</b> Payload too long (max 127 chars).</p>";
            appendWebUiPageEnd(html);
            server.send(400, "text/html", html);
            return;
        }

        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (err)
        {
            html += "<p><b>Validation failed:</b> Invalid JSON payload.</p>";
            appendWebUiPageEnd(html);
            server.send(400, "text/html", html);
            return;
        }

        const char* publishTopic = MQTT_TOPIC_HA_CMD;
        bool valid = false;
        String validationMsg;
        if (doc["ha"].is<const char*>())
        {
            const char* haCmd = doc["ha"];
            publishTopic = MQTT_TOPIC_HA_CMD;
            if (strcmp(haCmd, "set_scheduller_total_ml") == 0 || strcmp(haCmd, "set_scheduler_total_ml") == 0)
            {
                if (!(doc["ml"].is<float>() || doc["ml"].is<int>()))
                    validationMsg = "Field 'ml' is required and must be numeric.";
                else
                {
                    float ml = doc["ml"].as<float>();
                    float maxTotal = computeSchedullerTotalMlMax();
                    if (ml <= 0.0f || maxTotal <= 0.0f || ml > maxTotal)
                        validationMsg = "Field 'ml' is out of allowed range.";
                    else
                        valid = true;
                }
            }
            else if (strcmp(haCmd, "set_scheduller_part_a_pct") == 0 || strcmp(haCmd, "set_scheduler_part_a_pct") == 0 ||
                     strcmp(haCmd, "set_scheduller_part_b_pct") == 0 || strcmp(haCmd, "set_scheduler_part_b_pct") == 0)
            {
                if (!(doc["pct"].is<float>() || doc["pct"].is<int>()))
                    validationMsg = "Field 'pct' is required and must be numeric.";
                else
                {
                    float pct = doc["pct"].as<float>();
                    if (pct < 0.0f || pct > 100.0f)
                        validationMsg = "Field 'pct' must be between 0 and 100.";
                    else
                        valid = true;
                }
            }
            else if (strcmp(haCmd, "set_scheduller_pause") == 0 || strcmp(haCmd, "set_scheduler_pause") == 0)
            {
                if (!(doc["value"].is<float>() || doc["value"].is<int>()))
                    validationMsg = "Field 'value' is required and must be numeric.";
                else
                {
                    int value = (int)(doc["value"].as<float>() + 0.5f);
                    if (value < 0 || value > 1)
                        validationMsg = "Field 'value' must be 0 or 1.";
                    else
                        valid = true;
                }
            }
            else if (strcmp(haCmd, "set_low_bottle_alarm_pct") == 0 || strcmp(haCmd, "set_low_bottle_alarm") == 0)
            {
                if (!(doc["pct"].is<float>() || doc["pct"].is<int>()))
                    validationMsg = "Field 'pct' is required and must be numeric.";
                else
                {
                    float pct = doc["pct"].as<float>();
                    if (pct < 0.0f || pct > 100.0f)
                        validationMsg = "Field 'pct' must be between 0 and 100.";
                    else
                        valid = true;
                }
            }
            else if (strcmp(haCmd, "set_scheduller_hour") == 0 || strcmp(haCmd, "set_scheduler_hour") == 0)
            {
                if (!(doc["slot"].is<int>() || doc["slot"].is<float>()) ||
                    !(doc["hour"].is<int>() || doc["hour"].is<float>()))
                {
                    validationMsg = "Fields 'slot' and 'hour' are required.";
                }
                else
                {
                    int slot = (int)(doc["slot"].as<float>() + 0.5f);
                    int hour = (int)(doc["hour"].as<float>() + 0.5f);
                    if (slot < 1 || slot > 3 || !isValidSchedullerHour(hour))
                        validationMsg = "Slot/hour out of range.";
                    else
                        valid = true;
                }
            }
            else if (strcmp(haCmd, "set_capacity") == 0)
            {
                if (!(doc["bottle"].is<int>() || doc["bottle"].is<float>()) ||
                    !(doc["ml"].is<float>() || doc["ml"].is<int>()))
                {
                    validationMsg = "Fields 'bottle' and 'ml' are required.";
                }
                else
                {
                    int bottle = (int)(doc["bottle"].as<float>() + 0.5f);
                    float ml = doc["ml"].as<float>();
                    if (bottle < 1 || bottle > 2 || ml <= 0.0f || ml > 10000.0f)
                        validationMsg = "Bottle/ml out of range.";
                    else
                        valid = true;
                }
            }
            else
            {
                validationMsg = "Command not allowed from this page.";
            }
        }
        else if ((doc["bottle"].is<int>() || doc["bottle"].is<float>()) && doc["refill"].is<bool>())
        {
            publishTopic = MQTT_TOPIC_CMD;
            int bottle = (int)(doc["bottle"].as<float>() + 0.5f);
            bool refill = doc["refill"].as<bool>();
            if (bottle < 1 || bottle > 2 || !refill)
                validationMsg = "Refill command must target bottle 1 or 2 with refill=true.";
            else
                valid = true;
        }
        else
        {
            validationMsg = "Missing/invalid command fields.";
        }

        if (!valid)
        {
            html += "<p><b>Validation failed:</b> ";
            html += validationMsg;
            html += "</p>";
            appendWebUiPageEnd(html);
            server.send(400, "text/html", html);
            return;
        }

        if (mqttConnected)
        {
            bool sent = mqttClient.publish(publishTopic, payload.c_str(), false);
            if (!sent)
            {
                html += "<p><b>MQTT publish failed.</b> Command not sent.</p>";
                appendWebUiPageEnd(html);
                server.send(502, "text/html", html);
                return;
            }

            html += "<p><b>Command successful.</b></p>";
            html += "<p>Published to <code>";
            html += publishTopic;
            html += "</code>.</p>";
        }
        else
        {
            char topicBuf[64];
            size_t topicLen = strlen(publishTopic);
            if (topicLen >= sizeof(topicBuf))
            {
                html += "<p><b>Internal error:</b> topic too long.</p>";
                appendWebUiPageEnd(html);
                server.send(500, "text/html", html);
                return;
            }
            memcpy(topicBuf, publishTopic, topicLen + 1);

            byte msgBuf[128];
            size_t payloadLen = payload.length();
            if (payloadLen >= sizeof(msgBuf))
            {
                html += "<p><b>Validation failed:</b> Payload too long (max 127 chars).</p>";
                appendWebUiPageEnd(html);
                server.send(400, "text/html", html);
                return;
            }
            memcpy(msgBuf, payload.c_str(), payloadLen);
            msgBuf[payloadLen] = '\0';

            // Reuse the exact same HA/CMD parser so offline behavior matches MQTT path.
            mqttCallback(topicBuf, msgBuf, (unsigned int)payloadLen);

            html += "<p><b>Offline mode:</b> MQTT disconnected, command applied locally.</p>";
            html += "<p>No MQTT publish was sent.</p>";
            html += "<p>Target command path: <code>";
            html += publishTopic;
            html += "</code>.</p>";
        }

        html += "<p>Payload:</p><pre>";
        html += payload;
        html += "</pre>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
    });

    {
    server.on("/ha_discovery_reset", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        String html;
        html.reserve(2200);
        appendWebUiPageStart(html, "HydroDozerPump - HA Discovery Reset", "Home Assistant Discovery Reset");
        html += "<p><a href='/'><button type='button'>Back</button></a></p>";

        if (!mqttClient.connected())
        {
            html += "<p><b>MQTT is not connected.</b> Reset not sent.</p>";
            appendWebUiPageEnd(html);
            server.send(503, "text/html", html);
            return;
        }

        resetHADiscovery();
        html += "<p>Discovery reset sent (clear only, no auto republish).</p>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
    });
    }

    {
    server.on("/reboot", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/reboot.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/reboot.html");
        return;
        String html;
        html.reserve(1800);
        appendWebUiPageStart(html, "HydroDozerPump - Reboot", "Reboot Device");
        html += "<p>Restart the device now?</p>";
        html += "<p><a href='/reboot_do'><button class='bred'>Reboot</button></a></p>";
        html += "<p><a href='/'><button type='button'>Back</button></a></p>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
    });
    server.on("/reboot_do", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        String html;
        html.reserve(1400);
        appendWebUiPageStart(html, "HydroDozerPump - Rebooting", "Rebooting...");
        html += "<p>The device is restarting now.</p>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
        delay(1000);
        ESP.restart();
    });
    }

    server.on("/wifi_manager", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/wifi-manager.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/wifi-manager.html");
        return;
        String html;
        html.reserve(2200);
        appendWebUiPageStart(html, "HydroDozerPump - WiFiManager", "Open WiFiManager");
        html += "<p>This will erase saved WiFi credentials and reboot into setup portal.</p>";
        html += "<p><a href='/wifi_manager_do'><button class='bred'>Open WiFiManager</button></a></p>";
        html += "<p><a href='/'><button type='button'>Back</button></a></p>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
    });

    server.on("/wifi_manager_do", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        String html;
        html.reserve(1600);
        appendWebUiPageStart(html, "HydroDozerPump - WiFiManager", "Rebooting to WiFiManager...");
        html += "<p>WiFi credentials are being cleared, then setup mode will start.</p>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
        delay(1000);
        performFactoryReset(false);
    });

    server.on("/settings", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/settings.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/settings.html");
        return;
        sendWebUiPageStartChunked(200, "HydroDozerPump - Settings", "System Settings");
        server.sendContent("<p><a href='/'><button type='button'>Back</button></a></p>");

        if (server.hasArg("scheduller_cleared") && server.arg("scheduller_cleared") == "1")
            server.sendContent("<p><b>Schedule execution history cleared for QA.</b></p>");

        server.sendContent("<div><a href='/settings/network'><button type='button'>Networking</button></a></div>");
        server.sendContent("<div><a href='/settings/time'><button type='button'>Time Synchronization</button></a></div>");
        server.sendContent("<div><a href='/settings/mqtt'><button type='button'>MQTT Settings</button></a></div>");
        server.sendContent("<div><a href='/settings/ota'><button type='button'>OTA Settings</button></a></div>");
        server.sendContent("<div><a href='/schedule_clear'><button type='button'>Clear Schedule</button></a></div>");
        server.sendContent("<div><a href='/ha_discovery_reset'><button type='button'>Reset HA Discovery</button></a></div>");
        server.sendContent("<div><a href='/factory'><button type='button' class='bred'>Factory Reset</button></a></div>");
        sendWebUiPageEndChunked();
    });

    server.on("/settings/network", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/settings-network.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/settings-network.html");
        return;
        String html;
        html.reserve(4200);
        appendWebUiPageStart(html, "HydroDozerPump - Networking", "Networking");
        html += "<p><a href='/settings'><button type='button'>Back</button></a></p>";

        if (server.hasArg("saved"))
        {
            if (server.arg("saved") == "1")
                html += "<p><b>Settings saved.</b></p>";
            else
                html += "<p><b>No changes detected.</b></p>";
        }
        if (server.hasArg("warning") && server.arg("warning") == "1")
            html += "<p><b>Some values were not applied. Review the fields below.</b></p>";
        if (server.hasArg("network_changed") && server.arg("network_changed") == "1")
            html += "<p><b>Network mode/IP changes apply fully after reboot.</b></p>";
        html += "<fieldset class='r'>";
        html += "<form method='POST' action='/settings/network_save'>";
        html += "Mode:<br><select name='network_mode'>";
        html += "<option value='dhcp'";
        if (networkUseDhcp) html += " selected";
        html += ">Automatic (DHCP)</option>";
        html += "<option value='manual'";
        if (!networkUseDhcp) html += " selected";
        html += ">Manual</option>";
        html += "</select><br><br>";
        html += "Hostname:<br><input name='device_name' value='";
        html += deviceName;
        html += "' maxlength='31'><br><br>";
        html += "Static IP:<br><input name='network_ip' value='";
        html += networkIp;
        html += "' maxlength='15'><br><br>";
        html += "Gateway:<br><input name='network_gateway' value='";
        html += networkGateway;
        html += "' maxlength='15'><br><br>";
        html += "Netmask:<br><input name='network_netmask' value='";
        html += networkNetmask;
        html += "' maxlength='15'><br><br>";
        html += "DNS:<br><input name='network_dns' value='";
        html += networkDns;
        html += "' maxlength='15'><br><br>";
        html += "<button type='submit'>Save Networking</button>";
        html += "</form></fieldset><br>";
        html += "<div><a href='/wifi_manager'><button type='button' class='bred'>Open WiFiManager</button></a></div>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
    });

    server.on("/settings/time", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/settings-time.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/settings-time.html");
        return;
        String html;
        html.reserve(3200);
        appendWebUiPageStart(html, "HydroDozerPump - Time Synchronization", "Time Synchronization");
        html += "<p><a href='/settings'><button type='button'>Back</button></a></p>";

        if (server.hasArg("saved"))
        {
            if (server.arg("saved") == "1")
                html += "<p><b>Settings saved.</b></p>";
            else
                html += "<p><b>No changes detected.</b></p>";
        }
        if (server.hasArg("warning") && server.arg("warning") == "1")
            html += "<p><b>Invalid time zone format. Previous value kept.</b></p>";
        if (server.hasArg("time_changed") && server.arg("time_changed") == "1")
            html += "<p><b>Time zone applied and NTP sync requested.</b></p>";

        html += "<fieldset class='r'>";
        html += "<form method='POST' action='/settings/time_save'>";
        html += "Time zone (POSIX TZ string):<br>";
        html += "<p><b>Current zone : ";
        html += timeZoneSpec;
        html += "</b></p>";
        html += "<input name='time_tz' value='' maxlength='63'><br>";
        html += "<small>Examples: UTC0, EST5EDT,M3.2.0/2,M11.1.0/2, CET-1CEST,M3.5.0/2,M10.5.0/3</small><br><br>";
        html += "<button type='submit'>Apply Time Zone</button></form><br>";

        char nowBuf[48];
        if (hasValidSystemTime())
        {
            time_t nowTs = time(nullptr);
            struct tm localTm;
            localtime_r(&nowTs, &localTm);
            strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%d %H:%M:%S %Z", &localTm);
        }
        else
        {
            strncpy(nowBuf, "not synchronized yet", sizeof(nowBuf) - 1);
            nowBuf[sizeof(nowBuf) - 1] = '\0';
        }
        html += "Current timestamp:<br><b>";
        html += nowBuf;
        html += "</b><br><br>";
        html += "</fieldset>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
    });

    server.on("/settings/mqtt", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/settings-mqtt.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/settings-mqtt.html");
        return;
        String html;
        html.reserve(3600);
        appendWebUiPageStart(html, "HydroDozerPump - MQTT Settings", "MQTT Settings");
        html += "<p><a href='/settings'><button type='button'>Back</button></a></p>";

        if (server.hasArg("saved"))
        {
            if (server.arg("saved") == "1")
                html += "<p><b>Settings saved.</b></p>";
            else
                html += "<p><b>No changes detected.</b></p>";
        }
        if (server.hasArg("warning") && server.arg("warning") == "1")
            html += "<p><b>Invalid MQTT port. Previous value kept.</b></p>";

        html += "<fieldset class='r'>";
        html += "<form method='POST' action='/settings/mqtt_save'>";
        html += "Broker IP / Host:<br><input name='mqtt_broker' value='";
        html += mqttBroker;
        html += "' maxlength='39'><br><br>";
        html += "Broker port:<br><input name='mqtt_port' type='number' min='1' max='65535' value='";
        html += String(mqttPort);
        html += "'><br><br>";
        html += "Client ID:<br><input name='mqtt_client_id' value='";
        html += mqttClientId;
        html += "' maxlength='31'><br><br>";
        html += "MQTT user:<br><input name='mqtt_user' value='";
        html += mqttUser;
        html += "' maxlength='23'><br><br>";
        html += "MQTT pass:<br><input name='mqtt_pass' type='password' value='";
        html += mqttPass;
        html += "' maxlength='31'><br><br>";
        html += "<button type='submit'>Save MQTT Settings</button></form></fieldset>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
    });

    server.on("/settings/ota", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/settings-ota.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/settings-ota.html");
        return;
        String html;
        html.reserve(2800);
        appendWebUiPageStart(html, "HydroDozerPump - OTA Settings", "OTA Settings");
        html += "<p><a href='/settings'><button type='button'>Back</button></a></p>";

        if (server.hasArg("saved"))
        {
            if (server.arg("saved") == "1")
                html += "<p><b>Settings saved.</b></p>";
            else
                html += "<p><b>No changes detected.</b></p>";
        }

        html += "<fieldset class='r'>";
        html += "<form method='POST' action='/settings/ota_save'>";
        html += "OTA user:<br><input name='ota_user' value='";
        html += otaUser;
        html += "' maxlength='23'><br><br>";
        html += "OTA pass:<br><input name='ota_pass' type='password' value='";
        html += otaPass;
        html += "' maxlength='23'><br><br>";
        html += "<label><input type='checkbox' name='web_login_required' value='1'";
        if (webUiLoginRequired) html += " checked";
        html += ">Require Web UI login</label><br>";
        html += "<small>Uses OTA user/password credentials.</small><br><br>";
        html += "<button type='submit'>Save OTA Settings</button></form></fieldset>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);
    });

    server.on("/settings/backup_restore", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/settings-backup.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/settings-backup.html");
    });

    server.on("/settings/backup_restore/download_config", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (!LittleFS.exists("/config.json"))
        {
            server.send(404, "text/plain", "config.json not found.");
            return;
        }

        File f = LittleFS.open("/config.json", "r");
        if (!f)
        {
            server.send(500, "text/plain", "Unable to open config.json.");
            return;
        }

        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "0");
        server.sendHeader("Content-Type", "application/json");
        server.sendHeader("Content-Disposition", "attachment; filename=\"config.json\"");
        server.streamFile(f, "application/json");
        f.close();
    });

    server.on("/settings/backup_restore/download_pump_log", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (!LittleFS.exists(PUMP_RUN_LOG_CSV_PATH))
        {
            server.send(404, "text/plain", "pump_runs.csv not found.");
            return;
        }

        File f = LittleFS.open(PUMP_RUN_LOG_CSV_PATH, "r");
        if (!f)
        {
            server.send(500, "text/plain", "Unable to open pump log.");
            return;
        }

        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "0");
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", "attachment; filename=\"pump_runs.csv\"");
        server.streamFile(f, "text/csv");
        f.close();
    });

    server.on("/schedule_clear", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        clearSchedullerExecutionHistory();
        requestAutoStatePublish(100);
        server.sendHeader("Location", "/settings?saved=0&scheduller_cleared=1", true);
        server.send(303, "text/plain", "");
    });

    server.on("/pump_runs_view", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        sendWebUiPageStartChunked(200, "HydroDozerPump - Pump logs", "Pump logs");
        server.sendContent("<p><a href='/'><button type='button'>Back</button></a></p>");
        server.sendContent("<p><a href='/pump_runs_download'><button type='button'>Download Pump Log</button></a></p>");
        char nowBuf[48];
        if (hasValidSystemTime())
        {
            time_t nowTs = time(nullptr);
            struct tm localTm;
            localtime_r(&nowTs, &localTm);
            strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%d %H:%M:%S %Z", &localTm);
        }
        else
        {
            strncpy(nowBuf, "not synchronized yet", sizeof(nowBuf) - 1);
            nowBuf[sizeof(nowBuf) - 1] = '\0';
        }
        server.sendContent("<p><b>Current time:</b> ");
        server.sendContent(nowBuf);
        server.sendContent("</p>");
        server.sendContent("<h3>Scheduled :</h3>");
        float doseEachMl = schedullerTotalMl / 3.0f;
        float dailyPump1Ml = (schedullerTotalMl * schedullerPartAPct) / 100.0f;
        float dailyPump2Ml = (schedullerTotalMl * schedullerPartBPct) / 100.0f;
        float perSlotPump1Ml = (doseEachMl * schedullerPartAPct) / 100.0f;
        float perSlotPump2Ml = (doseEachMl * schedullerPartBPct) / 100.0f;
        char valueBuf[20];
        server.sendContent("<p>Daily ml to dispense<br>");
        server.sendContent("P1 : ");
        dtostrf(dailyPump1Ml, 0, 2, valueBuf);
        server.sendContent(valueBuf);
        server.sendContent(" ml<br>");
        server.sendContent("P2 : ");
        dtostrf(dailyPump2Ml, 0, 2, valueBuf);
        server.sendContent(valueBuf);
        server.sendContent(" ml<br>");
        server.sendContent("Pump 1 : ");
        dtostrf(perSlotPump1Ml, 0, 2, valueBuf);
        server.sendContent(valueBuf);
        server.sendContent(" ml per slot<br>");
        server.sendContent("Pump 2 : ");
        dtostrf(perSlotPump2Ml, 0, 2, valueBuf);
        server.sendContent(valueBuf);
        server.sendContent(" ml per slot</p>");

        auto formatRunDate = [](int year, int yday, char* out, size_t outLen)
        {
            if (outLen == 0)
                return;
            if (year < 0 || yday < 0)
            {
                strncpy(out, "empty", outLen - 1);
                out[outLen - 1] = '\0';
                return;
            }

            struct tm jan1 = {};
            jan1.tm_year = year;
            jan1.tm_mon = 0;
            jan1.tm_mday = 1;
            time_t base = mktime(&jan1);
            if (base < 0)
            {
                strncpy(out, "empty", outLen - 1);
                out[outLen - 1] = '\0';
                return;
            }

            time_t ts = base + ((time_t)yday * 86400);
            struct tm outTm;
            localtime_r(&ts, &outTm);
            if (strftime(out, outLen, "%Y-%m-%d", &outTm) == 0)
            {
                strncpy(out, "empty", outLen - 1);
                out[outLen - 1] = '\0';
            }
        };

        for (uint8_t slot = 1; slot <= 3; slot++)
        {
            char slotBuf[8];
            snprintf(slotBuf, sizeof(slotBuf), "%u", (unsigned)slot);
            server.sendContent("Slot ");
            server.sendContent(slotBuf);
            server.sendContent(" : ");

            int slotHour = getSchedullerHourBySlot(slot);
            if (isValidSchedullerHour(slotHour))
            {
                char hourBuf[8];
                snprintf(hourBuf, sizeof(hourBuf), "%02d:00", slotHour);
                server.sendContent(hourBuf);
            }
            else
            {
                server.sendContent("empty");
            }
            server.sendContent("<br>");

            bool hasP1 = (schedullerLastRunYear[slot][1] >= 0 && schedullerLastRunYDay[slot][1] >= 0);
            bool hasP2 = (schedullerLastRunYear[slot][2] >= 0 && schedullerLastRunYDay[slot][2] >= 0);

            server.sendContent("Last run : ");
            if (!hasP1 && !hasP2)
            {
                server.sendContent("empty");
            }
            else
            {
                int bestYear = -1;
                int bestYday = -1;
                if (hasP1)
                {
                    bestYear = schedullerLastRunYear[slot][1];
                    bestYday = schedullerLastRunYDay[slot][1];
                }
                if (hasP2)
                {
                    int y2 = schedullerLastRunYear[slot][2];
                    int d2 = schedullerLastRunYDay[slot][2];
                    if (bestYear < 0 || y2 > bestYear || (y2 == bestYear && d2 > bestYday))
                    {
                        bestYear = y2;
                        bestYday = d2;
                    }
                }

                char dateBuf[16];
                formatRunDate(bestYear, bestYday, dateBuf, sizeof(dateBuf));
                server.sendContent(dateBuf);

                bool p1OnBest = hasP1 &&
                                schedullerLastRunYear[slot][1] == bestYear &&
                                schedullerLastRunYDay[slot][1] == bestYday;
                bool p2OnBest = hasP2 &&
                                schedullerLastRunYear[slot][2] == bestYear &&
                                schedullerLastRunYDay[slot][2] == bestYday;
                if (p1OnBest && p2OnBest) server.sendContent(" (Pump 1+2)");
                else if (p1OnBest) server.sendContent(" (Pump 1)");
                else if (p2OnBest) server.sendContent(" (Pump 2)");
            }
            server.sendContent("<br>");
        }

        server.sendContent("<h3>Pump Run CSV Log</h3>");
        size_t logSizeBytes = 0;
        if (LittleFS.exists(PUMP_RUN_LOG_CSV_PATH))
        {
            File fSize = LittleFS.open(PUMP_RUN_LOG_CSV_PATH, "r");
            if (fSize)
            {
                logSizeBytes = fSize.size();
                fSize.close();
            }
        }
        size_t logSizeKb = (logSizeBytes == 0) ? 0 : ((logSizeBytes + 512) / 1024);
        size_t logMaxKb = PUMP_RUN_LOG_MAX_BYTES / 1024;
        size_t logLeftKb = (logSizeKb >= logMaxKb) ? 0 : (logMaxKb - logSizeKb);
        char sizeBuf[20];
        server.sendContent("<p>");
        snprintf(sizeBuf, sizeof(sizeBuf), "%u", (unsigned)logLeftKb);
        server.sendContent(sizeBuf);
        server.sendContent("KB Log space left</p>");
        server.sendContent("<p><a href='/pump_runs_view'><button type='button'>Refresh</button></a></p>");
        server.sendContent("<p>CSV Header:<br>Date, Pump ID, Volume in ML, Note</p>");

        if (!LittleFS.exists(PUMP_RUN_LOG_CSV_PATH))
        {
            server.sendContent("<p>No pump run entries yet.</p>");
        }
        else
        {
            File f = LittleFS.open(PUMP_RUN_LOG_CSV_PATH, "r");
            if (!f)
            {
                server.sendContent("<p>Unable to open log file.</p>");
            }
            else
            {
                const uint8_t maxLogLines = 10;
                const size_t lineBufLen = 96;
                char lines[maxLogLines][lineBufLen];
                uint8_t lineCount = 0;
                char revLine[lineBufLen];
                size_t revLen = 0;

                auto pushReversedLine = [&](size_t len)
                {
                    if (len == 0 || lineCount >= maxLogLines)
                        return;

                    char line[lineBufLen];
                    for (size_t i = 0; i < len; i++)
                        line[i] = revLine[len - 1 - i];
                    line[len] = '\0';

                    // Header is rendered above the view, so skip CSV header rows in log content.
                    if (strncmp(line, "Date,", 5) == 0)
                        return;

                    strncpy(lines[lineCount], line, lineBufLen - 1);
                    lines[lineCount][lineBufLen - 1] = '\0';
                    lineCount++;
                };

                size_t fileSize = f.size();
                if (fileSize > 0)
                {
                    size_t pos = fileSize;
                    while (pos > 0 && lineCount < maxLogLines)
                    {
                        pos--;
                        if (!f.seek((uint32_t)pos, SeekSet))
                            break;

                        int ch = f.read();
                        if (ch < 0)
                            break;

                        if (ch == '\n' || ch == '\r')
                        {
                            if (revLen > 0)
                            {
                                pushReversedLine(revLen);
                                revLen = 0;
                            }
                            continue;
                        }

                        if (revLen < (lineBufLen - 1))
                            revLine[revLen++] = (char)ch;
                    }

                    if (lineCount < maxLogLines && revLen > 0)
                        pushReversedLine(revLen);
                }
                f.close();

                if (lineCount == 0)
                {
                    server.sendContent("<p>No pump run entries yet.</p>");
                }
                else
                {
                    server.sendContent("<p>Showing latest 10 entries (newest first).</p>");
                    server.sendContent("<pre style='white-space:pre-wrap;border:1px solid #444;padding:10px;background:#1f1f1f;color:#65c115;'>");
                    for (uint8_t i = 0; i < lineCount; i++)
                    {
                        server.sendContent(lines[i]);
                        server.sendContent("\n");
                    }
                    server.sendContent("</pre>");
                }
            }
        }

        sendWebUiPageEndChunked();
    });

    server.on("/pump_runs_download", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();
        if (!LittleFS.exists(PUMP_RUN_LOG_CSV_PATH))
        {
            server.send(404, "text/plain", "Pump log file not found.");
            return;
        }

        File f = LittleFS.open(PUMP_RUN_LOG_CSV_PATH, "r");
        if (!f)
        {
            server.send(500, "text/plain", "Unable to open pump log file.");
            return;
        }

        char downloadName[48];
        if (hasValidSystemTime())
        {
            time_t nowTs = time(nullptr);
            struct tm localTm;
            localtime_r(&nowTs, &localTm);
            if (strftime(downloadName, sizeof(downloadName), "PumpLog-%Y%m%d-%H%M%S.csv", &localTm) == 0)
                strncpy(downloadName, "PumpLog.csv", sizeof(downloadName) - 1);
        }
        else
        {
            unsigned long uptimeSec = millis() / 1000UL;
            snprintf(downloadName, sizeof(downloadName), "PumpLog-uptime-%lus.csv", uptimeSec);
        }
        downloadName[sizeof(downloadName) - 1] = '\0';

        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "0");
        server.sendHeader("Content-Type", "text/csv");
        server.sendHeader("Content-Disposition", String("attachment; filename=\"") + downloadName + "\"");
        server.streamFile(f, "text/csv");
        f.close();
    });

    server.on("/littlefs_download", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();

        if (!server.hasArg("path"))
        {
            server.send(400, "text/plain", "Missing query parameter: path");
            return;
        }

        String path = server.arg("path");
        path.trim();
        if (!path.startsWith("/"))
            path = "/" + path;
        if (path.indexOf("..") >= 0 || path.indexOf('\\') >= 0 || path == "/")
        {
            server.send(400, "text/plain", "Invalid path.");
            return;
        }

        if (!LittleFS.exists(path))
        {
            server.send(404, "text/plain", "File not found.");
            return;
        }

        File f = LittleFS.open(path, "r");
        if (!f)
        {
            server.send(500, "text/plain", "Unable to open file.");
            return;
        }

        int slashPos = path.lastIndexOf('/');
        String fileName = (slashPos >= 0) ? path.substring(slashPos + 1) : path;
        if (fileName.length() == 0)
            fileName = "download.bin";

        server.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
        server.sendHeader("Pragma", "no-cache");
        server.sendHeader("Expires", "0");
        server.sendHeader("Content-Disposition", "attachment; filename=\"" + fileName + "\"");
        server.streamFile(f, "application/octet-stream");
        f.close();
    });

    server.on("/littlefs", HTTP_GET, []()
    {
        REQUIRE_WEB_UI_AUTH();

        sendWebUiPageStartChunked(200, "HydroDozerPump - LittleFS", "LittleFS files");
        server.sendContent("<p><a href='/'><button type='button'>Back</button></a></p>");

        FSInfo fsInfo;
        if (LittleFS.info(fsInfo))
        {
            char buf[64];
            server.sendContent("<p><b>Storage:</b><br>");
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)fsInfo.usedBytes);
            server.sendContent("Used: ");
            server.sendContent(buf);
            server.sendContent(" bytes<br>");
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)fsInfo.totalBytes);
            server.sendContent("Total: ");
            server.sendContent(buf);
            server.sendContent(" bytes</p>");
        }
        else
        {
            server.sendContent("<p>Unable to read filesystem info.</p>");
        }

        Dir dir = LittleFS.openDir("/");
        bool hasAnyFile = false;
        server.sendContent("<table style='width:100%;border-collapse:collapse;'>");
        server.sendContent("<tr><th style='text-align:left;'>Path</th><th style='text-align:left;'>Size (bytes)</th><th style='text-align:left;'>Download</th></tr>");
        while (dir.next())
        {
            hasAnyFile = true;
            String fileName = dir.fileName();
            char sizeBuf[20];
            snprintf(sizeBuf, sizeof(sizeBuf), "%lu", (unsigned long)dir.fileSize());
            server.sendContent("<tr><td>");
            server.sendContent(fileName);
            server.sendContent("</td><td>");
            server.sendContent(sizeBuf);
            server.sendContent("</td><td><a href='/littlefs_download?path=");
            server.sendContent(fileName);
            server.sendContent("'>download</a></td></tr>");
        }

        if (!hasAnyFile) server.sendContent("<tr><td colspan='3'>(no files)</td></tr>");
        server.sendContent("</table>");
        sendWebUiPageEndChunked();
    });

    server.on("/settings/network_save", HTTP_POST, []()
    {
        REQUIRE_WEB_UI_AUTH();
        bool changed = false;
        bool networkChanged = false;
        bool deviceNameChanged = false;
        String validationMsg = "";

        String v = server.arg("device_name");
        v.trim();
        if (v.length() > 0 && v != String(deviceName))
        {
            strncpy(deviceName, v.c_str(), DEVICE_NAME_LEN - 1);
            deviceName[DEVICE_NAME_LEN - 1] = '\0';
            rebuildMqttTopics();
            changed = true;
            deviceNameChanged = true;
        }

        String networkMode = server.arg("network_mode");
        networkMode.trim();
        bool wantManual = (networkMode == "manual");
        if (wantManual)
        {
            String ipS = server.arg("network_ip"); ipS.trim();
            String gwS = server.arg("network_gateway"); gwS.trim();
            String maskS = server.arg("network_netmask"); maskS.trim();
            String dnsS = server.arg("network_dns"); dnsS.trim();

            IPAddress ipA, gwA, maskA, dnsA;
            bool valid = ipA.fromString(ipS) &&
                         gwA.fromString(gwS) &&
                         maskA.fromString(maskS) &&
                         dnsA.fromString(dnsS);
            if (valid)
            {
                if (networkUseDhcp)
                {
                    networkUseDhcp = false;
                    changed = true;
                    networkChanged = true;
                }
                if (ipS != String(networkIp))
                {
                    strncpy(networkIp, ipS.c_str(), IPV4_STR_LEN - 1);
                    networkIp[IPV4_STR_LEN - 1] = '\0';
                    changed = true;
                    networkChanged = true;
                }
                if (gwS != String(networkGateway))
                {
                    strncpy(networkGateway, gwS.c_str(), IPV4_STR_LEN - 1);
                    networkGateway[IPV4_STR_LEN - 1] = '\0';
                    changed = true;
                    networkChanged = true;
                }
                if (maskS != String(networkNetmask))
                {
                    strncpy(networkNetmask, maskS.c_str(), IPV4_STR_LEN - 1);
                    networkNetmask[IPV4_STR_LEN - 1] = '\0';
                    changed = true;
                    networkChanged = true;
                }
                if (dnsS != String(networkDns))
                {
                    strncpy(networkDns, dnsS.c_str(), IPV4_STR_LEN - 1);
                    networkDns[IPV4_STR_LEN - 1] = '\0';
                    changed = true;
                    networkChanged = true;
                }
            }
            else
            {
                validationMsg = "Invalid manual network values. Previous network settings kept.";
            }
        }
        else
        {
            if (!networkUseDhcp)
            {
                networkUseDhcp = true;
                changed = true;
                networkChanged = true;
            }
        }

        if (changed)
            saveConfig();

        if (deviceNameChanged)
            mqttClient.disconnect();

        String redirect = "/settings/network?saved=";
        redirect += (changed ? "1" : "0");
        if (validationMsg.length() > 0)
            redirect += "&warning=1";
        if (networkChanged)
            redirect += "&network_changed=1";

        server.sendHeader("Location", redirect, true);
        server.send(303, "text/plain", "");
    });

    server.on("/settings/time_save", HTTP_POST, []()
    {
        REQUIRE_WEB_UI_AUTH();
        bool changed = false;
        bool timeChanged = false;
        bool warning = false;

        String v = server.arg("time_tz");
        v.trim();
        if (v.length() > 0 && !isValidTimeZoneSpec(v))
        {
            warning = true;
        }
        else if (v.length() > 0 && v != String(timeZoneSpec))
        {
            strncpy(timeZoneSpec, v.c_str(), TIMEZONE_LEN - 1);
            timeZoneSpec[TIMEZONE_LEN - 1] = '\0';
            changed = true;
            timeChanged = true;
        }

        if (changed)
            saveConfig();

        if (timeChanged)
        {
            applyTimeZone();
            requestTimeSync("settings");
        }

        String redirect = "/settings/time?saved=";
        redirect += (changed ? "1" : "0");
        if (warning)
            redirect += "&warning=1";
        if (timeChanged)
            redirect += "&time_changed=1";
        server.sendHeader("Location", redirect, true);
        server.send(303, "text/plain", "");
    });

    server.on("/settings/mqtt_save", HTTP_POST, []()
    {
        REQUIRE_WEB_UI_AUTH();
        bool changed = false;
        bool mqttChanged = false;
        bool warning = false;

        String v = server.arg("mqtt_broker");
        v.trim();
        if (v.length() > 0 && v != String(mqttBroker))
        {
            strncpy(mqttBroker, v.c_str(), MQTT_HOST_LEN - 1);
            mqttBroker[MQTT_HOST_LEN - 1] = '\0';
            changed = true;
            mqttChanged = true;
        }

        v = server.arg("mqtt_port");
        v.trim();
        if (v.length() > 0)
        {
            long p = v.toInt();
            if (p > 0 && p <= 65535)
            {
                if ((uint16_t)p != mqttPort)
                {
                    mqttPort = (uint16_t)p;
                    changed = true;
                    mqttChanged = true;
                }
            }
            else
            {
                warning = true;
            }
        }

        v = server.arg("mqtt_client_id");
        v.trim();
        if (v.length() > 0 && v != String(mqttClientId))
        {
            strncpy(mqttClientId, v.c_str(), MQTT_CLIENT_ID_LEN - 1);
            mqttClientId[MQTT_CLIENT_ID_LEN - 1] = '\0';
            changed = true;
            mqttChanged = true;
        }

        v = server.arg("mqtt_user");
        if (v != String(mqttUser))
        {
            strncpy(mqttUser, v.c_str(), MQTT_USER_LEN - 1);
            mqttUser[MQTT_USER_LEN - 1] = '\0';
            changed = true;
            mqttChanged = true;
        }

        v = server.arg("mqtt_pass");
        if (v != String(mqttPass))
        {
            strncpy(mqttPass, v.c_str(), MQTT_PASS_LEN - 1);
            mqttPass[MQTT_PASS_LEN - 1] = '\0';
            changed = true;
            mqttChanged = true;
        }

        if (changed)
            saveConfig();

        if (mqttChanged)
        {
            mqttClient.setServer(mqttBroker, mqttPort);
            mqttClient.disconnect();
        }

        String redirect = "/settings/mqtt?saved=";
        redirect += (changed ? "1" : "0");
        if (warning)
            redirect += "&warning=1";
        server.sendHeader("Location", redirect, true);
        server.send(303, "text/plain", "");
    });

    server.on("/settings/ota_save", HTTP_POST, []()
    {
        REQUIRE_WEB_UI_AUTH();
        bool changed = false;
        bool otaChanged = false;

        String v = server.arg("ota_user");
        v.trim();
        if (v.length() > 0 && v != String(otaUser))
        {
            strncpy(otaUser, v.c_str(), OTA_USER_LEN - 1);
            otaUser[OTA_USER_LEN - 1] = '\0';
            changed = true;
            otaChanged = true;
        }

        v = server.arg("ota_pass");
        v.trim();
        if (v.length() > 0 && v != String(otaPass))
        {
            strncpy(otaPass, v.c_str(), OTA_PASS_LEN - 1);
            otaPass[OTA_PASS_LEN - 1] = '\0';
            changed = true;
            otaChanged = true;
        }

        bool wantWebUiLogin = server.hasArg("web_login_required");
        if (wantWebUiLogin != webUiLoginRequired)
        {
            webUiLoginRequired = wantWebUiLogin;
            changed = true;
        }

        if (changed)
            saveConfig();

        if (otaChanged)
            setupOTA();

        if (webUiLoginRequired)
        {
            if (!hasValidWebUiSession())
            {
                String token = generateWebUiSessionToken();
                strncpy(webUiSessionToken, token.c_str(), WEB_UI_SESSION_TOKEN_LEN - 1);
                webUiSessionToken[WEB_UI_SESSION_TOKEN_LEN - 1] = '\0';
                server.sendHeader("Set-Cookie", buildWebUiSessionCookie());
            }
        }
        else
        {
            clearWebUiSession();
            server.sendHeader("Set-Cookie", "HDPSESSID=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax");
        }

        String redirect = "/settings/ota?saved=";
        redirect += (changed ? "1" : "0");
        server.sendHeader("Location", redirect, true);
        server.send(303, "text/plain", "");
    });

    server.on("/settings/backup_restore/upload_config", HTTP_POST, []()
    {
        REQUIRE_WEB_UI_AUTH();

        bool restored = false;
        if (!backupRestoreUploadFailed &&
            backupRestoreValidateConfigJson("/upload_config.tmp") &&
            backupRestorePromoteUploadedFile("/upload_config.tmp", "/config.json"))
        {
            restored = true;
            loadConfig();
            applyDefaultIdentityFromMac();
            rebuildMqttTopics();
            applyTimeZone();
            requestTimeSync("config_restore");
        }

        if (LittleFS.exists("/upload_config.tmp"))
            LittleFS.remove("/upload_config.tmp");

        bool hadUploadError = (backupRestoreUploadError[0] != '\0');
        backupRestoreResetUploadState();
        String redirect = "/settings/backup_restore?config_restored=";
        redirect += (restored ? "1" : "0");
        if (!restored && hadUploadError)
            redirect += "&error=1";
        server.sendHeader("Location", redirect, true);
        server.send(303, "text/plain", "");
    }, []()
    {
        REQUIRE_WEB_UI_AUTH();
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START)
        {
            backupRestoreStartUpload("/upload_config.tmp");
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
            backupRestoreWriteUploadChunk(upload.buf, upload.currentSize);
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
            backupRestoreFinishUpload(upload.totalSize);
        }
        else if (upload.status == UPLOAD_FILE_ABORTED)
        {
            backupRestoreAbortUpload("Upload aborted.");
        }
    });

    server.on("/settings/backup_restore/upload_pump_log", HTTP_POST, []()
    {
        REQUIRE_WEB_UI_AUTH();

        bool restored = false;
        if (!backupRestoreUploadFailed &&
            backupRestoreValidatePumpLogCsv("/upload_pump_runs.tmp") &&
            backupRestorePromoteUploadedFile("/upload_pump_runs.tmp", PUMP_RUN_LOG_CSV_PATH))
        {
            restored = true;
        }

        if (LittleFS.exists("/upload_pump_runs.tmp"))
            LittleFS.remove("/upload_pump_runs.tmp");

        bool hadUploadError = (backupRestoreUploadError[0] != '\0');
        backupRestoreResetUploadState();
        String redirect = "/settings/backup_restore?pump_log_restored=";
        redirect += (restored ? "1" : "0");
        if (!restored && hadUploadError)
            redirect += "&error=1";
        server.sendHeader("Location", redirect, true);
        server.send(303, "text/plain", "");
    }, []()
    {
        REQUIRE_WEB_UI_AUTH();
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START)
        {
            backupRestoreStartUpload("/upload_pump_runs.tmp");
        }
        else if (upload.status == UPLOAD_FILE_WRITE)
        {
            backupRestoreWriteUploadChunk(upload.buf, upload.currentSize);
        }
        else if (upload.status == UPLOAD_FILE_END)
        {
            backupRestoreFinishUpload(upload.totalSize);
        }
        else if (upload.status == UPLOAD_FILE_ABORTED)
        {
            backupRestoreAbortUpload("Upload aborted.");
        }
    });

    {
        server.on("/factory", HTTP_GET, []()
        {
        REQUIRE_WEB_UI_AUTH();
        if (sendLittleFSFile("/web/factory.html", true))
            return;
        server.send(500, "text/plain", "UI file missing: /web/factory.html");
        return;

        String page;
        page.reserve(2600);
        appendWebUiPageStart(page, "HydroDozerPump - Factory Reset", "Factory Reset");
        page += "<p>This will erase WiFi settings and all local files (config + logs).</p>";
        page += "<p>The device will reboot into setup mode.</p>";
        page += "<p><b>Emergency recovery:</b> short D5 to GND during boot for 3 seconds to run full wipe.</p>";

        page += "<p><a href='/factory_confirm'><button class='bred'>Confirm Factory Reset</button></a></p>";
        page += "<p><a href='/'><button type='button'>Back</button></a></p>";
        appendWebUiPageEnd(page);

        server.send(200, "text/html", page);
        });
    }

    {
        server.on("/factory_confirm", HTTP_GET, []()
        {
        REQUIRE_WEB_UI_AUTH();
        String html;
        html.reserve(1600);
        appendWebUiPageStart(html, "HydroDozerPump - Factory Reset", "Factory Reset");
        html += "<p>Erasing WiFi settings and local files... Rebooting.</p>";
        appendWebUiPageEnd(html);
        server.send(200, "text/html", html);

        delay(1500);

        performFactoryReset(true);
        });

    }

    {

    }

#undef REQUIRE_WEB_UI_AUTH

server.begin();
    Serial.println("[WEB] HTTP server started");
}
