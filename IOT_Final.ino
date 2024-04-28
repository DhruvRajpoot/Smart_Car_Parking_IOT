#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <ESP32Servo.h>
#include <HTTPClient.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <vector>

#define SS_PIN 33
#define RST_PIN 27

#define ir_front 15
#define ir_back 2
#define ir_car1 13
#define ir_car2 14
#define ir_car3 26
#define ir_car4 35

#define red_led_pin 12
#define green_led_pin 25
#define servo_pin 5
#define buzzer_pin 4

#define MAX_PARKED_CARS 4

LiquidCrystal_I2C lcd(0x27, 16, 2);
Servo myservo;
MFRC522 mfrc522(SS_PIN, RST_PIN);

// Slots Variable
int S1 = 0, S2 = 0, S3 = 0, S4 = 0;
int emptySlots = 4;
int numParkedCars = 0;
String parkedCars[MAX_PARKED_CARS];
std::vector<String> registeredCars = { "D34559FB", "833E2011", "63972FF6", "13A53211", "D32A2D11" };
String car_direction = "";

// Wifi
const char* ssid = "Roomies";
const char* password = "utnd@27240803";
WiFiClient espClient;
PubSubClient client(espClient);

//MQTT
const char* mqtt_username = "hivemq";
const char* mqtt_password = "public";
const char* broker = "broker.hivemq.com";

// Time Synchronizing
String current_time = "";
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 5 * 3600 + 30 * 60;
const int daylightOffset_sec = 0;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, ntpServer, gmtOffset_sec, daylightOffset_sec);

// Variables for the number of columns and rows on the LCD.
int lcdColumns = 20;
int lcdRows = 4;

// Variable to read data from RFID-RC522.
char str[32] = "";
String cardID = "";

// Google script Web_App_URL & data variables
String Web_App_URL = "https://script.google.com/macros/s/AKfycbzE2YTu_-N7HPyqMWakGgJw7AEEMRtt7L7Ni94LgpRZvsumGuFrVn6vK0Uma58BATY/exec";
String reg_Info = "";
String atc_Info = "";
String atc_Name = "";
String atc_Date = "";
String atc_Time_In = "";
String atc_Time_Out = "";
String modes = "atc";

// Setup Function
void setup() {
  Serial.begin(9600);
  while (!Serial)
    ;
  Serial.println("Serial monitor opened");

  SPI.begin();  // Initialize SPI bus
  Serial.println("SPI initialized");

  mfrc522.PCD_Init();  // Initialize MFRC522 RFID reader
  Serial.println("RFID reader initialized");

  pinMode(ir_car1, INPUT);
  pinMode(ir_car2, INPUT);
  pinMode(ir_car3, INPUT);
  pinMode(ir_car4, INPUT);
  pinMode(ir_front, INPUT);
  pinMode(ir_back, INPUT);
  pinMode(buzzer_pin, OUTPUT);
  pinMode(red_led_pin, OUTPUT);
  pinMode(green_led_pin, OUTPUT);

  myservo.attach(servo_pin);
  myservo.write(90);

  lcd.begin();
  lcd.backlight();
  lcd.print("Car Parking System");
  delay(3000);
  lcd.clear();

  Serial.println("Car parking system");
  Serial.println("-------------");
  Serial.println("WIFI mode : STA");
  WiFi.mode(WIFI_STA);
  Serial.print("Connecting to : ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  int connecting_process_timed_out = 20;  // seconds
  connecting_process_timed_out = connecting_process_timed_out * 2;

  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");

    lcd.setCursor(0, 0);
    lcd.print("Connecting to SSID :");
    lcd.setCursor(0, 2);
    lcd.print(ssid);
    delay(1500);

    lcd.clear();
    delay(250);

    if (connecting_process_timed_out > 0) connecting_process_timed_out--;
    if (connecting_process_timed_out == 0) {
      delay(1000);
      ESP.restart();
    }
  }

  Serial.println();
  Serial.print("WiFi connected to : ");
  Serial.println(ssid);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("WiFi connected : ");
  lcd.setCursor(0, 2);
  lcd.print(ssid);
  delay(1500);

  client.setServer("broker.hivemq.com", 1883);
  client.setCallback(callback);

  timeClient.begin();
  timeClient.setTimeOffset(gmtOffset_sec);

  lcd.clear();
  delay(500);

  Read_IR_Sensor();
  emptySlots = 4 - (S1 + S2 + S3 + S4);

  Serial.println("Setup complete");
}

// Main Loop Function
void loop() {

  timeClient.update();
  current_time = timeClient.getFormattedTime();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  delay(500);

  Read_IR_Sensor();

  rfid_scanning_func();

  if (digitalRead(ir_front) == 0) {
    if (car_direction == "leaving") {
      MQTT_Publish("Ujjwal0901/m", "Thanks for using our service");
      delay(1000);
      myservo.write(90);
      car_direction = "";
      lcd.clear();
      digitalWrite(green_led_pin, false);
    }
  }

  if (digitalRead(ir_back) == 0) {
    if (car_direction == "entering") {
      delay(1000);
      myservo.write(90);
      car_direction = "";
      lcd.clear();
      digitalWrite(green_led_pin, false);
    }
  }

  if (car_direction == "") {
    if (emptySlots != (4 - numParkedCars)) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Please park properly");
      lcd.setCursor(0, 2);
      lcd.print("to avoid congestion");
      digitalWrite(red_led_pin, HIGH);
      digitalWrite(buzzer_pin, HIGH);
      MQTT_Publish("Ujjwal0901/m", "Please park properly to avoid congestion");
      delay(1500);
      lcd.clear();
      digitalWrite(buzzer_pin, LOW);
      digitalWrite(red_led_pin, LOW);
      return;
    } else {
      if (modes == "reg") {
        show_registration_msg();
      } else {
        show_slot_on_lcd();
      }
    }
  }
}

// Other functions definations

void callback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.print(". Message: ");
  String messageTemp = "";


  for (int i = 0; i < length; i++) {
    Serial.print((char)message[i]);
    messageTemp += (char)message[i];
  }
  Serial.println();

  if (String(topic) == "Ujjwal0901/modes") {
    if (messageTemp == "reg") {
      modes = "reg";
      Serial.println("Changing mode to registration mode");
    } else if (messageTemp == "atc") {
      modes = "atc";
      Serial.println("Changing mode to atc mode");
    }
  }
}

void reconnect() {
  if (!client.connected()) {
    Serial.print("connecting to : ");
    Serial.println(broker);
    if (client.connect("Ujjwal0901", mqtt_username, mqtt_password)) {
      Serial.print("connected to : ");
      Serial.println(broker);
      bool check = client.subscribe("Ujjwal0901/modes");
    } else {
      Serial.println("trying to connect...");
      delay(1000);
    }
  }
}

void MQTT_Publish(char* topic, char* message) {
  if (!client.connected()) reconnect();
  uint16_t packetIdPub1 = client.publish(String(topic).c_str(), String(message).c_str(), true);
}

void rfid_scanning_func() {
  if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {

    cardID = "";

    for (byte i = 0; i < mfrc522.uid.size; i++) {
      cardID += String(mfrc522.uid.uidByte[i], HEX);
    }

    cardID.toUpperCase();
    cardID.replace(" ", "");
    Serial.print("Detected Card ID: ");
    Serial.println(cardID);
    Serial.print("Modes : ");
    Serial.println(modes);

    bool make_http_request = false;

    if (isCarParked(cardID) == true) {
      removeParkedCar(cardID);
      make_http_request = true;
    } else {
      if (numParkedCars < MAX_PARKED_CARS) {
        if (isCardRegistered(cardID) == true) {
          parkedCars[numParkedCars] = cardID;
          numParkedCars++;
          make_http_request = true;
        } else {
          cardNotRegistered();
        }
      } else {
        slots_full();
        return;
      }
    }


    if (make_http_request) {
      lcd.clear();
      delay(100);
      lcd.setCursor(4, 0);
      lcd.print("Getting  UID");
      lcd.setCursor(4, 1);
      lcd.print("Successfully");
      lcd.setCursor(0, 2);
      lcd.print("");
      lcd.setCursor(3, 3);
      lcd.print("Please wait...");

      http_Req(modes, cardID, current_time);
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();

    delay(10);
  }
}

void show_registration_msg() {
  lcd.clear();
  lcd.setCursor(4, 0);
  lcd.print("REGISTRATION");
  lcd.setCursor(0, 1);
  lcd.print("Please tap your card");
  lcd.setCursor(2, 2);
  lcd.print("to register");
  MQTT_Publish("Ujjwal0901/m", "Please tap your card to register");
}

void show_slot_on_lcd() {
  lcd.setCursor(0, 0);
  lcd.print("Empty Slots : ");
  lcd.print(4 - numParkedCars);
  lcd.setCursor(0, 1);
  lcd.print("S1: ");
  lcd.print(S1 ? "Fill " : "Empty");
  lcd.setCursor(10, 1);
  lcd.print("S2: ");
  lcd.print(S2 ? "Fill " : "Empty");
  lcd.setCursor(0, 2);
  lcd.print("S3: ");
  lcd.print(S3 ? "Fill " : "Empty");
  lcd.setCursor(10, 2);
  lcd.print("S4: ");
  lcd.print(S4 ? "Fill " : "Empty");
}

void slots_full() {
  Serial.println("Sorry parking full");
  digitalWrite(red_led_pin, true);
  digitalWrite(buzzer_pin, HIGH);
  lcd.clear();
  lcd.print("Sorry parking full");
  MQTT_Publish("Ujjwal0901/m", "Sorry parking full.");
  delay(1000);
  lcd.clear();
  digitalWrite(buzzer_pin, LOW);
  digitalWrite(red_led_pin, false);
}

void Read_IR_Sensor() {
  S1 = 0;
  S2 = 0;
  S3 = 0;
  S4 = 0;

  if (digitalRead(ir_car1) == 0) {
    S1 = 1;
    MQTT_Publish("Ujjwal0901/slot1", "Filled");
  } else MQTT_Publish("Ujjwal0901/slot1", "Empty");

  if (digitalRead(ir_car2) == 0) {
    S2 = 1;
    MQTT_Publish("Ujjwal0901/slot2", "Filled");
  } else MQTT_Publish("Ujjwal0901/slot2", "Empty");

  if (digitalRead(ir_car3) == 0) {
    S3 = 1;
    MQTT_Publish("Ujjwal0901/slot3", "Filled");
  } else MQTT_Publish("Ujjwal0901/slot3", "Empty");

  if (digitalRead(ir_car4) == 0) {
    S4 = 1;
    MQTT_Publish("Ujjwal0901/slot4", "Filled");
  } else MQTT_Publish("Ujjwal0901/slot4", "Empty");

  emptySlots = MAX_PARKED_CARS - (S1 + S2 + S3 + S4);
}

void http_Req(String modes, String str_uid, String current_time) {
  if (WiFi.status() == WL_CONNECTED) {
    String http_req_url = "";

    if (modes == "atc") {
      http_req_url = Web_App_URL + "?sts=atc";
      http_req_url += "&uid=" + str_uid;
      http_req_url += "&time=" + current_time;
    }
    if (modes == "reg") {
      http_req_url = Web_App_URL + "?sts=reg";
      http_req_url += "&uid=" + str_uid;
      http_req_url += "&time=" + current_time;
    }

    Serial.println("-------------");
    Serial.println("Sending request to Google Sheets...");
    Serial.print("URL : ");
    Serial.println(http_req_url);

    HTTPClient http;

    http.begin(http_req_url.c_str());
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

    int httpCode = http.GET();
    Serial.print("HTTP Status Code : ");
    Serial.println(httpCode);

    String payload;
    if (httpCode > 0) {
      payload = http.getString();
      Serial.println("Payload : " + payload);
    }

    Serial.println("-------------");
    http.end();

    String sts_Res = getValue(payload, ',', 0);

    if (sts_Res == "OK") {
      if (modes == "atc") {
        atc_Info = getValue(payload, ',', 1);

        if (atc_Info == "TI_Successful") {
          carEntering(payload);
        }

        if (atc_Info == "TO_Successful") {
          carLeaving(payload);
        }

        atc_Info = "";
        atc_Name = "";
        atc_Date = "";
        atc_Time_In = "";
        atc_Time_Out = "";
      }

      if (modes == "reg") {
        reg_Info = getValue(payload, ',', 1);

        if (reg_Info == "R_Successful") {
          registrationSuccessful();
        }

        if (reg_Info == "regErr01") {
          cardAlreadyRegistered();
        }

        reg_Info = "";
      }
    }
  } else {
    wifiDisconnected();
  }
}

String getValue(String data, char separator, int index) {
  int found = 0;
  int strIndex[] = { 0, -1 };
  int maxIndex = data.length() - 1;

  for (int i = 0; i <= maxIndex && found <= index; i++) {
    if (data.charAt(i) == separator || i == maxIndex) {
      found++;
      strIndex[0] = strIndex[1] + 1;
      strIndex[1] = (i == maxIndex) ? i + 1 : i;
    }
  }
  return found > index ? data.substring(strIndex[0], strIndex[1]) : "";
}

void removeParkedCar(String carID) {
  for (int i = 0; i < numParkedCars; i++) {
    if (parkedCars[i] == carID) {
      for (int j = i; j < numParkedCars - 1; j++) {
        parkedCars[j] = parkedCars[j + 1];
      }
      numParkedCars--;
      break;
    }
  }
}

bool isCarParked(String carID) {
  for (int i = 0; i < numParkedCars; i++) {
    if (parkedCars[i] == carID) {
      return true;
    }
  }
  return false;
}

bool isCardRegistered(String cardID) {
  for (const auto& registeredID : registeredCars) {
    if (registeredID == cardID) {
      return true;
    }
  }
  return false;
}

void carEntering(String payload) {
  atc_Name = getValue(payload, ',', 2);
  atc_Date = getValue(payload, ',', 3);
  atc_Time_In = getValue(payload, ',', 4);

  int name_Lenght = atc_Name.length();
  int pos = 0;
  if (name_Lenght > 0 && name_Lenght <= lcdColumns) {
    pos = map(name_Lenght, 1, lcdColumns, 0, (lcdColumns / 2) - 1);
    pos = ((lcdColumns / 2) - 1) - pos;
  } else if (name_Lenght > lcdColumns) {
    atc_Name = atc_Name.substring(0, lcdColumns);
  }

  car_direction = "entering";

  digitalWrite(green_led_pin, true);
  lcd.clear();
  delay(100);
  lcd.setCursor(pos, 0);
  lcd.print(atc_Name);
  lcd.setCursor(0, 1);
  lcd.print("Date    : ");
  lcd.print(atc_Date);
  lcd.setCursor(0, 2);
  lcd.print("Time In : ");
  lcd.print(atc_Time_In);

  myservo.write(180);
}

void carLeaving(String payload) {
  atc_Name = getValue(payload, ',', 2);
  atc_Date = getValue(payload, ',', 3);
  atc_Time_In = getValue(payload, ',', 4);
  atc_Time_Out = getValue(payload, ',', 5);

  int name_Lenght = atc_Name.length();
  int pos = 0;
  if (name_Lenght > 0 && name_Lenght <= lcdColumns) {
    pos = map(name_Lenght, 1, lcdColumns, 0, (lcdColumns / 2) - 1);
    pos = ((lcdColumns / 2) - 1) - pos;
  } else if (name_Lenght > lcdColumns) {
    atc_Name = atc_Name.substring(0, lcdColumns);
  }

  car_direction = "leaving";
  digitalWrite(green_led_pin, true);
  lcd.clear();
  delay(100);
  lcd.setCursor(pos, 0);
  lcd.print(atc_Name);
  lcd.setCursor(0, 1);
  lcd.print("Time Out:   ");
  lcd.print(atc_Time_Out);
  lcd.setCursor(0, 2);
  lcd.print("Time In :   ");
  lcd.print(atc_Time_In);

  myservo.write(180);
}

void cardNotRegistered() {
  digitalWrite(red_led_pin, true);
  digitalWrite(buzzer_pin, HIGH);
  lcd.clear();
  delay(100);
  lcd.setCursor(6, 0);
  lcd.print("Error !");
  lcd.setCursor(3, 1);
  lcd.print("Your card is not");
  lcd.setCursor(6, 2);
  lcd.print("registered");
  MQTT_Publish("Ujjwal0901/m", "Error! unregistered card");
  delay(1000);
  digitalWrite(buzzer_pin, LOW);
  delay(1500);
  digitalWrite(red_led_pin, false);
  lcd.clear();
  delay(500);
}

void registrationSuccessful() {
  digitalWrite(green_led_pin, true);
  lcd.clear();
  delay(500);
  lcd.setCursor(0, 0);
  lcd.print("Your card has been");
  lcd.setCursor(6, 1);
  lcd.print("registered");
  lcd.setCursor(5, 2);
  lcd.print("successfully");
  MQTT_Publish("Ujjwal0901/m", "Card registered successfully");
  delay(2500);
  lcd.clear();
  digitalWrite(green_led_pin, false);
  delay(500);
}

void cardAlreadyRegistered() {
  digitalWrite(red_led_pin, true);
  digitalWrite(buzzer_pin, HIGH);
  lcd.clear();
  delay(500);
  lcd.setCursor(6, 0);
  lcd.print("Error !");
  lcd.setCursor(0, 1);
  lcd.print("Your card is already");
  lcd.setCursor(6, 2);
  lcd.print("registered");
  MQTT_Publish("Ujjwal0901/m", "Card already registered");
  delay(1000);
  digitalWrite(buzzer_pin, LOW);
  delay(1500);
  digitalWrite(red_led_pin, false);
  lcd.clear();
  delay(500);
}

void wifiDisconnected() {
  digitalWrite(red_led_pin, true);
  digitalWrite(buzzer_pin, HIGH);
  lcd.clear();
  delay(500);
  lcd.setCursor(6, 0);
  lcd.print("Error !");
  lcd.setCursor(2, 2);
  lcd.print("WiFi disconnected");
  delay(1000);
  digitalWrite(buzzer_pin, LOW);
  delay(1500);
  lcd.clear();
  digitalWrite(red_led_pin, false);
  delay(500);
}
