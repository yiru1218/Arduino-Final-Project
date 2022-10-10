/*
   D0=3(RX),D1=1(TX),D2=16,D3=5,D4=4
   D5=0,D6=2,D7=14,D8=12,D9=13
   D10=15,D11=13,D12=12,D13=14
*/

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <BlynkSimpleEsp8266.h>
#include "HX711.h"

boolean IsStart = false ;
int waterflowTime = 30 * 1000;
int balanceTemp = 25;
unsigned long startTime; // 抽水馬達持續時間
unsigned long duration ; // 持續時間
int ButtonStatePrevious = 0;

/* -----------WiFi credentials----------- */
char ssid[] = "mouse";
char pass[] = "12345678";

/* -----------Relay----------- */
int WaterRelay = 14;
int TempRelay = 13;
int Temprelaystate = 0; //off
int RelayVButton = 0;  //off state for virtual button
int waterboolean;
int tempboolean;
int TempState;

/* -----------DS18B20----------- */
#define ONE_WIRE_BUS 12 //D6
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
float temp1_check = 0, temp2_check = 0, temp3_check = 0;

/* -----------HX711----------- */
int DTPin = 5;
int CKPin = 4;
int reading_HX711;
HX711 scale;


/* -----------TIMER----------- */
#include <SimpleTimer.h>
SimpleTimer timer;

/* -----------Blynk----------- */
#define BLYNK_PRINT Serial    // Comment this out to disable prints and save space
char auth[] = "HO1CBApPyTB3G50b8tLRmDvRUUrVLafo";
WidgetLCD lcd(V1);
WidgetLCD relaylcd(V2);
WidgetLED waterled(V4);
WidgetLED templed(V5);
WidgetLED hx711(V6);

/* -----------Thingspeak----------- */
#include <ESP8266HTTPClient.h>
String UrlString;
HTTPClient _httpClient;
long previousTime = 0;
unsigned long currentTime = millis();
long interval = 15000; //一秒=1000
int _httpGET(String url) {
  _httpClient.end();
  _httpClient.begin(url);
  return _httpClient.GET();
}

BLYNK_CONNECTED() {
  pinMode(WaterRelay, OUTPUT);
  pinMode(TempRelay, OUTPUT);
  pinMode(V5, INPUT);
  lcd.clear(); //Use it to clear the LCD Widget
  relaylcd.clear(); //Use it to clear the LCD Widget
  relaylcd.print(0, 0, "Water:");
  relaylcd.print(0, 1, "Temp:");
  Blynk.virtualWrite(V3, LOW); /*****/
  Blynk.syncVirtual(V3); /*****/

}

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  scale.begin(DTPin, CKPin);
  scale.set_scale();  // Start scale
  scale.tare();       // Reset scale to zero

  //digitalWrite(WaterRelay, LOW);
  /*
    lcd.begin (16, 2); // for 16 x 2 LCD module
    lcd.setBacklightPin(3, POSITIVE);
    lcd.setBacklight(LOW);
    lcd.home();
  */
  timer.setInterval(500L, getSendData);

  while (!Serial)continue;
  Blynk.begin(auth, ssid, pass);

  /* -----------OTA(無線上傳)----------- */
  Serial.println("Booting");
  lcd.print(0, 0, "Reconnecting.  ");
  lcd.print(0, 0, "Reconnecting.. ");
  lcd.print(0, 0, "Reconnecting...");
  lcd.print(0, 0, "               ");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);
  lcd.print(0, 0, "WiFi connect to");
  lcd.print(0, 1, ssid);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }
  ArduinoOTA.onStart([]() {

    Serial.println("Start");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    else if (error == OTA_END_ERROR) Serial.println("End Failed");
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

}

/* -----------加熱Relay----------- */
void heating() {
  if (reading_HX711 <= 850) {
    digitalWrite(TempRelay, HIGH); //OFF
    relaylcd.print(5, 1, "水太少無法加熱 ");
    tempboolean = 0;
    templed.off();
    Blynk.virtualWrite(15, tempboolean);
    Blynk.virtualWrite(V3, LOW);
  } else if (sensors.getTempCByIndex(0) >= balanceTemp) {
    digitalWrite(TempRelay, HIGH); //OFF
    relaylcd.print(5, 1, "水溫已達到");
    relaylcd.print(10, 1, balanceTemp);
    tempboolean = 0;
    templed.off();
    Blynk.virtualWrite(15, tempboolean);
    Blynk.virtualWrite(V3, LOW);
  } else {
    digitalWrite(TempRelay, LOW); //ON
    relaylcd.print(5, 1, "加熱中       ");
    tempboolean = 1;
    templed.on();
    Blynk.virtualWrite(15, tempboolean);
  }
}

/* -----------控制溫度----------- */
BLYNK_WRITE(V3) { //called when V3 updated
  int buttonState = param.asInt() + 0; //獲取Blynk按鈕實際輸入值as Int
  //  relaylcd.print(5, 1, buttonState);
  if (buttonState == 1) { //按按鈕
    TempState = 1;
    while (duration >= waterflowTime) { //流速30秒後開關跳
      TempState = 0;
      digitalWrite(TempRelay, HIGH); //OFF
      relaylcd.print(5, 1, "可以享用手沖咖啡");
      tempboolean = 0;
      templed.off();
      Blynk.virtualWrite(15, tempboolean);
      Blynk.virtualWrite(V3, LOW);
      if (buttonState == 1) {
        duration = 0;//重新計算一杯咖啡
      }
    }
    heating();
  } else { //沒按按鈕
    relaylcd.print(5, 1, "未啟動按鈕   ");
    TempState = 0;
    digitalWrite(TempRelay, HIGH); //OFF
    tempboolean = 0;
    templed.off();
    Blynk.virtualWrite(15, tempboolean);
    Blynk.virtualWrite(V3, LOW);
  }
  //    if (buttonState == 1) {
  //      relaylcd.print(5, 1, "123");
  //    } else if (buttonState == 0) {
  //      relaylcd.print(5, 1, "456");
  //    }
}


void getSendData() {
  sensors.requestTemperatures(); //讀取溫度值請求

  /* -----------測量HX711值----------- */
  if (scale.is_ready()) {
    long reading = scale.read();
    Serial.print("HX711 reading:");
    reading_HX711 = ((35955 + reading) / 405); //杯子:452g--->672g
    if (reading_HX711 < 0) {
      reading_HX711 = 0;
    }
    if (reading_HX711 != -278) {
      Serial.println(reading_HX711);
      Blynk.virtualWrite(12, reading_HX711);
      if (reading_HX711 > 760) {
        hx711.on();
      } else {
        hx711.off();
      }
    } else {
      Serial.println("HX711 not found");
      Blynk.virtualWrite(12, reading_HX711);
    }
  }

  // put your main code here, to run repeatedly:
  float temp1 = sensors.getTempCByIndex(0);
  float temp2 = sensors.getTempCByIndex(1);
  float temp3 = sensors.getTempCByIndex(2);

  //這邊的bug沒有處理完
  if ((temp1 && temp2 && temp3) != -127) {
    if ((temp1 - temp1_check) <= 15 || (temp1 - temp1_check) >= -15) { //若兩次溫差>=15 不計入Data
      temp1_check = temp1;
      Blynk.virtualWrite(10, temp1_check); //virtual pin V10
    }
    if ((temp2 - temp2_check) <= 15 || (temp2 - temp2_check) >= -15) { //若兩次溫差>=15 不計入Data
      temp2_check = temp2;
      Blynk.virtualWrite(11, temp2_check); //virtual pin V10
    }
    if ((temp3 - temp3_check) <= 15 || (temp3 - temp1_check) >= -15) { //若兩次溫差>=15 不計入Data
      temp3_check = temp3;
      Blynk.virtualWrite(13, temp3_check); //virtual pin V10
    }
  }

  /* -----------控制抽水馬達----------- */
  /* 目前可以控制秒數出水 */

  if (TempState == 1) {
    if (temp1_check < balanceTemp ) {
      digitalWrite(WaterRelay, HIGH); //OFF
      relaylcd.print(6, 0, "水溫不夠無法抽水");
      waterboolean = 0;
      waterled.off();
      Blynk.virtualWrite(14, waterboolean);
    } else {//如果溫度到了
      while (reading_HX711 <= 850)
        relaylcd.print(6, 0, "水太少無法抽水  ");
      digitalWrite(WaterRelay, HIGH); //OFF
      if (IsStart == false) {
        IsStart = true;
        startTime = millis(); //紀錄起始時間
      } else { //IsStart=true
        duration = millis() - startTime;
        digitalWrite(WaterRelay, LOW);  //ON
        relaylcd.print(6, 0, "目前已沖泡");
        relaylcd.print(11, 0, duration * 2.2 / 1000);
        waterboolean = 1;
        waterled.on();
        Blynk.virtualWrite(14, waterboolean);
      }
    }
  } else {
    relaylcd.print(6, 0, "未啟動加熱鈕");
  }


  if (duration >= waterflowTime) {
    IsStart = false;
    digitalWrite(WaterRelay, HIGH); //OFF
    relaylcd.print(6, 0, "可以享用手沖咖啡  ");
    waterboolean = 0;
    waterled.off();
    Blynk.virtualWrite(14, waterboolean);
  }
  Serial.print(startTime);
  Serial.print(" , ");
  Serial.print(duration);
  Serial.print(" , ");

  Blynk.virtualWrite(16, startTime);
  Blynk.virtualWrite(17, duration);

  Serial.print(temp1_check);
  Serial.print(" , ");
  Serial.print(temp2_check);
  Serial.print(" , ");
  Serial.println(temp3_check);

  /* -----------LCD----------- */
  /*int fsr_value = analogRead(fsr_pin); //讀取fsr402
    int map_value = map(fsr_value, 0, 1023, 0, 255); //從0~1023映射到0~255
  */

}

/* -----------Send data to thingspeak----------- */
void thingspeak() {
  UrlString = String(u8"http://api.thingspeak.com/update?api_key=OSWWJPTHMKPYU6L8&field1=") + String(temp1_check)
              + "&field2=" + String(temp2_check)
              + "&field3=" + String(temp3_check)
              + "&field4=" + String(waterboolean)
              + "&field5=" + String(tempboolean)
              + "&field6=" + String(reading_HX711);
  if (_httpGET(UrlString) > 0) {
    if ((currentTime - previousTime) >= interval) { //等待15秒上傳一次資料到Thingspeak
      previousTime = currentTime; //紀錄更新時間
    }
  }
}

void loop() {
  ArduinoOTA.handle();
  timer.run(); // Initiates SimpleTimer
  Blynk.run();

}
