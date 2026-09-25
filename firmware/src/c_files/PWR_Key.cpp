#include "PWR_Key.h"

static uint8_t BAT_State = 0; 
static uint8_t Device_State = 0; 
static uint16_t Long_Press = 0;


void PWR_Loop(void)
{
    static bool lastButtonState = HIGH;

    static unsigned long lastReleaseTime = 0;
    static int pressCount = 0;

    bool buttonState = digitalRead(PWR_KEY_Input_PIN);

    // ====================================================
    // SINGLE PRESS RELEASE -> POWER ON
    // ====================================================
    if (buttonState == HIGH && lastButtonState == LOW)
    {
        // ------------------------------------------------
        // SINGLE PRESS
        // ------------------------------------------------
        pressCount++;

        if (pressCount == 1)
        {
            lastReleaseTime = millis();

            // POWER ON IMMEDIATELY
            Serial.println("[PWR] Single Press -> Power ON");

            digitalWrite(PWR_Control_PIN, HIGH);

            Set_Backlight(255);
        }

        // ------------------------------------------------
        // DOUBLE PRESS -> SHUTDOWN
        // ------------------------------------------------
        else if (pressCount == 2)
        {
            if (millis() - lastReleaseTime <= DOUBLE_PRESS_TIME)
            {
                Serial.println("[PWR] Double Press -> Shutdown");

                Shutdown();
            }

            pressCount = 0;
        }
    }

    // ====================================================
    // RESET DOUBLE PRESS WINDOW
    // ====================================================
    if (pressCount == 1 &&
        millis() - lastReleaseTime > DOUBLE_PRESS_TIME)
    {
        pressCount = 0;
    }

    lastButtonState = buttonState;
}

void Fall_Asleep(void)
{

}
void Restart(void)
{

}
void Shutdown(void)
{
  digitalWrite(PWR_Control_PIN, LOW);
  Set_Backlight(0);
  delay(100);
  digitalWrite(PWR_Control_PIN, LOW);        
}
void PWR_Init(void) {
  pinMode(PWR_KEY_Input_PIN, INPUT);    
  pinMode(PWR_Control_PIN, OUTPUT);
  digitalWrite(PWR_Control_PIN, LOW);
  vTaskDelay(100);
  if(!digitalRead(PWR_KEY_Input_PIN)) {   
    BAT_State = 1;               
    digitalWrite(PWR_Control_PIN, HIGH);
  }
}
