#include <WiFi.h>
#include <HTTPClient.h>
#include <Keypad.h>


// ============================================
// FAMILYTREE - KEYPAD CONTROLLER
// ============================================


// Copy secrets.example.h to secrets.h and configure it locally.
#include "secrets.h"

const char* KEYPAD_URL = "/api/keypad";
const char* PIN_RESULT_URL = "/api/pin-result";

// Prototype-only PIN, checked locally (not secure authentication).
const int PIN_LENGTH = 4;
String enteredPIN = "";


// Keypad configuration
const byte ROWS = 4;
const byte COLS = 4;


char keys[ROWS][COLS] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};


byte rowPins[ROWS] = {21, 22, 19, 18};
byte colPins[COLS] = {13, 14, 25, 26};


Keypad keypad = Keypad(
  makeKeymap(keys), rowPins, colPins, ROWS, COLS
);


unsigned long lastWiFiAttempt = 0;
unsigned long eventID = 0;
const unsigned long WIFI_RETRY_INTERVAL = 5000;


// ============================================
// WIFI
// ============================================


void startWiFi() {
  Serial.println("Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastWiFiAttempt = millis();
}


void maintainWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;


  if (millis() - lastWiFiAttempt >= WIFI_RETRY_INTERVAL) {
    Serial.println("WiFi disconnected; reconnecting...");
    WiFi.disconnect();
    startWiFi();
  }
}


// ============================================
// POST JSON TO FLASK
// ============================================


bool postJSON(const char* path, const String& payload) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("ERROR: WiFi not connected.");
    return false;
  }


  WiFiClient client;
  HTTPClient http;
  http.setTimeout(2000);


  String url = String(SERVER_BASE) + path;


  if (!http.begin(client, url)) {
    Serial.println("ERROR: Could not start HTTP request.");
    return false;
  }


  http.addHeader("Content-Type", "application/json");


  int code = http.POST(payload);


  if (code >= 200 && code < 300) {
    Serial.print("Server accepted request: ");
    Serial.println(http.getString());
    http.end();
    return true;
  }


  Serial.print("HTTP request failed. Code: ");
  Serial.println(code);


  if (code > 0) {
    Serial.println(http.getString());
  }


  http.end();
  return false;
}


// ============================================
// SEND KEYPRESS EVENT
// ============================================


void sendKeyToServer(char key) {
  eventID++;


  String payload = "{\"device\":\"keypad\",\"key\":\"";
  payload += key;
  payload += "\",\"event_id\":";
  payload += String(eventID);
  payload += "}";


  postJSON(KEYPAD_URL, payload);
}


// ============================================
// SEND PIN RESULT
// ============================================


void sendPINResult(const char* result) {
  String payload = "{\"result\":\"";
  payload += result;
  payload += "\"}";


  Serial.print("Sending PIN result: ");
  Serial.println(result);


  if (!postJSON(PIN_RESULT_URL, payload)) {
    Serial.println("WARNING: PIN result was not delivered.");
  }
}


// ============================================
// CHECK PIN LOCALLY
// ============================================


void handlePIN(char key) {
  if (key == '*') {
    enteredPIN = "";
    Serial.println("PIN cleared.");
    return;
  }


  if (key == '#') {
    if (enteredPIN.length() != PIN_LENGTH) {
      Serial.println("PIN must contain 4 digits.");
      enteredPIN = "";
      return;
    }


    if (enteredPIN == CORRECT_PIN) {
      Serial.println("ACCESS GRANTED");
      sendPINResult("ACCESS_GRANTED");
    } else {
      Serial.println("ACCESS DENIED");
      sendPINResult("ACCESS_DENIED");
    }


    enteredPIN = "";
    Serial.println("Enter PIN:");
    return;
  }


  if (key >= '0' && key <= '9') {
    if (enteredPIN.length() < PIN_LENGTH) {
      enteredPIN += key;
      Serial.print("PIN digits entered: ");
      Serial.println(enteredPIN.length());
    } else {
      Serial.println("PIN is full. Press # or *.");
    }
    return;
  }


  Serial.println("Use digits, * to clear, or # to submit.");
}


// ============================================
// SETUP
// ============================================


void setup() {
  Serial.begin(115200);
  delay(500);


  Serial.println();
  Serial.println("=========================");
  Serial.println(" FAMILYTREE KEYPAD");
  Serial.println("=========================");


  keypad.setDebounceTime(35);
  keypad.setHoldTime(500);


  startWiFi();
}


// ============================================
// LOOP
// ============================================


void loop() {
  maintainWiFi();


  char key = keypad.getKey();


  if (key != NO_KEY) {
    Serial.print("Button pressed: ");
    Serial.println(key);


    // Send every keypress to Flask.
    sendKeyToServer(key);


    // Check the PIN locally.
    handlePIN(key);
  }


  delay(5);
}