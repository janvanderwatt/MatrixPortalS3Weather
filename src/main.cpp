#include <Arduino.h>

/* ----------------------------------------------------------------------
"Pixel dust" Protomatter library example. As written, this is
SPECIFICALLY FOR THE ADAFRUIT MATRIXPORTAL with 64x32 pixel matrix.
Change "SCREEN_HEIGHT" below for 64x64 matrix. Could also be adapted to other
Protomatter-capable boards with an attached LIS3DH accelerometer.

PLEASE SEE THE "simple" EXAMPLE FOR AN INTRODUCTORY SKETCH,
or "doublebuffer" for animation basics.
------------------------------------------------------------------------- */

#include <Wire.h>                 // For I2C communication
#include <Adafruit_LIS3DH.h>      // For accelerometer
#include <Adafruit_Protomatter.h> // For RGB matrix

#include <JvdW_ImageReader.h>

#include <WiFi.h>
#include <HTTPClient.h>

#include <time.h>
#include <ArduinoJson.h>

#include "secrets.h"

// ----------------------------------------------------------------------------------------------------------
// LittleFS (was SPIFFS)
// ----------------------------------------------------------------------------------------------------------
// https://randomnerdtutorials.com/esp32-vs-code-platformio-littlefs/
#include <LittleFS.h>
#define FORMAT_LITTLEFS_IF_FAILED true

// ----------------------------------------------------------------------------------------------------------
// Display
// ----------------------------------------------------------------------------------------------------------
#define SCREEN_HEIGHT 64 // Matrix height (pixels) - SET TO 64 FOR 64x64 MATRIX!
#define SCREEN_WIDTH 64  // Matrix width (pixels)
#define MAX_FPS 45       // Maximum redraw rate, frames/second

#if defined(_VARIANT_MATRIXPORTAL_M4_) // MatrixPortal M4
uint8_t rgbPins[] = {7, 8, 9, 10, 11, 12};
uint8_t addrPins[] = {17, 18, 19, 20, 21};
uint8_t clockPin = 14;
uint8_t latchPin = 15;
uint8_t oePin = 16;
#else // MatrixPortal ESP32-S3
uint8_t rgbPins[] = {42, 41, 40, 38, 39, 37};
uint8_t addrPins[] = {45, 36, 48, 35, 21};
uint8_t clockPin = 2;
uint8_t latchPin = 47;
uint8_t oePin = 14;
#endif

#if SCREEN_HEIGHT == 16
#define NUM_ADDR_PINS 3
#elif SCREEN_HEIGHT == 32
#define NUM_ADDR_PINS 4
#elif SCREEN_HEIGHT == 64
#define NUM_ADDR_PINS 5
#endif

Adafruit_Protomatter matrix(
    SCREEN_WIDTH, 4, 1, rgbPins, NUM_ADDR_PINS, addrPins,
    clockPin, latchPin, oePin, true);

Adafruit_LIS3DH accel = Adafruit_LIS3DH();

uint32_t prevTime = 0; // Used for frames-per-second throttle

GFXcanvas16 *top_canvas, *bottom_canvas;

void err(int x) {
    uint8_t i;
    pinMode(LED_BUILTIN, OUTPUT);         // Using onboard LED
    for (i = 1;; i++) {                   // Loop forever...
        digitalWrite(LED_BUILTIN, i & 1); // LED on/off blink to alert user
        delay(x);
    }
}

// ----------------------------------------------------------------------------------------------------------
// Wifi
// ----------------------------------------------------------------------------------------------------------
char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASSWORD;
WiFiClient client;

// ----------------------------------------------------------------------------------------------------------
// Time
// ----------------------------------------------------------------------------------------------------------
const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 10 * 3600L;
const int daylightOffset_sec = 0;

// ----------------------------------------------------------------------------------------------------------
// Weather API
// ----------------------------------------------------------------------------------------------------------
// #define SIMULATE_WEATHER_API
// Use cityname, country code where countrycode is ISO3166 format.
// E.g. "New York, US" or "London, GB"
// String LOCATION = "Melbourne";
String LOCATION = "Langwarrin";
String FULL_LOCATION = LOCATION + ",%20AU";
String openweather_token = OPENWEATHER_TOKEN;
String UNITS = "metric"; // can pick 'imperial' or 'metric' as part of URL query
const uint32_t WEATHER_INTERVAL_MIN = 10;

float CURRENT_TEMP, CURRENT_WIND, CURRENT_HUMIDITY;
String CURRENT_ICON;

//  Set up from where we'll be fetching data
String DATA_SOURCE = "http://api.openweathermap.org/data/2.5/weather?q=" + FULL_LOCATION + "&units=" + UNITS + "&appid=" + openweather_token;

const uint8_t ICON_COUNT = 9, INDICATOR_COUNT_TOP = 3, INDICATOR_COUNT_BOTTOM = 2;
Adafruit_ImageReader img_reader(LittleFS);
Adafruit_Image img, icon[ICON_COUNT][2], ind_top[INDICATOR_COUNT_TOP], *current_icon = NULL, *previous_icon = NULL;
String icon_names[ICON_COUNT] = {
    "01",
    "02", // "few clouds"
    "03", // "scattered clouds"
    "04", // "broken clouds"
    "09", // "shower rain"
    "10", // "rain"
    "11", // "thunderstorm"
    "13", // "snow"
    "50"  // "mist"
};

// ----------------------------------------------------------------------------------------------------------
// Weather Icons and Indicators
// ----------------------------------------------------------------------------------------------------------
String indicator_names[INDICATOR_COUNT_TOP] = {
    "temp",
    "wind",
    "humidity"};

enum TopIndicator {
    IndTemperature = 0,
    IndWind = 1,
    IndHumidity = 2
};

enum BottomIndicator {
    IndTime = 0,
    IndLocation = 1
};

struct IndicatorAttr
{
    int16_t x;
    uint16_t w;
    uint16_t pause_ms;
};

const uint16_t WIDTH_TEMP = SCREEN_WIDTH, WIDTH_WIND = SCREEN_WIDTH, WIDTH_HUMIDITY = SCREEN_WIDTH;
int16_t indicator_left_x_top = 0, total_w_top = 0;

IndicatorAttr indicator_info_top[INDICATOR_COUNT_TOP] = {
    {.x = 0, .w = WIDTH_TEMP, .pause_ms = 15000},
    {.x = 0, .w = WIDTH_WIND, .pause_ms = 5000},
    {.x = 0, .w = WIDTH_HUMIDITY, .pause_ms = 3000}};

const uint16_t WIDTH_TIME = SCREEN_WIDTH, WIDTH_LOCATION = LOCATION.length() * 6 + SCREEN_WIDTH / 4;
int16_t indicator_left_x_bottom = 0, total_w_bottom = 0;
IndicatorAttr indicator_info_bottom[INDICATOR_COUNT_BOTTOM] = {
    {.x = 0, .w = WIDTH_TIME, .pause_ms = 15000},
    {.x = 0, .w = WIDTH_LOCATION, .pause_ms = 0}};

uint64_t waiting_time_top, waiting_time_bottom;

const uint8_t OFFSET_TEXT_TOP_Y = 2, OFFSET_IMG_TOP_Y = 0;
const uint8_t OFFSET_IMG_MID_Y = 33;
const uint8_t OFFSET_TEXT_BOTTOM_Y = 2;

const uint8_t IND_WIDTH = 8, IND_HEIGHT = 10;
const uint16_t CANVAS_Y_TOP = 0, CANVAS_Y_BOTTOM = SCREEN_HEIGHT - 1 - IND_HEIGHT;

// ----------------------------------------------------------------------------------------------------------
// Waiting ....
// ----------------------------------------------------------------------------------------------------------
TaskHandle_t task_animate;
volatile uint8_t do_animation = 1;

void animate_wait(void *p) {
    int8_t x = 8, r = 5, y = 48 + 8, c = matrix.color565(117, 7, 135);
    while (true) {
        if (do_animation) {
            matrix.fillRect(0, 48, SCREEN_WIDTH, 16, 0);
            matrix.fillCircle(x, y, r, c);
            x++;
            if (x > (63 + r)) x = -r;
            matrix.show(); // Copy data to matrix buffers
        }
        delay(10);
    }
}

// ----------------------------------------------------------------------------------------------------------
// Time
// ----------------------------------------------------------------------------------------------------------
void printLocalTime() {
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }
    Serial.println(&timeinfo, "%A, %d %B %Y %H:%M:%S zone %Z %z");
}

void initTime() {
    Serial.printf("Getting Network time\n");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    printLocalTime();
}

// ----------------------------------------------------------------------------------------------------------
// METHOD: Get weather update
// ----------------------------------------------------------------------------------------------------------
void get_weather_icon() {
    current_icon = NULL;
    String icon_code = CURRENT_ICON.substring(0, 2);
    Serial.printf("CURRENT_ICON:[%s]\n", CURRENT_ICON.c_str());
    for (uint8_t i = 0; i < ICON_COUNT; i++) {
        if (icon_code == icon_names[i]) {
            current_icon = &icon[i][CURRENT_ICON.endsWith("n") ? 1 : 0];
            Serial.printf("MATCH!\n");
            return;
        }
    }
    Serial.printf("NO MATCH!\n");
}

// ----------------------------------------------------------------------------------------------------------
// METHOD: Get weather update
// ----------------------------------------------------------------------------------------------------------
void get_weather() {
    JsonDocument doc;
#if defined(SIMULATE_WEATHER_API)
    deserializeJson(
        doc,
        "{"
        "\"weather\":[{\"id\":803,\"main\":\"Clouds\",\"description\":\"broken clouds\",\"icon\":\"10d\"}],"
        "\"main\":{\"temp\":16.58,\"feels_like\":15.55,\"humidity\":48}"
        "}");
#else
    WiFiClient wifi;
    HTTPClient client;
    Serial.printf("%s\n", DATA_SOURCE.c_str());
    client.begin(DATA_SOURCE);
    client.GET();
    String response = client.getString();
    Serial.printf("%s\n", response.c_str());

    deserializeJson(doc, response.c_str());

    // {
    //     "coord":{"lon":144.9633,"lat":-37.814},
    //     "weather":[{"id":803,"main":"Clouds","description":"broken clouds","icon":"04n"}],
    //     "base":"stations",
    //     "main":{"temp":16.58,"feels_like":15.55,"temp_min":15.53,"temp_max":17.34,"pressure":1003,"humidity":48,"sea_level":1003,"grnd_level":999},
    //     "visibility":10000,
    //     "wind":{"speed":8.23,"deg":330,"gust":13.38},
    //     "clouds":{"all":59},
    //     "dt":1725094432,
    //     "sys":{"type":2,"id":2080970,"country":"AU","sunrise":1725050610,"sunset":1725091077},
    //     "timezone":36000,"id":2158177,"name":"Melbourne","cod":200
    // }
#endif
    CURRENT_TEMP = doc["main"]["temp"];
    CURRENT_WIND = doc["wind"]["speed"];
    CURRENT_WIND *= 3.6f;
    CURRENT_HUMIDITY = doc["main"]["humidity"];
    CURRENT_ICON = (String)doc["weather"][0]["icon"];
}

TaskHandle_t task_weather;
void weather_task(void *) {
    const uint32_t WEATHER_INTERVAL_MS = WEATHER_INTERVAL_MIN * 60 * 1000;
    uint64_t next_check = 0;
    while (1) {
        uint64_t now = millis();
        if (now > next_check) {
            next_check += WEATHER_INTERVAL_MS;
            Serial.printf("Getting weather for %s\n", FULL_LOCATION.c_str());
            get_weather();
            get_weather_icon();
        }
        delay(100);
    }
}

// ----------------------------------------------------------------------------------------------------------
// METHOD: Display the Temperature
// ----------------------------------------------------------------------------------------------------------
void display_indicator(GFXcanvas16 *canvas, const char *format, float value, TopIndicator indicator, uint16_t text_colour_565) {
    uint8_t indicator_index = (uint8_t)indicator, left_x = SCREEN_WIDTH / 2 + indicator_info_top[indicator_index].x;

    char temp_buffer[16];
    snprintf(temp_buffer, sizeof(temp_buffer), format, value);
    uint16_t pixels = 3 * strlen(temp_buffer) + IND_WIDTH / 2 + 1;

    left_x -= pixels;

    if (ind_top[indicator_index].canvas.canvas16)
        canvas->drawRGBBitmap(left_x, 0, ind_top[indicator_index].canvas.canvas16->getBuffer(), IND_WIDTH, IND_HEIGHT);
    left_x += IND_WIDTH + 2;

    canvas->setCursor(left_x, OFFSET_TEXT_TOP_Y);
    canvas->setTextColor(text_colour_565);
    canvas->printf(temp_buffer);
}

void display_temperature(GFXcanvas16 *canvas) {
    display_indicator(canvas, "%.1f"
                              "\xF8"
                              "C",
                      CURRENT_TEMP, IndTemperature, matrix.color565(255, 237, 128)); // light yellow
}

// ----------------------------------------------------------------------------------------------------------
// METHOD: Display the Wind
// ----------------------------------------------------------------------------------------------------------
void display_wind(GFXcanvas16 *canvas) {
    display_indicator(canvas, "%.0f km/h", CURRENT_WIND, IndWind, matrix.color565(255, 192, 255)); // purplish
}

// ----------------------------------------------------------------------------------------------------------
// METHOD: Display the Humidity
// ----------------------------------------------------------------------------------------------------------
void display_humidity(GFXcanvas16 *canvas) {
    display_indicator(canvas, "%.0f%%%%", CURRENT_HUMIDITY, IndHumidity, matrix.color565(128, 192, 255)); // cyanish
}

// ----------------------------------------------------------------------------------------------------------
// METHOD: Display the Time
// ----------------------------------------------------------------------------------------------------------
void display_time(GFXcanvas16 *canvas) {
    uint8_t indicator_index = (uint8_t)IndTime, left_x = SCREEN_WIDTH / 2 + indicator_info_bottom[indicator_index].x;

    char temp_buffer[16];
    struct tm timeinfo;
    getLocalTime(&timeinfo);
    snprintf(temp_buffer, sizeof(temp_buffer), "%02d%c%02d", timeinfo.tm_hour, timeinfo.tm_sec % 2 ? ':' : ' ', timeinfo.tm_min);
    uint16_t pixels = 3 * strlen(temp_buffer);

    left_x -= pixels;

    canvas->setCursor(left_x, OFFSET_TEXT_BOTTOM_Y);
    canvas->setTextColor(matrix.color565(255, 255, 255)); // white
    canvas->printf(temp_buffer);
}

// ----------------------------------------------------------------------------------------------------------
// METHOD: Display the Location
// ----------------------------------------------------------------------------------------------------------
void display_location(GFXcanvas16 *canvas) {
    uint8_t indicator_index = (uint8_t)IndLocation, left_x = indicator_info_bottom[indicator_index].x;

    char temp_buffer[16];
    snprintf(temp_buffer, sizeof(temp_buffer), "%s", LOCATION.c_str());

    canvas->setCursor(left_x, OFFSET_TEXT_BOTTOM_Y);
    canvas->setTextColor(matrix.color565(128, 255, 128)); // green
    canvas->printf(temp_buffer);
}

// ==========================================================================================================
// SETUP
// ==========================================================================================================
void setup(void) {
    Serial.begin(115200);
    // while (!Serial) delay(10);
    delay(3000);

    Serial.printf("\nBEGIN\n");

    if (!LittleFS.begin(FORMAT_LITTLEFS_IF_FAILED)) {
        Serial.println("LittleFS Mount Failed");
    }

    Serial.println("Loading image");
    ImageReturnCode rc = img_reader.loadBMP("/loading24.bmp", img, 0.7 * 256);
    if (rc == IMAGE_SUCCESS) {
        Serial.printf("Image LOADED! [%d x %d]\n", img.width(), img.height());

    } else {
        Serial.printf("Image load FAILED : [%d]\n", (uint8_t)rc);
    }

    // Load the weather icons
    for (uint8_t i = 0; i < ICON_COUNT; i++) {
        for (uint8_t j = 0; j < 2; j++) {
            String icon_name = "/" + icon_names[i] + (j == 0 ? "d" : "n") + ".bmp";
            rc = img_reader.loadBMP(icon_name.c_str(), icon[i][j]);
            Serial.printf("Weather icon [%d/%d:%s] ", i, j, icon_name.c_str());
            if (rc == IMAGE_SUCCESS) {
                Serial.printf("LOADED! [%d x %d]\n", icon[i][j].width(), icon[i][j].height());
            } else {
                Serial.printf("FAILED : [%d]\n", (uint8_t)rc);
            }
        }
    }
    Serial.println();

    // Load the indicators
    for (uint8_t i = 0; i < INDICATOR_COUNT_TOP; i++) {
        String indicator_name = "/ind_" + indicator_names[i] + ".bmp";
        rc = img_reader.loadBMP(indicator_name.c_str(), ind_top[i]);
        Serial.printf("Top indicator [%d:%s] ", i, indicator_name.c_str());
        if (rc == IMAGE_SUCCESS) {
            Serial.printf("LOADED! [%d x %d]\n", ind_top[i].width(), ind_top[i].height());

        } else {
            Serial.printf("FAILED : [%d]\n", (uint8_t)rc);
        }
    }
    Serial.println();

    // Initialise the x-offsets of the top indicators (the first is 0, the rest build on the width of the previous one)
    indicator_info_top[0].x = 0;
    for (uint8_t i = 1; i < INDICATOR_COUNT_TOP; i++) {
        indicator_info_top[i].x = indicator_info_top[i - 1].x + indicator_info_top[i - 1].w;
    }
    total_w_top = indicator_info_top[INDICATOR_COUNT_TOP - 1].x + indicator_info_top[INDICATOR_COUNT_TOP - 1].w;

    for (uint8_t i = 0; i < INDICATOR_COUNT_TOP; i++) {
        Serial.printf("x[%d] = %d\n", i, indicator_info_top[i].x);
    }
    Serial.printf("total_w_top = %d\n", total_w_top);

    // create the top canvas
    top_canvas = new GFXcanvas16(total_w_top, IND_HEIGHT);
    top_canvas->cp437(true);
    top_canvas->setTextWrap(false);

    // Initialise the x-offsets of the top indicators (the first is 0, the rest build on the width of the previous one)
    indicator_info_bottom[0].x = 0;
    for (uint8_t i = 1; i < INDICATOR_COUNT_BOTTOM; i++) {
        indicator_info_bottom[i].x = indicator_info_bottom[i - 1].x + indicator_info_bottom[i - 1].w;
    }
    total_w_bottom = indicator_info_bottom[INDICATOR_COUNT_BOTTOM - 1].x + indicator_info_bottom[INDICATOR_COUNT_BOTTOM - 1].w;

    for (uint8_t i = 0; i < INDICATOR_COUNT_BOTTOM; i++) {
        Serial.printf("x[%d] = %d\n", i, indicator_info_bottom[i].x);
    }
    Serial.printf("total_w_bottom = %d\n", total_w_bottom);

    // create the bottom canvas
    bottom_canvas = new GFXcanvas16(total_w_bottom, IND_HEIGHT);
    bottom_canvas->cp437(true);
    bottom_canvas->setTextWrap(false);

    ProtomatterStatus status = matrix.begin();
    Serial.printf("Protomatter begin() status: %d\n", status);

    if (!accel.begin(0x19)) {
        Serial.println("Couldn't find accelerometer");
        err(250); // Fast bink = I2C error
    }
    accel.setRange(LIS3DH_RANGE_4_G); // 2, 4, 8 or 16 G!

    matrix.fillScreen(0x0);
    matrix.drawRGBBitmap(0, 16, img.canvas.canvas16->getBuffer(), SCREEN_WIDTH, 32);
    matrix.show(); // Copy data to matrix buffers

    xTaskCreatePinnedToCore(animate_wait, "animate", 4096, NULL, 2, &task_animate, 0);
    delay(1000);

    // attempt to connect to Wifi network:
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);

    WiFi.begin(ssid, pass);
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(5);
    }
    Serial.printf("\n");

    // Init and get the time
    initTime();

    xTaskCreatePinnedToCore(weather_task, "weather", 4096, NULL, 2, &task_weather, 0);
    // get_weather();
    // get_weather_icon();

    delay(1000);
    while (millis() < 10000) {
        delay(100);
    }

    do_animation = 0;
    waiting_time_top = millis() + indicator_info_top[0].pause_ms;
}

// ==========================================================================================================
// MAIN LOOP
// ==========================================================================================================
// const int16_t r = 15, scale = 2;
// int16_t x = 16 << scale, y = 16 << scale, dx = 1, dy = 2;
int16_t icon_x = 0;
const uint16_t icon_bits = 7, icon_mod = (1 << icon_bits);
uint8_t icon_direction = 1;

void loop() {
    // Limit the animation frame rate to MAX_FPS.
    // uint32_t t;
    // while (((t = micros()) - prevTime) < (1000000L / MAX_FPS))
    //     ;
    // prevTime = t;

    // Read accelerometer...
    sensors_event_t event;
    accel.getEvent(&event);
    // Serial.printf("(%0.1f, %0.1f, %0.1f)\n", event.acceleration.x, event.acceleration.y, event.acceleration.z);

    double xx, yy, zz;
    xx = event.acceleration.x * 1000;
    yy = event.acceleration.y * 1000;
    zz = event.acceleration.z * 1000;

    // Update pixel data in LED driver
    matrix.fillScreen(0x0);

    // uint32_t t = (millis() / 1000) % (2 * ICON_COUNT);
    // int8_t i = t / 2, j = t % 2;

    // Adafruit_Image *bg = &icon[i][j];
    // matrix.drawRGBBitmap(32 - bg->width() / 2, 32 - bg->height() / 2, bg->canvas.canvas16->getBuffer(), bg->width(), bg->height());

    if (previous_icon != current_icon) {
        previous_icon = current_icon;
    }
    if (previous_icon != NULL) {
        uint8_t k0 = (icon_x % icon_mod), k1 = icon_mod - k0;
        uint16_t W = previous_icon->width(), H = previous_icon->height();
        uint16_t bmp[W * H];
        memset(bmp, 0, W * H * 2);
        uint16_t *icon_buffer = previous_icon->canvas.canvas16->getBuffer();
        for (uint8_t y = 0; y < previous_icon->height() - 1; y++) {
            for (uint8_t x = 0; x < previous_icon->width() - 1; x++) {
                uint16_t c0 = icon_buffer[y * W + x], c1 = icon_buffer[y * W + x + 1];
                uint16_t r0 = (c0 >> 11), g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
                uint16_t r1 = (c1 >> 11), g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
                r0 = ((r0 * k0 + r1 * k1) >> icon_bits) << 11;
                g0 = ((g0 * k0 + g1 * k1) >> icon_bits) << 5;
                b0 = (b0 * k0 + b1 * k1) >> icon_bits;
                bmp[y * W + x + 1] = r0 | g0 | b0;
            }
        }
        // matrix.drawRGBBitmap(32 - previous_icon->width() / 2, 32 - previous_icon->height() / 2, previous_icon->canvas.canvas16->getBuffer(), previous_icon->width(), previous_icon->height());
        matrix.drawRGBBitmap(icon_x >> icon_bits, 32 - H / 2, bmp, W, H);

        if (icon_direction) {
            icon_x++;
            if (icon_x == ((63 - W) * icon_mod) - 1) {
                icon_direction = 0;
            }

        } else {
            icon_x--;
            if (icon_x == 0) {
                icon_direction = 1;
            }
        }
    }

    // are we animating or waiting?
    uint64_t now = millis();
    if (now > waiting_time_top) {
        // not waiting any more

        indicator_left_x_top++;
        indicator_left_x_top %= total_w_top;

        for (uint8_t i = 0; i < INDICATOR_COUNT_TOP; i++) {
            // has an indicator reached the centre of the screen?
            if (indicator_info_top[i].x == indicator_left_x_top) {
                waiting_time_top = now + indicator_info_top[i].pause_ms;
            }
        }
    }

    if (now > waiting_time_bottom) {
        // not waiting any more

        indicator_left_x_bottom++;
        indicator_left_x_bottom %= total_w_bottom;

        for (uint8_t i = 0; i < INDICATOR_COUNT_BOTTOM; i++) {
            // has an indicator reached the centre of the screen?
            if (indicator_info_bottom[i].x == indicator_left_x_bottom) {
                waiting_time_bottom = now + indicator_info_bottom[i].pause_ms;
            }
        }
    }

    // erase the top canvas
    top_canvas->fillRect(0, 0, total_w_top, IND_HEIGHT, 0);
    // display the scrolling items in the top lane. they each check if they are in view before rendering anything
    display_temperature(top_canvas);
    display_wind(top_canvas);
    display_humidity(top_canvas);
    matrix.drawRGBBitmap(-indicator_left_x_top, CANVAS_Y_TOP, top_canvas->getBuffer(), top_canvas->width(), IND_HEIGHT);
    if (indicator_left_x_top >= (total_w_top - SCREEN_WIDTH)) {
        matrix.drawRGBBitmap(total_w_top - indicator_left_x_top, CANVAS_Y_TOP, top_canvas->getBuffer(), top_canvas->width(), IND_HEIGHT);
    }

    // erase the bottom canvas
    bottom_canvas->fillRect(0, 0, total_w_bottom, IND_HEIGHT, 0);
    // display the scrolling items in the bottom lane. they each check if they are in view before rendering anything
    display_time(bottom_canvas);
    display_location(bottom_canvas);
    matrix.drawRGBBitmap(-indicator_left_x_bottom, CANVAS_Y_BOTTOM, bottom_canvas->getBuffer(), bottom_canvas->width(), IND_HEIGHT);
    if (indicator_left_x_bottom >= (total_w_bottom - SCREEN_WIDTH)) {
        matrix.drawRGBBitmap(total_w_bottom - indicator_left_x_bottom, CANVAS_Y_BOTTOM, bottom_canvas->getBuffer(), bottom_canvas->width(), IND_HEIGHT);
    }

    matrix.show(); // Copy data to matrix buffers
}
