#include "network.h"

#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_wifi.h>

#include "config.h"
#include "state.h"

static WiFiClient wifi_client;
static PubSubClient mqtt_client(wifi_client);

Network network = {};

String mac2String(uint8_t ar[]) {
  String s;
  for (uint8_t i = 0; i < 6; ++i) {
    char buf[3];
    sprintf(buf, "%02X", ar[i]);  // J-M-L: slight modification, added the 0 in
                                  // the format for padding
    s += buf;
    if (i < 5) s += ':';
  }
  return s;
}

void Network::connect() {
  cmd_state = CMDState::CONNECT;
  this->erase = false;
  log_d("[NET] cmd connect");
}

void Network::disconnect(bool erase) {
  cmd_state = CMDState::DISCONNECT;
  this->erase = erase;
  log_d("[NET] cmd disconnect");
}

void Network::update() {
  mqtt_client.loop();

  if (sm_state != prev_sm_state) {
    prev_sm_state = sm_state;
    log_d("[NET] state machine changed to: %d", sm_state);
  }

  switch (sm_state) {
    case IDLE:  // wait for cmd connect
      if (cmd_state == CMDState::CONNECT) {
        cmd_state = CMDState::NONE;
        mqtt_client.setServer(device_state.network().mqtt.server.c_str(),
                              device_state.network().mqtt.port);
        mqtt_client.setBufferSize(MQTT_BUFFER_SIZE);
        mqtt_client.setCallback(
            std::bind(&Network::callback, this, std::placeholders::_1,
                      std::placeholders::_2, std::placeholders::_3));
        WiFi.mode(WIFI_STA);
        WiFi.persistent(true);
        wifi_start_time = millis();
        if (device_state.wifi_quick_connect) {
          log_i("[NET] connecting Wi-Fi (quick mode)...");
          WiFi.begin();
          sm_state = AWAIT_QUICK_WIFI_CONNECTION;
        } else {
          sm_state = BEGIN_WIFI_NORMAL_CONNECTION;
        }
      }
      break;
    case AWAIT_QUICK_WIFI_CONNECTION:
      if (cmd_state == CMDState::DISCONNECT) {
        cmd_state = CMDState::NONE;
        log_i("[NET] disconnecting...");
        WiFi.disconnect(true, erase);
        state = State::DISCONNECTED;
        sm_state = IDLE;
        log_i("[NET] disconnected.");
      } else if (WiFi.status() == WL_CONNECTED) {
        state = State::W_CONNECTED;
        device_state.set_ip(WiFi.localIP().toString());
        log_i("[NET] Wi-Fi connected (quick mode).");
        String ssid = WiFi.SSID();
        device_state.save_all();
        uint8_t* bssid = WiFi.BSSID();
        int32_t ch = WiFi.channel();
        log_i("[NET] SSID: %s, BSSID: %s, CH: %d", ssid.c_str(),
              mac2String(bssid).c_str(), ch);
        // proceed with MQTT connection
        log_i("[NET] connecting MQTT....");
        connect_mqtt();
        mqtt_start_time = millis();
        sm_state = AWAIT_MQTT_CONNECTION;
      } else if (millis() - wifi_start_time > QUICK_WIFI_TIMEOUT) {
        // try again with normal mode
        log_i(
            "[NET] Wi-Fi connect failed (quick mode). Retrying with normal "
            "mode...");
        device_state.wifi_quick_connect = false;
        WiFi.disconnect();
        delay(500);
        sm_state = BEGIN_WIFI_NORMAL_CONNECTION;
      }
      break;
    case BEGIN_WIFI_NORMAL_CONNECTION: {
      WiFi.begin();
      delay(500);
      // get ssid from esp32 saved config
      wifi_config_t conf;
      esp_wifi_get_config(WIFI_IF_STA, &conf);
      String ssid = reinterpret_cast<const char*>(conf.sta.ssid);
      String psk = WiFi.psk();
      log_i("[NET] connecting Wi-Fi (normal mode): SSID: %s", ssid.c_str());
      WiFi.begin(ssid.c_str(), psk.c_str());
      wifi_start_time = millis();
      sm_state = AWAIT_NORMAL_WIFI_CONNECTION;
      break;
    }
    case AWAIT_NORMAL_WIFI_CONNECTION:
      if (cmd_state == CMDState::DISCONNECT) {
        cmd_state = CMDState::NONE;
        log_i("[NET] disconnecting...");
        WiFi.disconnect(true, erase);
        state = State::DISCONNECTED;
        sm_state = IDLE;
        log_i("[NET] disconnected.");
      } else if (WiFi.status() == WL_CONNECTED) {
        // get bssid and ch, disconnect, reconnect with bssid and ch to save it
        // to ESP
        log_i(
            "[NET] Wi-Fi connected (normal mode). Saving settings for "
            "quick mode...");
        String ssid = WiFi.SSID();
        String psk = WiFi.psk();
        device_state.save_all();
        uint8_t* bssid = WiFi.BSSID();
        int32_t ch = WiFi.channel();
        log_i("[NET] SSID: %s, BSSID: %s, CH: %d", ssid.c_str(),
              mac2String(bssid).c_str(), ch);
        WiFi.disconnect();
        WiFi.begin(ssid.c_str(), psk.c_str(), ch, bssid, true);
        wifi_start_time = millis();
        sm_state = AWAIT_CONFIRM_QUICK_WIFI_SETTINGS;
      } else if (millis() - wifi_start_time >= WIFI_TIMEOUT) {
        log_w("[NET] Wi-Fi connect failed (normal mode). Retrying...");
        WiFi.disconnect(true);
        delay(500);
        WiFi.mode(WIFI_STA);
        WiFi.persistent(true);
        sm_state = BEGIN_WIFI_NORMAL_CONNECTION;
      }
      break;
    case AWAIT_CONFIRM_QUICK_WIFI_SETTINGS:
      if (cmd_state == CMDState::DISCONNECT) {
        cmd_state = CMDState::NONE;
        log_i("[NET] disconnecting...");
        WiFi.disconnect(true, erase);
        state = State::DISCONNECTED;
        sm_state = IDLE;
        log_i("[NET] disconnected.");
      } else if (WiFi.status() == WL_CONNECTED) {
        device_state.wifi_quick_connect = true;
        state = State::W_CONNECTED;
        device_state.set_ip(WiFi.localIP().toString());
        log_i("[NET] Wi-Fi connected, quick mode settings saved.");
        // proceed with MQTT connection
        log_i("[NET] connecting MQTT....");
        connect_mqtt();
        mqtt_start_time = millis();
        sm_state = AWAIT_MQTT_CONNECTION;
      } else if (millis() - wifi_start_time > WIFI_TIMEOUT) {
        device_state.wifi_quick_connect = false;
        log_i("[NET] Wi-Fi quick mode save settings failed. Retrying...");
        sm_state = BEGIN_WIFI_NORMAL_CONNECTION;
      }
      break;
    case AWAIT_MQTT_CONNECTION:
      if (cmd_state == CMDState::DISCONNECT) {
        cmd_state = CMDState::NONE;
        log_i("[NET] disconnecting...");
        WiFi.disconnect(true, erase);
        WiFi.mode(WIFI_OFF);
        state = State::DISCONNECTED;
        sm_state = IDLE;
        log_i("[NET] disconnected.");
      } else if (mqtt_client.connected()) {
        log_i("[NET] MQTT connected.");
        last_conn_check_time = millis();
        if (on_connect != NULL) {
          on_connect();
        }
        state = State::M_CONNECTED;
        sm_state = CONNECTED;
      } else if (millis() - mqtt_start_time > MQTT_TIMEOUT) {
        if (WiFi.status() == WL_CONNECTED) {
          state = State::W_CONNECTED;
          log_w("[NET] MQTT connect failed. Retrying...");
          connect_mqtt();
          mqtt_start_time = millis();
          sm_state = AWAIT_MQTT_CONNECTION;
        } else {
          state = State::DISCONNECTED;
          log_w(
              "[NET] MQTT connect failed. Wi-Fi not connected. Retrying "
              "Wi-Fi...");
          WiFi.disconnect(true);
          delay(500);
          WiFi.mode(WIFI_STA);
          WiFi.persistent(true);
          sm_state = BEGIN_WIFI_NORMAL_CONNECTION;
        }
      }
      break;
    case CONNECTED:
      if (cmd_state == CMDState::DISCONNECT) {
        cmd_state = CMDState::NONE;
        log_i("[NET] disconnecting...");
        disconnect_start_time = millis();
        sm_state = AWAIT_DISCONNECTING_TIMEOUT;
      } else if (millis() - last_conn_check_time > NET_CONN_CHECK_INTERVAL) {
        if (WiFi.status() != WL_CONNECTED) {
          state = State::DISCONNECTED;
          log_w("[NET] Wi-Fi connection interrupted. Reconnecting...");
          WiFi.disconnect(true);
          delay(500);
          WiFi.mode(WIFI_STA);
          WiFi.persistent(true);
          sm_state = BEGIN_WIFI_NORMAL_CONNECTION;
        } else if (!mqtt_client.connected()) {
          state = State::W_CONNECTED;
          log_w("[NET] MQTT connection interrupted. Reconnecting...");
          connect_mqtt();
          mqtt_start_time = millis();
          sm_state = AWAIT_MQTT_CONNECTION;
        }
        last_conn_check_time = millis();
      }
      break;
    case AWAIT_DISCONNECTING_TIMEOUT:
      if (millis() - disconnect_start_time > MQTT_DISCONNECT_TIMEOUT) {
        mqtt_client.disconnect();
        wifi_client.flush();
        sm_state = DISCONNECT;
      }
      break;
    case DISCONNECT:
      if (mqtt_client.state() == -1) {
        WiFi.disconnect(true, erase);
        delay(100);
        state = State::DISCONNECTED;
        log_i("[NET] disconnected.");
        sm_state = IDLE;
      }
      break;
  }
}

Network::State Network::get_state() { return state; }

bool Network::publish(const char* topic, const char* payload, bool retained) {
  bool ret;
  if (retained) {
    ret = mqtt_client.publish(topic, payload, true);
  } else {
    ret = mqtt_client.publish(topic, payload);
    delay(10);
  }
  if (ret) {
    log_d("[NET] pub to: %s SUCCESS.", topic);
  } else {
    log_w("[NET] pub to: %s FAIL.", topic);
  }
  return ret;
}

bool Network::subscribe(const String& topic) {
  if (topic.length() <= 0) {
    log_w("[NET] sub to empty topic blocked", topic.c_str());
    return false;
  }
  bool ret;
  ret = mqtt_client.subscribe(topic.c_str());
  delay(10);
  if (ret) {
    log_d("[NET] sub to: %s SUCCESS.", topic.c_str());
  } else {
    log_w("[NET] sub to: %s FAIL.", topic.c_str());
  }
  return ret;
}

void Network::set_callback(std::function<void(const String&, const String&)> callback) {
  usr_callback = callback;
}

void Network::set_on_connect(std::function<void()> on_connect) {
  this->on_connect = on_connect;
}

uint32_t Network::get_cmd_connect_time() { return cmd_connect_time; }

bool Network::connect_mqtt() {
  if (device_state.network().mqtt.user.length() > 0 &&
      device_state.network().mqtt.password.length() > 0) {
    return mqtt_client.connect(device_state.factory().unique_id.c_str(),
                               device_state.network().mqtt.user.c_str(),
                               device_state.network().mqtt.password.c_str());
  } else {
    return mqtt_client.connect(device_state.factory().unique_id.c_str());
  }
}

void Network::callback(const char* topic, uint8_t* payload, uint32_t length) {
  char buff[length + 1];
  memcpy(buff, payload, (size_t)length);
  buff[length] = '\0';  // required so it can be converted to String
  String topic_str = topic;
  String payload_str = (char*)buff;
  log_d("[NET] msg on topic: %s, len: %d, payload: %s", topic_str.c_str(),
        length, payload_str.c_str());
  if (length < 1) {
    return;
  }
  if (usr_callback != NULL) {
    usr_callback(topic_str, payload_str);
  }
}
