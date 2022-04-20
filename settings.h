#define DEBUG true

#define IDX_CPS_KEY 0
#define IDX_CPM_KEY 2
#define IDX_uSv_KEY 4

#define IDX_CPS 1
#define IDX_CPM 3
#define IDX_uSv 5
#define IDX_MODE 6

#define PIN_UART_RX D6
#define PIN_UART_TX 13 // UNUSED
#define PIN_PULSE D7
#define PIN_BUTTON D5

#define FRAME_GEIGER  1
#define FRAME_INFO    2
#define FRAME_AUTHOR  3

#define BAUD_GEIGERCOUNTER 9600

#define OLED_I2C_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

#define UNIT_CPS   1
#define UNIT_CPM   2
#define UNIT_USV   3

#define BAR_MX  15
#define BAR_X1  0
#define BAR_Y1  15
#define BAR_X2  100
#define BAR_Y2  15

#define BARPOINTS_SIZE 50
#define HISTORY_SIZE 60

#define WIFI_ENABLED true
#define WIFI_HOSTNAME "geigercounter"
#define WIFI_SSID "SSID"
#define WIFI_PASSWORD "PW"

#define OTA_PASSWORD "OTAPW"

#define MQTT_ENABLED false
#define MQTT_HOST "BROKER_IP"
#define MQTT_USER "USERNAME"
#define MQTT_PASSWORD "PW"
#define MQTT_CLIENTID "geiger"

#define MQTT_TOPIC_CPM "geiger/cpm"
#define MQTT_TOPIC_USV "geiger/uSv"
#define MQTT_TOPIC_LAST_WILL "geiger/geigercounter/status"
//#define MQTT_TOPIC_PULSE "sensor/geigercounter/pulse"

#define HTTP_CLIENT_ENABLED false
#define HTTP_CLIENT_URL "CLIENT_URL"
#define HTTP_CLIENT_SECRECT "CLIENT_PW"

//Interval to send data via http
#define HTTP_PUSH_INTERVAL_SECONDS 50
//Interval to push data via mqtt
#define MQTT_PUSH_INTERVAL_SECONDS 60
