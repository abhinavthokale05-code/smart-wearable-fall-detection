#pragma once
#include "Arduino.h"
#include "Display_SPD2010.h"

#define PWR_KEY_Input_PIN   6
#define PWR_Control_PIN     7

#define DOUBLE_PRESS_TIME    500

void Fall_Asleep(void);
void Shutdown(void);
void Restart(void);

void PWR_Init(void);
void PWR_Loop(void);