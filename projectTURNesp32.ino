#include <WiFi.h>
#include <HTTPClient.h>
#include <esp32cam.h>
#include <WebServer.h>

const char* WIFI_SSID = "Rishi";
const char* WIFI_PASS = "12345678";

const char* firebaseStorageUrl = "https://firebasestorage.googleapis.com/v0/b/esp32-4c4de.appspot.com/o/";
const char* idToken = "eyJhbGciOiJSUzI1NiIsImtpZCI6IjBjYjQyNzQyYWU1OGY0ZGE0NjdiY2RhZWE0Yjk1YTI5ZmJhMGM1ZjkiLCJ0eXAiOiJKV1QifQ.eyJpc3MiOiJodHRwczovL3NlY3VyZXRva2VuLmdvb2dsZS5jb20vZXNwMzItNGM0ZGUiLCJhdWQiOiJlc3AzMi00YzRkZSIsImF1dGhfdGltZSI6MTcyMjMxNDIwNSwidXNlcl9pZCI6InUwcmgwSjE2TFBmOXVqRnYzcjBZTHJuU2F3cDEiLCJzdWIiOiJ1MHJoMEoxNkxQZjl1akZ2M3IwWUxyblNhd3AxIiwiaWF0IjoxNzIyMzE0MjA1LCJleHAiOjE3MjIzMTc4MDUsImVtYWlsIjoibWFub3RodXNoYW50aEBnbWFpbC5jb20iLCJlbWFpbF92ZXJpZmllZCI6ZmFsc2UsImZpcmViYXNlIjp7ImlkZW50aXRpZXMiOnsiZW1haWwiOlsibWFub3RodXNoYW50aEBnbWFpbC5jb20iXX0sInNpZ25faW5fcHJvdmlkZXIiOiJwYXNzd29yZCJ9fQ.VNfBfQ4oLffB8js10BZqhUhga8l-4M_0tRFivlFaErKglt5JBKFhFdx8NjpgCHI1mP2JMKmYTZmrolAF3ySNbcWvy0KZeklSPZ8a5Sap34Fzl5LhqoP6IN3DjQ7qZ4NqgjeBW8_PSGROAQ9KD_SbfHs6ZA2wg-4ALwhS9Yb1CCYxhqolL4NDeR8uqLNOAlD6_UHKlrux6rkWNOTQQ0ctKL9LH_KPo1ZPqPH60awyKZH9yIt7MJZDJsFndREhkMtwHyOebQ0TYbs261E-3lu-Ct9atq8Yyx9HkB__mSy7JXqN8XaXNPfeJ14-FOagquHBDh3OSL6eU-e15ylKEfJyYQ";

const int signalPin = 4; // GPIO for signal reception
unsigned long lastSignalTime = 0;
const unsigned long minInterval = 4000; // Minimum interval between captures

WebServer server(80);

static auto hiRes = esp32cam::Resolution::find(800, 600);

void setup() {
  Serial.begin(115200);
  Serial.println();

  // Initialize camera
  esp32cam::Config cfg;
  cfg.setPins(esp32cam::pins::AiThinker);
  cfg.setResolution(hiRes);
  cfg.setBufferCount(2);
  cfg.setJpeg(80);

  if (!esp32cam::Camera.begin(cfg)) {
    Serial.println("CAMERA FAIL");
    return;
  }
  Serial.println("CAMERA OK");

  // Connect to Wi-Fi
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected! IP address: ");
  Serial.println(WiFi.localIP());

  // Initialize signal pin
  pinMode(signalPin, INPUT_PULLDOWN);

  // Setup HTTP server routes
  server.on("/cam.mjpeg", handleMjpeg);

  server.begin();
  Serial.println("HTTP server started.");
}

void loop() {
  server.handleClient();

  // Check for signal from Arduino Mega
  int signalState = digitalRead(signalPin);
  unsigned long currentMillis = millis();

  if (signalState == HIGH && currentMillis - lastSignalTime > minInterval) {
    Serial.println("Signal received from Arduino Mega");
    captureImage();
    lastSignalTime = currentMillis;
    delay(10); // Debounce delay
  }
}

void captureImage() {
  auto frame = esp32cam::capture();
  if (frame == nullptr) {
    Serial.println("CAPTURE FAIL");
    return;
  }

  Serial.printf("CAPTURE OK %dx%d %db\n", frame->getWidth(), frame->getHeight(), static_cast<int>(frame->size()));

  // Generate a unique filename
  static int imageCounter = 0;
  char fileName[32];
  snprintf(fileName, sizeof(fileName), "/image_%d.jpg", imageCounter++);

  // Save image to Firebase Storage
  uploadToFirebaseStorage(fileName, frame->data(), frame->size());
}

void uploadToFirebaseStorage(const char* fileName, const uint8_t* data, size_t size) {
  HTTPClient http;

  // Ensure the fileName does not start with '/' to avoid consecutive '/'
  String sanitizedFileName = String(fileName).startsWith("/") ? fileName + 1 : fileName;

  // Construct the URL for uploading
  String url = String("https://firebasestorage.googleapis.com/v0/b/") + 
               String("esp32-4c4de.appspot.com") + 
               String("/o?uploadType=media&name=") + 
               sanitizedFileName;

  // Encode the URL (handle spaces and other special characters)
  url.replace(" ", "%20");
  url.replace("#", "%23");

  http.begin(url);
  http.addHeader("Authorization", "Bearer " + String(idToken));
  http.addHeader("Content-Type", "image/jpeg");

  // Create a mutable copy of data
  uint8_t* mutableData = const_cast<uint8_t*>(data);

  int httpResponseCode = http.sendRequest("POST", mutableData, size);
  if (httpResponseCode > 0) {
    String response = http.getString();
    Serial.println("Upload Response Code: " + String(httpResponseCode));
    Serial.println("Upload Response: " + response);
  } else {
    Serial.print("Upload Error: ");
    Serial.println(httpResponseCode);
  }
  http.end();
}

void handleMjpeg() {
  if (!esp32cam::Camera.changeResolution(hiRes)) {
    Serial.println("SET-HI-RES FAIL");
  }

  Serial.println("STREAM BEGIN");
  WiFiClient client = server.client();
  auto startTime = millis();
  int res = esp32cam::Camera.streamMjpeg(client);
  if (res <= 0) {
    Serial.printf("STREAM ERROR %d\n", res);
    return;
  }
  auto duration = millis() - startTime;
  Serial.printf("STREAM END %dfrm %0.2ffps\n", res, 1000.0 * res / duration);
}
