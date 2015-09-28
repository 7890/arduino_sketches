//https://github.com/FortySevenEffects/arduino_midi_library/
//http://arduinomidilib.fortyseveneffects.com/index.html
//http://www.pjrc.com/teensy/td_libs_Encoder.html
//https://www.sparkfun.com/datasheets/LCD/SerLCD_V2_5.PDF
//https://www.sparkfun.com/tutorials/246
//tb/120911 RE
//tb/150915 RE, MIDI

/*
-device sending and receiving MIDI bytes
-i.e. use with jack_tty (https://github.com/7890/jack_tools)

2 rotary encoders with push button
left:
  channel 1, controller 1.
  decrement: value 1. increment value 2.
  release: value 10. push: value 11
right:
  controller 2.

2 buttons, parallel 2 jack inputs for foot pedals
left: channel 1, controller 40
  release value: 10. push value: 11
right: controller 41

2 LEDs
same as buttons. i.e. connect device output to device input -> 'local feedback'

1 LCD
for now: displays incoming CC, Note on/off events

wiring:

rotary encoder

  D5  GND  D7
    \  |  /
    .|_|_|.
    |     |
    |  O  |
    |     |
    .-----.
      | |
    GND  \____R 10k___5V
          |
          D2

rotary 1: D5, D7 (rotation) D2 (button)
rotary 2: D6, D8 (rotation) D3 (button)

switch 1: A0 (14)
switch 2: A1 (15)

led 1: D11
led 2: D12

lcd: D4 (softserial)
*/

#include <MIDI.h>
#include <Encoder.h>
#include <SoftwareSerial.h>

#define MIDI_SERIAL_SPEED 115200

#define LED_COUNT 2
#define BUTTON_COUNT 4
#define ROTARY_ENCODER_COUNT 2

#define ENC_PIN1_LEFT 5
#define ENC_PIN2_LEFT 7

#define ENC_PIN1_RIGHT 6
#define ENC_PIN2_RIGHT 8

Encoder knobLeft(ENC_PIN1_LEFT, ENC_PIN2_LEFT);
Encoder knobRight(ENC_PIN1_RIGHT, ENC_PIN2_RIGHT);

#define LCD_PIN 4

//pin 4 = TX, pin 13 = RX (unused)
SoftwareSerial lcdSerial(13, LCD_PIN);

typedef struct
{
  Encoder *enc;
  //set values in setup()
  int ctrl_channel = 1;
  int ctrl_number = -1;
  int ctrl_decrement_value = 1; //reserve 0 to query (state-less)
  int ctrl_increment_value = 2;
} RotaryEncType;
RotaryEncType knobs[ROTARY_ENCODER_COUNT];

typedef struct
{
  //set values in setup()
  int ctrl_channel = 1;
  int ctrl_number = -1;
  int ctrl_off_value = 10; //off: <=10    on: >10
  int ctrl_led_pin = -1;
} LedType;
LedType leds[LED_COUNT];

long ctrl_debounce_delay = 50; //ms

typedef struct
{
  //set values in setup()
  int ctrl_channel = 1;
  int ctrl_number = -1;
  int ctrl_press_value = 11;
  int ctrl_release_value = 10;
  int ctrl_push_pin = 2;
  int ctrl_push_state = HIGH;
  int ctrl_push_state_prev = LOW;
  long ctrl_last_debounce = 0;
} ButtonType;
ButtonType buttons[BUTTON_COUNT];

struct MIDISettings : public midi::DefaultSettings
{
  static const long BaudRate = MIDI_SERIAL_SPEED;
  static const bool UseRunningStatus = false;
};
MIDI_CREATE_CUSTOM_INSTANCE(HardwareSerial, Serial, MIDI_, MIDISettings);

//==============================================================
void setup()
{
  setupLeds();
  setupButtons();
  setupKnobs();

  MIDI_.setHandleControlChange(handle_control_change);
  MIDI_.setHandleNoteOn(handle_note_on);
  MIDI_.setHandleNoteOff(handle_note_off);

  //listen on all channels  
  MIDI_.begin(MIDI_CHANNEL_OMNI);
  MIDI_.turnThruOff();

  setupLCD();
}

//==============================================================
void setupLCD()
{
  lcdSerial.begin(9600);
  //wait for display to boot up
  delay(500);
  //configure speed to 38400 (max)
  lcdSerial.write(124);
  lcdSerial.write(16);
  lcdSerial.end();
  //start over
  lcdSerial.begin(38400);
  delay(500);

  //putp cursor to beginning of first line
  lcdSerial.write(254);
  lcdSerial.write(128);

  /*
  To move the cursor, send the special character 254 decimal (0xFE hex), followed by the cursor position you'd like to set. 
  position 	1 	2 	3 	4 	5 	6 	7 	8 	9 	10 	11 	12 	13 	14 	15 	16
  line 1 	128 	129 	130 	131 	132 	133 	134 	135 	136 	137 	138 	139 	140 	141 	142 	143
  line 2 	192 	193 	194 	195 	196 	197 	198 	199 	200 	201 	202 	203 	204 	205 	206 	207
  */

  //display when ready
  lcdSerial.write("     ready.     ");
  lcdSerial.write("                ");

  /*
  //store splashscreen (currently displayed lines)
  lcdSerial.write(124);
  lcdSerial.write(10);
  */
}

//==============================================================
void setupLeds()
{
  leds[0].ctrl_channel = 1;
  leds[0].ctrl_number = 40;
  leds[0].ctrl_led_pin = 11;

  leds[1].ctrl_channel = 1;
  leds[1].ctrl_number = 41;
  leds[1].ctrl_led_pin = 12;

  for (int i = 0; i < LED_COUNT; i++)
  {
    pinMode(leds[i].ctrl_led_pin, OUTPUT);
    digitalWrite(leds[i].ctrl_led_pin, LOW);
  }
}

//==============================================================
void setupButtons()
{
  //left knob push
  //buttons[0].ctrl_channel = 1; //default
  buttons[0].ctrl_number = 1; //same as left rotary
  //buttons[0]ctrl_press_value = 11; //default
  //buttons[0]ctrl_release_value = 10; //default
  buttons[0].ctrl_push_pin = 2;

  //right knob push
  buttons[1].ctrl_number = 2; //same as right rotary
  buttons[1].ctrl_push_pin = 3;

  //single button
  buttons[2].ctrl_number = 40; //for all other buttons
  buttons[2].ctrl_push_pin = 14; //A0

  //single button
  buttons[3].ctrl_number = 41; //for all other buttons
  buttons[3].ctrl_push_pin = 15; //A1

  for (int i = 0; i < BUTTON_COUNT; i++)
  {
    pinMode(buttons[i].ctrl_push_pin, INPUT);
  }
}

//==============================================================
void setupKnobs()
{
  knobs[0].enc = &knobLeft;
  knobs[0].ctrl_channel = 1;
  knobs[0].ctrl_number = 1;

  knobs[1].enc = &knobRight;
  knobs[1].ctrl_channel = 1;
  knobs[1].ctrl_number = 2;

  for (int i = 0; i < ROTARY_ENCODER_COUNT; i++)
  {
    //pinMode(knobs[i].ctrl_push_pin, INPUT);
    knobs[i].enc->write(0);
  }
}

int input_toggler = 1;
//==============================================================
void lcd_indicate_event()
{
  lcdSerial.write(254);
  lcdSerial.write(199); //pos 8 of second line
  if (input_toggler == 1)
  {
    lcdSerial.write(". ");
  }
  else
  {
    lcdSerial.write(" .");
  }
  input_toggler *= -1;
}

//==============================================================
void handle_control_change(byte channel, byte number, byte value)
{
  char lcd_line_1[16]; 
  sprintf(lcd_line_1, "I:CC  %2d %3d %3d", channel, number, value);
  lcdSerial.write(254);
  lcdSerial.write(128); //start of first line
  lcdSerial.write(lcd_line_1);

  //indicate event change
  lcd_indicate_event();

  for (int i = 0; i < LED_COUNT; i++)
  {
    if (leds[i].ctrl_channel == channel && leds[i].ctrl_number == number)
    {
      if (value > leds[i].ctrl_off_value)
      {
        digitalWrite(leds[i].ctrl_led_pin, HIGH);
      }
      else
      {
        digitalWrite(leds[i].ctrl_led_pin, LOW);
      }
    }
  }
}

//==============================================================
void handle_note(byte channel, byte note, byte velocity, int type) //type 0: off 1: on
{
  char lcd_line_1[16];
   
  if(type==0)
  {
    sprintf(lcd_line_1, "I:OFF %2d %3d %3d", channel, note, velocity);
  }
  else
  {
    sprintf(lcd_line_1, "I:ON  %2d %3d %3d", channel, note, velocity);
  }
  lcdSerial.write(254);
  lcdSerial.write(128); //start of first line
  lcdSerial.write(lcd_line_1);

  lcd_indicate_event();
}

//==============================================================
void handle_note_on(byte channel, byte note, byte velocity)
{
  handle_note(channel,note,velocity,1);
}

//==============================================================
void handle_note_off(byte channel, byte note, byte velocity)
{
  handle_note(channel,note,velocity,0);
}

//==============================================================
void handleKnobs()
{
  for (int i = 0; i < ROTARY_ENCODER_COUNT; i++)
  {
    int ctrl_value = 0;
    long encval = knobs[i].enc->read();
    if (encval == 0)
    {
      continue;
    }
    else if (encval % 2 != 0)
    {
      continue;
    }
    else if (encval < 0)
    {
      ctrl_value = knobs[i].ctrl_decrement_value;
    }
    else if (encval > 0)
    {
      ctrl_value = knobs[i].ctrl_increment_value;
    }
    MIDI_.sendControlChange (knobs[i].ctrl_number, ctrl_value, knobs[i].ctrl_channel);

    //reset value
    knobs[i].enc->write(0);
  }//end for all knobs
}//end handleKnobs()

//==============================================================
void handleButtons()
{
  for (int i = 0; i < BUTTON_COUNT; i++)
  {
    int reading = digitalRead(buttons[i].ctrl_push_pin);

    //if the switch changed, due to noise or pressing:
    if (reading != buttons[i].ctrl_push_state_prev)
    {
      //reset the debouncing timer
      buttons[i].ctrl_last_debounce = millis();
    }

    //if current state longer than debounce delay
    if ((millis() - buttons[i].ctrl_last_debounce) > ctrl_debounce_delay)
    {
      if (reading != buttons[i].ctrl_push_state)
      {
        buttons[i].ctrl_push_state = reading;

        if (buttons[i].ctrl_push_state == LOW)
        {
          MIDI_.sendControlChange (buttons[i].ctrl_number, buttons[i].ctrl_press_value, buttons[i].ctrl_channel);
        }
        else
        {
          MIDI_.sendControlChange (buttons[i].ctrl_number, buttons[i].ctrl_release_value, buttons[i].ctrl_channel);
        }
      }//end state changed
    }//end debounce delay reached
    //store current reading for next cycle
    buttons[i].ctrl_push_state_prev = reading;
  }//end for all buttons
}//end handleButtons()

//==============================================================
void loop()
{
  handleKnobs();
  handleButtons();
  //dispatch incoming MIDI messages to handlers
  MIDI_.read();
}
//EOF

