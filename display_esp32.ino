#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <string.h>
#include <vector>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ==================================================
// 1. WIFI AND FLASK SERVER
// Copy secrets.example.h to secrets.h and configure it locally.
#include "secrets.h"

const unsigned long POLL_INTERVAL = 500;
const unsigned long RECONNECT_INTERVAL = 5000;

// Welcome screen duration: 8 seconds.
const unsigned long WELCOME_SCREEN_MS = 8000;

const unsigned long UNKNOWN_SCREEN_MS = 4000;
const unsigned long PIN_SCREEN_MS = 4000;

// ==================================================
// 2. HARDWARE PINS
// ==================================================

// TFT
#define TFT_SCK   18
#define TFT_MOSI  23
#define TFT_RST   17
#define TFT_DC    16
#define TFT_CS    5
#define TFT_BLK   33

// Security hardware
#define BUZZER    25
#define GREEN_LED 26
#define RED_LED   27

// Optional demo button
#define DEMO_BUTTON 32

// Rotary encoder
#define ENCODER_CLK 14
#define ENCODER_DT  12
#define ENCODER_SW  4

// ==================================================
// 3. DISPLAY COLORS
// ==================================================

#define BLACK      0x0000
#define WHITE      0xFFFF
#define GRAY       0x8410
#define GREEN      0x07E0
#define RED        0xF800
#define BLUE       0x001F
#define YELLOW     0xFFE0
#define DARK_GREEN 0x0320

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// ==================================================
// 4. SCREEN STATES
// ==================================================

enum ScreenMode {
    SCREEN_IDLE,
    SCREEN_WELCOME,
    SCREEN_UNKNOWN,
    SCREEN_PIN_GRANTED,
    SCREEN_PIN_DENIED,
    SCREEN_MULTIPLE,
    SCREEN_TASK_MENU,
    SCREEN_SEAA_LOADING,
    SCREEN_SEAA_RESULT,
    SCREEN_SEAA_ERROR
};

ScreenMode currentScreen = SCREEN_IDLE;

// ==================================================
// 5. SYSTEM STATE
// ==================================================

bool accessLatched = false;
bool personalTaskAccess = false;
bool serverConnected = false;

String currentPerson = "";
String lastPerson = "";
String lastFaceStatus = "";

// Dynamic task storage.
// Unlike the original todoItems[4], this can grow
// to accommodate the tasks received from Flask.
std::vector<String> todoItems;

int todoCount = 0;
int selectedTask = 0;
int firstVisibleTask = 0;

// Four task rows are displayed at a time.
const int VISIBLE_TASKS = 4;

long lastPinResultId = -1;

unsigned long lastPoll = 0;
unsigned long lastReconnect = 0;
unsigned long screenStarted = 0;
unsigned long suppressFaceUntil = 0;

unsigned long lastEncoderPress = 0;
unsigned long lastEncoderRotation = 0;

const unsigned long ENCODER_DEBOUNCE_MS = 180;

int lastCLKState = HIGH;
bool encoderPressed = false;

// ==================================================
// 6. SEAA STATE
// ==================================================

int seaaEstimatedMinutes = 0;

String seaaTask = "";
String seaaTips[3];
String seaaError = "";

// ==================================================
// 7. BUZZER AND LEDS
// ==================================================

void stopBuzzer() {
    noTone(BUZZER);
}

void successSound() {
    tone(BUZZER, 2000, 120);
    delay(150);
    tone(BUZZER, 2800, 150);
}

void warningSound() {
    tone(BUZZER, 1500, 250);
}

void setLeds(bool greenOn, bool redOn) {
    digitalWrite(GREEN_LED, greenOn ? HIGH : LOW);
    digitalWrite(RED_LED, redOn ? HIGH : LOW);
}

// ==================================================
// 8. DISPLAY HELPERS
// ==================================================

void centeredText(
    String text,
    int y,
    int textSize,
    uint16_t color
) {
    tft.setTextSize(textSize);
    tft.setTextColor(color);

    int16_t x1, y1;
    uint16_t w, h;

    tft.getTextBounds(
        text,
        0,
        y,
        &x1,
        &y1,
        &w,
        &h
    );

    int x = (tft.width() - (int)w) / 2;

    if (x < 0) {
        x = 0;
    }

    tft.setCursor(x, y);
    tft.print(text);
}

// Display a task without allowing excessively long
// strings to run beyond the right edge.
String shortenTask(String task, int maxLength) {
    if ((int)task.length() > maxLength) {
        return task.substring(0, maxLength - 3) + "...";
    }

    return task;
}

// ==================================================
// 9. IDLE SCREEN
// ==================================================

void drawIdleScreen() {
    tft.fillScreen(BLACK);

    centeredText("SECURITY SYSTEM", 35, 2, YELLOW);

    tft.drawFastHLine(
        15, 65, tft.width() - 30, GRAY
    );

    centeredText("SYSTEM SECURED", 95, 3, GREEN);
    centeredText("Waiting for face...", 150, 2, WHITE);

    if (WiFi.status() == WL_CONNECTED && serverConnected) {
        centeredText("WiFi / Server OK", 210, 1, GRAY);
    } else if (WiFi.status() == WL_CONNECTED) {
        centeredText("Waiting for server", 210, 1, YELLOW);
    } else {
        centeredText("WiFi Disconnected", 210, 1, RED);
    }
}

// ==================================================
// 10. PERSONALIZED TASK HEADING
// ==================================================

String getTaskHeading() {
    if (currentPerson.length() == 0) {
        return "YOUR TASKS";
    }

    return currentPerson + "'s Tasks";
}

// ==================================================
// 11. WELCOME SCREEN
// ==================================================

void drawWelcomeScreen() {
    tft.fillScreen(DARK_GREEN);

    centeredText("ACCESS GRANTED", 10, 2, WHITE);

    tft.drawFastHLine(
        10, 40, tft.width() - 20, GREEN
    );

    centeredText(
        currentPerson,
        50,
        currentPerson.length() > 10 ? 2 : 3,
        YELLOW
    );

    centeredText(getTaskHeading(), 95, 2, WHITE);

    if (todoCount == 0) {
        centeredText("No tasks available", 135, 1, WHITE);
    } else {
        // The welcome screen previews up to three tasks.
        // The complete list is available in the task menu.
        tft.setTextSize(1);
        tft.setTextColor(WHITE);

        int previewCount = min(todoCount, 3);

        for (int i = 0; i < previewCount; i++) {
            String item = shortenTask(todoItems[i], 30);

            int y = 125 + i * 28;

            tft.setCursor(10, y);
            tft.print("- ");
            tft.print(item);
        }

        if (todoCount > previewCount) {
            tft.setCursor(10, 125 + previewCount * 28);
            tft.print("+ ");
            tft.print(todoCount - previewCount);
            tft.print(" more tasks");
        }
    }

    centeredText("Opening tasks shortly...", 235, 1, YELLOW);
    centeredText("Press RESET to secure", 255, 1, WHITE);
}

// ==================================================
// 12. UNKNOWN PERSON SCREEN
// ==================================================

void drawUnknownScreen() {
    tft.fillScreen(RED);

    centeredText("WARNING", 55, 4, WHITE);
    centeredText("UNKNOWN PERSON", 120, 2, WHITE);
    centeredText("PIN REQUIRED", 165, 2, YELLOW);
}

// ==================================================
// 13. PIN GRANTED SCREEN
// ==================================================

void drawPinGrantedScreen() {
    tft.fillScreen(DARK_GREEN);

    centeredText("ACCESS", 65, 4, WHITE);
    centeredText("GRANTED", 115, 4, WHITE);
    centeredText("PIN VERIFIED", 180, 2, YELLOW);
    centeredText("Press RESET to secure", 240, 1, WHITE);
}

// ==================================================
// 14. PIN DENIED SCREEN
// ==================================================

void drawPinDeniedScreen() {
    tft.fillScreen(RED);

    centeredText("ACCESS", 65, 4, WHITE);
    centeredText("DENIED", 115, 4, WHITE);
    centeredText("Incorrect PIN", 180, 2, YELLOW);
}

// ==================================================
// 15. MULTIPLE PEOPLE SCREEN
// ==================================================

void drawMultipleScreen() {
    tft.fillScreen(BLUE);

    centeredText("PLEASE WAIT", 45, 3, WHITE);
    centeredText("ONE PERSON", 100, 3, YELLOW);
    centeredText("AT A TIME", 145, 3, YELLOW);
    centeredText("Step back and retry", 210, 1, WHITE);
}

// ==================================================
// 16. SCROLLABLE TASK MENU
// ==================================================

void drawTaskMenu() {
    tft.fillScreen(BLACK);

    centeredText(getTaskHeading(), 8, 2, YELLOW);

    tft.drawFastHLine(10, 35, 220, GRAY);

    if (todoCount == 0) {
        centeredText("No tasks available", 90, 1, WHITE);
        centeredText("Press RESET to secure", 250, 1, YELLOW);
        return;
    }

    // Keep the selected item visible.
    if (selectedTask < firstVisibleTask) {
        firstVisibleTask = selectedTask;
    }

    if (selectedTask >= firstVisibleTask + VISIBLE_TASKS) {
        firstVisibleTask = selectedTask - VISIBLE_TASKS + 1;
    }

    // Ensure the viewport never extends past the list.
    int maxFirst = max(0, todoCount - VISIBLE_TASKS);

    if (firstVisibleTask > maxFirst) {
        firstVisibleTask = maxFirst;
    }

    int lastVisibleTask = min(
        firstVisibleTask + VISIBLE_TASKS,
        todoCount
    );

    // Show only the rows that fit on the screen.
    for (int i = firstVisibleTask; i < lastVisibleTask; i++) {
        int row = i - firstVisibleTask;
        int y = 48 + row * 43;

        if (i == selectedTask) {
            tft.fillRoundRect(
                5, y - 4, 230, 35, 5, BLUE
            );

            tft.setTextColor(YELLOW);
        } else {
            tft.setTextColor(WHITE);
        }

        tft.setTextSize(1);
        tft.setCursor(12, y + 5);

        tft.print(i == selectedTask ? "> " : "  ");
        tft.print(shortenTask(todoItems[i], 27));
    }

    // Show the current position in the full task list.
    String position =
        String(selectedTask + 1) + "/" + String(todoCount);

    centeredText(position, 222, 1, GRAY);
    centeredText("Rotate: Select task", 237, 1, WHITE);
    centeredText("Press: Ask SEAA", 252, 1, YELLOW);
    centeredText("RESET: Secure system", 267, 1, GRAY);
}

// ==================================================
// 17. SEAA LOADING SCREEN
// ==================================================

void drawSeaaLoading() {
    tft.fillScreen(BLACK);

    centeredText("SEAA AI", 35, 3, YELLOW);
    centeredText("Analyzing task...", 100, 2, WHITE);
    centeredText("Please wait", 145, 2, GREEN);

    tft.setTextSize(1);
    tft.setTextColor(GRAY);
    tft.setCursor(20, 195);
    tft.println("Connecting to local AI");
}

// ==================================================
// 18. PRINT WRAPPED TEXT
// ==================================================

int drawWrappedText(
    String text,
    int x,
    int y,
    int maxChars,
    int lineHeight,
    int bottomLimit
) {
    int start = 0;

    while (start < (int)text.length() && y < bottomLimit) {
        int end = min(
            start + maxChars,
            (int)text.length()
        );

        if (end < (int)text.length()) {
            int space = text.lastIndexOf(' ', end);

            if (space > start) {
                end = space;
            }
        }

        tft.setCursor(x, y);
        tft.println(text.substring(start, end));

        start = end;

        while (
            start < (int)text.length() &&
            text.charAt(start) == ' '
        ) {
            start++;
        }

        y += lineHeight;
    }

    return y;
}

// ==================================================
// 19. SEAA RESULTS SCREEN
// ==================================================

void drawSeaaResult() {
    tft.fillScreen(BLACK);

    centeredText("SEAA ANALYSIS", 5, 2, YELLOW);

    tft.setTextSize(1);
    tft.setTextColor(GRAY);
    tft.setCursor(8, 32);
    tft.println("TASK:");

    tft.setTextColor(WHITE);

    drawWrappedText(
        seaaTask,
        8,
        44,
        34,
        12,
        75
    );

    tft.setTextColor(GREEN);
    tft.setCursor(8, 80);
    tft.print("Estimated time: ");

    if (seaaEstimatedMinutes >= 60) {
        tft.print(seaaEstimatedMinutes / 60);
        tft.print("h ");

        if (seaaEstimatedMinutes % 60 > 0) {
            tft.print(seaaEstimatedMinutes % 60);
            tft.print("m");
        }
    } else {
        tft.print(seaaEstimatedMinutes);
        tft.print(" min");
    }

    tft.setTextColor(YELLOW);
    tft.setCursor(8, 104);
    tft.println("TIPS:");

    int y = 120;

    for (int i = 0; i < 3 && y < 244; i++) {
        tft.setTextColor(YELLOW);
        tft.setCursor(8, y);
        tft.print(i + 1);
        tft.print(". ");

        tft.setTextColor(WHITE);

        y = drawWrappedText(
            seaaTips[i],
            23,
            y,
            31,
            11,
            244
        );

        y += 3;
    }

    centeredText("Press encoder: Back", 263, 1, YELLOW);
}

// ==================================================
// 20. SEAA ERROR SCREEN
// ==================================================

void drawSeaaError() {
    tft.fillScreen(RED);

    centeredText("SEAA ERROR", 35, 3, WHITE);

    tft.setTextSize(1);
    tft.setTextColor(WHITE);

    drawWrappedText(
        seaaError,
        10,
        100,
        32,
        14,
        200
    );

    centeredText("Press encoder to return", 245, 1, YELLOW);
}

// ==================================================
// 21. SCREEN STATE FUNCTIONS
// ==================================================

void showIdle() {
    currentScreen = SCREEN_IDLE;
    screenStarted = millis();

    currentPerson = "";
    personalTaskAccess = false;

    todoItems.clear();
    todoCount = 0;
    selectedTask = 0;
    firstVisibleTask = 0;

    setLeds(false, false);
    stopBuzzer();

    drawIdleScreen();
}

void showWelcome(
    String person,
    const std::vector<String>& tasks
) {
    currentPerson = person;

    todoItems = tasks;
    todoCount = (int)todoItems.size();

    selectedTask = 0;
    firstVisibleTask = 0;

    currentScreen = SCREEN_WELCOME;
    screenStarted = millis();

    setLeds(true, false);

    drawWelcomeScreen();
    successSound();

    Serial.println("Recognized: " + currentPerson);
    Serial.printf("Displaying %d tasks.\n", todoCount);
}

void showPinGranted() {
    // PIN access must not expose personal tasks.
    todoItems.clear();
    todoCount = 0;

    currentPerson = "";
    personalTaskAccess = false;

    selectedTask = 0;
    firstVisibleTask = 0;

    currentScreen = SCREEN_PIN_GRANTED;
    screenStarted = millis();

    setLeds(true, false);

    drawPinGrantedScreen();
    successSound();

    Serial.println("PIN accepted. Personal tasks hidden.");
}

void showUnknown() {
    currentScreen = SCREEN_UNKNOWN;
    screenStarted = millis();

    setLeds(false, true);

    drawUnknownScreen();
    warningSound();

    Serial.println("Unknown person detected.");
}

void showPinDenied() {
    currentScreen = SCREEN_PIN_DENIED;
    screenStarted = millis();

    setLeds(false, true);

    drawPinDeniedScreen();
    warningSound();

    Serial.println("PIN rejected.");
}

void showMultiple() {
    currentScreen = SCREEN_MULTIPLE;
    screenStarted = millis();

    setLeds(false, false);
    stopBuzzer();

    drawMultipleScreen();

    Serial.println("Multiple people detected.");
}

// ==================================================
// 22. RESET FLASK SERVER
// ==================================================

void notifyServerReset() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println(
            "Cannot reset server: WiFi is disconnected."
        );
        return;
    }

    HTTPClient http;

    http.setConnectTimeout(2000);
    http.setTimeout(2000);

    if (!http.begin(RESET_URL)) {
        Serial.println("Could not start reset request.");
        return;
    }

    http.addHeader(
        "Content-Type",
        "application/json"
    );

    int code = http.POST("{}");

    if (code >= 200 && code < 300) {
        Serial.println(
            "Server reset. Security system is armed."
        );
    } else {
        Serial.printf(
            "Server reset failed. HTTP: %d\n",
            code
        );
    }

    http.end();
}

// ==================================================
// 23. PROCESS FLASK RESPONSE
// ==================================================

void processServerResponse(String response) {
    // Larger JSON document for larger task lists.
    DynamicJsonDocument doc(16384);

    DeserializationError error =
        deserializeJson(doc, response);

    if (error) {
        Serial.print("JSON error: ");
        Serial.println(error.c_str());
        return;
    }

    String accessMode = doc["access_mode"] | "SECURED";
    String faceStatus = doc["face_status"] | "NO_FACE";
    String person = doc["person"] | "";
    String pinResult = doc["pin_result"] | "";
    String accessType = doc["access_type"] | "";

    long pinId = doc["pin_result_id"] | 0;

    Serial.print("Server face_status: ");
    Serial.print(faceStatus);
    Serial.print(" | person: ");
    Serial.print(person);
    Serial.print(" | access_mode: ");
    Serial.println(accessMode);

    // Handle each new PIN result only once.
    if (pinId > lastPinResultId) {
        lastPinResultId = pinId;

        if (pinResult == "ACCESS_GRANTED") {
            showPinGranted();

            accessLatched = true;
            personalTaskAccess = false;

            lastFaceStatus = faceStatus;
            lastPerson = person;

            return;
        }

        if (pinResult == "ACCESS_DENIED") {
            suppressFaceUntil = millis() + PIN_SCREEN_MS;

            showPinDenied();

            return;
        }
    }

    // Keep PIN denial visible temporarily.
    if ((long)(suppressFaceUntil - millis()) > 0) {
        return;
    }

    // Granted access stays visible until reset.
    if (accessLatched) {
        return;
    }

    // Multiple people.
    if (
        faceStatus == "MULTIPLE_PEOPLE" ||
        faceStatus == "MULTIPLE"
    ) {
        if (currentScreen != SCREEN_MULTIPLE) {
            showMultiple();
        }

        lastFaceStatus = faceStatus;
        lastPerson = "";

        return;
    }

    // Recognized person.
    if (
        (faceStatus == "WELCOME" ||
         faceStatus == "KNOWN") &&
        person.length() > 0
    ) {
        std::vector<String> tasks;

        // Personal tasks are only available for recognized-face access.
        if (
            accessMode == "GRANTED" &&
            (
                accessType == "RECOGNIZED_FACE" ||
                accessType == "FACE_RECOGNITION"
            )
        ) {
            JsonArray taskArray = doc["tasks"].as<JsonArray>();

            if (!taskArray.isNull()) {
                for (JsonVariant value : taskArray) {
                    const char* task = value.as<const char*>();

                    if (task != nullptr && strlen(task) > 0) {
                        tasks.push_back(String(task));
                    }
                }
            }
        }

        Serial.printf("Tasks received: %d\n", (int)tasks.size());

        for (int i = 0; i < (int)tasks.size(); i++) {
            Serial.printf(
                "Task %d: %s\n",
                i + 1,
                tasks[i].c_str()
            );
        }

        // Show the welcome screen once for this access.
        if (
            currentScreen != SCREEN_WELCOME &&
            currentScreen != SCREEN_TASK_MENU
        ) {
            showWelcome(person, tasks);
        }

        lastPerson = person;
        lastFaceStatus = faceStatus;

        if (accessMode == "GRANTED") {
            accessLatched = true;

            if (
                accessType == "RECOGNIZED_FACE" ||
                accessType == "FACE_RECOGNITION"
            ) {
                personalTaskAccess = true;

                // Do not switch immediately to the task menu.
                // updateScreen() switches after WELCOME_SCREEN_MS.
                Serial.println(
                    "Recognized-face access granted. "
                    "Welcome screen displayed."
                );
            } else {
                personalTaskAccess = false;
            }
        }

        return;
    }

    // Unknown person.
    if (faceStatus == "UNKNOWN") {
        if (
            currentScreen != SCREEN_UNKNOWN ||
            lastFaceStatus != faceStatus
        ) {
            showUnknown();
        }

        lastFaceStatus = faceStatus;
        lastPerson = "";

        return;
    }

    // No face / secured.
    if (
        faceStatus == "NO_FACE" ||
        faceStatus == "IDLE" ||
        faceStatus == "SECURED"
    ) {
        lastFaceStatus = faceStatus;
        lastPerson = "";

        if (currentScreen != SCREEN_IDLE) {
            showIdle();
        }

        return;
    }

    Serial.print("Unrecognized face status: ");
    Serial.println(faceStatus);
}

// ==================================================
// 24. POLL FLASK SERVER
// ==================================================

void pollServer() {
    unsigned long now = millis();

    if (now - lastPoll < POLL_INTERVAL) {
        return;
    }

    lastPoll = now;

    if (WiFi.status() != WL_CONNECTED) {
        serverConnected = false;

        if (now - lastReconnect >= RECONNECT_INTERVAL) {
            lastReconnect = now;

            Serial.println("Reconnecting to WiFi...");
            WiFi.reconnect();
        }

        return;
    }

    HTTPClient http;

    http.setConnectTimeout(1500);
    http.setTimeout(1500);

    if (!http.begin(STATUS_URL)) {
        serverConnected = false;
        return;
    }

    int code = http.GET();

    if (code == 200) {
        serverConnected = true;

        String response = http.getString();
        processServerResponse(response);
    } else {
        serverConnected = false;

        Serial.printf(
            "Server request failed: HTTP %d\n",
            code
        );
    }

    http.end();
}

// ==================================================
// 25. SEAA TASK ANALYSIS
// ==================================================

void analyzeSelectedTask() {
    if (!personalTaskAccess || !accessLatched) {
        return;
    }

    if (todoCount == 0) {
        seaaError = "No tasks available.";

        currentScreen = SCREEN_SEAA_ERROR;
        drawSeaaError();

        return;
    }

    if (selectedTask < 0 || selectedTask >= todoCount) {
        return;
    }

    if (WiFi.status() != WL_CONNECTED) {
        seaaError = "WiFi disconnected.";

        currentScreen = SCREEN_SEAA_ERROR;
        drawSeaaError();

        return;
    }

    seaaTask = todoItems[selectedTask];

    currentScreen = SCREEN_SEAA_LOADING;
    drawSeaaLoading();

    Serial.println("Sending task to SEAA:");
    Serial.println(seaaTask);

    HTTPClient http;

    http.setConnectTimeout(3000);
    http.setTimeout(120000);

    if (!http.begin(SEAA_URL)) {
        seaaError = "Could not connect to Flask.";

        currentScreen = SCREEN_SEAA_ERROR;
        drawSeaaError();

        return;
    }

    http.addHeader(
        "Content-Type",
        "application/json"
    );

    DynamicJsonDocument requestDoc(1024);
    requestDoc["task"] = seaaTask;

    String requestBody;
    serializeJson(requestDoc, requestBody);

    int httpCode = http.POST(requestBody);

    if (httpCode == 200) {
        String response = http.getString();

        DynamicJsonDocument responseDoc(4096);

        DeserializationError error =
            deserializeJson(responseDoc, response);

        if (
            !error &&
            responseDoc["ok"] == true
        ) {
            int estimatedMinutes =
                responseDoc["estimated_minutes"] | 0;

            JsonArray tips =
                responseDoc["tips"].as<JsonArray>();

            if (
                estimatedMinutes > 0 &&
                tips.size() >= 3
            ) {
                seaaEstimatedMinutes = estimatedMinutes;

                for (int i = 0; i < 3; i++) {
                    seaaTips[i] = tips[i].as<String>();
                }

                currentScreen = SCREEN_SEAA_RESULT;
                drawSeaaResult();

                Serial.println("SEAA analysis successful.");

                Serial.printf(
                    "Estimated minutes: %d\n",
                    seaaEstimatedMinutes
                );
            } else {
                seaaError = "Incomplete AI response.";

                currentScreen = SCREEN_SEAA_ERROR;
                drawSeaaError();
            }
        } else {
            seaaError = "Invalid server response.";

            currentScreen = SCREEN_SEAA_ERROR;
            drawSeaaError();
        }
    } else {
        Serial.printf(
            "SEAA HTTP error: %d\n",
            httpCode
        );

        if (httpCode == 503) {
            seaaError = "AI unavailable. Check Ollama.";
        } else if (httpCode == 504) {
            seaaError = "AI request timed out.";
        } else {
            seaaError = "HTTP error: " + String(httpCode);
        }

        currentScreen = SCREEN_SEAA_ERROR;
        drawSeaaError();
    }

    http.end();
}

// ==================================================
// 26. ROTARY ENCODER INPUT
// ==================================================

void handleEncoder() {
    if (!personalTaskAccess || !accessLatched) {
        return;
    }

    // Don't navigate while SEAA is analyzing or
    // while the welcome screen is being displayed.
    if (
        currentScreen == SCREEN_SEAA_LOADING ||
        currentScreen == SCREEN_WELCOME
    ) {
        return;
    }

    int clkState = digitalRead(ENCODER_CLK);

    if (
        currentScreen == SCREEN_TASK_MENU &&
        todoCount > 0 &&
        clkState != lastCLKState &&
        clkState == LOW &&
        millis() - lastEncoderRotation >= 3
    ) {
        lastEncoderRotation = millis();

        if (digitalRead(ENCODER_DT) != clkState) {
            selectedTask++;

            if (selectedTask >= todoCount) {
                selectedTask = 0;
            }
        } else {
            selectedTask--;

            if (selectedTask < 0) {
                selectedTask = todoCount - 1;
            }
        }

        // drawTaskMenu adjusts the visible window
        // so the selected task stays on the screen.
        drawTaskMenu();
    }

    lastCLKState = clkState;

    // Read encoder button.
    bool buttonState = digitalRead(ENCODER_SW);

    if (
        buttonState == LOW &&
        !encoderPressed &&
        millis() - lastEncoderPress >= ENCODER_DEBOUNCE_MS
    ) {
        encoderPressed = true;
        lastEncoderPress = millis();

        if (currentScreen == SCREEN_TASK_MENU) {
            analyzeSelectedTask();
        } else if (
            currentScreen == SCREEN_SEAA_RESULT ||
            currentScreen == SCREEN_SEAA_ERROR
        ) {
            currentScreen = SCREEN_TASK_MENU;
            drawTaskMenu();
        }
    }

    if (buttonState == HIGH) {
        encoderPressed = false;
    }
}

// ==================================================
// 27. SCREEN TIMEOUTS
// ==================================================

void updateScreen() {
    unsigned long elapsed = millis() - screenStarted;

    // Switch from welcome screen to task menu after 8 seconds.
    // This check must happen before the accessLatched return.
    if (
        accessLatched &&
        personalTaskAccess &&
        currentScreen == SCREEN_WELCOME &&
        elapsed >= WELCOME_SCREEN_MS
    ) {
        currentScreen = SCREEN_TASK_MENU;
        drawTaskMenu();

        Serial.println(
            "Welcome screen finished. Opening task menu."
        );

        return;
    }

    // Granted access remains latched until physical reset.
    if (accessLatched) {
        return;
    }

    if (
        currentScreen == SCREEN_UNKNOWN &&
        elapsed >= UNKNOWN_SCREEN_MS
    ) {
        showIdle();
    }

    if (
        (
            currentScreen == SCREEN_PIN_GRANTED ||
            currentScreen == SCREEN_PIN_DENIED
        ) &&
        elapsed >= PIN_SCREEN_MS
    ) {
        showIdle();
    }
}

// ==================================================
// 28. OPTIONAL DEMO BUTTON
// ==================================================

void checkDemoButton() {
    static bool previousState = HIGH;
    static unsigned long lastChange = 0;

    bool state = digitalRead(DEMO_BUTTON);

    if (
        state != previousState &&
        millis() - lastChange > 50
    ) {
        lastChange = millis();
        previousState = state;

        if (
            state == LOW &&
            currentScreen == SCREEN_IDLE &&
            !accessLatched
        ) {
            std::vector<String> demoTasks;

            demoTasks.push_back("Example task one");
            demoTasks.push_back("Example task two");
            demoTasks.push_back("Example task three");
            demoTasks.push_back("Example task four");
            demoTasks.push_back("Example task five");

            showWelcome("Demo User", demoTasks);

            // Demo mode does not grant real access.
            personalTaskAccess = false;
        } else if (!accessLatched) {
            showIdle();
        }
    }
}

// ==================================================
// 29. SETUP
// ==================================================

void setup() {
    Serial.begin(115200);
    Serial.setTimeout(50);

    pinMode(BUZZER, OUTPUT);
    pinMode(GREEN_LED, OUTPUT);
    pinMode(RED_LED, OUTPUT);
    pinMode(TFT_BLK, OUTPUT);

    pinMode(DEMO_BUTTON, INPUT_PULLUP);

    // Rotary encoder.
    pinMode(ENCODER_CLK, INPUT_PULLUP);
    pinMode(ENCODER_DT, INPUT_PULLUP);
    pinMode(ENCODER_SW, INPUT_PULLUP);

    lastCLKState = digitalRead(ENCODER_CLK);

    digitalWrite(TFT_BLK, HIGH);

    setLeds(false, false);
    stopBuzzer();

    // Initialize TFT.
    SPI.begin(
        TFT_SCK,
        -1,
        TFT_MOSI,
        TFT_CS
    );

    tft.init(240, 280);
    tft.setRotation(0);
    tft.setTextWrap(false);

    // Display startup test.
    tft.fillScreen(RED);
    delay(250);

    tft.fillScreen(GREEN);
    delay(250);

    tft.fillScreen(BLUE);
    delay(250);

    tft.fillScreen(BLACK);

    centeredText("STARTING...", 110, 3, WHITE);

    // Connect to hotspot.
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    Serial.print("Connecting to WiFi");

    unsigned long started = millis();

    while (
        WiFi.status() != WL_CONNECTED &&
        millis() - started < 15000
    ) {
        delay(300);
        Serial.print(".");
    }

    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("WiFi connected.");

        Serial.print("ESP32 IP address: ");
        Serial.println(WiFi.localIP());

        // Reset Flask state on ESP32 startup.
        notifyServerReset();
    } else {
        Serial.println("WiFi connection failed.");
    }

    showIdle();

    Serial.println("==============================");
    Serial.println("Security display initialized.");
    Serial.println("Rotary encoder initialized.");
    Serial.println("Dynamic task list enabled.");
    Serial.println("SEAA task analysis enabled.");
    Serial.println("Welcome screen duration: 8 seconds.");
    Serial.println("Waiting for Flask server...");
    Serial.println("==============================");
}

// ==================================================
// 30. MAIN LOOP
// ==================================================

void loop() {
    pollServer();

    checkDemoButton();

    updateScreen();

    if (accessLatched && personalTaskAccess) {
        handleEncoder();
    }

    delay(5);
}