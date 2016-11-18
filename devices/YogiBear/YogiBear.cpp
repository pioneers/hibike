#include "YogiBear.h"
#include <TimerOne.h>

// each device is responsible for keeping track of it's own params
uint32_t params[NUM_PARAMS];

/*
  YOGI BEAR PARAMS:
    duty = percent duty cycle
    fault : {1 = normal operation, 0 = fault}
    forward : {1 = forward, 0 = reverse}
    inputA : {1 = High, 0 = Low}
    inputB : {1 = High, 0 = Low}
    vel : {0 to maximum velocity (encoder difference)}
    CCW : {0 = one direction, 1 = the other}
    limit_state: {STANDARD, CAUGHT_MAX, LIMIT, SPIKE}
*/

uint32_t duty;
uint32_t fault;
uint32_t forward;
uint32_t inputA;
uint32_t inputB;
uint8_t PWM_val;
volatile unsigned int vel = 0;
volatile boolean CCW = true;

int current_threshold = 100;
int above_threshold = 0;
int in_limit = 0;
int in_spike = 0;
int below_threshold = 0;

//FSM STATES
int limit_state = 0; //tells us which state the FSM is in and how we are modifying the PWM
#define STANDARD 0 //normal state when pwm gets passed though
#define CAUGHT_MAX 1 //alerted state where we are concerned about high current
#define LIMIT 2 //high current for too long and PWM is now limited
#define SPIKE 3 //checks to see if returning to full PWM is safe

/* Pins according to TwistIt Board */
int INA = 16;
int INB = 10;
int PWM = 9;
int EN = 14;
int CS = 8;

#define encoder0PinA  2
#define encoder0PinB  3
volatile unsigned int encoder0Pos = 0;

void setup() {
  hibike_setup();
  pinMode(encoder0PinA, INPUT);
  digitalWrite(encoder0PinA, HIGH);       // turn on pullup resistor
  pinMode(encoder0PinB, INPUT);
  digitalWrite(encoder0PinB, HIGH);       // turn on pullup resistor

  pinMode(INA, OUTPUT);
  pinMode(INB, OUTPUT);
  pinMode(PWM, OUTPUT);
  pinMode(CS, INPUT);

  digitalWrite(INA, LOW);
  digitalWrite(INB, HIGH);
  attachInterrupt(digitalPinToInterrupt(encoder0PinA), doEncoder_Expanded, CHANGE);
}

void loop() {
  hibike_loop();
}

uint32_t device_update(uint8_t param, uint32_t value) {
  switch (param) {

    case DUTY:
      if ((value <= 100) && (value >= 0)) {
        duty = value;
        PWM_val = value * (255/100);
        analogWrite(PWM, PWM_val);
      }
      return duty;
      break;

    case FORWARD:
      forward = 1;
      inputA = 0;
      inputB = 1;
      digitalWrite(INA, LOW);
      digitalWrite(INB, HIGH);
      return forward;
      break;

    case REVERSE:
      forward = 0;
      inputA = 1;
      inputB = 0;
      digitalWrite(INA, HIGH);
      digitalWrite(INB, LOW);
      return forward;
      break;

    case FAULT:
      if (fault == 0){
          clearFault();
          return fault;
      } else {
          return fault;
      }

    default:
      return ~((uint32_t) 0);
  }
}

// you must implement this function. It is called when the device receives a DeviceStatus packet.
// the return value is the value field of the DeviceRespond packet hibike will respond with
uint32_t device_status(uint8_t param) {
  // if (param < NUM_PARAMS) {
  //   return params[param];
  // }
  switch (param) {
    case DUTY:
      return duty;
      break;

    case FAULT:
      return fault;
      break;

    case FORWARD:
      return forward;
      break;

    case REVERSE:
      return !forward;
      break;

    case CURRLIMSTATE:
      return limit_state;
  }
  return ~((uint32_t) 0);
}

void doEncoder_Expanded(){
  if (digitalRead(encoder0PinA) == HIGH) {   // found a low-to-high on channel A
    if (digitalRead(encoder0PinB) == LOW) {  // check channel B to see which way
                                             // encoder is turning
      encoder0Pos = encoder0Pos - 1;         // CCW
      CCW = true;
    }
    else {
      encoder0Pos = encoder0Pos + 1;         // CW
      CCW = false;
    }
  }
  else                                        // found a high-to-low on channel A
  {
    if (digitalRead(encoder0PinB) == LOW) {   // check channel B to see which way
                                              // encoder is turning
      encoder0Pos = encoder0Pos + 1;          // CW
      CCW = false;
    }
    else {
      encoder0Pos = encoder0Pos - 1;          // CCW
      CCW = true;
    }
  }
  Serial.println(encoder0Pos);
}

void velocity(){
  vel = encoder0Pos - old_encoder0Pos;
  old_encoder0Pos = encoder0Pos; //calculate for the next vel calc
}

void current_limiting() {
  int targetPWM = PWM_val;
  int current_read = analogRead(current_pin);
  if (targetPWM < 0) {
    pwm_sign = -1;
    targetPWM = targetPWM * -1;
  } else {
    pwm_sign = 1;
  }

  switch(limit_state) {
    case STANDARD: //we allow the pwm to be passed through normally and check to see if the CURRENT ever spikes above the threshold
      if (current_read > current_threshold) {
        limit_state = CAUGHT_MAX;
        Serial.println("from STANDARD to CAUGHT_MAX");
      } else {
        if (above_threshold > 0) {
          above_threshold--;
        }
      }
      break;
    case CAUGHT_MAX: //we have seen the max and we check to see if we are above max for EXIT_MAX consecutive cycles and then we go to LIMIT to protect the motor
      if (in_max > EXIT_MAX) {
        if(above_threshold >= EXIT_SPIKE / 2) {
          Serial.println("from CAUGHT_MAX to LIMIT");
          above_threshold = 0;
          limit_state = LIMIT;
        } else {
          Serial.println("from CAUGHT_MAX to STANDARD");
          limit_state = STANDARD;
        }
        in_max = 0;
      }

      if (current_read > current_threshold) {
        above_threshold++;
      }
      in_max++;
      break;
    case LIMIT: //we limit the pwm to 0.25 the value and wait EXIT_LIMIT cycles before attempting to spike and check the current again
      targetPWM = targetPWM / LIMITED;
      if (in_limit > EXIT_LIMIT) {
        Serial.println("from LIMIT to SPIKE");
        in_limit = 0;
        limit_state = SPIKE;
      } else {
        in_limit++;
      }
      break;
    case SPIKE: //we bring the pwm back to the target for a brief amount of time and check to see if it can continue in standard or if we need to continue limiting
      if (in_spike > EXIT_SPIKE) {
        Serial.println(below_threshold);
        if (below_threshold >= (EXIT_SPIKE/100)*99) {
          Serial.println("from SPIKE to STANDARD");
          limit_state = STANDARD;
        } else {
          Serial.println("from SPIKE to LIMIT");
          limit_state = LIMIT;
        }
        below_threshold = 0;
        in_spike = 0;
      } else {
        if (current_read < current_threshold) {
          below_threshold++;
        }
        in_spike++;
      }
      break;
  }
  analogWrite(PWM, targetPWM * pwm_sign);
}
