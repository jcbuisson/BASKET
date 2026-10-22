
#include <Arduino.h>
#include <HardwareSerial.h>
#include <DFRobotDFPlayerMini.h>

HardwareSerial mp3Serial(2);
DFRobotDFPlayerMini player;
bool playerReady = false;
constexpr uint8_t BUTTON_PIN = 27; // Normally open button between GPIO27 and GND.
constexpr unsigned long DEBOUNCE_MS = 40;
int lastButtonReading = HIGH;
int stableButtonState = HIGH;
unsigned long buttonChangedAt = 0;
uint16_t trackCount = 0; // Total reported by DFPlayer; keep all tracks in /mp3.
uint16_t nextTrack = 1;
uint16_t lastRequestedTrack = 0;

void playSound() {
  if (!playerReady) {
    Serial.println("Cannot play: DFPlayer initialization failed.");
    return;
  }
  if (trackCount == 0) {
    Serial.println("Cannot play: no track count available. Check SD card and reset.");
    return;
  }
  lastRequestedTrack = nextTrack;
  player.playMp3Folder(nextTrack);
  Serial.printf("Requested /mp3/%04u.mp3.\n", static_cast<unsigned int>(nextTrack));
  nextTrack = (nextTrack % trackCount) + 1;
  Serial.printf("Next press: track %u (cycle length %u).\n",
                static_cast<unsigned int>(nextTrack), static_cast<unsigned int>(trackCount));
}

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  delay(2000);
  Serial.println("ESP32 started. Initializing DFPlayer on RX=16, TX=17...");

  mp3Serial.begin(9600, SERIAL_8N1, 16, 17);
  delay(1000); // Allow the DFPlayer and SD card to power up.

  if (!player.begin(mp3Serial)) {
    Serial.println("DFPlayer not found!");
    Serial.println("Check 5V, common GND, TX/RX wiring, and the inserted SD card.");
    return;
  }

  playerReady = true;
  Serial.println("DFPlayer connected. Waiting for SD card to settle...");
  // The module can acknowledge initialization before it accepts playback.
  delay(2000);
  player.volume(30);
  delay(500);
  int detectedCount = -1;
  for (uint8_t attempt = 0; attempt < 3; ++attempt) {
    detectedCount = player.readFileCounts(DFPLAYER_DEVICE_SD);
    if (detectedCount > 0) {
      break;
    }
    delay(500);
  }
  if (detectedCount > 0 && detectedCount <= 9999) {
    trackCount = static_cast<uint16_t>(detectedCount);
    Serial.printf("SD card: %u files detected.\n", static_cast<unsigned int>(trackCount));
    Serial.println("Keep tracks in /mp3, numbered consecutively from 0001.mp3.");
    Serial.println("Ready. Press the GPIO27 button or send p for the next track.");
  } else {
    Serial.printf("SD file count unavailable or unsupported (response: %d). Check card and reset.\n", detectedCount);
  }
}

void loop() {
  const unsigned long now = millis();
  const int buttonReading = digitalRead(BUTTON_PIN);
  if (buttonReading != lastButtonReading) {
    lastButtonReading = buttonReading;
    buttonChangedAt = now;
  }
  if (now - buttonChangedAt >= DEBOUNCE_MS && buttonReading != stableButtonState) {
    stableButtonState = buttonReading;
    if (stableButtonState == LOW) {
      playSound();
    }
  }

  if (playerReady && Serial.available()) {
    if (Serial.read() == 'p') {
      playSound();
    }
  }

  if (playerReady && player.available()) {
    const uint8_t type = player.readType();
    const int value = player.read();
    Serial.printf("DFPlayer event: type=%u, value=%d\n", type, value);
    if (type == DFPlayerError) {
      if ((value == FileIndexOut || value == FileMismatch) && lastRequestedTrack > 1) {
        // The whole-card count can exceed the numbered tracks in /mp3.
        // With consecutive filenames, the missing number marks the end.
        trackCount = lastRequestedTrack - 1;
        Serial.printf("Track %u unavailable; cycling %u tracks and returning to track 1.\n",
                      static_cast<unsigned int>(lastRequestedTrack),
                      static_cast<unsigned int>(trackCount));
        nextTrack = 1;
        playSound();
      } else {
        Serial.println("DFPlayer error: check SD card and consecutive filenames in /mp3.");
      }
    } else if (type == DFPlayerPlayFinished) {
      Serial.println("Playback finished. Press the button or send p for the next track.");
    }
  }

  static unsigned long lastStatus = 0;
  if (millis() - lastStatus >= 5000) {
    lastStatus = millis();
    Serial.println(playerReady ? "ESP32 running; DFPlayer initialized."
                               : "ESP32 running; DFPlayer initialization failed. Reset to retry.");
  }
}
