#include <Adafruit_GFX.h>     // Thư viện đồ họa Adafruit
#include <Adafruit_ST7735.h>  // Include Adafruit Hardware-specific library for ST7735 display
#include <SPI.h>              // Include Arduino SPI library
#include <ChronosESP32.h>

// Color definitions
#define BLUE 0x001F
#define RED 0xF800
#define GREEN 0x07E0
#define CYAN 0x07FF
#define MAGENTA 0xF81F
#define YELLOW 0xFFE0
#define BLACK 0x0000
// Khai báo màn hình OLED
// Kích thước màn hình (pixels)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 160
#define LINE_HEIGHT 16
// Define ST7735 display pin connection
#define TFT_RST 20  // Or set to -1 and connect to Arduino RESET pin
#define TFT_CS 7
#define TFT_DC 21
Adafruit_ST7735 display = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);

Notification notify_fifo[NOTIF_SIZE];  // mảng lưu dữ liệu
int head = 0;                          // vị trí ghi
int tail = 0;                          // vị trí đọc
int count = 0;                         // số phần tử hiện có
uint8_t priority = 0;

#define MASK_TIME 0x10
#define MASK_NAVI 0x08
#define MASK_MESS 0x04
#define MASK_CALL 0x02
#define MASK_END_CALL 0x01
// Thêm dữ liệu vào FIFO
bool enqueue(Notification value) {
  if (count == NOTIF_SIZE)
    return false;  // đầy
  notify_fifo[head] = value;
  head = (head + 1) % NOTIF_SIZE;
  count++;
  return true;
}

// Lấy dữ liệu từ FIFO
bool dequeue(Notification &value) {
  if (count == 0)
    return false;  // rỗng
  value = notify_fifo[tail];
  tail = (tail + 1) % NOTIF_SIZE;
  count--;
  return true;
}
#include "FontMaker.h"

void setpx(int16_t x, int16_t y, uint16_t color) {
  display.drawPixel(x, y, color);  // Thay đổi hàm này thành hàm vẽ pixel mà thư viện led bạn dùng cung cấp
}

enum StateDisplay {
  DEFAULT_VALUE,
  NAVIGATION,
  TIME,
  MESSAGE,
  CALLING,
  ENDCALL
};
// StateDisplay displayState = TIME;
// void set_displaystate(StateDisplay state) {
//   if ((uint8_t)state > (uint8_t)displayState) {
//     displayState = state;
//   }
// }

MakeFont myfont(&setpx);
ChronosESP32 watch(F("Màn hình chỉ đường"));  // set the bluetooth name

bool change = false;
uint32_t nav_crc = 0xFFFFFFFF;
bool time5s = false;
bool time2s = false;
bool time1s = false;
// Thêm biến toàn cục để lưu trữ dữ liệu navigation và trạng thái
Navigation currentNavData;
bool isNavigationActive = false;  // Biến theo dõi trạng thái dẫn đường
#define MAX_LINES 10              // số lượng phần tử tối đa muốn tách
#define MAX_TOKENS 30             // số lượng phần tử tối đa muốn tách
String displayContent[MAX_LINES];
int split(String str, char delimiter, String out[]) {
  uint8_t count = 0;
  int start = 0;
  for (int i = 0; i < str.length(); i++) {
    if (str.charAt(i) == ' ') {  // vẫn tách được theo khoảng trắng
      String word = str.substring(start, i);
      out[count] = word;
      start = i + 1;
      count++;
      if (count == MAX_TOKENS) {
        return count;
      }
    }
  }
  // In từ cuối cùng
  out[count] = str.substring(start);

  return count + 1;  // trả về số phần tử đã tách
}
void showText(uint8_t start_x, uint8_t &line, String text, uint16_t color) {
  String messChar[MAX_TOKENS];
  String messLine[MAX_LINES];
  uint8_t numChar = split(text, ' ', messChar);
  uint8_t numLine = 0;
  uint8_t cntChar = 0;

  do {
    String buffer;
    buffer = messChar[cntChar];
    cntChar++;
    messLine[numLine] = buffer;
    while (cntChar < numChar) {
      buffer = buffer + " " + messChar[cntChar];
      if (myfont.getLength(buffer) < (SCREEN_WIDTH - start_x)) {
        messLine[numLine] = buffer;
        cntChar++;
      } else {
        numLine++;
        break;
      }
    }
  } while (cntChar < numChar);
  numLine++;
  for (int i = 0; i < numLine && i < MAX_LINES; i++) {
    myfont.print(start_x, line * LINE_HEIGHT, messLine[i], color, ST77XX_BLACK);
    line++;
  }
}
bool isBlueToothConnected = false;
void connectionCallback(bool state) {
  // Serial.print("Connection state: ");
  // Serial.println(state ? "Connected" : "Disconnected");
  isBlueToothConnected = state;
  priority |= MASK_TIME;
  // Cập nhật trạng thái kết nối lên OLED
  display.fillScreen(ST77XX_BLACK);
  display.setTextSize(1);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(0, 0);
  display.print("Status: ");
  display.println(state ? "Connected" : "Disconnected");
}

void notificationCallback(Notification notification) {
  Serial.print("Notification received at ");
  Serial.println(notification.time);
  Serial.print("From: ");
  Serial.print(notification.app);
  Serial.print("\tIcon: ");
  Serial.println(notification.icon);
  Serial.println(notification.title);
  Serial.println(notification.message);
  // displayState = MESSAGE;
  priority |= MASK_MESS;
  enqueue(notification);
}

// Hàm mới để cập nhật hiển thị OLED (bao gồm icon và văn bản)
void updateNavigationDisplay() {
  // Chỉ hiển thị nếu navigation đang hoạt động
  if (!isNavigationActive) {
    display.fillScreen(ST77XX_BLACK);
    display.setTextSize(1);
    display.setTextColor(RED);
    display.setCursor(LINE_HEIGHT * 5, 0);
    display.println(F("Navigation Inactive"));

    return;
  }

  // VẼ ICON ĐIỀU HƯỚNG
  // Vẽ icon ở góc trên bên trái (0,0) nếu có dữ liệu icon hợp lệ
  if (nav_crc != 0xFFFFFFFF) {  // nav_crc = 0xFFFFFFFF nghĩa là chưa có icon nào được gửi
    display.drawBitmap(0, 0, currentNavData.icon, 48, 48, ST77XX_WHITE);
  } else {
    // Nếu không có icon, bạn có thể để trống hoặc vẽ một hình nền đen ở đó
    display.fillRect(0, 0, 48, 48, YELLOW);
  }

  int text_start_x = 50;  // Bắt đầu văn bản từ cột 55 (bên phải icon 48px + khoảng trống)
  uint8_t line = 0;
  showText(text_start_x, line, currentNavData.duration + " nữa đến nơi", ST77XX_WHITE);
  line = 4;
  showText(0, line, currentNavData.title + "--" + currentNavData.directions, ST77XX_WHITE);
  showText(0, line, "còn " + currentNavData.distance + " nữa", ST77XX_WHITE);
  showText(0, line, currentNavData.eta, ST77XX_WHITE);
}

void configCallback(Config config, uint32_t a, uint32_t b) {
  switch (config) {
    case CF_NAV_DATA:
      Serial.print("Navigation state: ");
      Serial.println(a ? "Active" : "Inactive");
      isNavigationActive = a;  // Cập nhật trạng thái dẫn đường toàn cục

      if (isNavigationActive)  // Nếu navigation active
      {
        currentNavData = watch.getNavigation();  // Lưu dữ liệu navigation vào biến toàn cục
        // displayState = NAVIGATION;
        priority |= MASK_NAVI;
        Serial.println(currentNavData.directions);

        Serial.println(currentNavData.eta);
        Serial.println((char)currentNavData.eta[currentNavData.eta.length() - 5]);
        Serial.println((int)currentNavData.eta[currentNavData.eta.length() - 5]);
        Serial.println((char)currentNavData.eta[currentNavData.eta.length() - 4]);
        Serial.println((int)currentNavData.eta[currentNavData.eta.length() - 4]);
        Serial.println((char)currentNavData.eta[currentNavData.eta.length() - 3]);
        Serial.println((int)currentNavData.eta[currentNavData.eta.length() - 3]);
        Serial.println((char)currentNavData.eta[currentNavData.eta.length() - 2]);
        Serial.println((int)currentNavData.eta[currentNavData.eta.length() - 2]);
        Serial.println(currentNavData.duration);
        Serial.println(currentNavData.distance);
        Serial.println(currentNavData.title);
        Serial.println(currentNavData.speed);
        // In thêm next_step_distance nếu thư viện của bạn có
        // Serial.println(currentNavData.next_step_distance);
      } else {
        priority &= ~MASK_NAVI;
      }
      change = true;
      break;

    case CF_NAV_ICON:
      if (a == 2) {                                  // Khi icon đã được truyền đầy đủ
        Navigation tempNav = watch.getNavigation();  // Lấy dữ liệu icon
        if (nav_crc != tempNav.iconCRC)              // Chỉ cập nhật nếu CRC thay đổi
        {
          nav_crc = tempNav.iconCRC;
          currentNavData = tempNav;  // Lưu dữ liệu icon vào biến toàn cục
          change = true;             // Đặt cờ để cập nhật hiển thị OLED
        }
      }
      break;
  }
}
// Callback function
void myRingerHandler(String caller, bool incoming) {
  display.fillScreen(ST77XX_BLACK);
  uint8_t line = 0;
  if (incoming) {

    Serial.println("Cuộc gọi đến từ: " + caller);
    showText(0, line, F("Cuộc gọi đến từ: "), ST77XX_WHITE);
    showText(0, line, caller, ST77XX_WHITE);
    priority |= MASK_CALL;
  } else {
    Serial.println(F("Cuộc gọi đã kết thúc hoặc bị từ chối"));
    showText(0, line, F("Cuộc gọi đã kết thúc hoặc bị từ chối"), RED);
    priority |= MASK_END_CALL;
  }
}
void setup() {
  Serial.begin(115200);
  Serial.print(F("Hello! ST77xx TFT Test"));

  // KHỞI TẠO MÀN HÌNH OLED

  display.initR(INITR_BLACKTAB);  // Init ST7735S chip, black tab
  myfont.set_font(MakeFont_Font1);
  // Cài đặt hiển thị ban đầu
  display.fillScreen(ST77XX_BLACK);
  display.println(F("Chronos Nav Ready!"));
  //   display.display();
  delay(2000);  // Hiển thị trong 2 giây

  // set the callbacks before calling begin funtion
  watch.setConnectionCallback(connectionCallback);
  watch.setNotificationCallback(notificationCallback);
  watch.setConfigurationCallback(configCallback);
  watch.setRingerCallback(myRingerHandler);
  watch.begin();                       // initializes the BLE
  Serial.println(watch.getAddress());  // mac address, call after begin()

  watch.setBattery(80);  // set the battery level, will be synced to the app
}
unsigned long previous1s = 0;
unsigned long previous2s = 0;
unsigned long previous5s = 0;
const long interval1s = 1000;  // 1 giây
const long interval2s = 2000;  // 2 giây
const long interval5s = 5000;  // 5 giây
void UpdateDisplay() {
  StateDisplay displayState = TIME;
  static Notification *showNotification = NULL;
  static bool oneTime = true;
  static unsigned long timeStart_time = 0;
  static bool timeOut_time = true;
  static unsigned long timeStart_message = 0;
  static bool timeOut_message = true;
  static unsigned long timeStart_endcall = 0;
  static bool timeOut_endcall = true;
  unsigned long currentMillis = millis();
  if ((priority & MASK_END_CALL) != 0) {
    displayState = ENDCALL;
  } else if ((priority & MASK_CALL) != 0) {
    displayState = CALLING;
  } else if ((priority & MASK_MESS) != 0) {
    displayState = MESSAGE;
  } else if ((priority & MASK_NAVI) != 0) {
    displayState = NAVIGATION;
  } else if ((priority & MASK_TIME) != 0) {
    displayState = TIME;
  }
  switch (displayState) {
    case NAVIGATION:
      {
        if (change) {
          Serial.println("show NAVIGATION");
          display.fillScreen(ST77XX_BLACK);
          updateNavigationDisplay();  // Gọi hàm cập nhật hiển thị
          change = false;             // Reset cờ để chỉ cập nhật khi có thay đổi mới
        }
        break;
      }
    case TIME:
      {

        if (timeOut_time) {
          display.fillScreen(ST77XX_BLACK);
          Serial.println("chỉ hiện thời gian");
          display.setTextSize(4);
          display.setTextColor(isBlueToothConnected ? GREEN : YELLOW);
          String hour = watch.getHourZ() + watch.getTime(":%M");
          int16_t x1, y1;
          uint16_t w, h;
          // Tính kích thước chuỗi
          display.getTextBounds(hour, 0, 0, &x1, &y1, &w, &h);
          int16_t x = (SCREEN_WIDTH - w) / 2;
          int16_t y = (SCREEN_HEIGHT - h) / 2;
          display.setCursor(x, y - 20);
          display.println(hour);
          display.getTextBounds(watch.getAmPmC(), 0, 0, &x1, &y1, &w, &h);
          x = (SCREEN_WIDTH - w) / 2;
          y = (SCREEN_HEIGHT - h) / 2;
          display.setCursor(x, y + h + 3);
          display.println(watch.getAmPmC());
          timeOut_time = false;
          timeStart_time = currentMillis;
        } else {
          if ((currentMillis - timeStart_time) > interval5s) {
            timeOut_time = true;
          }
        }
        break;
      }
    case MESSAGE:
      {

        if (timeOut_message) {
          static Notification n;
          bool isInterrupt = true;
          if (showNotification == NULL) {
            Serial.println("showNotification == NULL");
            isInterrupt = dequeue(n);
            showNotification = &n;
            Serial.println("showNotification: " + showNotification);
          } else {
            Serial.println("showNotification != NULL");
            n = *showNotification;
          }
          if (isInterrupt) {
            Serial.println("show message");
            display.fillScreen(ST77XX_BLACK);
            uint8_t line = 0;
            showText((SCREEN_WIDTH - myfont.getLength(n.app)) / 2, line, n.app, ST77XX_WHITE);
            showText(0, line, n.title, ST77XX_WHITE);
            showText(0, line, n.message, ST77XX_WHITE);
            timeOut_message = false;
            timeStart_message = currentMillis;
          } else {
            Serial.println("end message");
            priority &= ~MASK_MESS;
            timeOut_message = true;
            change = true;
            showNotification = NULL;
          }
        } else {
          if ((currentMillis - timeStart_message) > interval5s) {
            timeOut_message = true;
            showNotification = NULL;
          }
        }
        break;
      }

    case CALLING:
      {
        if (oneTime) {
          display.fillScreen(ST77XX_BLACK);
          uint8_t line = 0;
          showText(0, line, F("Cuộc gọi đến từ: "), ST77XX_WHITE);

          oneTime = false;
        }
        break;
      }
    case ENDCALL:
      {

        if (timeOut_endcall) {
          display.fillScreen(ST77XX_BLACK);
          uint8_t line = 0;
          showText(0, line, F("Cuộc gọi đã kết thúc hoặc bị từ chối"), RED);
          timeOut_endcall = false;
          timeStart_endcall = currentMillis;
          oneTime = true;
        } else {
          if ((currentMillis - timeStart_endcall) > interval2s) {
            timeOut_endcall = true;
            priority &= ~MASK_CALL;
            priority &= ~MASK_END_CALL;
            change = true;
          }
        }
        break;
      }
    default:
      break;
  }
}
void task1s() {
  time1s = true;
}
void task2s() {
  time2s = true;
}
void task5s() {
  time5s = true;
}

void loop() {
  watch.loop();  // handles internal routine functions
  UpdateDisplay();
  // Kiểm tra cờ 'change' để cập nhật màn hình OLED

  unsigned long currentMillis = millis();
  if (currentMillis - previous1s >= interval1s) {
    previous1s = currentMillis;  // reset mốc thời gian
    task1s();
  }
  if (currentMillis - previous2s >= interval2s) {
    previous2s = currentMillis;  // reset mốc thời gian
    task2s();
  }
  if (currentMillis - previous5s >= interval5s) {
    previous5s = currentMillis;  // reset mốc thời gian
    task5s();
  }
}