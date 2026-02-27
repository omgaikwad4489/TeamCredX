#include <SPI.h>
#include <MFRC522.h>
#include <Keypad.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

/* ================== CONFIGURATION ================== */
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define SPI_SCK     18
#define SPI_MOSI    23
#define SPI_MISO    19
#define OLED_CS     17
#define OLED_DC     16
#define OLED_RESET  -1
#define RFID_CS     5
#define RFID_RST    4
#define BUZZER      15
#define LED         2

/* ================== NETWORK CONFIG ================== */
const char* ssid = "Nigga";
const char* password = "crazy_nigga";

const String baseURL = "http://10.250.95.155:8000/api/esp32"; 

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &SPI, OLED_DC, OLED_RESET, OLED_CS);
MFRC522 rfid(RFID_CS, RFID_RST);

/* ================== BITMAP ICONS ================== */
const unsigned char PROGMEM icon_wallet [] = {
0x00, 0x00, 0x0f, 0xf0, 0x10, 0x08, 0x20, 0x04, 0x40, 0x02, 0x40, 0x02, 0x7f, 0xe2, 0x40, 0x02, 
0x41, 0x02, 0x41, 0x02, 0x40, 0x02, 0x20, 0x04, 0x10, 0x08, 0x0f, 0xf0, 0x00, 0x00, 0x00, 0x00
};

const unsigned char PROGMEM icon_shield [] = {
0x00, 0x00, 0x1f, 0xf8, 0x3f, 0xfc, 0x60, 0x06, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03, 0xc0, 0x03, 
0xc0, 0x03, 0x60, 0x06, 0x30, 0x0c, 0x18, 0x18, 0x0c, 0x30, 0x06, 0x60, 0x03, 0xc0, 0x01, 0x80
};

const unsigned char PROGMEM icon_wifi [] = {
0x00, 0x00, 0x00, 0x00, 0x07, 0xe0, 0x18, 0x18, 0x20, 0x04, 0x40, 0x02, 0x03, 0xc0, 0x04, 0x20, 
0x08, 0x10, 0x01, 0x80, 0x02, 0x40, 0x00, 0x00, 0x01, 0x80, 0x01, 0x80, 0x00, 0x00, 0x00, 0x00
};

/* ================== KEYPAD CONFIG ================== */
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {27, 14, 12, 13};
byte colPins[COLS] = {32, 33, 25, 26};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

/* ================== VARIABLES ================== */
String currentUID = "";
String amountStr  = "";
String tenureStr  = ""; 
String enteredPin = "";
const String DEMO_PIN = "1234";

// CHANGED: These are now floats to support decimals
float outstandingLoan = 0.0;
float lenderBalance   = 0.0;
float lenderInvested  = 0.0;
float calculatedEMI   = 0.0; 
float expectedReturn  = 0.0; 

enum State { APP_IDLE, ENTERING_PIN, MODE_SELECT, BORROWER_MENU, LENDER_MENU, ENTERING_AMOUNT, ENTERING_TENURE, CONFIRMING_EMI, CONFIRMING_RETURN };
State currentState = APP_IDLE;
enum Action { NONE, BORROW, REPAY, FUND };
Action currentAction = NONE;

/* ================== UI FUNCTIONS ================== */
void centerText(String text, int y, int size = 1) {
  display.setTextSize(size);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(text, 0, 0, &x1, &y1, &w, &h);
  display.setCursor((SCREEN_WIDTH - w) / 2, y);
  display.print(text);
}

void drawHeader(String title) {
  display.fillRect(0, 0, SCREEN_WIDTH, 16, SSD1306_WHITE); 
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(4, 4);
  display.print(title);
  if (WiFi.status() == WL_CONNECTED) {
    display.drawBitmap(110, 0, icon_wifi, 16, 16, SSD1306_BLACK);
  }
  display.setTextColor(SSD1306_WHITE); 
}

void showScreen(String headerTitle, String mainText, String subText="", const unsigned char* icon=NULL, bool largeMain=false) {
  display.clearDisplay();
  drawHeader(headerTitle);
  
  display.drawRoundRect(0, 18, SCREEN_WIDTH, 46, 3, SSD1306_WHITE);

  if(icon != NULL) {
    display.drawBitmap(6, 30, icon, 16, 16, SSD1306_WHITE);
    
    display.setTextSize(largeMain ? 2 : 1);
    int yPos = largeMain ? 26 : 28; 
    display.setCursor(30, yPos);
    display.print(mainText);
    
    display.setTextSize(1);
    display.setCursor(30, 46);
    display.print(subText);
  } else {
    display.setTextSize(largeMain ? 2 : 1);
    int yPos = largeMain ? 26 : 28;
    centerText(mainText, yPos, largeMain ? 2 : 1);
    
    if(subText != "") {
      display.setTextSize(1);
      centerText(subText, 46, 1);
    }
  }
  display.display();
}

void showProgressBar(String actionName) {
  display.clearDisplay();
  drawHeader("PROCESSING");
  centerText(actionName, 26, 1);
  
  display.drawRoundRect(14, 44, 100, 10, 3, SSD1306_WHITE);
  display.display();

  for(int i = 0; i <= 96; i += 8) {
    display.fillRoundRect(16, 46, i, 6, 2, SSD1306_WHITE);
    display.display();
    delay(40); 
    digitalWrite(LED, !digitalRead(LED)); 
  }
  digitalWrite(LED, LOW);
}

void feedback(bool success) {
  if(success) {
     for(int i=0;i<2;i++) {
        digitalWrite(BUZZER, HIGH); delay(80);
        digitalWrite(BUZZER, LOW); delay(50);
     }
  } else {
    digitalWrite(BUZZER, HIGH); delay(500);
    digitalWrite(BUZZER, LOW); 
  }
}

String getUIDString(byte *buffer, byte bufferSize) {
  String uid="";
  for(byte i=0;i<bufferSize;i++){
    if(buffer[i]<0x10) uid+="0";
    uid+=String(buffer[i],HEX);
  }
  uid.toUpperCase();
  return uid;
}

/* ================== API HELPER ================== */
String sendPostRequest(String endpoint, String jsonPayload) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(baseURL + endpoint);
    http.addHeader("Content-Type", "application/json");
    
    int httpResponseCode = http.POST(jsonPayload);
    String response = "{}";
    
    if (httpResponseCode > 0) {
      response = http.getString();
    } else {
      Serial.print("Error on sending POST: ");
      Serial.println(httpResponseCode);
    }
    http.end();
    return response;
  }
  Serial.println("WiFi Disconnected!");
  return "{}";
}

/* ================== TRANSACTION EXECUTION ================== */
void executeBackendTransaction() {
  int amt = amountStr.toInt();
  int tenure = tenureStr.toInt(); 

  showProgressBar("Sending Data...");

  StaticJsonDocument<200> doc;
  doc["amount"] = amt;
  doc["tenure"] = tenure;
  
  String requestBody;
  serializeJson(doc, requestBody);

  String response = sendPostRequest("/loan-request/", requestBody); 
  
  StaticJsonDocument<200> responseDoc;
  deserializeJson(responseDoc, response);
  
  bool txSuccess = responseDoc["success"];
  String message = responseDoc["message"] | "SUCCESS";

  if (txSuccess) {
     feedback(true);
     showScreen("SUCCESS", "Sent to DB", "Tx Complete", icon_shield);
  } else {
     feedback(false);
     showScreen("FAILED", message, "SEND TO DATABASE", NULL);
  }

  delay(3000);
  resetSystem();
}

/* ================== RESET SYSTEM ================== */
void resetSystem() {
  currentState = APP_IDLE; 
  currentAction = NONE;
  amountStr = "";
  tenureStr = ""; 
  enteredPin = "";
  calculatedEMI = 0.0;
  expectedReturn = 0.0;
  showScreen("SYSTEM READY", "Scan Identity", "Platform Fee: 9%", icon_shield);
}

/* ================== SETUP ================== */
void setup() {
  Serial.begin(115200);
  pinMode(BUZZER,OUTPUT);
  pinMode(LED,OUTPUT);

  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  display.begin(SSD1306_SWITCHCAPVCC);
  rfid.PCD_Init();
  
  display.clearDisplay();
  display.drawRoundRect(10, 10, 108, 44, 6, SSD1306_WHITE);
  display.fillRoundRect(14, 14, 100, 36, 4, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  centerText("CREDX", 24, 2); 
  display.display();
  delay(1500);
  display.setTextColor(SSD1306_WHITE);
  
  WiFi.begin(ssid, password);
  showScreen("CONNECTING", "Wi-Fi...", "Please wait", icon_wifi);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  
  resetSystem();
}

/* ================== MAIN LOOP ================== */
void loop() {
  char key = keypad.getKey();

  if (currentState == APP_IDLE) { 
    if (rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()) {
      feedback(true);
      currentUID = getUIDString(rfid.uid.uidByte, rfid.uid.size);
      
      currentState = ENTERING_PIN; 
      enteredPin = "";
      showScreen("SECURITY", "", "Enter PIN", NULL, true);

      rfid.PICC_HaltA();
      rfid.PCD_StopCrypto1();
    }
    return;
  }

  if (key != NO_KEY) {
    
    if (currentState == ENTERING_PIN) {
      if (isDigit(key) && enteredPin.length() < 4) {
        enteredPin += key;
        
        String hiddenPin = "";
        for(int i = 0; i < enteredPin.length(); i++) hiddenPin += "* ";
        showScreen("SECURITY", hiddenPin, "Enter PIN", NULL, true);

        if (enteredPin.length() == 4) {
          delay(400); 
          
          if (enteredPin == DEMO_PIN) {
            feedback(true);
            currentState = MODE_SELECT;
            showScreen("SELECT MODE", "A:Borrow B:Lend", "Choose Option", icon_wallet);
          } else {
            showScreen("ACCESS DENIED", "Incorrect PIN", "Try Again", NULL);
            feedback(false);
            delay(2000);
            resetSystem();
          }
        }
      } 
      else if (key == '*') resetSystem();
    }

    else if (currentState == MODE_SELECT) {
      if (key == 'A') {
        currentState = BORROWER_MENU;
        // Output formatted to 2 decimal places
        showScreen("BORROWER", "1:Loan 2:Pay", "Due: Rs " + String(outstandingLoan, 2), icon_wallet);
      } 
      else if (key == 'B') {
        currentState = LENDER_MENU;
        // Output formatted to 2 decimal places
        showScreen("LENDER", "1:Fund 2:Profit", "Pool: Rs " + String(lenderBalance, 2), icon_wallet);
      }
      else if (key == '*') resetSystem();
    }

    else if (currentState == BORROWER_MENU) {
      if (key == '1') {
        currentAction = BORROW; currentState = ENTERING_AMOUNT; amountStr = "";
        showScreen("ENTER AMOUNT", "Rs 0", "#:Next | *:Back", NULL, true);
      } 
      else if (key == '2') {
        currentAction = REPAY; currentState = ENTERING_AMOUNT; amountStr = "";
        showScreen("REPAYMENT", "Rs 0", "#:Next | *:Back", NULL, true);
      }
      else if (key == '*') resetSystem();
    }

    else if (currentState == LENDER_MENU) {
      if (key == '1') {
        currentAction = FUND; currentState = ENTERING_AMOUNT; amountStr = "";
        showScreen("ADD FUNDS", "Rs 0", "#:Next | *:Back", NULL, true);
      }
      else if (key == '2') {
        float profit = lenderInvested * 0.07; 
        // Formatted to 2 decimal places
        showScreen("STATS", "Inv: " + String(lenderInvested, 2), "Profit: Rs " + String(profit, 2), icon_shield);
        currentAction = NONE;
      }
      else if (key == '*') resetSystem();
    }

    else if (currentState == ENTERING_AMOUNT) {
      if (isDigit(key)) {
        amountStr += key;
        showScreen("ENTER AMOUNT", "Rs " + amountStr, "#:Next | *:Back", NULL, true);
      } 
      else if (key == '*') {
        resetSystem();
      }
      else if (key == '#') {
        if (amountStr.length() == 0) return;

        if (currentAction == BORROW) {
          currentState = ENTERING_TENURE;
          tenureStr = "";
          showScreen("ENTER TENURE", "0 Mos", "#:Next | *:Back", NULL, true);
        } 
        else if (currentAction == FUND) {
          currentState = CONFIRMING_RETURN;
          float principal = amountStr.toFloat();
          
          expectedReturn = principal + (principal * 0.07);
          
          // Show float with 2 decimal places
          showScreen("EST. RETURN", "Rs " + String(expectedReturn, 2), "#:Accept | *:Cancel", NULL, true);
        }
        else {
          executeBackendTransaction(); 
        }
      }
    }
    
    // 6. Tenure Entry (Only reached if BORROWING)
    else if (currentState == ENTERING_TENURE) {
      if (isDigit(key)) {
        tenureStr += key;
        showScreen("ENTER TENURE", tenureStr + " Mos", "#:Next | *:Back", NULL, true);
      }
      else if (key == '*') {
        resetSystem();
      }
      else if (key == '#') {
        if (tenureStr.length() == 0) return;
        
        currentState = CONFIRMING_EMI;
        
        float principal = amountStr.toFloat();
        float months = tenureStr.toFloat(); // Ensure months is also a float for proper division
        
        // Borrower pays the full 9% fee
        float totalRepayment = principal + (principal * 0.09);
        calculatedEMI = totalRepayment / months; 
        
        // Formatted to 2 decimal places
        showScreen("EMI ESTIMATE", "Rs " + String(calculatedEMI, 2), "#:Accept | *:Cancel", NULL, true);
      }
    }

    // 7a. Confirming the Borrower EMI
    else if (currentState == CONFIRMING_EMI) {
      if (key == '#') {
        executeBackendTransaction();
      }
      else if (key == '*') {
        resetSystem();
      }
    }

    // 7b. Confirming the Lender Return
    else if (currentState == CONFIRMING_RETURN) {
      if (key == '#') {
        executeBackendTransaction();
      }
      else if (key == '*') {
        resetSystem();
      }
    }
  }
}
