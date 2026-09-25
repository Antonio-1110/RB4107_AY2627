#include <WiFi.h>
#include <PubSubClient.h>

// =====================================================
// CONFIGURATION
// =====================================================

// Your Wi-Fi
const char* WIFI_SSID = "SINGTEL-088A";
const char* WIFI_PASSWORD = "BQ6TcgkRsws6Vih";

// Your MacBook's LAN IP
const char* MQTT_BROKER = "192.168.1.127";
const int MQTT_PORT = 1883;

// MQTT topic
const char* MQTT_TOPIC = "aes/kitchen/01/status";


// =====================================================
// MQTT
// =====================================================

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);


// =====================================================
// TIMING
// =====================================================

unsigned long lastPublish = 0;
const unsigned long PUBLISH_INTERVAL = 5000;

unsigned long messageNumber = 0;


// =====================================================
// WIFI CONNECTION
// =====================================================

void connectWiFi() {

    Serial.println();
    Serial.print("Connecting to Wi-Fi: ");
    Serial.println(WIFI_SSID);

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println();
    Serial.println("Wi-Fi connected!");

    Serial.print("ESP32 IP: ");
    Serial.println(WiFi.localIP());

    Serial.print("Gateway: ");
    Serial.println(WiFi.gatewayIP());
}


// =====================================================
// MQTT CONNECTION
// =====================================================

void connectMQTT() {

    while (!mqttClient.connected()) {

        Serial.print("Connecting to MQTT broker at ");
        Serial.print(MQTT_BROKER);
        Serial.print(":");
        Serial.print(MQTT_PORT);
        Serial.println(" ...");

        // Give this ESP32 a unique MQTT client ID
        String clientID = "kitchen-controller-";
        clientID += String((uint32_t)ESP.getEfuseMac(), HEX);

        if (mqttClient.connect(clientID.c_str())) {

            Serial.println("MQTT connected!");

        } else {

            Serial.print("MQTT connection failed. Error code: ");
            Serial.println(mqttClient.state());

            Serial.println("Retrying in 2 seconds...");
            delay(2000);
        }
    }
}


// =====================================================
// FAKE SENSOR DATA
// =====================================================

void publishFakeData() {

    // -------------------------------------------------
    // Simulated values
    // -------------------------------------------------

    bool cookPresent = true;
    bool cookingActive = true;

    // Fake temperature slowly increases
    float temperature =
        120.0 + (messageNumber % 50);


    // -------------------------------------------------
    // Create JSON
    // -------------------------------------------------

    char payload[256];

    snprintf(
        payload,
        sizeof(payload),

        "{"
            "\"station_id\":\"KITCHEN_01\","
            "\"cook_present\":%s,"
            "\"cooking_active\":%s,"
            "\"temperature\":%.1f,"
            "\"message_number\":%lu"
        "}",

        cookPresent ? "true" : "false",
        cookingActive ? "true" : "false",
        temperature,
        messageNumber
    );


    // -------------------------------------------------
    // Publish
    // -------------------------------------------------

    Serial.println();
    Serial.println("Publishing:");
    Serial.println(payload);

    bool success =
        mqttClient.publish(
            MQTT_TOPIC,
            payload
        );

    if (success) {

        Serial.println("Publish successful.");

    } else {

        Serial.println("Publish FAILED.");
    }

    messageNumber++;
}


// =====================================================
// SETUP
// =====================================================

void setup() {

    Serial.begin(115200);

    delay(1000);

    Serial.println();
    Serial.println("==============================");
    Serial.println(" Kitchen Safety MQTT Prototype");
    Serial.println("==============================");

    connectWiFi();

    mqttClient.setServer(
        MQTT_BROKER,
        MQTT_PORT
    );
}


// =====================================================
// LOOP
// =====================================================

void loop() {

    // -------------------------------------------------
    // Maintain Wi-Fi connection
    // -------------------------------------------------

    if (WiFi.status() != WL_CONNECTED) {
        connectWiFi();
    }


    // -------------------------------------------------
    // Maintain MQTT connection
    // -------------------------------------------------

    if (!mqttClient.connected()) {
        connectMQTT();
    }

    mqttClient.loop();


    // -------------------------------------------------
    // Publish every 5 seconds
    // -------------------------------------------------

    unsigned long now = millis();

    if (now - lastPublish >= PUBLISH_INTERVAL) {

        lastPublish = now;

        publishFakeData();
    }
}