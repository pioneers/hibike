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
    velocity : {0 to maximum velocity (encoder difference)}
    limit_state: {STANDARD, CAUGHT_MAX, LIMIT, SPIKE}
*/

uint32_t duty;
uint32_t fault;
uint32_t forward;
uint32_t inputA;
uint32_t inputB;

volatile unsigned int velCounter = 0;
volatile int velocity = 0;

#define LIMITED 4 //how much we limit PWM by

//CONDITIONS TO SWITCH STATES
int current_threshold = 40; //threshold for duty cycle values, equates to about 3 amps when motor is stalled.

int in_max = 0; //how many times we have been in the CAUGHT_MAX state
#define EXIT_MAX 17000 //**FIXME** how many times we want to be in CAUGHT_MAX before moving onto LIMIT state
int above_threshold = 0;

int in_limit = 0; //how many times we have been in the LIMIT state
#define EXIT_LIMIT 20000 //**FIXME** how many times we want to be in LIMIT before moving onto SPIKE state

int in_spike = 0; //how many times we have been in the SPIKE state
#define EXIT_SPIKE 5000 //**FIXME** how many times we want to be in SPIKE before moving onto either the STANDARD or LIMIT state
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
  Timer1.attachInterrupt(interrupt, 1000);
}

void loop() {
  hibike_loop();
}

uint32_t device_update(uint8_t param, uint32_t value) {
  switch (param) {

    case DUTY:
      if ((value <= 100) && (value >= 0)) {
        duty = value;
        int PWM_val = value * (255/100);
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
          // clearFault();
          return fault;
      } else {
          return fault;
      }

    default:
      return ~((uint32_t) 0);
  }
}

uint32_t device_status(uint8_t param) {
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
      break;

    case VELOCITY:
      return velocity;
      break;
  }
  return ~((uint32_t) 0);
}

void doEncoder_Expanded(){
  if (digitalRead(encoder0PinA) == HIGH) {   // found a low-to-high on channel A
    if (digitalRead(encoder0PinB) == LOW) {  // check channel B to see which way
                                             // encoder is turning
      encoder0Pos = encoder0Pos - 1;         // CCW
    }
    else {
      encoder0Pos = encoder0Pos + 1;         // CW
    }
  }
  else                                        // found a high-to-low on channel A
  {
    if (digitalRead(encoder0PinB) == LOW) {   // check channel B to see which way
                                              // encoder is turning
      encoder0Pos = encoder0Pos + 1;          // CW
    }
    else {
      encoder0Pos = encoder0Pos - 1;          // CCW
    }
  }
}

void velocity(){
  if (velCounter  == 1000){
    velocity = encoder0Pos - old_encoder0Pos;
    old_encoder0Pos = encoder0Pos; //calculate for the next velocity calc
    velCounter = 0;
  }
  else {
    velCounter += 1;
  }
}

void current_limiting() {
  int targetPWM = duty;
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
      } else {
        if (above_threshold > 0) {
          above_threshold--;
        }
      }
      break;
    case CAUGHT_MAX: //we have seen the max and we check to see if we are above max for EXIT_MAX consecutive cycles and then we go to LIMIT to protect the motor
      if (in_max > EXIT_MAX) {
        if(above_threshold >= EXIT_SPIKE / 2) {
          above_threshold = 0;
          limit_state = LIMIT;
        } else {
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
          limit_state = STANDARD;
        } else {
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

void interrupt(){
  velocity();
  current_limiting();
}
