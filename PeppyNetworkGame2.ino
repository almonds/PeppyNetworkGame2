#include <esp_now.h>
#include <WiFi.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Display settings
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// Joystick pins
#define VRX_PIN 36
#define VRY_PIN 39
#define BUTTON_PIN 34

// Game constants
#define TRIANGLE_SIZE 10
#define MAX_SPEED 2.0 //this started at 5, but that gets kinda crazy!
#define SHOT_SPEED 5.0

// Timing constants
const unsigned long FRAME_TIME = 33; // ~30fps
unsigned long lastFrameTime = 0;
unsigned long frameCount = 0;
unsigned long lastFPSTime = 0;
float currentFPS = 0;

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// MAC Address of the other ESP32
uint8_t peerAddress[] = {0xD0, 0xEF, 0x76, 0x44, 0xB8, 0xDC};

// Game state structure - MUST be defined before any functions that use it
struct GameState {
    // Player 1 (Master) state
    float shipX1;
    float shipY1;
    float angle1;
    float speed1;
    float shotX1;
    float shotY1;
    float shotAngle1;
    bool shotActive1;
    int lives1;
    int score1;
    bool isRespawning1;
    unsigned long respawnTimer1;
    
    // Player 2 (Client) state
    float shipX2;
    float shipY2;
    float angle2;
    float speed2;
    float shotX2;
    float shotY2;
    float shotAngle2;
    bool shotActive2;
    int lives2;
    int score2;
    bool isRespawning2;
    unsigned long respawnTimer2;
    
    // Global game state
    bool gameOver;
    bool gameStarted;
    unsigned long gameTimer;
    
    // Explosion effects
    bool explosion1Active;
    bool explosion2Active;
    float explosionX1, explosionY1;
    float explosionX2, explosionY2;
    uint8_t explosionFrame1;
    uint8_t explosionFrame2;
    
    uint32_t sequence;
};

// Control input structure
struct ControlInput {
    int valueX;
    int valueY;
    bool buttonPressed;
    uint32_t sequence;
};

// Global variables
GameState gameState;
ControlInput remoteControl;
bool isPaired = false;
uint32_t currentSequence = 0;

// Function prototypes - declare ALL functions before they're used
void updateSecondShip();
void updateShipPosition();
void updateShot();
void drawTriangle(float x, float y, float angle, bool filled);
void renderGame();
void renderExplosion(float x, float y, uint8_t frame);
void sendGameState();
void updateFPS();

// Callback function prototypes
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len);
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);

// Now you can continue with the rest of your code...

// Callback when data is received
void OnDataRecv(const esp_now_recv_info_t *esp_now_info, const uint8_t *data, int len) {
    if (len == sizeof(ControlInput)) {
        memcpy(&remoteControl, data, sizeof(ControlInput));
        //updateSecondShip();
    }
}

// Callback when data is sent
void OnDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (status != ESP_OK) {
        Serial.println("Error sending data");
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize WiFi in Station mode
    WiFi.mode(WIFI_STA);
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("Error initializing ESP-NOW");
        return;
    }

    // Register callback functions
    esp_now_register_recv_cb(OnDataRecv);
    esp_now_register_send_cb(OnDataSent);

    // Add peer
    esp_now_peer_info_t peerInfo;
    memcpy(peerInfo.peer_addr, peerAddress, 6);
    peerInfo.channel = 0;
    peerInfo.encrypt = false;
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
        Serial.println("Failed to add peer");
        return;
    }

    // Initialize display
    if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        for(;;);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);  // Changed from WHITE to SSD1306_WHITE
    display.display();
    
    // Debug display
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(WHITE);
    display.setCursor(0,0);
    display.println("Game Starting...");
    display.display();
    delay(2000);
    
    // Initialize inputs
    pinMode(VRX_PIN, INPUT);
    pinMode(VRY_PIN, INPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);  // Using internal pullup for button
    
    // Initialize game state
    gameState.shipX1 = SCREEN_WIDTH * 0.1;
    gameState.shipY1 = SCREEN_HEIGHT * 0.9;
    gameState.angle1 = 0;
    gameState.speed1 = 0;
    gameState.shotActive1 = false;
    
    gameState.shipX2 = SCREEN_WIDTH * 0.9;
    gameState.shipY2 = SCREEN_HEIGHT * 0.1;
    gameState.angle2 = 0;
    gameState.speed2 = 0;
    gameState.shotActive2 = false;
    
    gameState.sequence = 0;

    // In setup() after other gameState initializations:
    gameState.lives1 = 3;
    gameState.lives2 = 3;
    gameState.score1 = 0;
    gameState.score2 = 0;
    gameState.isRespawning1 = false;
    gameState.isRespawning2 = false;
    gameState.respawnTimer1 = 0;
    gameState.respawnTimer2 = 0;
    gameState.gameOver = false;
    gameState.gameStarted = true;
    gameState.gameTimer = 0;
}

void updateShipPosition() {
    // Read local controls
    int valueX = analogRead(VRX_PIN);
    int valueY = analogRead(VRY_PIN);
    bool buttonPressed = !digitalRead(BUTTON_PIN);

    // Debug print
    //Serial.print("X: ");
   // Serial.print(valueX);
    //Serial.print(" Y: ");
   // Serial.println(valueY);

    // Angle adjustment based on X-axis
    if (abs(valueX - 1855) > 100) {
        gameState.angle1 += map(valueX, 0, 4095, -5, 5);
    }

    // Wrap angle
    if (gameState.angle1 >= 360) gameState.angle1 -= 360;
    else if (gameState.angle1 < 0) gameState.angle1 += 360;

    // Speed adjustment based on Y-axis
    int normalizedY = valueY - 2048; // Center around 0
    if (abs(normalizedY) > 2000) { // Apply dead zone
        float acceleration = 0.1;
        
        // Apply acceleration or deceleration
        if (normalizedY > 0) {
            // Joystick moved up, increase speed
            gameState.speed1 += acceleration;
        } else {
            // Joystick moved down, decrease speed
            gameState.speed1 -= acceleration;
        }

        // Constrain speed
        gameState.speed1 = constrain(gameState.speed1, 0, MAX_SPEED);
    }

    // Update position
    float radianAngle = gameState.angle1 * (PI / 180);
    gameState.shipX1 += gameState.speed1 * cos(radianAngle);
    gameState.shipY1 += gameState.speed1 * sin(radianAngle);

    // Wrap position
    if (gameState.shipX1 > SCREEN_WIDTH) gameState.shipX1 = 0;
    else if (gameState.shipX1 < 0) gameState.shipX1 = SCREEN_WIDTH;
    if (gameState.shipY1 > SCREEN_HEIGHT) gameState.shipY1 = 0;
    else if (gameState.shipY1 < 0) gameState.shipY1 = SCREEN_HEIGHT;

      
    // Handle shooting for Player 1
    if (buttonPressed && !gameState.shotActive1 && !gameState.isRespawning1) {  // Added respawn check
        gameState.shotActive1 = true;
        gameState.shotAngle1 = radianAngle;
        gameState.shotX1 = gameState.shipX1 + TRIANGLE_SIZE * cos(radianAngle);
        gameState.shotY1 = gameState.shipY1 + TRIANGLE_SIZE * sin(radianAngle);
    }
}
void updateSecondShip() {
    // Update angle based on received control input
    if (abs(remoteControl.valueX - 2048) > 100) {
        gameState.angle2 += map(remoteControl.valueX, 0, 4095, -5, 5);
    }

    // Wrap angle
    if (gameState.angle2 >= 360) gameState.angle2 -= 360;
    else if (gameState.angle2 < 0) gameState.angle2 += 360;

    // Update speed based on received Y value
    int normalizedY = remoteControl.valueY - 2048;
    if (abs(normalizedY) > 2000) {
        float acceleration = 0.1;
        gameState.speed2 += (normalizedY > 0) ? acceleration : -acceleration;
        gameState.speed2 = constrain(gameState.speed2, 0, MAX_SPEED);
    }

    // Update position
    float radianAngle = gameState.angle2 * (PI / 180);
    gameState.shipX2 += gameState.speed2 * cos(radianAngle);
    gameState.shipY2 += gameState.speed2 * sin(radianAngle);

    // Wrap position
    if (gameState.shipX2 > SCREEN_WIDTH) gameState.shipX2 = 0;
    else if (gameState.shipX2 < 0) gameState.shipX2 = SCREEN_WIDTH;
    if (gameState.shipY2 > SCREEN_HEIGHT) gameState.shipY2 = 0;
    else if (gameState.shipY2 < 0) gameState.shipY2 = SCREEN_HEIGHT;

      
    // Handle shooting for Player 2
    if (remoteControl.buttonPressed && !gameState.shotActive2 && !gameState.isRespawning2) {  // Added respawn check
        gameState.shotActive2 = true;
        gameState.shotAngle2 = radianAngle;
        gameState.shotX2 = gameState.shipX2 + TRIANGLE_SIZE * cos(radianAngle);
        gameState.shotY2 = gameState.shipY2 + TRIANGLE_SIZE * sin(radianAngle);
    }
}

void updateShot() {
    // Update Player 1's shot
    if (gameState.shotActive1) {
        gameState.shotX1 += SHOT_SPEED * cos(gameState.shotAngle1);
        gameState.shotY1 += SHOT_SPEED * sin(gameState.shotAngle1);

        // Deactivate shot when off screen
        if (gameState.shotX1 < 0 || gameState.shotX1 > SCREEN_WIDTH ||
            gameState.shotY1 < 0 || gameState.shotY1 > SCREEN_HEIGHT) {
            gameState.shotActive1 = false;
        } else {
            // Check if Player 1's shot hit Player 2
            float dx = gameState.shotX1 - gameState.shipX2;
            float dy = gameState.shotY1 - gameState.shipY2;
            float distance = sqrt(dx*dx + dy*dy);
            if (distance < TRIANGLE_SIZE && !gameState.isRespawning2) {
                gameState.shotActive1 = false;
                gameState.score1 += 1;
                gameState.lives2--;
                gameState.isRespawning2 = true;
                gameState.respawnTimer2 = millis();
                // Add explosion for Player 2
                gameState.explosion2Active = true;
                gameState.explosionX2 = gameState.shipX2;
                gameState.explosionY2 = gameState.shipY2;
                gameState.explosionFrame2 = 0;
                Serial.printf("Hit! P1 Score: %d, P2 Lives: %d\n", gameState.score1, gameState.lives2);
            }
        }
    }

    // Update Player 2's shot
    if (gameState.shotActive2) {
        gameState.shotX2 += SHOT_SPEED * cos(gameState.shotAngle2);
        gameState.shotY2 += SHOT_SPEED * sin(gameState.shotAngle2);

        // Deactivate shot when off screen
        if (gameState.shotX2 < 0 || gameState.shotX2 > SCREEN_WIDTH ||
            gameState.shotY2 < 0 || gameState.shotY2 > SCREEN_HEIGHT) {
            gameState.shotActive2 = false;
        } else {
            // Check if Player 2's shot hit Player 1
            float dx = gameState.shotX2 - gameState.shipX1;
            float dy = gameState.shotY2 - gameState.shipY1;
            float distance = sqrt(dx*dx + dy*dy);
            if (distance < TRIANGLE_SIZE && !gameState.isRespawning1) {
                gameState.shotActive2 = false;
                gameState.score2 += 1;
                gameState.lives1--;
                gameState.isRespawning1 = true;
                gameState.respawnTimer1 = millis();
                // Add explosion for Player 1
                gameState.explosion1Active = true;
                gameState.explosionX1 = gameState.shipX1;
                gameState.explosionY1 = gameState.shipY1;
                gameState.explosionFrame1 = 0;
                Serial.printf("Hit! P2 Score: %d, P1 Lives: %d\n", gameState.score2, gameState.lives1);
            }
        }
    }
}

void handleRespawns() {
    // Handle Player 1 respawn
    if (gameState.isRespawning1 && !gameState.explosion1Active) {  // Wait for explosion to finish
        if (millis() - gameState.respawnTimer1 > 2000) {  // 2 second respawn time
            gameState.isRespawning1 = false;
            // Respawn at random position
            gameState.shipX1 = random(SCREEN_WIDTH);
            gameState.shipY1 = random(SCREEN_HEIGHT);
            gameState.speed1 = 0;  // Reset speed
        }
    }

    // Handle Player 2 respawn
    if (gameState.isRespawning2 && !gameState.explosion2Active) {  // Wait for explosion to finish
        if (millis() - gameState.respawnTimer2 > 2000) {  // 2 second respawn time
            gameState.isRespawning2 = false;
            // Respawn at random position
            gameState.shipX2 = random(SCREEN_WIDTH);
            gameState.shipY2 = random(SCREEN_HEIGHT);
            gameState.speed2 = 0;  // Reset speed
        }
    }
}

void renderExplosion(float x, float y, uint8_t frame) {
    // Simple expanding circle explosion
    int size = frame * 2;  // Explosion grows with each frame
    display.drawCircle(x, y, size, WHITE);
    display.drawCircle(x, y, size-1, WHITE);
    
    // Add some particles
    for(int i = 0; i < 8; i++) {
        float angle = i * (2 * PI / 8);
        float particleX = x + cos(angle) * size;
        float particleY = y + sin(angle) * size;
        display.drawPixel(particleX, particleY, WHITE);
    }
}

void drawTriangle(float x, float y, float angle, bool filled) {
    float radianAngle = angle * (PI / 180);
    int x1 = x + TRIANGLE_SIZE * cos(radianAngle);
    int y1 = y + TRIANGLE_SIZE * sin(radianAngle);
    int x2 = x + TRIANGLE_SIZE * cos(radianAngle + 2.4);
    int y2 = y + TRIANGLE_SIZE * sin(radianAngle + 2.4);
    int x3 = x + TRIANGLE_SIZE * cos(radianAngle - 2.4);
    int y3 = y + TRIANGLE_SIZE * sin(radianAngle - 2.4);

    if (filled) {
        display.fillTriangle(x1, y1, x2, y2, x3, y3, WHITE);
    } else {
        display.drawTriangle(x1, y1, x2, y2, x3, y3, WHITE);
    }
}

void updateFPS() {
    frameCount++;
    unsigned long currentTime = millis();
    if (currentTime - lastFPSTime >= 1000) {
        currentFPS = frameCount;
        frameCount = 0;
        lastFPSTime = currentTime;
        Serial.printf("FPS: %.1f\n", currentFPS);
    }
}

void renderGame() {
    display.clearDisplay();

    // Draw ships (if not exploding/respawning)
    if (!gameState.isRespawning1) {
        drawTriangle(gameState.shipX1, gameState.shipY1, gameState.angle1, false);
    }
    if (!gameState.isRespawning2) {
        drawTriangle(gameState.shipX2, gameState.shipY2, gameState.angle2, true);
    }

    // Draw shots
    if (gameState.shotActive1) {
        display.fillRect(gameState.shotX1, gameState.shotY1, 2, 2, WHITE);
    }
    if (gameState.shotActive2) {
        display.fillRect(gameState.shotX2, gameState.shotY2, 2, 2, WHITE);
    }

    // Draw explosions
    if (gameState.explosion1Active) {
        renderExplosion(gameState.explosionX1, gameState.explosionY1, gameState.explosionFrame1);
    }
    if (gameState.explosion2Active) {
        renderExplosion(gameState.explosionX2, gameState.explosionY2, gameState.explosionFrame2);
    }

    // Draw scores and lives
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.printf("P1:%d L:%d", gameState.score1, gameState.lives1);
    display.setCursor(SCREEN_WIDTH/2, 0);
    display.printf("P2:%d L:%d", gameState.score2, gameState.lives2);

    display.display();
}

void sendGameState() {
    gameState.sequence = currentSequence++;
    esp_err_t result = esp_now_send(peerAddress, (uint8_t *)&gameState, sizeof(GameState));
    if (result != ESP_OK) {
        Serial.println("Error sending the data");
    }
}

void loop() {
    unsigned long currentTime = millis();
    
    if (currentTime - lastFrameTime >= FRAME_TIME) {
        updateShipPosition();
        updateSecondShip();
        updateShot();
        
        // Update explosion animations
        if (gameState.explosion1Active) {
            gameState.explosionFrame1++;
            if (gameState.explosionFrame1 > 8) {
                gameState.explosion1Active = false;
            }
        }
        if (gameState.explosion2Active) {
            gameState.explosionFrame2++;
            if (gameState.explosionFrame2 > 8) {
                gameState.explosion2Active = false;
            }
        }
        
        handleRespawns();  
        
        sendGameState();
        renderGame();
        updateFPS();
        lastFrameTime = currentTime;
    }
}