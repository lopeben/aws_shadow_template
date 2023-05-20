#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <PubSubClient.h>
#include <ArduinoJson.h> //https://github.com/bblanchon/ArduinoJson (use v6.xx)
#include <time.h>

//Follow instructions from https://github.com/debsahu/ESP-MQTT-AWS-IoT-Core/blob/master/doc/README.md

#include "secrets.h"

#define emptyString String()

#define TXINTERVAL_S  (60)
#define TXINTERVAL_MS (TXINTERVAL_S*1000)

#if !(ARDUINOJSON_VERSION_MAJOR == 6 and ARDUINOJSON_VERSION_MINOR >= 7)
#error "Install ArduinoJson v6.7.0-beta or higher"
#endif

const int  MQTT_PORT = 8883;
const char MQTT_DATA_SUBTOPIC[] = "$aws/things/" THINGNAME "/data/update";
const char MQTT_DATA_PUBTOPIC[] = "$aws/things/" THINGNAME "/data/update";

const char MQTT_SHADOW_UPDATE_PUBTOPIC[] = "$aws/things/" THINGNAME "/shadow/update";
const char MQTT_SHADOW_UPDATE_DOCUMENT_SUBTOPIC[] = "$aws/things/" THINGNAME "/shadow/documents";
const char MQTT_SHADOW_UPDATE_DELTA_SUBTOPIC[] = "$aws/things/" THINGNAME "/shadow/update/delta";
const char MQTT_SHADOW_UPDATE_ACCEPTED_SUBTOPIC[] = "$aws/things/" THINGNAME "/shadow/update/accepted";

#ifdef USE_SUMMER_TIME_DST
uint8_t DST = 1;
#else
uint8_t DST = 0;
#endif


BearSSL::X509List cert(cacert);
BearSSL::X509List client_crt(client_cert);
BearSSL::PrivateKey key(privkey);

WiFiClientSecure net;
PubSubClient client(net);

unsigned long lastMillis = 0;
time_t now;
time_t nowish = 1510592825;

unsigned long previousMillis = 0;
const long interval = 5000;

void ntpConnect(void) {
    
    Serial.print("Setting time using SNTP");
    configTime(TIME_ZONE * 3600, DST * 3600, "pool.ntp.org", "time.nist.gov");
    now = time(nullptr);
    while (now < nowish) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
    }
    Serial.println("done!");
    
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    Serial.print("Current time: ");
    Serial.print(asctime(&timeinfo));
}

void pubSubErr(int8_t MQTTErr) {

    if (MQTTErr == MQTT_CONNECTION_TIMEOUT)
        Serial.print("Connection tiemout");
    else if (MQTTErr == MQTT_CONNECTION_LOST)
        Serial.print("Connection lost");
    else if (MQTTErr == MQTT_CONNECT_FAILED)
        Serial.print("Connect failed");
    else if (MQTTErr == MQTT_DISCONNECTED)
        Serial.print("Disconnected");
    else if (MQTTErr == MQTT_CONNECTED)
        Serial.print("Connected");
    else if (MQTTErr == MQTT_CONNECT_BAD_PROTOCOL)
        Serial.print("Connect bad protocol");
    else if (MQTTErr == MQTT_CONNECT_BAD_CLIENT_ID)
        Serial.print("Connect bad Client-ID");
    else if (MQTTErr == MQTT_CONNECT_UNAVAILABLE)
        Serial.print("Connect unavailable");
    else if (MQTTErr == MQTT_CONNECT_BAD_CREDENTIALS)
        Serial.print("Connect bad credentials");
    else if (MQTTErr == MQTT_CONNECT_UNAUTHORIZED)
        Serial.print("Connect unauthorized");
}


void connectToMqtt(bool nonBlocking = false) {

    Serial.print("MQTT connecting ");
    while (!client.connected()) {
        if (client.connect(THINGNAME)) {
            Serial.println("connected!");
            if (!client.subscribe(MQTT_DATA_SUBTOPIC))
                pubSubErr(client.state());
        } else {
            Serial.print("failed, reason -> ");
            pubSubErr(client.state());
            if (!nonBlocking) {
                Serial.println(" < try again in 5 seconds");
                delay(5000);
            } else {
                Serial.println(" <");
            }
        }
        if (nonBlocking)
            break;
    }
}


void connectToWiFi(String init_str) {

    if (init_str != emptyString)
        Serial.print(init_str);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(1000);
    }
    if (init_str != emptyString)
        Serial.println("ok!");
}


void checkWiFiThenMQTT(void) {

    connectToWiFi("Checking WiFi");
    connectToMqtt();
}


void checkWiFiThenMQTTNonBlocking(void) {

    connectToWiFi(emptyString);
    if (millis() - previousMillis >= interval && !client.connected()) {
        previousMillis = millis();
        connectToMqtt(true);
    }
}


void checkWiFiThenReboot(void) {

    connectToWiFi("Checking WiFi");
    Serial.print("Rebooting");
    ESP.restart();
}


void sendData(bool data) {

    DynamicJsonDocument jsonBuffer(JSON_OBJECT_SIZE(3) + 100);

    JsonObject root = jsonBuffer.to<JsonObject>();
    JsonObject state = root.createNestedObject("state");
    JsonObject state_reported = state.createNestedObject("reported");
    state_reported["value"] = data;
    
    Serial.printf("Sending  [%s]: ", MQTT_DATA_PUBTOPIC);
    serializeJson(root, Serial);
    Serial.println();
    
    char shadow[measureJson(root) + 1];
    serializeJson(root, shadow, sizeof(shadow));

    if (!client.publish(MQTT_DATA_PUBTOPIC, shadow, false))
        pubSubErr(client.state());
}


void createShadowDocumentThenSend(void){
	
    DynamicJsonDocument jsonBuffer(JSON_OBJECT_SIZE(3) + 100);

    JsonObject root = jsonBuffer.to<JsonObject>();
    JsonObject state = root.createNestedObject("state");
	
	JsonObject state_desired = state.createNestedObject("desired");
	state_desired["value"] = false;
	
    JsonObject state_reported = state.createNestedObject("reported");
    state_reported["value"] = false;
	
	//Debug out
    Serial.printf("Sending  [%s]: ", MQTT_DATA_PUBTOPIC);
    serializeJson(root, Serial);
    Serial.println();
    
    char shadow_document[measureJson(root) + 1];
    serializeJson(root, shadow_document, sizeof(shadow_document));

    if (!client.publish(MQTT_SHADOW_UPDATE_PUBTOPIC, shadow_document, false));
        pubSubErr(client.state());
}


void messageReceived(char *topic, byte *payload, unsigned int length) {

    Serial.print("\nReceived [");
    Serial.print(topic);
    Serial.print("]: ");
    for (int i = 0; i < length; i++) {
        Serial.print((char)payload[i]);
    }

    Serial.println();
}


void setup(void)
{
    Serial.begin(115200);
    delay(5000);

    Serial.println();
    Serial.println();
    WiFi.hostname(THINGNAME);
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);

    connectToWiFi(String("Attempting to connect to SSID: ") + String(ssid));

    ntpConnect();

    net.setTrustAnchors(&cert);
    net.setClientRSACert(&client_crt, &key);

    client.setServer(MQTT_HOST, MQTT_PORT);
    client.setCallback(messageReceived);


    connectToMqtt();

    if (!client.subscribe(MQTT_SHADOW_UPDATE_DELTA_SUBTOPIC)) {
        Serial.println("Error subscribing1");
    }

    if (!client.subscribe(MQTT_SHADOW_UPDATE_ACCEPTED_SUBTOPIC)) {
        Serial.println("Error subscribing2");
    }

    createShadowDocumentThenSend();
}


void loop(void)
{
    now = time(nullptr);
    if (!client.connected()) {
        checkWiFiThenMQTT();
        checkWiFiThenMQTTNonBlocking();
        checkWiFiThenReboot();
    } else {
        client.loop();
        if (millis() - lastMillis > TXINTERVAL_MS) {
            lastMillis = millis();
            int raw_sensor_value = analogRead(A0);
            sendData(raw_sensor_value);
    }
  }
}
