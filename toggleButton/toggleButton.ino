// Simple mode toggle test (ESP32)
// Button on GPIO 5 to GND. Default mode = MANUAL.

#define MODE_BTN_PIN 4   // Push button -> GND

bool autoMode = false;    // false = MANUAL (default), true = AUTO

// Debounce vars
int lastStable  = HIGH;   // debounced state (HIGH = not pressed with INPUT_PULLUP)
int lastReading = HIGH;   // last raw read
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; // ms

void printMode() {
  Serial.print("Mode: ");
  Serial.println(autoMode ? "AUTO" : "MANUAL");
}

void setup() {
  Serial.begin(9600);
  pinMode(MODE_BTN_PIN, INPUT);  // use internal pull-up; button to GND
  delay(100);
  Serial.println("Button toggle test starting...");
  printMode();  // should show MANUAL at start
}

void loop() {
  int reading = digitalRead(MODE_BTN_PIN);

  // any change? start debounce timer
  if (reading != lastReading) {
    lastDebounceTime = millis();
  }

  // has the reading been stable long enough?
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != lastStable) {
      lastStable = reading;

      // button pressed (active LOW)
      if (lastStable == LOW) {
        autoMode = !autoMode;  // toggle
        printMode();
      }
    }
  }

  lastReading = reading;
}
