#ifndef HOMEBUTTONS_NETWORK_H
#define HOMEBUTTONS_NETWORK_H

#include <Arduino.h>


class Network {

  public:
    enum class State {
      DISCONNECTED,
      W_CONNECTED,
      M_CONNECTED,
    };

    enum class CMDState {
      NONE,
      CONNECT,
      DISCONNECT
    };

    void connect();
    void disconnect(bool erase = false);
    void update();

    State get_state();

    bool publish(const char* topic, const char* payload, bool retained = false);
    bool subscribe(const String& topic);
    void set_callback(std::function<void(const String&, const String&)> callback);
    void set_on_connect(std::function<void()> on_connect);

    uint32_t get_cmd_connect_time();
  
  private:
    State state = State::DISCONNECTED;
    CMDState cmd_state = CMDState::NONE;

    uint32_t wifi_start_time = 0;
    uint32_t mqtt_start_time = 0;
    uint32_t last_conn_check_time = 0;
    uint32_t disconnect_start_time = 0;
    uint32_t cmd_connect_time = 0;

    bool erase = false;

    enum StateMachineState {
      NONE,
      IDLE,
      AWAIT_QUICK_WIFI_CONNECTION,
      BEGIN_WIFI_NORMAL_CONNECTION,
      AWAIT_NORMAL_WIFI_CONNECTION,
      AWAIT_CONFIRM_QUICK_WIFI_SETTINGS,
      AWAIT_MQTT_CONNECTION,
      CONNECTED,
      AWAIT_DISCONNECTING_TIMEOUT,
      DISCONNECT
    };

    StateMachineState sm_state = IDLE;
    StateMachineState prev_sm_state = IDLE;

    std::function<void(const String&, const String&)> usr_callback = NULL;
    std::function<void()> on_connect = NULL;

    bool connect_mqtt();
    void callback(const char* topic, uint8_t* payload, uint32_t length);
};

extern Network network;

#endif // HOMEBUTTONS_NETWORK_H
