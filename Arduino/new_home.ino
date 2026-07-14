#include <Servo.h>

// ================================
// Pins
// ================================
#define BTN_PIN     2
#define BUZ_PIN     8


// #define DHTPIN      6
// #define DHTTYPE     DHT11

#define RED_LED     13
#define GREEN_LED   12

#define WATER_PIN   A1
#define GAS_PIN     A2
#define GAS_LED     11

// Flame: Digital Output (DO)
#define FLAME_DO_PIN 4     // DO من حساس اللهب إلى D4

// Servo
#define SERVO_PIN   5      // سيرفو على D5

// Traffic light LEDs (مستقلة)
#define TRAFFIC_RED_PIN     9
#define TRAFFIC_YELLOW_PIN  10
#define TRAFFIC_GREEN_PIN   7

// إذا كان حساس اللهب عند وجود لهب يعطي LOW خليها true، إذا يعطي HIGH خليها false
const bool FLAME_ACTIVE_LOW = false;

// ================================
// Objects
// ================================
Servo myServo;

// ================================
// Button Debounce
// ================================
bool lastButtonState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// ================================
// Timing
// ================================
// unsigned long lastDhtRead = 0;

const unsigned long WATER_READ_INTERVAL = 200; // <-- حساس الماء 200ms
unsigned long lastWaterRead = 0;

const unsigned long GAS_READ_INTERVAL = 200;
unsigned long lastGasRead = 0;

const unsigned long FLAME_READ_INTERVAL = 50;
unsigned long lastFlameRead = 0;

// ================================
// Readings
// ================================
int waterValue = 0;
// float temperature = NAN;   // (ملغى)

int gasValue = 0;
bool gasAlarm = false;

bool fireDetected = false;
bool flameRawState = false;

// فلترة لإشارة اللهب (DO)
unsigned long flameLastChangeTime = 0;
bool flameLastRawState = false;
const unsigned long FLAME_STABLE_MS = 120;

// ================================
// Thresholds
// ================================
const int WATER_THRESHOLD = 400;
const int GAS_THRESHOLD   = 350;

// ================================
// Buzzer pattern (الطوارئ)
// ================================
unsigned long lastBeepTime = 0;
bool beepState = false;
const unsigned long alarmBeepInterval = 300;
const int alarmBeepFreq = 1000;

// ================================
// Flame emergency
// ================================
bool flameEmergencyActive = false;
unsigned long flameEmergencyStart = 0;
const unsigned long FLAME_EMERGENCY_DURATION = 4000;
bool lastFireStable = false;

// ================================
// Servo timed action
// ================================
enum ServoState { SERVO_IDLE, SERVO_AT_90_WAIT, SERVO_RETURNING };
ServoState servoState = SERVO_IDLE;
unsigned long servoActionStart = 0;

const int SERVO_HOME_ANGLE = 0;
const int SERVO_ALARM_ANGLE = 90;
const unsigned long SERVO_HOLD_DURATION = 3000;

int currentServoAngle = -1;

// ================================
// Traffic light AUTO
// ================================
enum TrafficState { TRAFFIC_RED, TRAFFIC_GREEN, TRAFFIC_YELLOW };
TrafficState trafficState = TRAFFIC_RED;
unsigned long trafficStateStart = 0;

const unsigned long TRAFFIC_RED_MS    = 6000;
const unsigned long TRAFFIC_GREEN_MS  = 6000;
const unsigned long TRAFFIC_YELLOW_MS = 4000;

// (مضاف فقط) لتمييز الأصفر بعد الأحمر أو بعد الأخضر
bool yellowAfterRed = false;

void setTraffic(TrafficState s) {
  trafficState = s;
  trafficStateStart = millis();
  digitalWrite(TRAFFIC_RED_PIN,    (s == TRAFFIC_RED)    ? HIGH : LOW);
  digitalWrite(TRAFFIC_YELLOW_PIN, (s == TRAFFIC_YELLOW) ? HIGH : LOW);
  digitalWrite(TRAFFIC_GREEN_PIN,  (s == TRAFFIC_GREEN)  ? HIGH : LOW);
}

// ================================
// Helpers
// ================================
bool readFlameDO() {
  int v = digitalRead(FLAME_DO_PIN);
  if (FLAME_ACTIVE_LOW) return (v == LOW);
  else                  return (v == HIGH);
}

void setServoAngle(int angle) {
  angle = constrain(angle, 0, 180);
  if (angle != currentServoAngle) {
    myServo.write(angle);
    currentServoAngle = angle;
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(BUZ_PIN, OUTPUT);

  pinMode(RED_LED, OUTPUT);
  pinMode(GREEN_LED, OUTPUT);

  pinMode(GAS_LED, OUTPUT);
  pinMode(FLAME_DO_PIN, INPUT);

  pinMode(TRAFFIC_RED_PIN, OUTPUT);
  pinMode(TRAFFIC_YELLOW_PIN, OUTPUT);
  pinMode(TRAFFIC_GREEN_PIN, OUTPUT);

  digitalWrite(BUZ_PIN, LOW);
  digitalWrite(GAS_LED, LOW);

  // (تعديل فقط) يبدأ بالأخضر كما طلبت
  setTraffic(TRAFFIC_GREEN);

  // dht.begin(); // (ملغى)

  myServo.attach(SERVO_PIN);
  setServoAngle(SERVO_HOME_ANGLE);

  flameLastRawState = readFlameDO();
  flameLastChangeTime = millis();
  fireDetected = flameLastRawState;
  lastFireStable = fireDetected;

  // (مضاف حسب طلبك): اللمبة 12 شغالة دائماً إذا لا يوجد لهب
  digitalWrite(GREEN_LED, fireDetected ? LOW : HIGH);
}

void loop() {
  unsigned long now = millis();

  // ================================
  // Button read (debounce)  ← (مضاف فقط)
  // ================================
  bool reading = digitalRead(BTN_PIN);
  if (reading != lastButtonState) {
    lastDebounceTime = now;
  }

  bool buttonPressed = false;
  if ((now - lastDebounceTime) > debounceDelay) {
    buttonPressed = (reading == LOW); // INPUT_PULLUP
  }
  lastButtonState = reading;

  // ================================
  // Gas
  // ================================
  if (now - lastGasRead >= GAS_READ_INTERVAL) {
    lastGasRead = now;
    gasValue = analogRead(GAS_PIN);
    gasAlarm = (gasValue > GAS_THRESHOLD);
    digitalWrite(GAS_LED, gasAlarm ? HIGH : LOW);
  }

  // ================================
  // Water  (كل 200ms)
  // ================================
  if (now - lastWaterRead >= WATER_READ_INTERVAL) {
    lastWaterRead = now;
    waterValue = analogRead(WATER_PIN);
  }
  bool waterAlarm = (waterValue > WATER_THRESHOLD);

  // ================================
  // (تم إلغاء Temperature بالكامل)
  // ================================

  // ================================
  // Flame filter
  // ================================
  if (now - lastFlameRead >= FLAME_READ_INTERVAL) {
    lastFlameRead = now;
    flameRawState = readFlameDO();
    if (flameRawState != flameLastRawState) {
      flameLastRawState = flameRawState;
      flameLastChangeTime = now;
    }
    if ((now - flameLastChangeTime) >= FLAME_STABLE_MS) {
      fireDetected = flameLastRawState;
    }
  }

  bool fireRisingEdge = (fireDetected && !lastFireStable);
  if (fireRisingEdge) {
    flameEmergencyActive = true;
    flameEmergencyStart = now;
    lastBeepTime = now;
    beepState = false;

    // السرفو يفتح فقط عند النار
    servoState = SERVO_AT_90_WAIT;
    servoActionStart = now;
    setServoAngle(SERVO_ALARM_ANGLE);
  }
  lastFireStable = fireDetected;

  if (flameEmergencyActive && (now - flameEmergencyStart >= FLAME_EMERGENCY_DURATION)) {
    flameEmergencyActive = false;
  }

  // ================================
  // Servo  (يفتح بالنار فقط)
  // ================================
  if (servoState == SERVO_AT_90_WAIT) {
    if (now - servoActionStart >= SERVO_HOLD_DURATION) {
      servoState = SERVO_RETURNING;
      setServoAngle(SERVO_HOME_ANGLE);
    }
  } else if (servoState == SERVO_RETURNING) {
    servoState = SERVO_IDLE;
  } else {
    // كان: if (gasAlarm || fireDetected) ...
    // صار: نار فقط
    if (fireDetected) setServoAngle(SERVO_ALARM_ANGLE);
    else setServoAngle(SERVO_HOME_ANGLE);
  }

  // ================================
  // Buzzer (الطوارئ + زر)
  // ================================
  bool alarmNow = (fireDetected || waterAlarm || flameEmergencyActive);

  if (alarmNow) {
    if (now - lastBeepTime >= alarmBeepInterval) {
      lastBeepTime = now;
      beepState = !beepState;
      if (beepState) tone(BUZ_PIN, alarmBeepFreq);
      else noTone(BUZ_PIN);
    }
  } else if (buttonPressed) {
    tone(BUZ_PIN, 1000);   // يعمل فقط أثناء الضغط
    beepState = false;
  } else {
    noTone(BUZ_PIN);
    beepState = false;
  }

  // ================================
  // Red LED emergency blink  + (مضاف) تثبيت GREEN_LED دائماً عند عدم وجود لهب
  // ================================
  if (flameEmergencyActive) {
    digitalWrite(RED_LED, beepState ? HIGH : LOW);
    digitalWrite(GREEN_LED, LOW);
  } else {
    // حسب طلبك: اللمبة 12 شغالة دائماً طالما لا يوجد لهب
    if (!fireDetected) {
      digitalWrite(GREEN_LED, HIGH);
    } else {
      // عند وجود لهب: تبقى كما هي في الكود (لا نجبرها على HIGH)
      digitalWrite(GREEN_LED, LOW);
    }
  }

  // ================================
  // Traffic light AUTO (معدل للتسلسل: أخضر -> أصفر -> أحمر -> أصفر -> أخضر)
  // ================================
  if (trafficState == TRAFFIC_GREEN) {
    if (now - trafficStateStart >= TRAFFIC_GREEN_MS) {
      yellowAfterRed = false;        // أصفر بعد الأخضر
      setTraffic(TRAFFIC_YELLOW);
    }

  } else if (trafficState == TRAFFIC_YELLOW) {
    if (now - trafficStateStart >= TRAFFIC_YELLOW_MS) {
      if (yellowAfterRed) {
        setTraffic(TRAFFIC_GREEN);  // أصفر -> أخضر
      } else {
        setTraffic(TRAFFIC_RED);    // أصفر -> أحمر
      }
    }

  } else if (trafficState == TRAFFIC_RED) {
    if (now - trafficStateStart >= TRAFFIC_RED_MS) {
      yellowAfterRed = true;         // أصفر بعد الأحمر
      setTraffic(TRAFFIC_YELLOW);
    }
  }
}
