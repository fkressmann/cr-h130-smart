#include <ArduinoOTA.h>
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <WiFiUdp.h>
#include <credentials.h>

const String commandPrefix = MQTT_PREFIX + String("cmnd/");
const char *deviceName = DEVICE_NAME;

WiFiClient espClient;
PubSubClient client(espClient);

int ampVol = 0;
int ampVolDelay = 15;
boolean ampPower = false;
boolean volRead = false;
String tele = "tele";
#define VU D7
#define VD D8
#define VR D0
#define PR D5
#define PW D2
#define LED D4
#define OC1 D1
#define OC2 D6

void sendData(String subtopic, String data, bool retained) {
  Serial.println("SENDING: " + subtopic + ":" + data);
  subtopic = MQTT_PREFIX + subtopic;
  client.publish(subtopic.c_str(), data.c_str(), retained);
}

void sendData(String subtopic, String data) {
  sendData(subtopic, data, false);
}

void log(String line) {
  sendData("log", line, true);
}

void checkEncoderAngle() {
  if (digitalRead(VR) != volRead) {
    volRead = digitalRead(VR);
    if (volRead) {
      sendData("volume/angle", "BAD");
      digitalWrite(LED, HIGH);
    } else {
      sendData("volume/angle", "OK");
      digitalWrite(LED, LOW);
    }
  }
}

void checkAmpPower() {
  if (digitalRead(PR) == ampPower) {
    ampPower = !digitalRead(PR);
    if (ampPower) {
      sendData("power/state", "ON");
    } else {
      sendData("power/state", "OFF");
    }
  }
}

void ampVolUp(int steps) {
  for (int i = 0; i < steps; i++) {
    digitalWrite(VU, HIGH);
    digitalWrite(LED, LOW);
    delay(ampVolDelay);
    digitalWrite(VD, HIGH);
    delay(ampVolDelay);
    digitalWrite(VU, LOW);
    delay(ampVolDelay);
    digitalWrite(VD, LOW);
    digitalWrite(LED, HIGH);
    delay(ampVolDelay);
  }
}

void ampVolDown(int steps) {
  for (int i = 0; i < steps; i++) {
    digitalWrite(VD, HIGH);
    digitalWrite(LED, LOW);
    delay(ampVolDelay);
    digitalWrite(VU, HIGH);
    delay(ampVolDelay);
    digitalWrite(VD, LOW);
    delay(ampVolDelay);
    digitalWrite(VU, LOW);
    digitalWrite(LED, HIGH);
    delay(ampVolDelay);
  }
}

void ampVolSync() {
  log("Syncing Volume to 42");
  // Go down more steps than supported by amp, so we get to 0 volume
  ampVolDown(65);
  delay(ampVolDelay);
  ampVolUp(42);
  ampVol = 42;
  sendData("volume/state", String(42));
}

void ampPowerToggle() {
  digitalWrite(PW, HIGH);
  delay(50);
  digitalWrite(PW, LOW);
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    // if you MQTT broker has clientID,username and password
    // please change following line to    if (client.connect(clientId,userName,passWord))
    if (client.connect(deviceName)) {
      Serial.println("connected");
      // once connected to MQTT broker, subscribe command if any
      client.subscribe((commandPrefix + '#').c_str());
      sendData("ip", WiFi.localIP().toString(), true);
      sendData("rssi", String(WiFi.RSSI()), true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 6 seconds before retrying
      delay(6000);
    }
  }
} // end reconnect()

void callback(char *topic, byte *payload, unsigned int length) {
  String plString = String((char *)payload);
  String tpString = String(topic);

  // Amp Power
  if (tpString.equals(commandPrefix + "power")) {
    if (plString.equals("ON")) {
      if (!ampPower) {
        ampPowerToggle();
      }
    } else {
      if (ampPower) {
        ampPowerToggle();
      }
    }
  }

  // Amp Volume
  if (tpString.equals(commandPrefix + "volume")) {

    if (plString.equals("SYNC")) {
      if (volRead) {
        ampVolSync();
      } else {
        log("Can't sync, Vol Read not 1-1");
      }
    } else {
      int vol = plString.toInt();

      log("Setting Volume to: " + String(vol));
      if (vol >= 0 && vol <= 63 && ampPower) {
        if (vol < ampVol) {
          ampVolDown(ampVol - vol);
        } else {
          ampVolUp(vol - ampVol);
        }
        ampVol = vol;
      }
      sendData("volume/state", String(ampVol));
    }
  }

  // open collector output 1, supports PWM
  if (tpString.equals(commandPrefix + "oc1")) {
    if (plString.equals("ON")) {
      digitalWrite(OC1, HIGH);
    } else if (plString.equals("OFF")) {
      digitalWrite(OC1, LOW);
    } else {
      int duty = plString.toInt();
      analogWrite(OC1, duty * 10.23);
    }
  }

  // open collector output 1, supports PWM
  if (tpString.equals(commandPrefix + "oc2")) {
    if (plString.equals("ON")) {
      digitalWrite(OC2, HIGH);
    } else if (plString.equals("OFF")) {
      digitalWrite(OC2, LOW);
    } else {
      int duty = plString.toInt();
      analogWrite(OC2, duty * 10.23);
    }
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(VU, OUTPUT);
  pinMode(VD, OUTPUT);
  pinMode(VR, INPUT);
  pinMode(PR, INPUT);
  pinMode(PW, OUTPUT);
  pinMode(LED, OUTPUT);
  pinMode(OC1, OUTPUT);
  pinMode(OC2, OUTPUT);
  WiFi.mode(WIFI_STA);
  WiFi.hostname(deviceName);
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  client.setServer(MQTT_SERVER, 1883);
  client.setCallback(callback);
  reconnect();

  ArduinoOTA.setHostname(deviceName);

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  sendData(tele, "UP", true);

  checkAmpPower();
  if (ampPower) {
    delay(1000);
    ampVolSync();
  }
}

void loop() {
  ArduinoOTA.handle();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  checkEncoderAngle();
  checkAmpPower();
}
