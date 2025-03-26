#include <EEPROM.h>
#include <SoftwareSerial.h>

SoftwareSerial GSM(8, 9);

String phone_no[9] = {"+91xxxxxxxxxx", "+91xxxxxxxxxx", "+91xxxxxxxxxx", "+91xxxxxxxxxx", "+91xxxxxxxxxx", "+91xxxxxxxxxx", "+91xxxxxxxxxx", "+91xxxxxxxxxx", "+91xxxxxxxxxx"};
String masterNumber;

String RxString = "";
char RxChar = ' ';
int Counter = 0;
String GSM_Nr = "";
String GSM_Msg = "";
String lastTriggerNumber = "";

#define Relay1 2
#define Relay2 3
#define ManualSwitch 6
#define GSM_RST 4
#define AUTO_MODE_EEPROM_ADDRESS 140

bool pumpState = false;
bool autoMode = false;

void initModule(String cmd, char *res, int t);
void deleteSMS(int index);
void sendSMS(String number, String msg);
void processCommand();
void handleManualTrigger();
void handleIncomingCall();
bool isRegisteredNumber(String number);
String getCallerNumber();
void loadNumbersFromEEPROM();
void storeNumberToEEPROM(int index, String number);
void checkAutoModeOnStartup();
void sendReplySMS(String triggeringNumber, String msg);

void setup() {
    pinMode(Relay1, OUTPUT);
    digitalWrite(Relay1, HIGH);
    pinMode(Relay2, OUTPUT);
    digitalWrite(Relay2, HIGH);
    pinMode(ManualSwitch, INPUT_PULLUP);
    pinMode(GSM_RST, OUTPUT);
    digitalWrite(GSM_RST, HIGH);

    Serial.begin(9600);
    GSM.begin(9600);

    Serial.println("Initializing....");

    digitalWrite(GSM_RST, LOW);
    delay(100);
    digitalWrite(GSM_RST, HIGH);

    initModule("AT", "OK", 1000);
    initModule("AT+CPIN?", "READY", 1000);
    initModule("AT+CMGF=1", "OK", 1000);
    initModule("AT+CNMI=2,2,0,0,0", "OK", 1000);
    Serial.println("Initialized");

    loadNumbersFromEEPROM();
    masterNumber = phone_no[0];

    checkAutoModeOnStartup();
    String startupMsg = "GSM POWER ON. Auto Mode: ";
    startupMsg += (autoMode ? "ON" : "OFF");

    delay(2000);
    sendSMS(masterNumber, startupMsg);
    delay(100);
}

void loop() {
    RxString = "";
    Counter = 0;
    while (GSM.available()) {
        delay(1);
        RxChar = char(GSM.read());
        if (Counter < 200) {
            RxString.concat(RxChar);
            Counter++;
        }
    }
    if (Received(F("CMT:"))) {
        GetSMS();
        processCommand();
    }

    handleManualTrigger();
    handleIncomingCall();
}

void processCommand() {
    if (GSM_Msg.startsWith("#1STN") || GSM_Msg.startsWith("#2STN") || GSM_Msg.startsWith("#3STN") ||
        GSM_Msg.startsWith("#4STN") || GSM_Msg.startsWith("#5STN") || GSM_Msg.startsWith("#6STN") ||
        GSM_Msg.startsWith("#7STN") || GSM_Msg.startsWith("#8STN") || GSM_Msg.startsWith("#9STN")) {
        int index = GSM_Msg.substring(1, 2).toInt() - 1;
        String newNumber = GSM_Msg.substring(5);
        if (index >= 0 && index < 9 && newNumber.length() > 0) {
            if(newNumber.startsWith("+91")){
                newNumber = newNumber.substring(3);
            }
            storeNumberToEEPROM(index,newNumber);
             if (index == 0) {
                 masterNumber = newNumber;
             }
            sendSMS(GSM_Nr, "Number " + String(index + 1) + " Changed Successfully!");
        }
            return;
      }


    if (isRegisteredNumber(GSM_Nr)) {
        if (GSM_Msg == "#1") {
            digitalWrite(Relay1, LOW);
            delay(3000);
            digitalWrite(Relay1, HIGH);
            pumpState = true;
            lastTriggerNumber = GSM_Nr;
            sendReplySMS(GSM_Nr, "PUMP ON");
        } else if (GSM_Msg == "#2") {
            digitalWrite(Relay2, LOW);
            delay(3000);
            digitalWrite(Relay2, HIGH);
            pumpState = false;
            sendReplySMS(GSM_Nr, "PUMP OFF");
        } else if (GSM_Msg == "#3") {
            autoMode = true;
            EEPROM.write(AUTO_MODE_EEPROM_ADDRESS, 1);
            sendReplySMS(GSM_Nr, "Auto Mode ON");
        } else if (GSM_Msg == "#4") {
            autoMode = false;
            EEPROM.write(AUTO_MODE_EEPROM_ADDRESS, 0);
            sendReplySMS(GSM_Nr, "Auto Mode OFF");
        } else if (GSM_Msg == "#7") {
            String status = "Status: PUMP: " + String(pumpState ? "ON" : "OFF") + ", Auto: " + String(autoMode ? "ON" : "OFF");
            sendReplySMS(GSM_Nr, status);
        } else if (GSM_Msg == "#CLR") {
            for (int i = 0; i < 135; i++) {
                EEPROM.write(i, '\0');
            }
            for (int i = 0; i < 9; i++) {
                phone_no[i] = "";
            }
            masterNumber = "";
            sendReplySMS(GSM_Nr, "System Reset Please REG.By Sms> #1STN9999999999");
        } else if (GSM_Msg.startsWith("#SPN")) {
            String reply = "STORED NO:\n";
            for (int i = 0; i < 9; i++) {
                reply += phone_no[i] + "\n";
            }
            sendReplySMS(GSM_Nr, reply);
        }
    } else {
        if (GSM_Msg == "#1" || GSM_Msg == "#2" || GSM_Msg == "#7" || GSM_Msg == "#CLR" || GSM_Msg.startsWith("#SPN") || GSM_Msg == "#3" || GSM_Msg == "#4") {
            sendSMS(GSM_Nr, "Unregistered Number");
            Serial.println("Unregistered SMS");
        }
    }
}

void handleManualTrigger() {
    if (digitalRead(ManualSwitch) == LOW) {
        delay(50);
        digitalWrite(Relay2, LOW);
        delay(3000);
        digitalWrite(Relay2, HIGH);
        pumpState = false;
        if (!lastTriggerNumber.equals("") && !lastTriggerNumber.equals(masterNumber)) {
            sendSMS(lastTriggerNumber, "TANK FULL PUMP OFF");
        }
        sendSMS(masterNumber, "TANK FULL PUMP OFF");
        Serial.println("Manual switch");
        delay(300);
    }
}

void handleIncomingCall() {
    if (RxString.indexOf("RING") != -1) {
        String callerNumber = getCallerNumber();
        Serial.print("Caller ID: ");
        Serial.println(callerNumber);

        // Respond based on pump state before hanging up.
        if (callerNumber.length() > 0 && isRegisteredNumber(callerNumber)) {
             if (pumpState) {
                // If pump is ON, prepare to turn it OFF after 2 rings
                delay(2000); // Wait for approximately 2 rings (adjust as needed)
                GSM.println("ATH"); // Hang up
                delay(500);
                digitalWrite(Relay2, LOW); //Turn OFF
                delay(3000);
                digitalWrite(Relay2, HIGH);
                pumpState = false; // Update state
                sendReplySMS(callerNumber, "PUMP OFF via CALL");


            } else {
                // If pump is OFF, prepare to turn it ON after 6 rings
                delay(7000); // Wait for approximately  rings
                GSM.println("ATH"); // Hang up
                delay(500);
                digitalWrite(Relay1, LOW); // Turn ON
                delay(3000);
                digitalWrite(Relay1, HIGH);
                pumpState = true;  // Update state
                lastTriggerNumber = callerNumber; // Store last trigger
                sendReplySMS(callerNumber, "PUMP ON via CALL");

            }
        }
        else {
            GSM.println("ATH"); // Hang up immediately for unregistered calls
            delay(500);
            Serial.println("Unregistered Call or no caller ID");
        }
        RxString = ""; // Clear RxString
    }
}


void sendSMS(String number, String msg) {
    GSM.println("AT+CMGF=1");
    delay(1000);
    GSM.print("AT+CMGS=\"");
    GSM.print(number);
    GSM.println("\"");
    delay(1000);
    GSM.print(msg);
    delay(100);
    GSM.write(26);
    delay(1000);
}

void sendReplySMS(String triggeringNumber, String msg) {
    sendSMS(triggeringNumber, msg);
    if (triggeringNumber != masterNumber) {
        sendSMS(masterNumber, msg);
    }
}


void GetSMS() {
    GSM_Nr = RxString;
    int t1 = GSM_Nr.indexOf('"');
    GSM_Nr.remove(0, t1 + 1);
    t1 = GSM_Nr.indexOf('"');
    GSM_Nr.remove(t1);
    GSM_Nr.trim();

    GSM_Msg = RxString;
    int t2 = GSM_Msg.indexOf('"');
    GSM_Msg.remove(0, t2 + 1);
    t2 = GSM_Msg.indexOf('"');
    GSM_Msg.remove(0, t2 + 1);
    t2 = GSM_Msg.indexOf('"');
    GSM_Msg.remove(0, t2 + 1);
    t2 = GSM_Msg.indexOf('"');
    GSM_Msg.remove(0, t2 + 1);
    t2 = GSM_Msg.indexOf('"');
    GSM_Msg.remove(0, t2 + 1);
    t2 = GSM_Msg.indexOf('"');
    GSM_Msg.remove(0, t2 + 1);
    GSM_Msg.remove(0, 1);
    GSM_Msg.trim();

    if (RxString.startsWith("+CMT:")) {
      int index = RxString.substring(6, 8).toInt();
        deleteSMS(index);
    }
}

void deleteSMS(int index) {
    GSM.println("AT+CMGD=" + String(index));
    delay(1000);
}

boolean Received(String S) {
    return RxString.indexOf(S) >= 0;
}

void initModule(String cmd, char *res, int t) {
    while (1) {
        Serial.println(cmd);
        GSM.println(cmd);
        delay(100);
        while (GSM.available() > 0) {
            if (GSM.find(res)) {
                Serial.println(res);
                delay(t);
                return;
            }
        }
        delay(100);
    }
}

bool isRegisteredNumber(String number) {
    if (number.startsWith("+91")) {
        number = number.substring(3);
    }
    for (int i = 0; i < 9; i++) {
        if (phone_no[i] == number) {
            return true;
        }
    }
    return false;
}

String getCallerNumber() {
    String callerId = "";
    GSM.println("AT+CLIP=0");
    delay(500);
    GSM.println("AT+CLIP=1");
    delay(1000);

    int timeout = 10;
    while (timeout > 0 && callerId == "") {
        RxString = "";
        Counter = 0;
        while (GSM.available()) {
            delay(1);
            RxChar = char(GSM.read());
            if (Counter < 200) {
                RxString.concat(RxChar);
                Counter++;
            }
        }
        if (RxString.indexOf("+CLIP:") != -1) {
            int startIndex = RxString.indexOf('"') + 1;
            int endIndex = RxString.indexOf('"', startIndex);
            if (startIndex != -1 && endIndex != -1) {
                callerId = RxString.substring(startIndex, endIndex);
                callerId.trim();
            }
        }
        delay(1000);
        timeout--;
    }
    GSM.println("AT+CLIP=0"); // Disable CLIP again
    delay(500);
    return callerId;
}

void loadNumbersFromEEPROM() {
    for (int i = 0; i < 9; i++) {
        char temp[15];
        for (int j = 0; j < 14; j++) {
            temp[j] = EEPROM.read(i * 15 + j);
        }
        temp[14] = '\0';
        phone_no[i] = String(temp);
    }
}

void storeNumberToEEPROM(int index, String number) {
    phone_no[index] = number;
    for (int j = 0; j < 14; j++) {
        EEPROM.write(index * 15 + j, j < number.length() ? number[j] : '\0');
    }
}

void checkAutoModeOnStartup() {
    autoMode = EEPROM.read(AUTO_MODE_EEPROM_ADDRESS) == 1;
    if (autoMode) {
        Serial.println("Auto ON. Delay 60s");
        delay(60000);
        digitalWrite(Relay1, LOW);
        delay(3000);
        digitalWrite(Relay1, HIGH);
        pumpState = true;
        Serial.println("Pump ON Auto");
        sendSMS(masterNumber, "Pump ON in Auto Mode");
    } else {
        Serial.println("Auto OFF.");
    }
}