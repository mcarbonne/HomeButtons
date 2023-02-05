#include "state.h"
#include "config.h"

State device_state = {};

void State::save_factory() {
  preferences.begin("factory", false);
  preferences.putString("serial_number", m_factory.serial_number);
  preferences.putString("random_id", m_factory.random_id);
  preferences.putString("model_name", m_factory.model_name);
  preferences.putString("model_id", m_factory.model_id);
  preferences.putString("hw_version", m_factory.hw_version);
  preferences.putString("unique_id", m_factory.unique_id);
  preferences.end();
}

void State::load_factory() {
  preferences.begin("factory", true);
  m_factory.serial_number = preferences.getString("serial_number", "");
  m_factory.random_id = preferences.getString("random_id", "");
  m_factory.model_name = preferences.getString("model_name", "");
  m_factory.model_id = preferences.getString("model_id", "");
  m_factory.hw_version = preferences.getString("hw_version", "");
  m_factory.unique_id = preferences.getString("unique_id", "");
  preferences.end();
}

void State::clear_factory() {
  preferences.begin("factory", false);
  preferences.clear();
  preferences.end();
}

void State::save_user() {
  preferences.begin("user", false);
  preferences.putString("device_name", m_personalization.device_name);
  preferences.putString("mqtt_srv", m_network.mqtt.server);
  preferences.putUInt("mqtt_port", m_network.mqtt.port);
  preferences.putString("mqtt_user", m_network.mqtt.user);
  preferences.putString("mqtt_pass", m_network.mqtt.password);
  preferences.putString("base_topic", m_network.mqtt.base_topic);
  preferences.putString("disc_prefix", m_network.mqtt.discovery_prefix);
  preferences.putString("btn1_txt", m_personalization.btn_labels[0]);
  preferences.putString("btn2_txt", m_personalization.btn_labels[1]);
  preferences.putString("btn3_txt", m_personalization.btn_labels[2]);
  preferences.putString("btn4_txt", m_personalization.btn_labels[3]);
  preferences.putString("btn5_txt", m_personalization.btn_labels[4]);
  preferences.putString("btn6_txt", m_personalization.btn_labels[5]);
  preferences.putUInt("sen_itv", m_personalization.sensor_interval);
  preferences.end();
}

void State::load_user() {
  preferences.begin("user", true);
  m_personalization.device_name = preferences.getString("device_name", DEVICE_NAME_DFLT + String(" ") + m_factory.random_id);
  m_network.mqtt.server = preferences.getString("mqtt_srv", "");
  m_network.mqtt.port = preferences.getUInt("mqtt_port", MQTT_PORT_DFLT);
  m_network.mqtt.user = preferences.getString("mqtt_user", "");
  m_network.mqtt.password = preferences.getString("mqtt_pass", "");
  m_network.mqtt.base_topic = preferences.getString("base_topic", BASE_TOPIC_DFLT);
  m_network.mqtt.discovery_prefix = preferences.getString("disc_prefix", DISCOVERY_PREFIX_DFLT);
  set_btn_label(0, preferences.getString("btn1_txt", BTN_1_LABEL_DFLT).c_str());
  set_btn_label(1, preferences.getString("btn2_txt", BTN_2_LABEL_DFLT).c_str());
  set_btn_label(2, preferences.getString("btn3_txt", BTN_3_LABEL_DFLT).c_str());
  set_btn_label(3, preferences.getString("btn4_txt", BTN_4_LABEL_DFLT).c_str());
  set_btn_label(4, preferences.getString("btn5_txt", BTN_5_LABEL_DFLT).c_str());
  set_btn_label(5, preferences.getString("btn6_txt", BTN_6_LABEL_DFLT).c_str());
  m_personalization.sensor_interval = preferences.getUInt("sen_itv", SEN_INTERVAL_DFLT);
  preferences.end();
}

void State::clear_user() {
  preferences.begin("user", false);
  preferences.clear();
  preferences.end();
}

void State::save_persisted() {
  preferences.begin("persisted", false);
  preferences.putBool("lb_mode", low_batt_mode);
  preferences.putBool("wifi_done", wifi_done);
  preferences.putBool("setup_done", setup_done);
  preferences.putString("last_sw", last_sw_ver);
  preferences.putBool("u_awake", user_awake_mode);
  preferences.putBool("wifi_qc", wifi_quick_connect);
  preferences.putBool("chg_cpt_shwn", charge_complete_showing);
  preferences.putBool("info_shwn", info_screen_showing);
  preferences.putBool("chk_conn", check_connection);
  preferences.putUInt("faild_cons", failed_connections);
  preferences.putBool("rst_to_w_stp", restart_to_wifi_setup);
  preferences.putBool("rst_to_stp", restart_to_setup);
  preferences.putBool("send_adisc", send_discovery_config);
  preferences.putBool("silent_rst", silent_restart);
  preferences.end();
}

void State::load_persisted() {
  preferences.begin("persisted", false);
  low_batt_mode = preferences.getBool("lb_mode", false);
  wifi_done = preferences.getBool("wifi_done", false);
  setup_done = preferences.getBool("setup_done", false);
  last_sw_ver = preferences.getString("last_sw", "");
  user_awake_mode = preferences.getBool("u_awake", false);
  wifi_quick_connect = preferences.getBool("wifi_qc", false);
  charge_complete_showing = preferences.getBool("chg_cpt_shwn", false);
  info_screen_showing = preferences.getBool("info_shwn", false);
  check_connection = preferences.getBool("chk_conn", false);
  failed_connections = preferences.getUInt("faild_cons", 0);
  restart_to_wifi_setup = preferences.getBool("rst_to_w_stp", false);
  restart_to_setup = preferences.getBool("rst_to_stp", false);
  send_discovery_config = preferences.getBool("send_adisc", false);
  silent_restart = preferences.getBool("silent_rst", false);
  preferences.end();
}

void State::clear_persisted() {
  preferences.begin("persisted", false);
  preferences.clear();
  preferences.end();
}

void State::clear_persisted_flags() {
  wifi_quick_connect = false;
  charge_complete_showing = false;
  info_screen_showing = false;
  check_connection = false;
  failed_connections = 0;
  restart_to_wifi_setup = false;
  restart_to_setup = false;
  silent_restart = false;
  save_all();
}

void State::save_all() {
  log_d("[PREF] state save all");
  save_user();
  save_persisted();
}

void State::load_all() {
  log_d("[PREF] state load all");
  load_factory();
  load_user();
  load_persisted();
  set_topics();
}

void State::clear_all() {
  log_d("[PREF] state clear all");
  clear_user();
  clear_persisted();
}

const char* State::get_btn_label(uint8_t i) const {
  if (i < NUM_BUTTONS) {
    return m_personalization.btn_labels[i];
  } else {
    return "";
  }
}

void State::set_btn_label(uint8_t i, const char* label) {
  if (i < NUM_BUTTONS) {
    snprintf(m_personalization.btn_labels[i], sizeof(m_personalization.btn_labels[i]), "%s", label);
  }
}

void State::set_topics() {
  m_topics.t_common = m_network.mqtt.base_topic + "/" + m_personalization.device_name + "/";
  m_topics.t_cmd = m_topics.t_common + "cmd/";

  // button press topics
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    m_topics.t_btn_press[i] = m_topics.t_common + "button_" + String(i + 1);
    m_topics.t_btn_press_double[i] = m_topics.t_common + "button_" + String(i + 1) + "_double";
    m_topics.t_btn_press_triple[i] = m_topics.t_common + "button_" + String(i + 1) + "_triple";
    m_topics.t_btn_press_quad[i] = m_topics.t_common + "button_" + String(i + 1) + "_quad";
  }

  // sensors
  m_topics.t_temperature = m_topics.t_common + "temperature";
  m_topics.t_humidity = m_topics.t_common + "humidity";
  m_topics.t_battery = m_topics.t_common + "battery";

  // sensor interval
  m_topics.t_sensor_interval_state = m_topics.t_common + "sensor_interval";
  m_topics.t_sensor_interval_cmd = m_topics.t_cmd + "sensor_interval";

  // button label state & cmd
  for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
    m_topics.t_btn_label_state[i] =
        m_topics.t_common + "btn_" + String(i + 1) + "_label";
    m_topics.t_btn_label_cmd[i] = m_topics.t_cmd + "btn_" + String(i + 1) + "_label";
  }

  // awake mode
  m_topics.t_awake_mode_state = m_topics.t_common + "awake_mode";
  m_topics.t_awake_mode_cmd = m_topics.t_cmd + "awake_mode";
  m_topics.t_awake_mode_avlb = m_topics.t_awake_mode_state + "/available";

}

String State::get_button_topic(uint8_t btn_id, Button::ButtonAction action) {
  if (btn_id < 1 || btn_id > NUM_BUTTONS) {
    return "";
  }
  String append;
  switch (action) {
    case Button::SINGLE:
      append = "";
      break;
    case Button::DOUBLE:
      append = "_double";
      break;
    case Button::TRIPLE:
      append = "_triple";
      break;
    case Button::QUAD:
      append = "_quad";
      break;
    default:
      return "";
  }
  String topic_common = m_network.mqtt.base_topic + "/" + m_personalization.device_name + "/";
  return topic_common + "button_" + String(btn_id) + append;
}