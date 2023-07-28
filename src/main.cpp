#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "time.h"
#include <NostrEvent.h>
#include <NostrRelayManager.h>
#include <NostrRequestOptions.h>
#include <Wire.h>
#include "Bitcoin.h"
#include "Hash.h"
#include <esp_random.h>
#include "QRCode.h"
#include <math.h>
#include <SPIFFS.h>
#include <vector>

#include "wManager.h"

#include <ArduinoJson.h>

// freertos
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define PARAM_FILE "/elements.json"

int triggerAp = false;

int ledPin = 13; // Pin number where the LED is connected
extern int buttonPin; // Pin number where the button is connected
int minFlashDelay = 100; // Minimum delay between flashes (in milliseconds)
int maxFlashDelay = 5000; // Maximum delay between flashes (in milliseconds)
int lightBrightness = 50; // The brightness of the LED (0-255)

// create a vector for storing zap amount for the flash queue
std::vector<int> zapAmountsFlashQueue;

NostrEvent nostr;
NostrRelayManager nostrRelayManager;
NostrQueueProcessor nostrQueue;

String serialisedEventRequest;

extern bool hasInternetConnection;

NostrRequestOptions* eventRequestOptions;

bool hasSentEvent = false;

String apPassword = "ToTheMoon1"; //default WiFi AP password
extern char npubHexString[80];
extern char relayString[80];

fs::SPIFFSFS &FlashFS = SPIFFS;
#define FORMAT_ON_FAIL true

// define funcs
void configureAccessPoint();
void initWiFi();
bool whileCP(void);
void changeBrightness();
void signalWithLightning(int numFlashes, int duration);
void flashLightning(int zapAmountSats);
void doLightningFlash(int numberOfFlashes);
void fadeOutFlash(int intensity);
void initLamp();
unsigned long getUnixTimestamp();
void zapReceiptEvent(const std::string& key, const char* payload);
void okEvent(const std::string& key, const char* payload);
void nip01Event(const std::string& key, const char* payload);
uint8_t getRandomNum(uint8_t min, uint8_t max);
void relayConnectedEvent(const std::string& key, const std::string& message);
void loadSettings();
int64_t getAmountInSatoshis(const String &input);
String getBolt11InvoiceFromEvent(String jsonStr);
void createZapEventRequest();
void connectToNostrRelays();

#define BUTTON_PIN 0 // change this to the pin your button is connected to
#define DOUBLE_TAP_DELAY 250 // delay for double tap in milliseconds

volatile unsigned long lastButtonPress = 0;
volatile bool doubleTapDetected = false;

void IRAM_ATTR handleButtonInterrupt() {
  unsigned long now = millis();
  if (now - lastButtonPress < DOUBLE_TAP_DELAY) {
    doubleTapDetected = true;
  }
  lastButtonPress = now;
}


//free rtos task for lamp control
void lampControlTask(void *pvParameters) {
  Serial.println("Starting lamp control task");
  attachInterrupt(digitalPinToInterrupt(BUTTON_PIN), handleButtonInterrupt, FALLING);

  for(;;) {
    Serial.println("In the lamp freertos task loop");
    Serial.println("Has internet connection: " + String(hasInternetConnection));

    if(!hasInternetConnection) {
    // slow fade pulse of LED
    for (int i = 100; i < 255; i++) {
      analogWrite(ledPin, i); // set the LED to the desired intensity
      delay(10);  // wait for a moment
    }
    // now fade out
    for (int i = 255; i >= 100; i--) {
      analogWrite(ledPin, i); // set the LED bright ness
      delay(10);  // wait for a moment
    }
  }

    // detect double tap on button 
    if (doubleTapDetected) {
      Serial.println("Double tap detected. REstarting");
      // restart device
      ESP.restart();
    }

    // watch for button press and call changeBrightness
    if (digitalRead(buttonPin) == LOW) {
      Serial.println("Button pressed. Changing brightness");
      changeBrightness();
    }

    // watch for lamp state and do as needed
    if (zapAmountsFlashQueue.size() > 0) {
      Serial.println("Zap amount in queue " + String(zapAmountsFlashQueue[0]));
      int zapAmount = zapAmountsFlashQueue[0];
      zapAmountsFlashQueue.erase(zapAmountsFlashQueue.begin());
      doLightningFlash(zapAmount);
      vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

/**
 * @brief Create a Zap Event Request object
 * 
 */
void createZapEventRequest() {
  // Create the REQ
  eventRequestOptions = new NostrRequestOptions();
  // Populate kinds
  int kinds[] = {9735};
  eventRequestOptions->kinds = kinds;
  eventRequestOptions->kinds_count = sizeof(kinds) / sizeof(kinds[0]);

  // // Populate #p
  if(npubHexString != "") {
    String* pubkeys = new String[1];  // Allocate memory dynamically
    pubkeys[0] = npubHexString;
    eventRequestOptions->p = pubkeys;
    eventRequestOptions->p_count = 1;
  }

  eventRequestOptions->limit = 0;

  // We store this here for sending this request again if a socket reconnects
  serialisedEventRequest = "[\"REQ\", \"" + nostrRelayManager.getNewSubscriptionId() + "\"," + eventRequestOptions->toJson() + "]";

  delete eventRequestOptions;
}

/**
 * @brief Connect to the Nostr relays
 * 
 */
void connectToNostrRelays() {
  Serial.println("Requesting Zap notifications");

      const char *const relays[] = {
          relayString,
          "nostr.wine",
      };
      int relayCount = sizeof(relays) / sizeof(relays[0]);
      
      nostr.setLogging(false);
      nostrRelayManager.setRelays(relays, relayCount);
      nostrRelayManager.setMinRelaysAndTimeout(1,10000);

      // Set some event specific callbacks here
      nostrRelayManager.setEventCallback("ok", okEvent);
      nostrRelayManager.setEventCallback("connected", relayConnectedEvent);
      nostrRelayManager.setEventCallback(9735, zapReceiptEvent);

      nostrRelayManager.connect();
}



void initWiFi() {
  

  // WiFi.onEvent(WiFiEvent);

  // configureAccessPoint();
    
  // // WiFi.mode(WIFI_STA);
  // // while (WiFi.status() != WL_CONNECTED) {
  // //   delay(1000);
  // // }

  // signalWithLightning(3,100);
  
}

/**
 * @brief While captive Portal callback
 * 
 * @return true 
 * @return false 
 */
bool whileCP(void) {
  bool rc;
  // light the led full brightness
  analogWrite(ledPin, 255);
  rc = true;
  return rc;
}

/**
 * @brief Configure the WiFi AP
 * 
 */
void configureAccessPoint() {
  
}

bool adjustLightingBrightnessUp = true;
/**
 * @brief change lamp brightness
 * 
 */
void changeBrightness() {
  // use lastLightingAdjustmentWasUp and max value of 255 to decide whether to adjust up or down
  if (adjustLightingBrightnessUp) {
    lightBrightness = lightBrightness + 5;
    if (lightBrightness >= 255) {
      lightBrightness = 255;
      adjustLightingBrightnessUp = false;
      delay(500); // pause to let the user take their finger off the button
    }
  } else {
    lightBrightness = lightBrightness - 5;
    if (lightBrightness <= 0) {
      lightBrightness = 0;
      adjustLightingBrightnessUp = true;
      delay(500); // pause to let the user take their finger off the button
    }
  }
  
  // write to spiffs
  File file = SPIFFS.open("/brightness.txt", FILE_WRITE);
  if(!file){
    Serial.println("Failed to open file for writing");
    return;
  }
  file.println(lightBrightness);
  file.close();

  analogWrite(ledPin, lightBrightness);
  // delay(250);
}

void fadeOutFlash(int intensity) {
  analogWrite(ledPin, intensity); // set the LED to the desired intensity

  delay(50); // delay to give the LED time to light up

  // fast fade-out
  for (int i = intensity; i >= 0; i--) {
    analogWrite(ledPin, i); // set the LED brightness
    delay(1);  // wait for a moment
  }

  delay(50);
}

void doLightningFlash(int numberOfFlashes) {

  fadeOutFlash(5);
  fadeOutFlash(10);
  fadeOutFlash(15);

  delay(100);

  for(int flash = 1; flash <= numberOfFlashes; flash++) {
    // turn the LED on
    digitalWrite(ledPin, HIGH);

    // wait for the specified time, longer for the first flash and shorter for subsequent flashes
    int flashDuration = 250 / flash * random(1,5);
    delay(flashDuration / 2);

    // fast fade-out
    for (int i = 255; i >= lightBrightness; i = i - 2) {
      analogWrite(ledPin, i);  // set the LED brightness
      delay(1);  // wait for a moment
    }
  }

  delay(50);

  // fadeOutFlash(15);
  // fadeOutFlash(5);

  delay(50);

  // set led to brightness
  analogWrite(ledPin, lightBrightness);
}

void flashLightning(int zapAmountSats) {
  int flashCount = 1;
  // set flash count length of the number in the zap amount
  if (zapAmountSats > 0) {
    flashCount = floor(log10(zapAmountSats)) + 1;
  }
  Serial.println("Zap amount: " + String(zapAmountSats) + " sats");
  Serial.println("Flashing " + String(flashCount) + " times");

  // push to the flash queue
  zapAmountsFlashQueue.push_back(zapAmountSats);
}

/**
 * @brief Flash the LED a random number of times with a random delay between flashes
 * 
 * @param numFlashes 
 */
void signalWithLightning(int numFlashes, int duration = 500) {
  for (int i = 0; i < numFlashes; i++) {
    digitalWrite(ledPin, HIGH);
    delay(duration);

    digitalWrite(ledPin, LOW);
    delay(duration);
  }
}


unsigned long getUnixTimestamp() {
  time_t now;
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)){
    Serial.println("Failed to obtain time");
    return 0;
  } else {
    Serial.println("Got timestamp of " + String(now));
  }
  time(&now);
  return now;
}

String lastPayload = "";

void relayConnectedEvent(const std::string& key, const std::string& message) {
  Serial.println("Relay connected: ");
  
  Serial.print(F("Requesting events:"));
  Serial.println(serialisedEventRequest);

  nostrRelayManager.broadcastEvent(serialisedEventRequest);
}

void okEvent(const std::string& key, const char* payload) {
    if(lastPayload != payload) { // Prevent duplicate events from multiple relays triggering the same logic
      lastPayload = payload;
      Serial.println("payload is: ");
      Serial.println(payload);
    }
}

void nip01Event(const std::string& key, const char* payload) {
    if(lastPayload != payload) { // Prevent duplicate events from multiple relays triggering the same logic
      lastPayload = payload;
      // We can convert the payload to a StaticJsonDocument here and get the content
      StaticJsonDocument<1024> eventJson;
      deserializeJson(eventJson, payload);
      String pubkey = eventJson[2]["pubkey"].as<String>();
      String content = eventJson[2]["content"].as<String>();
      Serial.println(pubkey + ": " + content);
    }
}

String getBolt11InvoiceFromEvent(String jsonStr) {
  // Remove all JSON formatting characters
  String str = jsonStr.substring(1, jsonStr.length()-1); // remove the first and last square brackets
  str.replace("\\", ""); // remove all backslashes

  // Search for the "bolt11" substring
  int index = str.indexOf("bolt11");

  // Extract the value associated with "bolt11"
  String bolt11 = "";
  if (index != -1) {
    int start = index + 9; // the value of "bolt11" starts 9 characters after the substring index
    int end = start; // initialize the end index
    while (str.charAt(end) != '\"') {
      end++; // increment the end index until the closing double-quote is found
    }
    bolt11 = str.substring(start, end); // extract the value of "bolt11"
  }
  return bolt11;
}

/**
 * @brief Get the Amount In Satoshis from a lightning bol11 invoice
 * 
 * @param input 
 * @return int64_t 
 */
int64_t getAmountInSatoshis(const String &input) {
    int64_t number = -1;
    char multiplier = ' ';

    for (unsigned int i = 0; i < input.length(); ++i) {
        if (isdigit(input[i])) {
            number = 0;
            while (isdigit(input[i])) {
                number = number * 10 + (input[i] - '0');
                ++i;
            }
            for (unsigned int j = i; j < input.length(); ++j) {
                if (isalpha(input[j])) {
                    multiplier = input[j];
                    break;
                }
            }
            break;
        }
    }

    if (number == -1 || multiplier == ' ') {
        return -1;
    }

    int64_t satoshis = number;

    switch (multiplier) {
        case 'm':
            satoshis *= 100000; // 0.001 * 100,000,000
            break;
        case 'u':
            satoshis *= 100; // 0.000001 * 100,000,000
            break;
        case 'n':
            satoshis /= 10; // 0.000000001 * 100,000,000
            break;
        case 'p':
            satoshis /= 10000; // 0.000000000001 * 100,000,000
            break;
        default:
            return -1;
    }

    return satoshis;
}


uint8_t getRandomNum(uint8_t min, uint8_t max) {
  uint8_t rand  = (esp_random() % (max - min + 1)) + min;
  Serial.println("Random number: " + String(rand));
  return rand;
}

void zapReceiptEvent(const std::string& key, const char* payload) {
  Serial.println("Zap receipt event");
    if(lastPayload != payload) { // Prevent duplicate events from multiple relays triggering the same logic, as we are using multiple relays, this is likely to happen
      lastPayload = payload;
      String bolt11 = getBolt11InvoiceFromEvent(payload);
      Serial.println("BOLT11: " + bolt11);
      uint64_t amountInSatoshis = getAmountInSatoshis(bolt11);
      Serial.println("Zapped! " + String(amountInSatoshis));
      flashLightning(amountInSatoshis);
    }
}

void initLamp() {
  // Set the LED pin as OUTPUT
  pinMode(ledPin, OUTPUT);

  // get brightness value from spiffs brightness.txt
  File file = SPIFFS.open("/brightness.txt");
  if(!file){
    Serial.println("Failed to open file for reading");
    return;
  }
  String brightnessStr = file.readStringUntil('\n');
  file.close();
  lightBrightness = brightnessStr.toInt();
}

void setup() {
  buttonPin = 4;
  Serial.begin(115200);

  // randomSeed(analogRead(0)); // Seed the random number generator

  FlashFS.begin(FORMAT_ON_FAIL);
  // init spiffs
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }

  initLamp();

  delay(2000);
  signalWithLightning(2,250);

  // Set the button pin as INPUT
  pinMode(buttonPin, INPUT_PULLUP);

    // start lamp control task
  xTaskCreatePinnedToCore(
    lampControlTask,   /* Task function. */
    "lampControlTask",     /* String with name of task. */
    5000,            /* Stack size in bytes. */
    NULL,             /* Parameter passed as input of the task */
    2,                /* Priority of the task. */
    NULL,             /* Task handle. */
    1);               /* Core where the task should run */

   init_WifiManager();

   if(hasInternetConnection) {
    Serial.println("Has internet connection. Connectring to relays");
    connectToNostrRelays();
   }

  // Set the LED to the desired intensity
  analogWrite(ledPin, 20);

  createZapEventRequest();
}

void loop() {
  nostrRelayManager.loop();
}