#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <Stepper.h>

// Inisialisasi
#define ssid ""
#define password ""
#define auth ""
#define Pinhujan 35
#define PinLDR 33
#define LEDO 26
#define LEDM 27
#define IN1 17
#define IN2 16
#define IN3 19
#define IN4 18
#define stepsPerRevolution 2048
#define stepperSpeed 20
#define stepperStepExtend 4500
#define stepperStepsRetract -4500
#define interval "5000L"
#define sbegin 115200

// Set fuzzy untuk intensitas hujan dan cahaya
enum class RainIntensity { Normal,
                           Cerah,
                           Lebat };
enum class LightIntensity { Gelap,
                            Dim,
                            Cerah };

// Status sistem
enum class SystemState { Retracted,
                         Extended };

// Struktur untuk menyimpan data sensor
struct SensorData {
  uint8_t hujan;
  uint8_t cerah;
};

// Struktur untuk menyimpan konfigurasi sistem
struct SystemConfig {
  uint8_t maxAttempts;
  uint8_t attemptCount;
  SystemState state;
};

// Inisialisasi struktur dan variabel global
SystemConfig config = { 10, 0, SystemState::Retracted };
SensorData sensors = { 0, 0 };
int manualControl = 0;
int manualOverride = 0;

Stepper myStepper(stepsPerRevolution, IN1, IN3, IN2, IN4);
BlynkTimer timer;

// Fungsi untuk menghubungkan ke WiFi
void connectToWiFi() {
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Menghubungkan ke WiFi...");
    config.attemptCount++;
    if (config.attemptCount >= config.maxAttempts) {
      Serial.println("Gagal menghubungkan ke WiFi. Memulai ulang...");
      esp_restart();
    }
  }
  Serial.println("Terhubung ke WiFi");
  Serial.print("Alamat IP: ");
  Serial.println(WiFi.localIP());
}

// Fungsi untuk menggerakkan jemuran berdasarkan status baru
void moveClothesline(SystemState newState) {
  int steps = (newState == SystemState::Extended) ? stepperStepExtend : stepperStepsRetract;
  int currentPosition = 0;
  while (currentPosition != steps) {
    myStepper.step((steps > 0) ? 1 : -1);
    currentPosition += (steps > 0) ? 1 : -1;
    delay(1);
  }
  config.state = newState;
}

// Fungsi untuk mendapatkan intensitas hujan
RainIntensity getRainIntensity(uint8_t rainValue) {
  if (rainValue == 0) return RainIntensity::Normal;
  if (rainValue == 1) return RainIntensity::Cerah;
  return RainIntensity::Lebat;
}

// Fungsi untuk mendapatkan intensitas cahaya
LightIntensity getLightIntensity(uint8_t lightValue) {
  if (lightValue == 0) return LightIntensity::Gelap;
  if (lightValue == 1) return LightIntensity::Dim;
  return LightIntensity::Cerah;
}

// Fungsi untuk menentukan status jemuran berdasarkan intensitas hujan dan cahaya
SystemState determineClotheslineState(RainIntensity rain, LightIntensity light) {
  if (rain == RainIntensity::Normal && light == LightIntensity::Cerah) return SystemState::Extended;
  if (rain == RainIntensity::Lebat || light == LightIntensity::Gelap) return SystemState::Retracted;
  return config.state;
}

// Fungsi untuk mengirim data sensor ke Blynk
void sendToBlynk() {
  Serial.println("Mengirim data ke Blynk");
  if (sensors.hujan == 1 && sensors.cerah == 0) Blynk.virtualWrite(V1, "Cerah dan Terang");
  if (sensors.hujan == 1 && sensors.cerah == 1) Blynk.virtualWrite(V1, "Mendung dan Terang");
  if (sensors.hujan == 0 && sensors.cerah == 0) Blynk.virtualWrite(V1, "Cerah dan Hujan");
  if (sensors.hujan == 0 && sensors.cerah == 1) Blynk.virtualWrite(V1, "Mendung dan Hujan");
}

// Fungsi untuk kontrol manual dari Blynk
BLYNK_WRITE(V2) {
  int value = param.asInt();
  if (value == 1) manualControl = 1;
}

BLYNK_WRITE(V3) {
  int value = param.asInt();
  if (value == 1) manualControl = 2;
}

BLYNK_WRITE(V4) {
  int value = param.asInt();
  if (value == 1) manualOverride = 1;
}

BLYNK_WRITE(V0) {
  int value = param.asInt();
  if (value == 1) manualOverride = 0;
}

void setup() {
  Serial.begin(sbegin);
  myStepper.setSpeed(stepperSpeed);
  pinMode(Pinhujan, INPUT);
  pinMode(PinLDR, INPUT);
  pinMode(LEDO, OUTPUT);
  pinMode(LEDM, OUTPUT);
  digitalWrite(LEDO, LOW);
  digitalWrite(LEDM, LOW);
  connectToWiFi();
  Blynk.begin(auth, ssid, password);
  timer.setInterval(interval, sendToBlynk);
}

void loop() {
  Blynk.run();
  timer.run();
  sensors.hujan = digitalRead(Pinhujan);
  sensors.cerah = digitalRead(PinLDR);
  Serial.print("Intensitas Hujan: ");
  Serial.println(sensors.hujan);
  Serial.print("Intensitas Cahaya: ");
  Serial.println(sensors.cerah);
  RainIntensity hujan = getRainIntensity(sensors.hujan);
  LightIntensity cerah = getLightIntensity(sensors.cerah);

  if (manualOverride == 0) {
    digitalWrite(LEDO, HIGH);
    digitalWrite(LEDM, LOW);
    SystemState newState = determineClotheslineState(hujan, cerah);
    if (newState != config.state) {
      Serial.println(newState == SystemState::Extended ? "Mengeluarkan jemuran otomatis" : "Menarik jemuran otomatis");
      Blynk.logEvent(newState == SystemState::Extended ? "cerah" : "hujan", newState == SystemState::Extended ? "Jemuran keluar" : "Jemuran masuk");
      moveClothesline(newState);
    }
  } else {
    digitalWrite(LEDO, LOW);
    digitalWrite(LEDM, HIGH);
    if (manualControl == 1 && config.state == SystemState::Retracted) {
      Serial.println("Mengeluarkan jemuran manual");
      moveClothesline(SystemState::Extended);
    } else if (manualControl == 2 && config.state == SystemState::Extended) {
      Serial.println("Menarik jemuran manual");
      moveClothesline(SystemState::Retracted);
    }
  }

  delay(500);
}
