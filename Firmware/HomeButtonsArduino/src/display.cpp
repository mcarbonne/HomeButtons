#include "display.h"

#include <GxEPD2_BW.h>
#include <Fonts/FreeMono9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansBold24pt7b.h>
#include <Fonts/FreeSansBold9pt7b.h>
#include <SPIFFS.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <qrcode.h>

#include "bitmaps.h"
#include "config.h"
#include "hardware.h"
#include "types.h"

static constexpr uint16_t WIDTH = 128;
static constexpr uint16_t HEIGHT = 296;

uint16_t read16(File &f) {
  // BMP data is stored little-endian, same as Arduino.
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read();  // LSB
  ((uint8_t *)&result)[1] = f.read();  // MSB
  return result;
}

uint32_t read32(File &f) {
  // BMP data is stored little-endian, same as Arduino.
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read();  // LSB
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read();  // MSB
  return result;
}

#define GxEPD2_DISPLAY_CLASS GxEPD2_BW
#define GxEPD2_DRIVER_CLASS GxEPD2_290_T94_V2
#define MAX_DISPLAY_BUFFER_SIZE 65536ul  // e.g.
#define MAX_HEIGHT(EPD)                                      \
  (EPD::HEIGHT <= MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8) \
       ? EPD::HEIGHT                                         \
       : MAX_DISPLAY_BUFFER_SIZE / (EPD::WIDTH / 8))

static GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS,
                            MAX_HEIGHT(GxEPD2_DRIVER_CLASS)> *disp;

static U8G2_FOR_ADAFRUIT_GFX u8g2;

void Display::begin(HardwareDefinition &HW) {
  if (state != State::IDLE) return;
  disp = new GxEPD2_DISPLAY_CLASS<GxEPD2_DRIVER_CLASS,
                                  MAX_HEIGHT(GxEPD2_DRIVER_CLASS)>(
      GxEPD2_DRIVER_CLASS(/*CS=*/HW.EINK_CS, /*DC=*/HW.EINK_DC,
                          /*RST=*/HW.EINK_RST, /*BUSY=*/HW.EINK_BUSY));
  disp->init();
  u8g2.begin(*disp);
  current_ui_state = {};
  cmd_ui_state = {};
  draw_ui_state = {};
  pre_disappear_ui_state = {};
  state = State::ACTIVE;
  info("begin");
}

void Display::end() {
  if (state != State::ACTIVE) return;
  state = State::CMD_END;
  debug("cmd end");
}

void Display::update() {
  if (state == State::IDLE) return;

  if (state == State::CMD_END) {
    state = State::ENDING;
    if (current_ui_state.disappearing) {
      draw_ui_state = pre_disappear_ui_state;
    } else {
      disp->hibernate();
      state = State::IDLE;
      info("ended.");
      return;
    }
  } else if (current_ui_state.disappearing) {
    if (millis() - current_ui_state.appear_time >=
        current_ui_state.disappear_timeout) {
      if (new_ui_cmd) {
        draw_ui_state = cmd_ui_state;
        cmd_ui_state = {};
        new_ui_cmd = false;
      } else {
        draw_ui_state = pre_disappear_ui_state;
        pre_disappear_ui_state = {};
      }
    } else {
      return;
    }
  } else if (new_ui_cmd) {
    if (cmd_ui_state.disappearing) {
      pre_disappear_ui_state = current_ui_state;
    }
    draw_ui_state = cmd_ui_state;
    cmd_ui_state = {};
    new_ui_cmd = false;
  } else {
    return;
  }

  debug("update: page: %d; disappearing: %d, msg: %s",
        static_cast<int>(draw_ui_state.page), draw_ui_state.disappearing,
        draw_ui_state.message.c_str());

  redraw_in_progress = true;
  switch (draw_ui_state.page) {
    case DisplayPage::EMPTY:
      draw_white();
      break;
    case DisplayPage::MAIN:
      draw_main();
      break;
    case DisplayPage::INFO:
      draw_info();
      break;
    case DisplayPage::MESSAGE:
      draw_message(draw_ui_state.message);
      break;
    case DisplayPage::MESSAGE_LARGE:
      draw_message(draw_ui_state.message, false, true);
      break;
    case DisplayPage::ERROR:
      draw_message(draw_ui_state.message, true, false);
      break;
    case DisplayPage::WELCOME:
      draw_welcome();
      break;
    case DisplayPage::SETTINGS:
      draw_settings();
      break;
    case DisplayPage::AP_CONFIG:
      draw_ap_config();
      break;
    case DisplayPage::WEB_CONFIG:
      draw_web_config();
      break;
    case DisplayPage::TEST:
      draw_test();
      break;
    case DisplayPage::TEST_INV:
      draw_test(true);
      break;
  }
  current_ui_state = draw_ui_state;
  current_ui_state.appear_time = millis();
  draw_ui_state = {};
  redraw_in_progress = false;

  if (state == State::ENDING) {
    disp->hibernate();
    state = State::IDLE;
    info("ended.");
  }
}

void Display::disp_message(const char *message, uint32_t duration) {
  UIState new_cmd_state{DisplayPage::MESSAGE, UIState::MessageType{message}};
  if (duration > 0) {
    new_cmd_state.disappearing = true;
    new_cmd_state.disappear_timeout = duration;
  }
  set_cmd_state(new_cmd_state);
}

void Display::disp_message_large(const char *message, uint32_t duration) {
  UIState new_cmd_state{DisplayPage::MESSAGE_LARGE,
                        UIState::MessageType{message}};
  if (duration > 0) {
    new_cmd_state.disappearing = true;
    new_cmd_state.disappear_timeout = duration;
  }
  set_cmd_state(new_cmd_state);
}
void Display::disp_error(const char *message, uint32_t duration) {
  UIState new_cmd_state{DisplayPage::ERROR, UIState::MessageType{message}};
  if (duration > 0) {
    new_cmd_state.disappearing = true;
    new_cmd_state.disappear_timeout = duration;
  }
  set_cmd_state(new_cmd_state);
}

void Display::disp_main() {
  UIState new_cmd_state{DisplayPage::MAIN};
  set_cmd_state(new_cmd_state);
}

void Display::disp_info() {
  UIState new_cmd_state{DisplayPage::INFO};
  set_cmd_state(new_cmd_state);
}

void Display::disp_welcome() {
  UIState new_cmd_state{DisplayPage::WELCOME};
  set_cmd_state(new_cmd_state);
}

void Display::disp_settings() {
  UIState new_cmd_state{DisplayPage::SETTINGS};
  set_cmd_state(new_cmd_state);
}
void Display::disp_ap_config() {
  UIState new_cmd_state{DisplayPage::AP_CONFIG};
  set_cmd_state(new_cmd_state);
}

void Display::disp_web_config() {
  UIState new_cmd_state{DisplayPage::WEB_CONFIG};
  set_cmd_state(new_cmd_state);
}

void Display::disp_test(bool invert) {
  UIState new_cmd_state{};
  if (!invert) {
    new_cmd_state.page = DisplayPage::TEST;
  } else {
    new_cmd_state.page = DisplayPage::TEST_INV;
  }
  set_cmd_state(new_cmd_state);
}

UIState Display::get_ui_state() { return current_ui_state; }

void Display::init_ui_state(UIState ui_state) { current_ui_state = ui_state; }

Display::State Display::get_state() { return state; }

void Display::set_cmd_state(UIState cmd) {
  cmd_ui_state = cmd;
  new_ui_cmd = true;
}

void Display::draw_message(const UIState::MessageType &message, bool error,
                           bool large) {
  disp->setRotation(0);
  disp->setTextColor(text_color);
  disp->setTextWrap(true);
  disp->setFont(&FreeMono9pt7b);
  disp->setFullWindow();

  disp->fillScreen(bg_color);

  if (!error) {
    if (!large) {
      disp->setFont(&FreeMono9pt7b);
      disp->setCursor(0, 20);
    } else {
      disp->setFont(&FreeMonoBold12pt7b);
      disp->setCursor(0, 30);
    }
    disp->print(message.c_str());
  } else {
    disp->setFont(&FreeMonoBold12pt7b);
    disp->setCursor(0, 0);
    int16_t x, y;
    uint16_t w, h;
    const char *text = "ERROR";
    disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);
    disp->setCursor(WIDTH / 2 - w / 2, 20);
    disp->print(text);
    disp->setFont(&FreeMono9pt7b);
    disp->setCursor(0, 60);
    disp->print(message.c_str());
  }
  disp->display();
}

void Display::draw_main() {
  const uint16_t min_btn_clearance = 14;
  const uint16_t h_padding = 5;

  disp->setRotation(0);
  disp->setFullWindow();

  u8g2.setFontMode(1);
  u8g2.setForegroundColor(text_color);
  u8g2.setBackgroundColor(bg_color);

  disp->fillScreen(bg_color);

  // charging line
  if (device_state_.sensors().charging) {
    disp->fillRect(12, HEIGHT - 3, WIDTH - 24, 3, text_color);
  }

  mdi_.begin();
  LabelType label_type[NUM_BUTTONS] = {};
  for (uint16_t i = 0; i < NUM_BUTTONS; i++) {
    ButtonLabel label = device_state_.get_btn_label(i);
    if (label.substring(0, 4) == "mdi:") {
      if (label.index_of(' ') > 0) {
        label_type[i] = LabelType::Mixed;
      } else {
        label_type[i] = LabelType::Icon;
      }
    } else {
      label_type[i] = LabelType::Text;
    }
  }

  // Loop through buttons
  for (uint16_t i = 0; i < NUM_BUTTONS; i++) {
    ButtonLabel label = device_state_.get_btn_label(i);

    if (label_type[i] == LabelType::Icon) {
      MDIName icon = label.substring(
          4, label.index_of(' ') > 0 ? label.index_of(' ') : label.length());

      // make smaller if opposite is text or mixed
      uint16_t size;
      bool small;
      if (i % 2 == 0) {
        if (label_type[i + 1] == LabelType::Text ||
            label_type[i + 1] == LabelType::Mixed) {
          size = 48;
          small = true;
        } else {
          size = 64;
          small = false;
        }
      } else {
        if (label_type[i - 1] == LabelType::Text ||
            label_type[i - 1] == LabelType::Mixed) {
          size = 48;
          small = true;
        } else {
          size = 64;
          small = false;
        }
      }

      // calculate icon position on display
      uint16_t x = i % 2 == 0 ? 0 : WIDTH - size;
      uint16_t y;
      if (i < 2) {
        if (small) {
          y = static_cast<uint16_t>(round(HEIGHT / 12. + i * HEIGHT / 6.)) -
              size / 2;
        } else {
          y = 17;
        }
      } else if (i < 4) {
        if (small) {
          y = static_cast<uint16_t>(round(HEIGHT / 12. + i * HEIGHT / 6.)) -
              size / 2;
        } else {
          y = 116;
        }
      } else {
        if (small) {
          y = static_cast<uint16_t>(round(HEIGHT / 12. + i * HEIGHT / 6.)) -
              size / 2;
        } else {
          y = 215;
        }
      }
      draw_mdi(icon.c_str(), size, x, y);
    } else if (label_type[i] == LabelType::Mixed) {
      MDIName icon = label.substring(4, label.index_of(' '));
      StaticString<56> text = label.substring(label.index_of(' ') + 1);
      uint16_t icon_size = 48;
      uint16_t x = i % 2 == 0 ? 0 : WIDTH - icon_size;
      uint16_t y =
          static_cast<uint16_t>(round(HEIGHT / 12. + i * HEIGHT / 6.)) -
          icon_size / 2;

      draw_mdi(icon.c_str(), icon_size, x, y);
      // draw text
      uint16_t max_text_width = WIDTH - icon_size - h_padding;
      u8g2.setFont(u8g2_font_helvB24_te);
      uint16_t w, h;
      w = u8g2.getUTF8Width(text.c_str());
      h = u8g2.getFontAscent();
      if (w >= max_text_width) {
        u8g2.setFont(u8g2_font_helvB18_te);
        w = u8g2.getUTF8Width(text.c_str());
        h = u8g2.getFontAscent();
        if (w >= max_text_width) {
          text = text.substring(0, text.length() - 1) + ".";
          while (1) {
            w = u8g2.getUTF8Width(text.c_str());
            h = u8g2.getFontAscent();
            if (w >= max_text_width) {
              text = text.substring(0, text.length() - 2) + ".";
            } else {
              break;
            }
          }
        }
      }
      x = i % 2 == 0 ? icon_size + h_padding
                     : WIDTH - icon_size - w - h_padding;
      y = static_cast<uint16_t>(round(HEIGHT / 12. + i * HEIGHT / 6.)) + h / 2;
      u8g2.setCursor(x, y);
      u8g2.print(text.c_str());
    } else {
      uint16_t max_label_width = WIDTH - min_btn_clearance;
      u8g2.setFont(u8g2_font_helvB24_te);
      uint16_t w, h;
      w = u8g2.getUTF8Width(label.c_str());
      h = u8g2.getFontAscent();
      if (w >= max_label_width) {
        u8g2.setFont(u8g2_font_helvB18_te);
        w = u8g2.getUTF8Width(label.c_str());
        h = u8g2.getFontAscent();
        if (w >= max_label_width) {
          label = label.substring(0, label.length() - 1) + ".";
          while (1) {
            w = u8g2.getUTF8Width(label.c_str());
            h = u8g2.getFontAscent();
            if (w >= max_label_width) {
              label = label.substring(0, label.length() - 2) + ".";
            } else {
              break;
            }
          }
        }
      }
      int16_t x, y;
      if (i % 2 == 0) {
        x = h_padding;
      } else {
        x = WIDTH - w - h_padding;
      }
      y = static_cast<uint16_t>(round(HEIGHT / 12. + i * HEIGHT / 6.)) + h / 2;
      u8g2.setCursor(x, y);
      u8g2.print(label.c_str());
    }
  }
  mdi_.end();
  disp->display();
}

void Display::draw_info() {
  disp->setRotation(0);
  disp->setTextColor(text_color);
  disp->setTextWrap(false);
  disp->setFullWindow();

  disp->fillScreen(bg_color);
  disp->setCursor(0, 0);

  int16_t x, y;
  uint16_t w, h;
  UIState::MessageType text;

  text = "- Temp -";
  disp->setFont(&FreeMono9pt7b);
  disp->getTextBounds(text.c_str(), 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 30);
  disp->print(text.c_str());

  text = UIState::MessageType("%.1f %s", device_state_.sensors().temperature,
                              device_state_.get_temp_unit().c_str());
  disp->setFont(&FreeSansBold18pt7b);
  disp->getTextBounds(text.c_str(), 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2 - 2, 70);
  disp->print(text.c_str());

  text = "- Humd -";
  disp->setFont(&FreeMono9pt7b);
  disp->getTextBounds(text.c_str(), 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 129);
  disp->print(text.c_str());

  text = UIState::MessageType("%.0f %%", device_state_.sensors().humidity);
  disp->setFont(&FreeSansBold18pt7b);
  disp->getTextBounds(text.c_str(), 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2 - 2, 169);
  disp->print(text.c_str());

  text = "- Batt -";
  disp->setFont(&FreeMono9pt7b);
  disp->getTextBounds(text.c_str(), 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 228);
  disp->print(text.c_str());

  if (device_state_.sensors().battery_present) {
    text = UIState::MessageType("%d %%", device_state_.sensors().battery_pct);
  } else {
    text = "-";
  }
  disp->setFont(&FreeSansBold18pt7b);
  disp->getTextBounds(text.c_str(), 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2 - 2, 268);
  disp->print(text.c_str());

  disp->display();
}

void Display::draw_welcome() {
  disp->setRotation(0);
  disp->setTextColor(text_color);
  disp->setTextWrap(false);
  disp->setFullWindow();

  disp->fillScreen(bg_color);
  disp->setCursor(0, 0);

  int16_t x, y;
  uint16_t w, h;
  const char *text = "Home Buttons";
  disp->setFont(&FreeSansBold9pt7b);
  disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 40);
  disp->print(text);

  disp->drawBitmap(54, 52, hb_logo_20x21px, 20, 21, GxEPD_BLACK);

  disp->setFont(&FreeSansBold9pt7b);
  disp->getTextBounds("--------------------", 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 102);
  disp->print("--------------------");

  uint8_t version = 6;  // 41x41px
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  qrcode_initText(&qrcode, qrcodeData, version, ECC_HIGH, DOCS_LINK);

  text = "Setup guide:";
  disp->setFont(&FreeSans9pt7b);
  disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 145);
  disp->print(text);

  uint16_t qr_x = 23;
  uint16_t qr_y = 165;
  for (uint8_t y2 = 0; y2 < qrcode.size; y2++) {
    // Each horizontal module
    for (uint8_t x2 = 0; x2 < qrcode.size; x2++) {
      // Display each module
      if (qrcode_getModule(&qrcode, x2, y2)) {
        disp->drawRect(qr_x + x2 * 2, qr_y + y2 * 2, 2, 2, GxEPD_BLACK);
      }
    }
  }

  disp->setFont();
  disp->setTextSize(1);

  disp->setCursor(0, 265);
  UIState::MessageType sw_ver = UIState::MessageType("Software: ") + SW_VERSION;
  disp->print(sw_ver.c_str());

  disp->setCursor(0, 275);
  UIState::MessageType model_info = UIState::MessageType("Model: ") +
                                    device_state_.factory().model_id.c_str() +
                                    " rev " +
                                    device_state_.factory().hw_version.c_str();
  disp->print(model_info.c_str());

  disp->setCursor(0, 285);
  disp->print(device_state_.factory().unique_id.c_str());

  disp->display();
}

void Display::draw_settings() {
  disp->setRotation(0);
  disp->setTextColor(text_color);
  disp->setTextWrap(false);
  disp->setFullWindow();

  disp->fillScreen(bg_color);

  disp->drawXBitmap(0, 17, account_cog_64x64, 64, 64, text_color);
  disp->drawXBitmap(WIDTH / 2, 17, wifi_cog_64x64, 64, 64, text_color);
  disp->drawXBitmap(0, 116, restore_64x64, 64, 64, text_color);
  disp->drawXBitmap(WIDTH / 2, 116, close_64x64, 64, 64, text_color);

  disp->drawXBitmap(40, 200, hb_logo_48x48, 48, 48, text_color);

  disp->setFont();
  disp->setTextSize(1);
  disp->setCursor(0, 255);
  disp->print(device_state_.device_name().c_str());

  disp->setCursor(0, 265);
  UIState::MessageType sw_ver =
      UIState::MessageType("Software: %s", SW_VERSION);
  disp->print(sw_ver.c_str());

  disp->setCursor(0, 275);
  UIState::MessageType model_info = UIState::MessageType(
      "Model: %s rev %s", device_state_.factory().model_id.c_str(),
      device_state_.factory().hw_version.c_str());
  disp->print(model_info.c_str());

  disp->setCursor(0, 285);
  disp->print(device_state_.factory().unique_id.c_str());

  disp->display();
}

void Display::draw_ap_config() {
  UIState::MessageType contents = UIState::MessageType("WIFI:T:WPA;S:") +
                                  device_state_.get_ap_ssid().c_str() +
                                  ";P:" + device_state_.get_ap_password() +
                                  ";;";

  uint8_t version = 6;  // 41x41px
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  qrcode_initText(&qrcode, qrcodeData, version, ECC_HIGH, contents.c_str());

  disp->setRotation(0);
  disp->setTextColor(text_color);
  disp->setTextWrap(false);
  disp->setFont(&FreeSans9pt7b);
  disp->setFullWindow();

  disp->fillScreen(bg_color);

  int16_t x, y;
  uint16_t w, h;
  const char *text = "Scan:";
  disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 20);
  disp->print(text);

  uint16_t qr_x = 23;
  uint16_t qr_y = 35;
  for (uint8_t y2 = 0; y2 < qrcode.size; y2++) {
    // Each horizontal module
    for (uint8_t x2 = 0; x2 < qrcode.size; x2++) {
      // Display each module
      if (qrcode_getModule(&qrcode, x2, y2)) {
        disp->drawRect(qr_x + x2 * 2, qr_y + y2 * 2, 2, 2, GxEPD_BLACK);
      }
    }
  }
  text = "------ or ------";
  disp->setFont(&FreeSansBold9pt7b);
  disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 153);
  disp->print(text);

  text = "Connect to:";
  disp->setFont(&FreeSans9pt7b);
  disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 190);
  disp->print(text);

  disp->setCursor(0, 220);
  disp->print("Wi-Fi:");
  disp->setFont(&FreeSansBold9pt7b);
  disp->setCursor(0, 235);
  disp->print(device_state_.get_ap_ssid().c_str());

  disp->setFont(&FreeSans9pt7b);
  disp->setCursor(0, 260);
  disp->print("Password:");
  disp->setFont(&FreeSansBold9pt7b);
  disp->setCursor(0, 275);
  disp->print(device_state_.get_ap_password());

  disp->display();
}

void Display::draw_web_config() {
  UIState::MessageType contents =
      UIState::MessageType("http://") + device_state_.ip();

  uint8_t version = 6;  // 41x41px
  QRCode qrcode;
  uint8_t qrcodeData[qrcode_getBufferSize(version)];
  qrcode_initText(&qrcode, qrcodeData, version, ECC_HIGH, contents.c_str());

  disp->setRotation(0);
  disp->setTextColor(text_color);
  disp->setTextWrap(false);
  disp->setFont(&FreeSans9pt7b);
  disp->setFullWindow();

  disp->fillScreen(bg_color);

  int16_t x, y;
  uint16_t w, h;
  const char *text = "Scan:";
  disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 20);
  disp->print(text);

  uint16_t qr_x = 23;
  uint16_t qr_y = 35;

  for (uint8_t y2 = 0; y2 < qrcode.size; y2++) {
    // Each horizontal module
    for (uint8_t x2 = 0; x2 < qrcode.size; x2++) {
      // Display each module
      if (qrcode_getModule(&qrcode, x2, y2)) {
        disp->drawRect(qr_x + x2 * 2, qr_y + y2 * 2, 2, 2, GxEPD_BLACK);
      }
    }
  }
  text = "------ or ------";
  disp->setFont(&FreeSansBold9pt7b);
  disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 153);
  disp->print(text);

  text = "Go to:";
  disp->setFont(&FreeSans9pt7b);
  disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);
  disp->setCursor(WIDTH / 2 - w / 2, 200);
  disp->print(text);

  disp->setFont(&FreeSansBold9pt7b);
  disp->setCursor(0, 240);
  disp->print("http://");
  disp->setCursor(0, 260);
  disp->print(device_state_.ip());

  disp->display();
}

void Display::draw_test(bool invert) {
  uint16_t fg, bg;
  if (!invert) {
    fg = GxEPD_BLACK;
    bg = GxEPD_WHITE;
  } else {
    fg = GxEPD_WHITE;
    bg = GxEPD_BLACK;
  }

  disp->setRotation(0);
  disp->setTextColor(fg);
  disp->setTextWrap(true);
  disp->setFont(&FreeSansBold24pt7b);
  disp->setFullWindow();

  disp->fillScreen(bg);

  int16_t x, y;
  uint16_t w, h;
  const char *text = "TEST";

  disp->getTextBounds(text, 0, 0, &x, &y, &w, &h);

  disp->setCursor(WIDTH / 2 - w / 2, 90);
  disp->print(text);

  disp->setCursor(WIDTH / 2 - w / 2, 170);
  disp->print(text);

  disp->setCursor(WIDTH / 2 - w / 2, 250);
  disp->print(text);

  disp->display();
}

void Display::draw_white() {
  disp->setFullWindow();
  disp->fillScreen(GxEPD_WHITE);
  disp->display();
}

void Display::draw_black() {
  disp->setFullWindow();
  disp->fillScreen(GxEPD_BLACK);
  disp->display();
}

// based on GxEPD2_Spiffs_Example.ino - drawBitmapFromSpiffs_Buffered()
// Warning - SPIFFS.begin() must be called before this function
bool Display::draw_bmp(File &file, int16_t x, int16_t y) {
  uint32_t startTime = millis();
  if (!file) {
    error("error opening file");
    return false;
  }
  bool valid = false;  // valid format to be handled
  bool flip = true;    // bitmap is stored bottom-to-top
  if ((x >= disp->width()) || (y >= disp->height())) return false;

  // Parse BMP header
  if (read16(file) == 0x4D42) {
    debug("BMP signature detected");
    uint32_t fileSize = read32(file);
    uint32_t creatorBytes = read32(file);
    (void)creatorBytes;                   // unused
    uint32_t imageOffset = read32(file);  // Start of image data
    uint32_t headerSize = read32(file);
    uint32_t width = read32(file);
    int32_t height = (int32_t)read32(file);
    uint16_t planes = read16(file);
    uint16_t depth = read16(file);  // bits per pixel
    uint32_t format = read32(file);
    if ((planes == 1) && ((format == 0) || (format == 3))) {
      debug("BMP Image Offset: %d", imageOffset);
      debug("BMP Header size: %d", headerSize);
      debug("BMP File size: %d", fileSize);
      debug("BMP Bit Depth: %d", depth);
      debug("BMP Image size: %d x %d", width, height);
      // BMP rows are padded (if needed) to 4-byte boundary
      uint32_t rowSize = (width * depth / 8 + 3) & ~3;
      if (depth < 8) rowSize = ((width * depth + 8 - depth) / 8 + 3) & ~3;
      if (height < 0) {
        height = -height;
        flip = false;
      }
      uint16_t w = width;
      uint16_t h = height;
      if ((x + w - 1) >= disp->width()) w = disp->width() - x;
      if ((y + h - 1) >= disp->height()) h = disp->height() - y;
      valid = true;
      uint8_t bitmask = 0xFF;
      uint8_t bitshift = 8 - depth;
      uint16_t red, green, blue;
      bool whitish = false;
      bool colored = false;
      if (depth <= 8) {
        if (depth < 8) bitmask >>= depth;
        file.seek(imageOffset - (4 << depth));
        for (uint16_t pn = 0; pn < (1 << depth); pn++) {
          blue = file.read();
          green = file.read();
          red = file.read();
          file.read();
          whitish = (red + green + blue) > 3 * 0x80;
          // reddish or yellowish?
          colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));
          if (0 == pn % 8) mono_palette_buffer[pn / 8] = 0;
          mono_palette_buffer[pn / 8] |= whitish << pn % 8;
          if (0 == pn % 8) color_palette_buffer[pn / 8] = 0;
          color_palette_buffer[pn / 8] |= colored << pn % 8;
          rgb_palette_buffer[pn] = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) |
                                   ((blue & 0xF8) >> 3);
        }
      }
      uint32_t rowPosition =
          flip ? imageOffset + (height - h) * rowSize : imageOffset;
      for (uint16_t row = 0; row < h;
           row++, rowPosition += rowSize)  // for each line
      {
        uint32_t in_remain = rowSize;
        uint32_t in_idx = 0;
        uint32_t in_bytes = 0;
        uint8_t in_byte = 0;  // for depth <= 8
        uint8_t in_bits = 0;  // for depth <= 8
        uint16_t color = GxEPD_WHITE;
        file.seek(rowPosition);
        for (uint16_t col = 0; col < w; col++)  // for each pixel
        {
          // Time to read more pixel data?
          if (in_idx >= in_bytes)  // ok, exact match for 24bit also (size
                                   // IS multiple of 3)
          {
            in_bytes = file.read(input_buffer, in_remain > sizeof(input_buffer)
                                                   ? sizeof(input_buffer)
                                                   : in_remain);
            in_remain -= in_bytes;
            in_idx = 0;
          }
          switch (depth) {
            case 24:
              blue = input_buffer[in_idx++];
              green = input_buffer[in_idx++];
              red = input_buffer[in_idx++];
              whitish = (red + green + blue) > 3 * 0x80;
              // reddish or yellowish?
              colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));
              color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) |
                      ((blue & 0xF8) >> 3);
              break;
            case 16: {
              uint8_t lsb = input_buffer[in_idx++];
              uint8_t msb = input_buffer[in_idx++];
              if (format == 0)  // 555
              {
                blue = (lsb & 0x1F) << 3;
                green = ((msb & 0x03) << 6) | ((lsb & 0xE0) >> 2);
                red = (msb & 0x7C) << 1;
                color = ((red & 0xF8) << 8) | ((green & 0xFC) << 3) |
                        ((blue & 0xF8) >> 3);
              } else  // 565
              {
                blue = (lsb & 0x1F) << 3;
                green = ((msb & 0x07) << 5) | ((lsb & 0xE0) >> 3);
                red = (msb & 0xF8);
                color = (msb << 8) | lsb;
              }
              whitish = (red + green + blue) > 3 * 0x80;
              // reddish or yellowish?
              colored = (red > 0xF0) || ((green > 0xF0) && (blue > 0xF0));
            } break;
            case 1:
            case 4:
            case 8: {
              if (0 == in_bits) {
                in_byte = input_buffer[in_idx++];
                in_bits = 8;
              }
              uint16_t pn = (in_byte >> bitshift) & bitmask;
              whitish = mono_palette_buffer[pn / 8] & (0x1 << pn % 8);
              colored = color_palette_buffer[pn / 8] & (0x1 << pn % 8);
              in_byte <<= depth;
              in_bits -= depth;
              color = rgb_palette_buffer[pn];
            } break;
          }
          if (whitish) {
            color = GxEPD_WHITE;
          } else if (colored) {
            color = GxEPD_COLORED;
          } else {
            color = GxEPD_BLACK;
          }
          uint16_t yrow = y + (flip ? h - row - 1 : row);
          disp->drawPixel(x + col, yrow, color);
        }  // end pixel
      }    // end line
    }
  }
  file.close();
  if (!valid) {
    error("BMP format not valid.");
  }
  debug("BMP loaded in %lu ms", millis() - startTime);
  return valid;
}

void Display::draw_mdi(const char *name, uint16_t size, int16_t x, int16_t y) {
  bool draw_placeholder = false;
  File file;
  if (mdi_.exists(name, size)) {
    File file = mdi_.get_file(name, size);
    if (!draw_bmp(file, x, y)) {
      error("Could not draw icon: %s", name);
      // file might be corrupted - remove so it will be downloaded again
      mdi_.remove(name, size);
      draw_placeholder = true;
    }
  } else {
    error("Icon not found: %s", name);
    draw_placeholder = true;
  }
  if (draw_placeholder) {
    disp->drawXBitmap(x, y, file_question_outline_64x64, 64, 64, text_color);
  }
}
