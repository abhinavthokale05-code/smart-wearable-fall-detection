  /*
   MIT License

  Copyright (c) 2024 Felix Biego

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.

  ______________  _____
  ___  __/___  /_ ___(_)_____ _______ _______
  __  /_  __  __ \__  / _  _ \__  __ `/_  __ \
  _  __/  _  /_/ /_  /  /  __/_  /_/ / / /_/ /
  /_/     /_.___/ /_/   \___/ _\__, /  \____/
                              /____/
*/

/*
   MIT License
*/

#include <stdint.h>
#include "app_hal.h"
#include "apps/sample/sample.h"
#include <ChronosESP32.h>
#include <NimBLEDevice.h>
#include "MAX30105.h"
#include "Pulse.h"
#include "driver/i2s.h"
#include <math.h>
#include "PWR_Key.h"
#include "rf_model.h"

Eloquent::ML::Port::RandomForest fallML;
bool mlEvaluated = false;
int mlPrediction = 0;



MAX30105 particleSensor;
int currentHR = 0;
long lastBeat = 0;

// --- HR ON-DEMAND VARIABLES ---
bool hrMeasurementActive = false;
unsigned long hrMeasurementStartTime = 0;
const unsigned long HR_MEASURE_DURATION = 15000; // 15 seconds measuring window

int validBeats[15]; // Array to hold good beats for final average
int validBeatCount = 0;

int displayHR = 0; // The value your UI should show
String hrStatusText = "Click To Measure"; // The text your UI should show

extern ChronosESP32 watch;

#if defined(LV_USE_SDL) && !defined(ARDUINO)

// ---------------- PC Emulator ----------------
int main(void)
{
    hal_setup();

    while (1)
    {
        hal_loop();
    }

    return 0;
}

#else

// ---------------- ESP32 Hardware Build ----------------
#include <Arduino.h>
#include <math.h>
#include "Gyro_QMI8658.h"
#include <time.h>
#include "ui/ui.h"

void playAlarmTone(); 
void alarmTask(void *param);
// ----------------------------------------------------
// GLOBAL FLAGS
// ----------------------------------------------------
// --- BLE QUEUE & TIMING VARIABLES ---
bool sosPending = false;
unsigned long lastSosRetry = 0;
unsigned long audioStopTime = 0;

volatile bool sosPopupRequest = false;
bool sosPopupShown = false;

bool forceSOSMode = false;
unsigned long forceSOSUntil = 0;

volatile bool emergencyLock = false;
void startEmergencyBLETrigger();

void handleBootButtonSOS();
void startManualSOS();
void startSosAudioAlert(bool continuous);
void stopEmergencySOS(const char *reason);
bool isSOSFlowActive();

String emergencyType = "Emergency"; // Default text
bool speakerInitDone = false;
bool sosActive = false;
unsigned long lastSOS = 0;
const int SOS_COOLDOWN = 20000; // 20 seconds
bool alarmPlaying = false;
bool bleAlreadySent = false;
volatile bool alarmStopRequested = false;
volatile bool alarmContinuousMode = false;
extern bool alarmRunning;
// ----------------------------------------------------
// FORCE OPEN SOS POPUP
// ----------------------------------------------------
void handleSOSPopup()
{
    static bool popupLoaded = false;
    static bool queueStarted = false;

    // Trigger popup once
    if (sosPopupRequest)
    {
        sosPopupRequest = false;
        bleAlreadySent = false;
        sosPending = false; // Reset queue state
        queueStarted = false;

        forceSOSMode = true;
        
        // 16 SECONDS TOTAL, AUDIO STOPS AT 10 SECONDS
        forceSOSUntil = millis() + 16000; 
        audioStopTime = millis() + 10000; 
        
        emergencyLock = true;
        popupLoaded = false;

        Serial.println("[UI] SOS START (16s Timer)");
    }

    // Keep popup visible
    if (forceSOSMode)
    {
        if (!popupLoaded)
        {
            sample_open_auto();   // open SOS screen once
            popupLoaded = true;
        }

        lv_timer_handler(); // refresh UI

        // Stop audio after 10 seconds, but keep popup alive
        if (millis() >= audioStopTime && !alarmStopRequested) {
            alarmStopRequested = true;
            Serial.println("[SOS] Audio stopped, 6 seconds remaining on UI...");
        }

        // Timer completely expires at 16 seconds
        if (millis() >= forceSOSUntil && !queueStarted)
        {
            Serial.println("[SOS] Countdown finished. Queuing BLE trigger...");

            sosPending = true; // Add to the aggressive retry queue
            lastSosRetry = 0;  // Force an immediate first attempt
            queueStarted = true; // Prevent re-triggering this block

            forceSOSMode = false;
            emergencyLock = false;
            popupLoaded = false;

            ui_app_exit();   // close popup
            Serial.println("[UI] SOS CLOSED");
        }
    }
}

bool isSOSFlowActive()
{
    return sosPopupRequest || forceSOSMode || sosActive || alarmRunning;
}

void startManualSOS()
{
    if (isSOSFlowActive())
    {
        return;
    }

    bleAlreadySent = false;
    emergencyType = "MANUAL SOS";
    sosPopupRequest = true;
    sosPopupShown = false;
    sosActive = true;
    lastSOS = millis();

    startSosAudioAlert(false);

    Serial.println("[SOS] MANUAL SOS INITIATED");
}

void startSosAudioAlert(bool continuous)
{
    alarmStopRequested = false;
    alarmContinuousMode = continuous;
    sosActive = true;
    lastSOS = millis();

    if (!alarmRunning)
    {
        xTaskCreate(alarmTask, "alarm", 4096, NULL, 1, NULL);
    }
}

void stopEmergencySOS(const char *reason)
{
    alarmStopRequested = true;
    alarmContinuousMode = false;
    sosPopupRequest = false;
    sosPopupShown = false;
    forceSOSMode = false;
    forceSOSUntil = 0;
    emergencyLock = false;
    sosActive = false;
    alarmPlaying = false;
    bleAlreadySent = false;
    sosPending = false; 

    // Set the lastSOS safety tracker to the current time. 
    // This forces the system to ignore raw mechanical impacts caused by 
    // pressing buttons or adjusting the strap for the next 7 seconds.
    lastSOS = millis(); 

    i2s_zero_dma_buffer(I2S_NUM_0);
    sample_force_close();

    Serial.printf("[SOS] STOPPED: %s\n", reason);
}

void handleBootButtonSOS()
{
    static int lastReading = HIGH;
    static int stableState = HIGH;
    static unsigned long lastDebounceTime = 0;
    static unsigned long lastReleaseTime = 0;

    const unsigned long debounceMs = 40;
    const unsigned long doubleClickMs = 450;

    int reading = digitalRead(0);

    if (reading != lastReading)
    {
        lastDebounceTime = millis();
        lastReading = reading;
    }

    if ((millis() - lastDebounceTime) < debounceMs)
    {
        return;
    }

    if (reading != stableState)
    {
        stableState = reading;

        if (stableState == HIGH)
        {
            unsigned long now = millis();
            bool doublePress = (now - lastReleaseTime) <= doubleClickMs;
            lastReleaseTime = now;

            if (doublePress && isSOSFlowActive())
            {
                stopEmergencySOS("BOOT BUTTON DOUBLE PRESS");
            }
            else if (!isSOSFlowActive())
            {
                startManualSOS();
            }
        }
    }
}

// ----------------------------------------------------
//  (FINAL TUNED VERSION
// ------------------------------------------------
#define FALL_DEBUG 1

class FallDetector
{
public:
    enum State
    {
        IDLE = 0,
        COLLAPSE_WATCH,
        IMPACT_WATCH,
        POST_FALL_CHECK,
        FALL_CONFIRMED,
        COOLDOWN
    };

    void reset()
    {
        state = IDLE;
        detectedOneFrame = false;
        confidence = 0.0f;
        mlEvaluated = false;
        mlPrediction = 0;

        lastMs = 0;
        stateStartMs = 0;
        cooldownStartMs = 0;

        lastAccMag = 1.0f;

        staticX = 0.0f;
        staticY = 0.0f;
        staticZ = 1.0f;

        sma50Sum = 0.0f;
        mag30Sum = 0.0f;
        mag30SqSum = 0.0f;
        smaLongAvg = 1.0f;

        sma50Index = 0;
        mag30Index = 0;
        sma50Count = 0;
        mag30Count = 0;

        collapseCount = 0;
        stillCount = 0;
        movingAfterImpactCount = 0;

        peakImpactG = 0.0f;
        peakJerk = 0.0f;
        peakGyro = 0.0f;
        minAcc = 10.0f;

        startTilt = 0.0f;
        impactTilt = 0.0f;
        finalTilt = 0.0f;

        // ===== DATASET V2 RESET =====

        lowGDurationMs = 0;
        highGDurationMs = 0;
        impactDurationMs = 0;
        timeToStillMs = 0;

        peakPostImpactGyro = 0.0f;
        postMotionEnergy = 0.0f;
        postGyroSqSum = 0.0f;

        pitchDelta = 0.0f;
        tiltDelta = 0.0f;

        impactStartMs = 0;
        stillReached = false;

        gyroStartValue = 0.0f;
        gyroEndValue = 0.0f;
        gyroDecayRate = 0.0f;

        postSmaSum = 0.0f;
        postGyroSum = 0.0f;
        postPitchSum = 0.0f;
        postCount = 0;
        postHighGyroCount = 0;
        postHighAccCount = 0;

        for (int i = 0; i < 50; i++)
            sma50Buf[i] = 0.0f;

        for (int i = 0; i < 30; i++)
            mag30Buf[i] = 1.0f;
    }

    void update(float ax, float ay, float az,
                float gx, float gy, float gz,
                uint32_t timestamp_ms)
    {
        detectedOneFrame = false;

        if (lastMs == 0)
        {
            lastMs = timestamp_ms;
            lastAccMag = sqrtf(ax * ax + ay * ay + az * az);

            staticX = ax;
            staticY = ay;
            staticZ = az;
            return;
        }

        float dt = (timestamp_ms - lastMs) / 1000.0f;

        if (dt <= 0.0f || dt > 0.20f)
            dt = 0.01f;

        lastMs = timestamp_ms;

        float accMag = sqrtf(ax * ax + ay * ay + az * az);
        float gyroMag = sqrtf(gx * gx + gy * gy + gz * gz);

        float jerk = fabsf((accMag - lastAccMag) / dt);
        lastAccMag = accMag;

        staticX = 0.90f * staticX + 0.10f * ax;
        staticY = 0.90f * staticY + 0.10f * ay;
        staticZ = 0.90f * staticZ + 0.10f * az;

        float staticMag = sqrtf(staticX * staticX +
                                staticY * staticY +
                                staticZ * staticZ);

        if (staticMag < 0.001f)
            staticMag = 0.001f;

        float zRatio = staticZ / staticMag;

        if (zRatio > 1.0f) zRatio = 1.0f;
        if (zRatio < -1.0f) zRatio = -1.0f;

        float tilt = acosf(zRatio) * 180.0f / PI;

        float pitchLike =
            atan2f(staticX, sqrtf(staticY * staticY + staticZ * staticZ)) *
            180.0f / PI;

        float smaInstant = fabsf(ax) + fabsf(ay) + fabsf(az);
        updateSma50(smaInstant);
        updateMag30(accMag);

        float sma = getSma50();
        float varAcc = getVariance30();

        if (state == IDLE && gyroMag < 10.0f && accMag > 0.85f && accMag < 1.15f)
            smaLongAvg = 0.999f * smaLongAvg + 0.001f * sma;

        if (smaLongAvg < 0.20f)
            smaLongAvg = 1.0f;

        float rho = sma / smaLongAvg;
        float clipped = rho - 1.0f;

        if (clipped < 0.0f) clipped = 0.0f;
        if (clipped > 2.0f) clipped = 2.0f;

        float adaptiveImpactG = 1.45f * (1.0f + 0.30f * clipped);

        if (adaptiveImpactG < 1.35f)
            adaptiveImpactG = 1.35f;

        if (adaptiveImpactG > 2.15f)
            adaptiveImpactG = 2.15f;

        switch (state)
        {
        case IDLE:
        {
            confidence = 0.0f;

            bool collapseMotion =
                gyroMag > 100.0f ||
                jerk > 15.0f ||
                accMag < 0.70f;

            if (collapseMotion)
            {
                state = COLLAPSE_WATCH;
                stateStartMs = timestamp_ms;

                collapseCount = 0;

                peakImpactG = accMag;
                peakJerk = jerk;
                peakGyro = gyroMag;
                minAcc = accMag;

                startTilt = tilt;
                impactTilt = tilt;
            }

            break;
        }

        case COLLAPSE_WATCH:
        {
            if (accMag < minAcc) minAcc = accMag;
            if (accMag > peakImpactG) peakImpactG = accMag;
            if (jerk > peakJerk) peakJerk = jerk;
            if (gyroMag > peakGyro) peakGyro = gyroMag;

            // ===== DATASET V2 =====

            if (accMag < 0.80f)
            {
                lowGDurationMs += (uint32_t)(dt * 1000.0f);
            }

            if (accMag > 1.50f)
            {
                highGDurationMs += (uint32_t)(dt * 1000.0f);
            }

            if (gyroMag > 18.0f || jerk > 5.5f || accMag < 0.90f)
                collapseCount++;

            bool impactNow =
                accMag > adaptiveImpactG ||
                (accMag > 1.28f && jerk > 14.0f) ||
                (jerk > 22.0f && accMag > 1.12f);

            if (impactNow && collapseCount >= 3)
            {
                mlEvaluated = false;
                mlPrediction = 0;
                state = IMPACT_WATCH;
                stateStartMs = timestamp_ms;

                // ===== DATASET V2 =====

                impactStartMs = timestamp_ms;

                gyroStartValue = gyroMag;

                peakPostImpactGyro = gyroMag;

                postMotionEnergy = 0.0f;

                postGyroSqSum = 0.0f;

                stillReached = false;

                impactTilt = tilt;

                postSmaSum = 0.0f;
                postGyroSum = 0.0f;
                postPitchSum = 0.0f;
                postCount = 0;
                postHighGyroCount = 0;
                postHighAccCount = 0;

                stillCount = 0;
                movingAfterImpactCount = 0;

#if FALL_DEBUG
                Serial.printf(
                    "[FALL] Impact candidate acc=%.2f jerk=%.2f gyro=%.2f adaptive=%.2f minAcc=%.2f\n",
                    accMag, jerk, gyroMag, adaptiveImpactG, minAcc
                );
#endif
            }

            if (timestamp_ms - stateStartMs > 750)
                state = IDLE;

            break;
        }

        case IMPACT_WATCH:
        {
            if (accMag > peakImpactG) peakImpactG = accMag;
            if (jerk > peakJerk) peakJerk = jerk;
            if (gyroMag > peakGyro) peakGyro = gyroMag;

            state = POST_FALL_CHECK;
            stateStartMs = timestamp_ms;

            break;
        }

        case POST_FALL_CHECK:
        {
            postSmaSum += sma;
            postGyroSum += gyroMag;
            postPitchSum += pitchLike;

            // ===== DATASET V2 =====

            impactDurationMs =
            timestamp_ms - impactStartMs;

            if (gyroMag > peakPostImpactGyro)
            {
                peakPostImpactGyro = gyroMag;
            }

            postMotionEnergy +=
                (gyroMag * gyroMag) * dt;

            postGyroSqSum +=
                gyroMag * gyroMag;

            if (gyroMag > 60.0f)
                postHighGyroCount++;

            if (accMag > 1.60f)
                postHighAccCount++;

            if (postCount < 100)
                postCount++;

            finalTilt = tilt;

            bool stillNow =
                gyroMag < 90.0f &&
                accMag > 0.68f &&
                accMag < 1.55f &&
                jerk < 16.0f;


            if (stillNow)
            {
                stillCount++;

                if (!stillReached && stillCount >= 8)
                {
                    stillReached = true;

                    timeToStillMs =
                        timestamp_ms - impactStartMs;
                }
            }
            else if (gyroMag > 36.0f || jerk > 12.0f)
            {
                stillCount = 0;
            }

            if (gyroMag > 140.0f || jerk > 45.0f || accMag > 2.20f)
                movingAfterImpactCount++;
            else if (movingAfterImpactCount > 0)
                movingAfterImpactCount--;
            

            float postSma = postSmaSum / (float)postCount;
            float postGyro = postGyroSum / (float)postCount;
            float postPitch = postPitchSum / (float)postCount;


            // ===== DATASET V2 FINAL FEATURES =====

            pitchDelta =
                fabsf(postPitch - pitchLike);

            tiltDelta =
                fabsf(finalTilt - startTilt);

            gyroEndValue = postGyro;

            if (impactDurationMs > 0)
            {
                gyroDecayRate =
                    (gyroStartValue - gyroEndValue) /
                    ((float)impactDurationMs / 1000.0f);
            }

            float postGyroVariance = 0.0f;

            if (postCount > 0)
            {
            float meanGyro =
                postGyroSum / (float)postCount;

            postGyroVariance =
                (postGyroSqSum / (float)postCount) -
                (meanGyro * meanGyro);
            }

            float highGyroFrac = (float)postHighGyroCount / (float)postCount;
            float highAccFrac = (float)postHighAccCount / (float)postCount;

            float tiltChange = fabsf(finalTilt - startTilt);
            float impactTiltChange = fabsf(impactTilt - startTilt);


            float s1Impact = clamp01((peakImpactG - 1.15f) / 0.85f);
            float s2Jerk = clamp01((peakJerk - 8.0f) / 28.0f);
            float s3Collapse = clamp01((float)collapseCount / 10.0f);
            float s4Pitch = clamp01((postPitch - 45.0f) / 30.0f);
            float s5Stillness = clamp01((float)stillCount / 12.0f);
            float s6LowVar = clamp01((0.080f - varAcc) / 0.080f);

            confidence =
                0.30f * s1Impact +
                0.20f * s2Jerk +
                0.15f * s3Collapse +
                0.15f * s4Pitch +
                0.10f * s5Stillness +
                0.10f * s6LowVar;

                float features[23] = {
    confidence,
    peakImpactG,
    minAcc,
    peakJerk,

    postPitch,  // Changed from pitchLike
    finalTilt,  // Changed from tilt
    impactTilt,

    (float)stillCount,
    (float)movingAfterImpactCount,

    postGyro,
    highGyroFrac,
    highAccFrac,

    postSma,

    (float)lowGDurationMs,
    (float)highGDurationMs,
    (float)impactDurationMs,
    (float)timeToStillMs,

    peakPostImpactGyro,
    gyroDecayRate,
    postMotionEnergy,
    postGyroVariance,

    pitchDelta,
    tiltDelta
};

            bool datasetFallBracket =
                peakImpactG > 1.75f &&
                peakJerk > 22.0f &&
                postPitch > 50.0f &&
                highGyroFrac < 0.45f &&
                highAccFrac < 0.22f &&
                postSma < 2.05f;
            
            bool lowGDrop =
                minAcc < 0.82f &&
                peakImpactG > 1.45f &&
                peakJerk > 22.0f;

            bool hardImpactDrop =
                peakImpactG > 2.20f &&
                peakJerk > 45.0f &&
                postPitch > 52.0f &&
                highGyroFrac < 0.35f &&
                highAccFrac < 0.18f;

            bool postureDrop =
                tiltChange > 25.0f ||
                impactTiltChange > 25.0f ||
                minAcc < 0.82f;

            bool rejectContinuousMotion =
                highGyroFrac > 0.45f ||
                highAccFrac > 0.28f ||
                movingAfterImpactCount > 20 ||
                postSma > 2.25f;


            bool rejectWeakNoDrop =
                peakImpactG < 1.55f &&
                peakJerk < 25.0f &&
                minAcc > 0.90f;
            
            // --- NEW: ARM SWING FILTER ---
            // A true fall generates sustained weightlessness. An arm swing does not.
            // Relaxed the freefall requirement to 80ms to avoid rejecting soft slumps.
            bool rejectArmSwing = 
                (lowGDurationMs < 80) && 
                (minAcc > 0.65f) && 
                (peakImpactG < 2.5f);
            
            bool normalFallConfirm =
                confidence > 0.60f &&
                datasetFallBracket &&
                postureDrop &&
                !rejectContinuousMotion &&
                !rejectWeakNoDrop &&
                !rejectArmSwing; // Added restriction

            bool strongFallConfirm =
                confidence > 0.68f &&
                hardImpactDrop &&
                !rejectContinuousMotion; 
                // Removed !rejectArmSwing here: A hard impact resulting in stillness is a fall, regardless of freefall time.

            bool lowGFallConfirm =
                confidence > 0.72f &&
                lowGDrop &&
                postPitch > 60.0f &&
                highGyroFrac < 0.50f &&
                highAccFrac < 0.25f &&
                postSma < 2.10f &&
                movingAfterImpactCount < 28 &&
                !rejectArmSwing; // Added restriction

            bool ruleFall =
                normalFallConfirm ||
                strongFallConfirm ||
                lowGFallConfirm;

            // ONLY evaluate the ML model and rules AFTER the 1.2 second post-fall window has finished gathering data
            if (timestamp_ms - stateStartMs > 1200)
            { 
                // Now that the data window is complete, run the prediction
                mlPrediction = fallML.predict(features);

                Serial.printf(
                    "F0=%.2f F1=%.2f F2=%.2f F3=%.2f F4=%.2f F5=%.2f\n",
                    features[0], features[1], features[2], features[3], features[4], features[5]
                );
                
                
                Serial.printf("[FINAL] Rule=%d ML=%d\n", ruleFall, mlPrediction);

                // --- ML SANITY CHECK ---
                bool mlTrusted = false;
                if (mlPrediction == 1) {
                    // Rule 1: NEVER trust the ML if the person keeps moving heavily (not a fall)
                    if (!rejectContinuousMotion) {
                        
                        // Rule 2 (THE BYPASS): If the physics heavily indicate a fall (High confidence, 
                        // solid impact, completely still), trust the ML even if there was no freefall time.
                        if (confidence > 0.75f && peakImpactG > 1.60f && stillCount > 50) {
                            mlTrusted = true;
                        }
                        // Rule 3 (STANDARD): Apply the stricter arm swing filters for weaker/borderline impacts
                        else if (peakImpactG > 1.35f && confidence > 0.45f && stillCount > 2 && !rejectArmSwing) {
                            mlTrusted = true;
                        }
                    }

                    if (!mlTrusted) {
                        Serial.println("[ML_REJECTED] ML predicted fall, but failed physics sanity check (Arm Swing / Continuous Motion).");
                    }
                }

                // Confirm the fall if the ML is Trusted, OR if the hardcoded physics rules catch it
                if (mlTrusted || ruleFall)
                {
                    Serial.printf(
                        "[FD_CONFIRM] ML=%d Rule=%d Conf=%.2f PeakG=%.2f PeakJerk=%.2f Pitch=%.2f\n",
                        mlPrediction, ruleFall, confidence, peakImpactG, peakJerk, postPitch
                    );

                    if (strongFallConfirm) Serial.println("[FD_CONFIRM] HARD IMPACT");
                    else if (normalFallConfirm) Serial.println("[FD_CONFIRM] NORMAL FALL");
                    else if (lowGFallConfirm) Serial.println("[FD_CONFIRM] LOW-G FALL");
                    else if (mlPrediction == 1 && !ruleFall) Serial.println("[FD_CONFIRM] ML ONLY DETECTED FALL");

                    Serial.printf(
                        "[FD_DETAIL] tilt=%.2f impactTilt=%.2f minAcc=%.2f still=%d move=%d\n",
                        tiltChange, impactTiltChange, minAcc, stillCount, movingAfterImpactCount
                    );
                    
                    Serial.printf(
                        "[FD_V2] lowG=%lu highG=%lu impactDur=%lu timeStill=%lu peakPostGyro=%.2f gyroDecay=%.2f energy=%.2f gyroVar=%.2f pitchDelta=%.2f tiltDelta=%.2f\n",
                        lowGDurationMs, highGDurationMs, impactDurationMs, timeToStillMs, peakPostImpactGyro, gyroDecayRate, postMotionEnergy, postGyroVariance, pitchDelta, tiltDelta
                    );
                    
                    state = FALL_CONFIRMED;
                    detectedOneFrame = true;

#if FALL_DEBUG
                    Serial.printf(
                        "[FALL] CONFIRMED C=%.2f peakG=%.2f minG=%.2f jerk=%.2f pitch=%.2f still=%d postGyro=%.2f highGyro=%.2f highAcc=%.2f postSMA=%.2f\n",
                        confidence, peakImpactG, minAcc, peakJerk, postPitch, stillCount, postGyro, highGyroFrac, highAccFrac, postSma
                    );
#endif
                }
                else 
                {
#if FALL_DEBUG
                    Serial.printf(
                        "[FALL] rejected C=%.2f peakG=%.2f minG=%.2f jerk=%.2f pitch=%.2f still=%d moving=%d postGyro=%.2f highGyro=%.2f highAcc=%.2f postSMA=%.2f tilt=%.2f\n",
                        confidence, peakImpactG, minAcc, peakJerk, postPitch, stillCount, movingAfterImpactCount, postGyro, highGyroFrac, highAccFrac, postSma, tiltChange
                    );
#endif
                    // If neither the ML nor the rules detected a fall, reset to IDLE
                    state = IDLE;
                    confidence = 0.0f;
                }
                
                // Reset the ML flag for the next event
                mlEvaluated = false;
            }

            break;
        }

        case FALL_CONFIRMED:
        {
            cooldownStartMs = timestamp_ms;
            state = COOLDOWN;
            break;
        }

        case COOLDOWN:
        {
            if (timestamp_ms - cooldownStartMs > 18000)
            {
                state = IDLE;
                confidence = 0.0f;
            }

            break;
        }
        }
        #if FALL_DEBUG
        static State lastState = IDLE;

        if(state != lastState)
        {
            Serial.printf(
                "[FD_TRANSITION] %d -> %d (%lu ms)\n",
                (int)lastState,
                (int)state,
                timestamp_ms
            );

            lastState = state;
        }
        #endif
    }

    bool isFallDetected()
    {
        if (detectedOneFrame)
        {
            detectedOneFrame = false;
            return true;
        }

        return false;
    }

    float getConfidence()
    {
        return confidence;
    }

    const char* getStateName()
    {
        switch (state)
        {
        case IDLE: return "IDLE";
        case COLLAPSE_WATCH: return "COLLAPSE_WATCH";
        case IMPACT_WATCH: return "IMPACT_WATCH";
        case POST_FALL_CHECK: return "POST_FALL_CHECK";
        case FALL_CONFIRMED: return "FALL_CONFIRMED";
        case COOLDOWN: return "COOLDOWN";
        default: return "UNKNOWN";
        }
    }

private:
    State state = IDLE;

    bool detectedOneFrame = false;
    float confidence = 0.0f;

    uint32_t lastMs = 0;
    uint32_t stateStartMs = 0;
    uint32_t cooldownStartMs = 0;

    float lastAccMag = 1.0f;

    float staticX = 0.0f;
    float staticY = 0.0f;
    float staticZ = 1.0f;

    float sma50Buf[50];
    float mag30Buf[30];

    int sma50Index = 0;
    int mag30Index = 0;
    int sma50Count = 0;
    int mag30Count = 0;

    float sma50Sum = 0.0f;
    float mag30Sum = 0.0f;
    float mag30SqSum = 0.0f;

    float smaLongAvg = 1.0f;

    int collapseCount = 0;
    int stillCount = 0;
    int movingAfterImpactCount = 0;

    int postHighGyroCount = 0;
    int postHighAccCount = 0;

    float peakImpactG = 0.0f;
    float peakJerk = 0.0f;
    float peakGyro = 0.0f;
    float minAcc = 10.0f;

    float startTilt = 0.0f;
    float impactTilt = 0.0f;
    float finalTilt = 0.0f;

    float postSmaSum = 0.0f;
    float postGyroSum = 0.0f;
    float postPitchSum = 0.0f;
    int postCount = 0;

    
    // ===== DATASET V2 FEATURES =====

    uint32_t lowGDurationMs = 0;
    uint32_t highGDurationMs = 0;
    uint32_t impactDurationMs = 0;
    uint32_t timeToStillMs = 0;

    float peakPostImpactGyro = 0.0f;
    float postMotionEnergy = 0.0f;

    float postGyroSqSum = 0.0f;

    float pitchDelta = 0.0f;
    float tiltDelta = 0.0f;

    uint32_t impactStartMs = 0;
    bool stillReached = false;

    float gyroStartValue = 0.0f;
    float gyroEndValue = 0.0f;
    float gyroDecayRate = 0.0f;







    static float clamp01(float v)
    {
        if (v < 0.0f) return 0.0f;
        if (v > 1.0f) return 1.0f;
        return v;
    }

    void updateSma50(float value)
    {
        if (sma50Count < 50)
        {
            sma50Buf[sma50Index] = value;
            sma50Sum += value;
            sma50Count++;
        }
        else
        {
            sma50Sum -= sma50Buf[sma50Index];
            sma50Buf[sma50Index] = value;
            sma50Sum += value;
        }

        sma50Index++;

        if (sma50Index >= 50)
            sma50Index = 0;
    }

    float getSma50()
    {
        if (sma50Count <= 0)
            return 1.0f;

        return sma50Sum / (float)sma50Count;
    }

    void updateMag30(float value)
    {
        if (mag30Count < 30)
        {
            mag30Buf[mag30Index] = value;
            mag30Sum += value;
            mag30SqSum += value * value;
            mag30Count++;
        }
        else
        {
            float old = mag30Buf[mag30Index];

            mag30Sum -= old;
            mag30SqSum -= old * old;

            mag30Buf[mag30Index] = value;

            mag30Sum += value;
            mag30SqSum += value * value;
        }

        mag30Index++;

        if (mag30Index >= 30)
            mag30Index = 0;
    }

    float getVariance30()
    {
        if (mag30Count <= 1)
            return 0.0f;

        float mean = mag30Sum / (float)mag30Count;
        float meanSq = mag30SqSum / (float)mag30Count;

        float variance = meanSq - mean * mean;

        if (variance < 0.0f)
            variance = 0.0f;

        return variance;
    }
};


void detectFall()
{
    static FallDetector fallDetector;
    static unsigned long lastSample = 0;

    if (millis() - lastSample < 10)
        return;

    lastSample = millis();

    QMI8658_Loop();

    fallDetector.update(
        Accel.x, Accel.y, Accel.z,
        Gyro.x, Gyro.y, Gyro.z,
        millis()
    );

#if FALL_DEBUG
    static unsigned long lastDebug = 0;

    if (millis() - lastDebug > 300)
    {
        lastDebug = millis();

        Serial.printf(
            "[FALL] state=%s C=%.2f\n",
            fallDetector.getStateName(),
            fallDetector.getConfidence()
        );
    }
#endif

    if (fallDetector.isFallDetected())
    {
        Serial.println("######## FALLOUT PUSH-UP IMPACT DETECTED ########");

        if (!sosActive &&
            (millis() - lastSOS > SOS_COOLDOWN))
        {
            sosActive = true;

            lastSOS = millis();

            if (!alarmPlaying &&
                !alarmRunning)
            {
                startSosAudioAlert(true);
            }

            emergencyType = "FALL DETECTED";

            sosPopupRequest = true;
            sosPopupShown = false;

            forceSOSMode = true;
            forceSOSUntil = millis() + 10000;
        }
    }
}


void startEmergencyBLETrigger()
{
    if (bleAlreadySent)
    {
        Serial.println("[BLE] Already sent -> skipping duplicate");
        return;
    }

    Serial.println("[BLE] Triggering SOS Custom Data...");

    if (watch.isConnected())
    {
        const char *packet = "SOS_TRIGGER";

        watch.sendCommand((uint8_t *)packet, strlen(packet));

        Serial.printf("[BLE] Data Sent: %s\n", packet);

        bleAlreadySent = true;
    }
    else
    {
        Serial.println("[BLE] SOS Trigger Aborted: No Active Connection");
    }
}

void handleBLEQueue() {
    if (sosPending) {
        // Try every 2000 ms (2 seconds)
        if (millis() - lastSosRetry >= 2000) {
            lastSosRetry = millis();

            if (watch.isConnected()) {
                const char *packet = "SOS_TRIGGER";
                watch.sendCommand((uint8_t *)packet, strlen(packet));
                
                Serial.printf("[BLE] QUEUE SUCCESS! Data Sent: %s\n", packet);
                
                sosPending = false;    // Successfully sent, remove from queue
                bleAlreadySent = true; // Mark as resolved
            } else {
                Serial.println("[BLE] Queue pending... Phone disconnected. Retrying in 2s...");
            }
        }
    }
}

void startHRMeasurement() {
    if (hrMeasurementActive) return; // Don't restart if already running
    
    hrMeasurementActive = true;
    hrMeasurementStartTime = millis();
    validBeatCount = 0;
    displayHR = 0; // Reset display to 0 or --
    hrStatusText = "Measuring...";
    Serial.println("[HR] Measurement Started...");
}

// ----------------------------------------------------
// HEART RATE SAMPLING
// ----------------------------------------------------
// ----------------------------------------------------
// HEART RATE SAMPLING WITH MOVING AVERAGE
// ----------------------------------------------------
// ----------------------------------------------------
// HEART RATE SAMPLING
// ----------------------------------------------------

void readHeartRate()
{
    particleSensor.check();

    if (!particleSensor.available())
        return;

    long irValue = particleSensor.getIR();

    particleSensor.nextSample();

    // =====================================================
    // STATIC VARIABLES
    // =====================================================

    static float filteredIR = 0;
    static float dcFilter = 0;

    static float prevFiltered = 0;

    static bool rising = false;

    static unsigned long lastBeatTime = 0;

    static int bpmBuffer[5] = {0};
    static int bpmIndex = 0;

    // =====================================================
    // FILTERING
    // =====================================================

    // Smooth IR signal

    filteredIR =
        (0.92 * filteredIR) +
        (0.08 * irValue);

    // Remove DC component

    dcFilter =
        (0.95 * dcFilter) +
        (0.05 * filteredIR);

    float signal =
        filteredIR - dcFilter;

    // =====================================================
    // FINGER DETECTION
    // =====================================================

    if (irValue < 50000)
    {
        currentHR = 0;
        displayHR = 0;

        return;
    }

    // =====================================================
    // MOTION REJECTION
    // =====================================================

    QMI8658_Loop();
    getGyroscope();

    float totalGyro =
        sqrt(
            Gyro.x * Gyro.x +
            Gyro.y * Gyro.y +
            Gyro.z * Gyro.z
        );

    // Ignore HR during heavy motion

    if (totalGyro > 35)
    {
        return;
    }

    // =====================================================
    // DYNAMIC THRESHOLD
    // =====================================================

    static float avgSignal = 0;

    avgSignal =
        (avgSignal * 0.95) +
        (abs(signal) * 0.05);

    // Weak signal rejection

    if (avgSignal < 250)
    {
        return;
    }

    float threshold =
        avgSignal * 1.4;

    // =====================================================
    // PEAK DETECTION
    // =====================================================

    float diff =
        signal - prevFiltered;

    // Rising edge

    if (diff > 4)
    {
        rising = true;
    }

    // Peak detected

    if (rising &&
        diff < -4 &&
        signal > threshold)
    {
        unsigned long now =
            millis();

        unsigned long delta =
            now - lastBeatTime;

        // Human BPM range

        if (delta > 350 &&
            delta < 1400)
        {
            int bpm =
                60000 / delta;

            // Valid HR range

            if (bpm > 45 &&
                bpm < 160)
            {
                static int lastValidBPM = 75;

                // Reject unrealistic jumps

                if (abs(bpm - lastValidBPM) < 35)
                {
                    bpmBuffer[bpmIndex] = bpm;

                    bpmIndex++;

                    if (bpmIndex >= 5)
                        bpmIndex = 0;

                    // =====================================================
                    // AVERAGE BPM
                    // =====================================================

                    int sum = 0;
                    int count = 0;

                    for (int i = 0; i < 5; i++)
                    {
                        if (bpmBuffer[i] > 0)
                        {
                            sum += bpmBuffer[i];
                            count++;
                        }
                    }

                    if (count > 0)
                    {
                        int avgBPM =
                            sum / count;

                        currentHR =
                            avgBPM;

                        displayHR =
                            avgBPM;

                        lastValidBPM =
                            avgBPM;

                        Serial.printf(
                            "[HR] BPM: %d\n",
                            avgBPM
                        );
                    }
                }
            }
        }

        lastBeatTime = now;

        rising = false;
    }

    // =====================================================
    // NO BEAT TIMEOUT
    // =====================================================

    if (millis() - lastBeatTime > 3000)
    {
        currentHR = 0;
        displayHR = 0;
    }

    prevFiltered = signal;
}

#include "driver/i2s.h"
#include <math.h>

#define I2S_BCLK 48
#define I2S_LRC  38
#define I2S_DOUT 47

void initSpeaker() {
    i2s_config_t config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = 0,
        .dma_buf_count = 8,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pins = {
        .bck_io_num = 48,
        .ws_io_num = 38,
        .data_out_num = 47,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
    i2s_set_pin(I2S_NUM_0, &pins);
}

void playAlarmTone()
{
    const int count = 256;
    int16_t buf[count];
    size_t written = 0;
    const float sample_rate = 16000.0f;
    static float phase = 0.0f;

    do
    {
        for (int pulse = 0; pulse < 10 && !alarmStopRequested; pulse++)
        {
            float freq = (pulse % 2 == 0) ? 3300.0f : 2000.0f;
            float step = 2.0f * M_PI * freq / sample_rate;

            for (int i = 0; i < count; i++)
            {
                phase += step;
                if (phase > 2.0f * M_PI) phase -= 2.0f * M_PI;

                buf[i] = (sin(phase) >= 0.0f) ? 24000 : -24000;
                
                // --- TEST VOLUME DIMMER ---
                // Divide the signal by 12 to make it a quiet lab volume
                buf[i] = buf[i] / 12; 
            }

            for (int duration = 0; duration < 25; duration++)
            {
                if (alarmStopRequested)
                {
                    break;
                }

                i2s_write(I2S_NUM_0, buf, sizeof(buf), &written, portMAX_DELAY);
            }

            if (!alarmStopRequested)
            {
                vTaskDelay(pdMS_TO_TICKS(300));
            }
        }

        if (alarmContinuousMode && !alarmStopRequested)
        {
            vTaskDelay(pdMS_TO_TICKS(150));
        }
    }
    while (alarmContinuousMode && !alarmStopRequested);

    i2s_zero_dma_buffer(I2S_NUM_0);
}

bool alarmRunning = false;

void alarmTask(void *param)
{
    if (alarmRunning)
    {
        vTaskDelete(NULL);
        return;
    }

    alarmRunning = true;
    alarmPlaying = true;

    playAlarmTone();

    alarmPlaying = false;
    alarmContinuousMode = false;
    alarmRunning = false;

    vTaskDelete(NULL);
}

// ----------------------------------------------------
// SETUP
// ----------------------------------------------------
void setup()
{
    Serial.begin(115200);
    
    PWR_Init();

    hal_setup();  

    QMI8658_Init();

    initSpeaker();

    // --- ADD THIS LINE TO FIX THE ERROR ---
    pinMode(0, INPUT_PULLUP); 
    // --------------------------------------

    Serial.println("--- FORGE SmartWatch Ready ---");
    Serial.print("My Device MAC: ");
    Serial.println(watch.getAddress());
    if (!particleSensor.begin(Wire, I2C_SPEED_FAST))
{
    Serial.println("MAX30102 not found!");
}
else
{
    // Optimized settings for wrist HR monitoring
    particleSensor.setup(
        60,   // LED brightness
        4,    // sample average
        2,    // LED mode (Red + IR)
        200,  // sample rate
        411,  // pulse width
        4096  // ADC range
    );

    // Fine tune LED power
    particleSensor.setPulseAmplitudeRed(0x2A);
    particleSensor.setPulseAmplitudeIR(0x3F);

    Serial.println("MAX30102 Optimized.");
}


}

// ----------------------------------------------------
// LOOP
// ----------------------------------------------------
void loop()
{
    PWR_Loop();
    delay(5);
    watch.loop();

    if (watch.isConnected())
    {
        static bool synced = false;

        if (!synced)
        {
            Serial.println("[TIME] Waiting for phone sync...");
            synced = true;
        }
    }
    
    hal_loop(); 
    
    // --- 1. THE AUTO TRIGGER ---
    static unsigned long autoTriggerTimer = 0;
    // Automatically start a new measurement every 20 seconds
    if (millis() - autoTriggerTimer > 20000) {
        startHRMeasurement();
        autoTriggerTimer = millis();
    }

    // --- 2. CALCULATE HEART RATE ---


    readHeartRate();

/* --- 3. UPDATE THE UI ---
    // If it is 0 (failed or standby), show "--", otherwise show the real number
    if (displayHR == 0) {
        lv_label_set_text(ui_HRValue, "--");
    } else {
        lv_label_set_text_fmt(ui_HRValue, "%d", displayHR); 
    }
    
    lv_label_set_text(ui_HRStatus, hrStatusText.c_str());
*/ 

    tm t = watch.getTimeStruct();
    static int lastMin = -1;
    if (t.tm_min != lastMin)
    {
    lastMin = t.tm_min;

    char hourStr[5];
    char minStr[5];

    // ✅ 12-hour conversion
    int hour12 = t.tm_hour % 12;
    if (hour12 == 0) hour12 = 12;

    snprintf(hourStr, sizeof(hourStr), "%02d", hour12);
    snprintf(minStr, sizeof(minStr), "%02d", t.tm_min);

    lv_label_set_text(ui_hourLabel, hourStr);
    lv_label_set_text(ui_minuteLabel, minStr);

    // ✅ AM/PM
    if (t.tm_hour >= 12)
        lv_label_set_text(ui_amPmLabel, "PM");
    else
        lv_label_set_text(ui_amPmLabel, "AM");

    // -------- DATE --------
    const char* days[] = {"SUN","MON","TUE","WED","THU","FRI","SAT"};
    const char* months[] = {"JAN","FEB","MAR","APR","MAY","JUN",
                           "JUL","AUG","SEP","OCT","NOV","DEC"};

    char fullDate[30];

    snprintf(fullDate, sizeof(fullDate), "%s %02d\n%s",
             days[t.tm_wday], t.tm_mday, months[t.tm_mon]);
lv_label_set_text(ui_dateLabel, fullDate);

    // -------- WEATHER --------
    // -------- WEATHER --------
    // -------- WEATHER --------
    char tempStr[10];

    // Fetch the synced temperature and icon for today (index 0) from Chronos
    int currentTemp = watch.getWeatherAt(0).temp; 
    int currentIcon = watch.getWeatherAt(0).icon;
    
    // Format the text with the degree symbol
    snprintf(tempStr, sizeof(tempStr), "%d°C", currentTemp);

    // 1. Update Main Home Screen
    lv_label_set_text(ui_weatherTemp, tempStr);
    setWeatherIcon(ui_weatherIcon, currentIcon, true);

    // 2. Update Dedicated Weather App Screen
    lv_label_set_text(ui_weatherCurrentTemp, tempStr);
    setWeatherIcon(ui_weatherCurrentIcon, currentIcon, true);
    
    // 3. Update City and Sync Time on Weather App Screen
    // Note: Changed getCity() to getWeatherCity() to fix the library error
    lv_label_set_text(ui_weatherCity, watch.getWeatherCity().c_str());
    lv_label_set_text(ui_weatherUpdateTime, watch.getWeatherTime().c_str());

    
}
    // ✅ PRINT TIME (IMPORTANT)

    

    // Heap debug
    static unsigned long lastHeapPrint = 0;
    if (millis() - lastHeapPrint > 5000) {
        // Serial.printf("Free heap: %u | HR: %d\n", ESP.getFreeHeap(), currentHR);
        lastHeapPrint = millis();
    }

/* BLE HR send (optimized)
    if (watch.isConnected()) {
        static unsigned long lastHRUpdate = 0;
        if (millis() - lastHRUpdate > 2000) {
            char hrData[16];
            snprintf(hrData, sizeof(hrData), "HR:%d", currentHR);

            watch.sendCommand((uint8_t*)hrData, strlen(hrData));
            lastHRUpdate = millis();
        }
    }
*/
    
    //BOOT button:
//single press  -> start SOS
//double press  -> stop SOS

handleBootButtonSOS();

if (millis() - lastSOS > SOS_COOLDOWN)
{
    sosActive = false;

    // Reset popup system
    sosPopupShown = false;
    sosPopupRequest = false;
}

detectFall();
handleSOSPopup();
handleBLEQueue();
}

#endif