#include <assert.h>
#include <TM1637Display.h>

#define __ASSERT_USE_STDERR
#define TRIG1 13
#define TRIG2 4
#define ECHO1 12
#define ECHO2 2
#define CLK 7
#define DIO 8
#define BUZZER 5
#define NOTE1 2489
#define NOTE2 311

bool entering, exiting, enter_sensed, exit_sensed, first_iter = true; // initialize indicators to be ready for first person to enter room
int range_val = 35, delay_val = 750, total = 0; // initialize defaults
long cm1, cm2;

TM1637Display display(CLK, DIO);
const uint8_t done_count[] = {0x40, 0x40, 0x40, 0x40};

void setup() {
  // initialize serial communication:
  Serial.begin(9600);
  pinMode(TRIG1, OUTPUT); // for sensor 1
  pinMode(ECHO1, INPUT);
  pinMode(TRIG2, OUTPUT); // for sensor 2
  pinMode(ECHO2, INPUT);
  pinMode(BUZZER, OUTPUT);
  entering = exiting = false;
}

long ultrasonic_sensor (int trig, int echo) {
  // send high pulse for 10 microseconds
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  // get the duration and convert microseconds to cm, and return that distance
  long duration = pulseIn(echo, HIGH);
  long cm = microsecondsToCentimeters(duration);
  return cm;
}

void loop() { 
  display.setBrightness(0x0f);
  if (first_iter) { // initial delay to account for any sensor errors at the start
    first_iter = false;
    for (int i = 3; i > 0; i--) {
      display.showNumberDec(i);
      tone(BUZZER, NOTE1);
      delay(200);
      noTone(BUZZER);
      delay(800);
    }
    display.setSegments(done_count);
    tone(BUZZER, NOTE2);
    delay(1000);
    noTone(BUZZER);
  }

  if  (Serial.available() > 0) { // if signal sent from server
    String comm = Serial.readString();
    if (comm.equals("reset")) // case 1: arduino needs to reset its counter
      total = 0;
    else { // case 2: config data received     
      int index1 = comm.indexOf('%'); // get index of first separator
      int index2 = comm.lastIndexOf('%'); // get index of last separator
      assert(index1 >= 0 && index2 >= 0); // make sure that the parsing didn't fail
      int r = comm.substring(0, index1).toInt(); // extract range value
      int d = comm.substring(index1 + 1, index2).toInt(); // extract delay value
      int t = comm.substring(index2 + 1).toInt(); // extract total value
      range_val = r;
      delay_val = d;
      total = t;
    }
    display.setSegments(done_count);
    tone(BUZZER, NOTE2);
    delay(1000);
    noTone(BUZZER);
  }  

  display.showNumberDec(total); // display the current total on 7 segment display

  // get the distances sensed from both sensors
  cm1 = ultrasonic_sensor(TRIG1, ECHO1);
  cm2 = ultrasonic_sensor(TRIG2, ECHO2);

  // see if enter or exit was sensed
  enter_sensed = cm1 <= range_val ? true : false;
  exit_sensed = cm2 <= range_val ? true : false;

  if (!entering && !exiting) { // there is no current pending enter/exit, so create new pending enter/exit based on which was sensed
    if (enter_sensed && !exit_sensed)
      entering = true; 
    else if (exit_sensed && !enter_sensed)
      exiting = true;
  } else { // there is a pending enter/exit, so handle the followup on it
    assert(!(entering && exiting)); // make sure that configuration didn't get messed up
    if (entering && exit_sensed) { // someone entered the room, increment total
      total++;
      entering = false;
      display.showNumberDec(total); // display the current total on 7 segment display
      Serial.println(total);
      tone(BUZZER, NOTE1);
      delay(delay_val); // delay a bit before reading data again
    } else if (exiting && enter_sensed) { // someone exited the room, decrement the total unless it is already 0
      total = total > 0 ? total - 1 : total; // decrement total if it is not already 0
      exiting = false;
      display.showNumberDec(total); // display the current total on 7 segment display
      Serial.println(total);
      tone(BUZZER, NOTE2);
      delay(delay_val); // delay a bit before reading data again
    }
    noTone(BUZZER);
  }

  Serial.println(total); // print the current total for serial communication

  delay(10); // configure delay on sensing for best results
}

long microsecondsToCentimeters(long microseconds) {
  // convert microseconds to cm using the proper conversion
  return microseconds / 29 / 2;
}
