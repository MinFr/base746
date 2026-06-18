#include "lvgl.h"
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

#ifdef ARDUINO

#include "lvglDrivers.h"

Adafruit_MPU6050 mpu; // Création de l'objet MPU6050

static lv_obj_t *ball; // Objet de la bille
static lv_obj_t *zone; // Objet de la zone de score, c'est une variable globale car elle va se déplacer pendant le jeu

static lv_obj_t *scoreLabel; // Objet de label du score, c'est une variable globale car on doit le mettre à jour pendant le jeu
static lv_obj_t *statusLabel; // Objet de label du titre en bas, c'est une variable globale car on doit le mettre à jour pendant le jeu
static lv_obj_t *timerLabel; // Objet de label du temps, c'est une variable globale car on doit le mettre à jour pendant le jeu
static lv_obj_t *restartButton; // Bouton pour recommencer après la fin du jeu

static int ballX = 230; // Position initiale de la bille, c'est une variable globale car elle va se déplacer pendant le jeu
static int ballY = 120; 

static int score = 0; // Score du joueur, c'est une variable globale car elle va être mise à jour pendant le jeu
static bool mpuOk = false; // Indique si le MPU6050 est bien connecté
static bool gameWon = false; // Indique si le joueur a gagné
static bool gameStarted = false; // Indique si le joueur a appuyé sur le bouton pour commencer le jeu

static unsigned long gameStartTime = 0; // Moment où la partie commence, utilisé pour calculer le temps de jeu
static bool restartButtonCreated = false; // Indique si le bouton recommencer a déjà été créé

// Valeurs de offset pour corriger la position inclinée du MPU6050
// A modifier petit à petit selon le comportement de la bille
const float OFFSET_X = 3.5;

// Visible dans updateGame() pour gérer les pénalités
static int penaltyCounter = 0; // Compteur de temps hors zone, c'est une variable globale car elle doit être mise à jour pendant le jeu
const int PENALTY_INTERVAL = 1; // Combien de fois le joueur doit être hors zone avant de perdre un point, unité : nombre d'appels à updateGame() (ex: 2 = 2*50ms = 100ms)

// Taille de l'écran
const int SCREEN_W = 480;
const int SCREEN_H = 272;

// Taille de la bille
const int BALL_SIZE = 20;

// Taille de la zone de score
const int ZONE_W = 80;
const int ZONE_H = 80;

// Position de la zone de score, maintenant c'est une variable, car elle va se déplacer
static int zoneX = 160;
static int zoneY = 80;

// Sensibilité de la bille, plus c'est grand plus elle se déplace vite
const float SENSIBILITY = 2.7;

// Tous les combien de temps la zone de score se déplace aléatoirement, unité ms
const unsigned long ZONE_MOVE_INTERVAL = 2000;
static unsigned long lastZoneMoveTime = 0; // Dernier moment où la zone a été déplacée

// Déplacement maximum de la zone à chaque fois qu'elle se déplace, unité pixels
const int ZONE_MOVE_STEP = 60;

void createGameScreen();

void resetGameVariables()
{
    ballX = 230;
    ballY = 120;
    zoneX = 160;
    zoneY = 80;
    score = 0;
    penaltyCounter = 0;
    gameWon = false;
    gameStarted = true;
    restartButtonCreated = false;
    lastZoneMoveTime = millis();
    gameStartTime = millis();

    ball = NULL;
    zone = NULL;
    scoreLabel = NULL;
    statusLabel = NULL;
    timerLabel = NULL;
    restartButton = NULL;
}

void restartGameEventHandler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        lv_obj_clean(lv_screen_active()); // Effacer l'écran de fin avant de recommencer
        resetGameVariables();
        createGameScreen();
    }
}

void startGameEventHandler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_CLICKED)
    {
        lv_obj_clean(lv_screen_active()); // Effacer le menu avant d'afficher le jeu
        resetGameVariables();
        createGameScreen();
    }
}

void createMenuScreen()
{
    lv_obj_t *screen = lv_screen_active();

    // couleur de fond
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEDEDED), LV_PART_MAIN);

    // Titre du menu
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Jeu d'equilibre");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 45);

    // Petite explication
    lv_obj_t *info = lv_label_create(screen);
    lv_label_set_text(info, "Inclinez la carte pour garder la bille dans la zone verte");
    lv_obj_align(info, LV_ALIGN_TOP_MID, 0, 85);

    // Bouton pour commencer
    lv_obj_t *startButton = lv_button_create(screen);
    lv_obj_set_size(startButton, 150, 50);
    lv_obj_align(startButton, LV_ALIGN_CENTER, 0, 25);
    lv_obj_add_event_cb(startButton, startGameEventHandler, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(startButton);
    lv_label_set_text(label, "Commencer");
    lv_obj_center(label);

    // Message si le capteur n'est pas détecté
    if (!mpuOk)
    {
        lv_obj_t *mpuLabel = lv_label_create(screen);
        lv_label_set_text(mpuLabel, "MPU6050 non detecte");
        lv_obj_align(mpuLabel, LV_ALIGN_BOTTOM_MID, 0, -15);
    }
}

void createGameScreen()
{
    lv_obj_t *screen = lv_screen_active(); // Récupère l'écran actif

    // couleur de fond
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xEDEDED), LV_PART_MAIN);

    // Titre
    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "Jeu d'equilibre - MPU6050");
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Score
    scoreLabel = lv_label_create(screen);
    lv_label_set_text(scoreLabel, "Score: 0");
    lv_obj_align(scoreLabel, LV_ALIGN_TOP_LEFT, 15, 10);

    // Temps
    timerLabel = lv_label_create(screen);
    lv_label_set_text(timerLabel, "Temps: 0s");
    lv_obj_align(timerLabel, LV_ALIGN_TOP_RIGHT, -15, 10);

    // Etat du jeu
    statusLabel = lv_label_create(screen);
    lv_label_set_text(statusLabel, "Gardez la bille dans la zone verte");
    lv_obj_align(statusLabel, LV_ALIGN_BOTTOM_MID, 0, -10);

    // Zone de score
    zone = lv_obj_create(screen);
    lv_obj_set_size(zone, ZONE_W, ZONE_H);
    lv_obj_set_pos(zone, zoneX, zoneY);

    lv_obj_set_style_bg_color(zone, lv_color_hex(0x66CC66), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(zone, LV_OPA_60, LV_PART_MAIN); // Rendre la zone un peu transparente pour mieux voir la bille quand elle est dessus

    lv_obj_set_style_border_color(zone, lv_color_hex(0x228822), LV_PART_MAIN);
    lv_obj_set_style_border_width(zone, 3, LV_PART_MAIN); 
    lv_obj_clear_flag(zone, LV_OBJ_FLAG_SCROLLABLE); // Empêcher la zone d'être scrollable, sinon elle pourrait ne pas suivre la bille correctement

    // Bille
    ball = lv_obj_create(screen);
    lv_obj_set_size(ball, BALL_SIZE, BALL_SIZE);
    lv_obj_set_style_radius(ball, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(ball, lv_color_hex(0x3366FF), LV_PART_MAIN);
    lv_obj_set_style_border_width(ball, 0, LV_PART_MAIN); // Enlever la bordure de la bille pour un rendu plus propre
    lv_obj_clear_flag(ball, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(ball, ballX, ballY);
}

void moveZoneRandomly()
{
    int minX = 40; // On laisse une marge de 40 pixels à gauche pour éviter que la zone ne soit trop proche du bord
    int maxX = SCREEN_W - ZONE_W - 40; // On laisse une marge de 40 pixels à droite + la largeur de la zone pour éviter que la zone ne soit trop proche du bord ou dépasse de l'écran

    int minY = 50; // On laisse une marge de 50 pixels en haut pour éviter que la zone ne soit trop proche du bord ou ne soit cachée par le titre
    int maxY = SCREEN_H - ZONE_H - 45; // On laisse une marge de 45 pixels en bas pour éviter que la zone ne soit trop proche du bord ou ne soit cachée par le label de statut (10 pixels de marge + 25 pixels de hauteur du label + 10 pixels de marge)

    // La zone se déplace autour de sa position actuelle
    int newX = zoneX + random(-ZONE_MOVE_STEP, ZONE_MOVE_STEP + 1); // random(a, b) génère un nombre aléatoire entre a (inclus) et b (exclus), d'où le +1 pour inclure ZONE_MOVE_STEP
    int newY = zoneY + random(-ZONE_MOVE_STEP, ZONE_MOVE_STEP + 1);

    if (newX < minX) newX = minX; // Si la nouvelle position est trop proche du bord gauche, on la remet à minX
    if (newX > maxX) newX = maxX;

    if (newY < minY) newY = minY; // Si la nouvelle position est trop proche du bord haut, on la remet à minY
    if (newY > maxY) newY = maxY;

    zoneX = newX;
    zoneY = newY;

    lv_obj_set_pos(zone, zoneX, zoneY);
}

bool isBallInsideZone()
{
    int centerX = ballX + BALL_SIZE / 2; 
    int centerY = ballY + BALL_SIZE / 2;

    return centerX >= zoneX &&
           centerX <= zoneX + ZONE_W &&
           centerY >= zoneY &&
           centerY <= zoneY + ZONE_H;
}

bool isBallNearZoneBorder()
{
    int centerX = ballX + BALL_SIZE / 2;
    int centerY = ballY + BALL_SIZE / 2;

    if (!isBallInsideZone()) {
        return false;
    }

    int distanceLeft = centerX - zoneX;
    int distanceRight = zoneX + ZONE_W - centerX;
    int distanceTop = centerY - zoneY;
    int distanceBottom = zoneY + ZONE_H - centerY;
    
    int minDistance = distanceLeft;

    if (distanceRight < minDistance) minDistance = distanceRight;
    if (distanceTop < minDistance) minDistance = distanceTop;
    if (distanceBottom < minDistance) minDistance = distanceBottom;

    return minDistance < 10;
}

void updateGame(float accelX, float accelY)
{
    if (gameWon) {
        return;
    }

    unsigned long now = millis();

    // Mise à jour du temps
    unsigned long elapsedTime = (now - gameStartTime) / 1000;
    char timerBuffer[32];
    snprintf(timerBuffer, sizeof(timerBuffer), "Temps: %lus", elapsedTime);
    lv_label_set_text(timerLabel, timerBuffer);

    // Déplacement aléatoire de la zone
    if (now - lastZoneMoveTime > ZONE_MOVE_INTERVAL)
    {
        moveZoneRandomly();
        lastZoneMoveTime = now;
    }

    // Contrôle de la bille avec le MPU6050
    ballX += (int)(accelY * SENSIBILITY);
    ballY += (int)(accelX * SENSIBILITY);

    // Limites écran
    if (ballX < 0) ballX = 0; // On laisse une marge de 0 pixels à GAUCHE pour que la bille puisse toucher le bord
    if (ballY < 35) ballY = 35; // On laisse une marge de 35 pixels EN HAUT pour que la bille puisse toucher le bord sans être cachée par le titre (10 pixels de marge + 25 pixels de hauteur du titre)

    if (ballX > SCREEN_W - BALL_SIZE) { // On laisse une marge de 0 pixels à DROITE pour que la bille puisse toucher le bord
        ballX = SCREEN_W - BALL_SIZE;
    }

    if (ballY > SCREEN_H - BALL_SIZE - 25) { // On laisse une marge de 25 pixels en bas pour que la bille puisse toucher le bord sans être cachée par le label de statut (10 pixels de marge + 25 pixels de hauteur du label + 10 pixels de marge)
        ballY = SCREEN_H - BALL_SIZE - 25;
    }

    bool inside = isBallInsideZone();
    bool nearBorder = isBallNearZoneBorder();

    lv_obj_t *screen = lv_screen_active();

    if (inside && !nearBorder)
    {
        penaltyCounter = 0;
        score++;

        lv_label_set_text(statusLabel, "Stable");
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xEDEDED), LV_PART_MAIN);
    }
    else if (inside && nearBorder)
    {
        penaltyCounter = 0;
        score++;

        lv_label_set_text(statusLabel, "Attention : proche du bord");
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFD28A), LV_PART_MAIN);
    }
    else
    {
        penaltyCounter++;

        if (penaltyCounter >= PENALTY_INTERVAL)
        {
            if (score > 0) {
                score--;
            }

            penaltyCounter = 0;
        }

        lv_label_set_text(statusLabel, "Hors zone");
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xFFD0D0), LV_PART_MAIN);
    }

    if (score >= 300)
    {
        score = 300;
        gameWon = true;

        lv_label_set_text(scoreLabel, "Score: 300");
        lv_label_set_text(statusLabel, "Tu gagnes!");
        lv_obj_set_style_bg_color(screen, lv_color_hex(0xCFFFD0), LV_PART_MAIN);

        if (!restartButtonCreated)
        {
            restartButton = lv_button_create(screen);
            lv_obj_set_size(restartButton, 150, 45);
            lv_obj_align(restartButton, LV_ALIGN_CENTER, 0, 35);
            lv_obj_add_event_cb(restartButton, restartGameEventHandler, LV_EVENT_CLICKED, NULL);

            lv_obj_t *restartLabel = lv_label_create(restartButton);
            lv_label_set_text(restartLabel, "Recommencer");
            lv_obj_center(restartLabel);

            restartButtonCreated = true;
        }

        lv_obj_set_pos(ball, ballX, ballY);
        return;
    }

    char buffer[32]; // Buffer pour construire le texte du score, on utilise snprintf pour éviter les débordements de buffer et formater correctement le texte avec le score actuel
    snprintf(buffer, sizeof(buffer), "Score: %d", score);
    lv_label_set_text(scoreLabel, buffer);

    lv_obj_set_pos(ball, ballX, ballY);
}

void mySetup()
{
    Serial.println("Start MPU6050 balance game");

    Wire.setSDA(PB9);   // D14 = SDA
    Wire.setSCL(PB8);   // D15 = SCL
    Wire.begin();

    delay(500);

    randomSeed(micros()); // Initialiser le générateur de nombres aléatoires avec une valeur qui change à chaque démarrage (le nombre de microsecondes depuis le démarrage de la carte) pour que la zone de score se déplace différemment à chaque partie

    if (!mpu.begin()) {
        Serial.println("MPU6050 NOT FOUND");
        mpuOk = false;  
    } else {
        Serial.println("MPU6050 FOUND");
        mpuOk = true;

        mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
        mpu.setGyroRange(MPU6050_RANGE_500_DEG);
        mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    }

    createMenuScreen();
}

void loop()
{

}

void myTask(void *pvParameters)
{
    TickType_t xLastWakeTime;
    xLastWakeTime = xTaskGetTickCount();

    while (1)
    {
        if (mpuOk && gameStarted)
        {
            sensors_event_t a, g, temp;
            mpu.getEvent(&a, &g, &temp);

            Serial.print("Accel X: ");
            Serial.print(a.acceleration.x);
            Serial.print(" Y: ");
            Serial.print(a.acceleration.y);
            Serial.print(" Z: ");
            Serial.println(a.acceleration.z);

            if (lvglLock(pdMS_TO_TICKS(20)))
            {
                updateGame(a.acceleration.x - OFFSET_X, a.acceleration.y);
                lvglUnlock();
            }
        }

        vTaskDelayUntil(&xLastWakeTime, pdMS_TO_TICKS(50)); // Attendre 50 ms avant la prochaine mise à jour du jeu, cela correspond à une fréquence de 20 mises à jour par seconde, ce qui est suffisant pour un jeu fluide tout en laissant assez de temps pour les autres tâches du système
    }
}

#else

#include "lvgl.h"
#include "app_hal.h"
#include <cstdio>

int main(void)
{
    printf("LVGL Simulator\n");
    fflush(stdout);

    lv_init();
    hal_setup();

    lv_obj_t *label = lv_label_create(lv_screen_active());
    lv_label_set_text(label, "Simulation PC - MPU6050 non disponible");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    hal_loop();
    return 0;
}

#endif