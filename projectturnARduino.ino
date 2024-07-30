#include <Adafruit_NeoPixel.h>
#include <Servo.h>
#include <AccelStepper.h>

#define PIN_WHITE      48   // Pin number connected to the NeoPixel strip (white color only)
#define NUM_WHITE_LEDS 100  // Number of LEDs in the white strip
#define PIN            40   // Pin number connected to the NeoPixel strip (color control)
#define NUM_LEDS       100  // Number of LEDs in the strip

#define LDR_PIN_A7     A7  // Analog pin connected to LDR
#define LDR_PIN_29     29  // Digital pin connected to LDR
#define DIR_PIN        6   // Direction pin
#define STEP_PIN       5   // Step pin
#define TRIG_PIN       41  // Trig pin of ultrasonic sensor
#define ECHO_PIN       43  // Echo pin of ultrasonic sensor
#define SIGNAL_PIN     36  // Pin to send signal to ESP32

Adafruit_NeoPixel stripWhite = Adafruit_NeoPixel(NUM_WHITE_LEDS, PIN_WHITE, NEO_GRB + NEO_KHZ800);
Adafruit_NeoPixel strip = Adafruit_NeoPixel(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);
Servo servoLeft;   
Servo servoUp;     

AccelStepper stepper(AccelStepper::DRIVER, STEP_PIN, DIR_PIN);

unsigned long previousLdrMillis = 0;
const unsigned long ldrInterval = 10000; // Interval for LDR measurement (10 seconds)

unsigned long previousDistanceMillis = 0;
const unsigned long distanceInterval = 3000; // Interval for ultrasonic measurement (3 seconds)
int distance = 0; // Variable to store the distance measured

void setup() {
  Serial.begin(9600);
  Serial1.begin(9600);

  stripWhite.begin();
  stripWhite.show(); // Initialize all pixels to 'off'
  strip.begin();
  strip.show(); // Initialize all pixels to 'off'

  servoLeft.attach(25);
  servoUp.attach(23);

  pinMode(LDR_PIN_29, INPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(SIGNAL_PIN, OUTPUT);
  digitalWrite(SIGNAL_PIN, LOW); // Ensure the pin starts LOW

  stepper.setMaxSpeed(100);
  stepper.setAcceleration(100);

  Serial.println("Setup complete. Ready to receive data...");
}

void loop() {
  unsigned long currentMillis = millis();

  // Measure distance every 3 seconds
  if (currentMillis - previousDistanceMillis >= distanceInterval) {
    previousDistanceMillis = currentMillis;
    distance = measureDistance();
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" cm");
  }

  // Read and display LDR value every 10 seconds
  if (currentMillis - previousLdrMillis >= ldrInterval) {
    previousLdrMillis = currentMillis;
    int ldrValue = analogRead(LDR_PIN_A7);
    int ldrDigital = digitalRead(LDR_PIN_29);
    Serial.print("LDR Value: ");
    Serial.print(ldrValue);
    Serial.print(", Digital LDR: ");
    Serial.println(ldrDigital);

    if (ldrValue < 200) {
      setOddWhiteLeds(stripWhite);
    } else {
      if (ldrDigital == HIGH) {
        setWhiteColor(stripWhite, NUM_WHITE_LEDS / 2);
      } else {
        setWhiteColor(stripWhite, NUM_WHITE_LEDS);
      }
    }
  }

  // Bluetooth communication and control logic
  if (Serial1.available()) {
    String data = "";
    while (Serial1.available()) {
      char c = Serial1.read();
      data += c;
    }
    Serial.print("Received data: ");
    Serial.println(data);

    if (data.endsWith("n")) {
      data.trim();
      data.remove(data.length() - 1);

      if (data.startsWith("g#") && data.length() == 8) {
        String hexColor = data.substring(2, 8);
        long color = strtol(hexColor.c_str(), NULL, 16);
        uint32_t neoColor = strip.Color((color >> 16) & 0xFF, (color >> 8) & 0xFF, color & 0xFF);
        fillStrip(neoColor);
      }

      if (data.startsWith("d") && data.length() >= 2 && distance <= 20) {
        int angle = data.substring(1).toInt();
        if (angle == 10 || angle == 20 || angle == 30 || angle == 40 ||
            angle == 45 || angle == 60 || angle == 90 || angle == 180) {
          rotateStepper(angle);
        }
      }

      if (data.equals("zreset")) {
        resetStepper();
      }
    }

    if (data.toInt() > 0) {
      processServoCommand(data.toInt());
    }
  }
  delay(100);
}

void rotateStepper(int angle) {
  int totalRotation = 0;
  int steps = map(angle, 0, 360, 0, 200); // Convert angle to steps (assuming 1.8° per step, 200 steps per revolution)

  while (totalRotation < 360) {
    stepper.move(steps);
    stepper.runToPosition();
    totalRotation += angle;
    digitalWrite(SIGNAL_PIN, HIGH);
    Serial.println("Signal sent to ESP32");
    delay(10); // Short delay to ensure the signal is detected
    digitalWrite(SIGNAL_PIN, LOW);
    delay(5000); // Wait for 5 seconds before the next increment

    if (totalRotation >= 360) {
      break;
    }
  }
}

void resetStepper() {
  stepper.stop();
  stepper.setMaxSpeed(100);
  stepper.setAcceleration(50);
  stepper.moveTo(0);
  stepper.runToPosition();
  stepper.setMaxSpeed(500);
  stepper.setAcceleration(250);
}

void fillStrip(uint32_t color) {
  for (int i = 0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void setWhiteColor(Adafruit_NeoPixel& stripWhite, int numLeds) {
  for (int i = 0; i < stripWhite.numPixels(); i++) {
    if (i < numLeds) {
      stripWhite.setPixelColor(i, stripWhite.Color(255, 255, 255));
    } else {
      stripWhite.setPixelColor(i, stripWhite.Color(0, 0, 0));
    }
  }
  stripWhite.show();
}

void setOddWhiteLeds(Adafruit_NeoPixel& stripWhite) {
  for (int i = 0; i < stripWhite.numPixels(); i++) {
    if (i % 2 != 0) {
      stripWhite.setPixelColor(i, stripWhite.Color(255, 255, 255));
    } else {
      stripWhite.setPixelColor(i, stripWhite.Color(0, 0, 0));
    }
  }
  stripWhite.show();
}

void processServoCommand(int cmd) {
  const int stepSize = 5;
  
  switch (cmd) {
    case 4:
      servoLeft.write(servoLeft.read() - stepSize);
      Serial.print("Turning left by ");
      Serial.print(stepSize);
      Serial.println(" degrees");
      break;
    case 3:
      servoLeft.write(servoLeft.read() + stepSize);
      Serial.print("Turning right by ");
      Serial.print(stepSize);
      Serial.println(" degrees");
      break;
    case 1:
      servoUp.write(servoUp.read() - stepSize);
      Serial.print("Turning up by ");
      Serial.print(stepSize);
      Serial.println(" degrees");
      break;
    case 2:
      servoUp.write(servoUp.read() + stepSize);
      Serial.print("Turning down by ");
      Serial.print(stepSize);
      Serial.println(" degrees");
      break;
    default:
      Serial.println("Invalid command");
      break;
  }
}

int measureDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long duration = pulseIn(ECHO_PIN, HIGH);
  int distance = duration * 0.034 / 2;

  return distance;
}
