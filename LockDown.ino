/* LockDown Virtual Analogue Synth
 *  
 * Spring 2020 C19 Lockdown fun
 * John Potter (TechnoWomble)
 * Last mod 26 Apr 2020
 * 
 */
#include <ADC.h>
#include <MozziGuts.h>
#include <Oscil.h> 
#include <LowPassFilter.h>
#include <RollingAverage.h>

#include <Audio.h>
#include <Wire.h>
#include <SPI.h>


// AudioOutputUSB      audioOutput;              try for USB audio later

#include <tables/cos2048_int8.h> 
#include <tables/triangle2048_int8.h> 
#include <tables/saw2048_int8.h> 
#include <tables/square_no_alias_2048_int8.h> 

#define CONTROL_RATE 64 

int ledPin = 13;
int currentNote;
int ampLevel = 0;                         //amplitude 0-255
int keyDownCount = 0;                     // number of keys currently pressed
int masterVol = 127;
int mixerVal = 63;
int osc1CoarseTune;
float osc2FineTune = 1.0;
float pbFactor = 1.0;
int osc1WavIndex = 0;
int osc2WavIndex = 0;
int octShift = -1;
int filterCutOff = 200;
byte lfoDepth = 0;


int osc1[2];
int osc2[2];

// continuous controllers

const byte ccVol = 0x07;
const byte ccMod = 0x01;
const byte ccOsc1Wav = 0x41;
const byte ccOsc2Wav = 0x7e;
const byte ccOsc1Tune = 0x4a;
const byte ccOsc2Tune = 0x47;
const byte ccMixer = 0x4c;
const byte ccLFO = 0x4d;
const byte ccCutOff = 0x12;
const byte ccResonance = 0x13;



float Note2Freq[] = {
  8.18, 8.66, 9.18, 9.72, 10.30, 10.91, 11.56, 12.25, 12.98, 13.75, 14.57, 15.43,
  16.35, 17.32, 18.35, 19,45, 20.60, 21.83, 23.12, 24.50, 25.96, 27.50, 29.14, 30.87,
  32.70, 34.65, 36.71, 38.89, 41.20, 43.65, 46.25, 49.00, 51.91, 55.00, 58.27, 61.74,
  65.41, 69.30, 73.42, 77.78, 82.41, 87.31, 92.50, 98.00, 103.83, 110.00, 116.54, 123.47,
  130.81, 138.59, 146.83, 155.56, 164.81, 174.61, 185.00, 196.00, 207.65, 220.00, 233.08, 246.94,
  261.63, 277.18, 293.66, 311.13, 329.63, 349.23, 369.99, 392.00, 415.30, 440.00, 466.16, 493.88,
  523.25, 554.37, 587.33, 622.25, 659.26, 698.46, 739.99, 783.99, 830.61, 880.00, 932.33, 987.77,
  1046.50, 1108.73, 1174.66, 1244.51, 1318.51, 1396.91, 1479.98, 1567.98, 1661.22, 1760.00, 1864.66, 1975.33,
  2093.00, 2217.46, 2349.32, 2489.02, 2637.02, 2793.83, 2959.96, 3135.96, 3322.44, 3520.00, 3729.31, 3951.07,
  4186.01, 4434.92, 4698.64, 4978.03, 5274.04, 5587.65, 5919.91, 6271.93, 6644.88, 7040.00, 7458.62, 7902.13,
  8372.02, 8869.84, 9397.27, 9956.06, 10548.08, 11175.30, 11839.82, 12543.85, 13289.75  
};

RollingAverage <int, 4> pAverage;   

Oscil <COS2048_NUM_CELLS, AUDIO_RATE> lfo1(COS2048_DATA);

Oscil <SAW2048_NUM_CELLS, AUDIO_RATE> osc1Saw(SAW2048_DATA);
Oscil <SAW2048_NUM_CELLS, AUDIO_RATE> osc2Saw(SAW2048_DATA);

Oscil <SQUARE_NO_ALIAS_2048_NUM_CELLS, AUDIO_RATE> osc1Squ(SQUARE_NO_ALIAS_2048_DATA);
Oscil <SQUARE_NO_ALIAS_2048_NUM_CELLS, AUDIO_RATE> osc2Squ(SQUARE_NO_ALIAS_2048_DATA);


LowPassFilter lpf;

void setup() {
  
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  usbMIDI.setHandleNoteOn(onNoteOn);
  usbMIDI.setHandleNoteOff(onNoteOff);
  usbMIDI.setHandleControlChange(onControlChange);
  usbMIDI.setHandlePitchChange(onPitchChange);
  startMozzi(CONTROL_RATE);
  digitalWrite(ledPin, HIGH);    
  delay(400);                                              // Blink LED once at startup to show life
  digitalWrite(ledPin, LOW);

  lfo1.setFreq(0.5);
}

void onNoteOn(byte channel, byte note, byte velocity) {
    digitalWrite(ledPin, HIGH);                             // turn the LED on
    currentNote = note; // + (octShift *12);
    setOscPitch();
    ampLevel = 255;
    keyDownCount++;
}

void onNoteOff(byte channel, byte note, byte velocity) {
     digitalWrite(ledPin, LOW);                             // turn the LED off
     keyDownCount--;
     if (keyDownCount <= 0) {                               // key rollover/legato
     ampLevel = 0;
     }
}

void onControlChange(byte channel, byte control, byte value) {
  switch (control){
     case ccCutOff: if (value < 9) value = 9; lpf.setCutoffFreq(value * 2); break;
     case ccResonance: lpf.setResonance(value * 2); break;
     case ccVol: masterVol = value; break;
     case ccMixer: mixerVal = value; break;
     case ccLFO : setLFOPitch(value); break;
     case ccMod : lfoDepth = value * 2; break;
     case ccOsc1Wav : if (value == 0x7f) {++ osc1WavIndex; if (osc1WavIndex > 1) osc1WavIndex = 0;} break;
     case ccOsc2Wav : if (value == 0x7f) {++ osc2WavIndex; if (osc2WavIndex > 1) osc2WavIndex = 0;}; osc2FineTune = 1.0; break;
     case ccOsc1Tune: osc1CoarseTune = map(value,0,127,-12,12); setOscPitch() ;  break;
     case ccOsc2Tune: osc2FineTune = (map(value,0,127,98.0,102.0)/100.0); setOscPitch();  break;
  }                     
}

void onPitchChange(byte channel, int pitch) {
  if (pitch == 0) pbFactor = 1.000000;
  if (pitch > 0) pbFactor = 1.0 + abs(pitch / 16384.0);
  if (pitch < 0) pbFactor = 1.0 - abs(pitch / 16384.0);
  setOscPitch(); 
}


void setOscPitch() {
   
   float osc1Freq = Note2Freq[(currentNote + (12 * octShift)) + osc1CoarseTune];
   float osc2Freq = Note2Freq[currentNote + (12 * octShift)] * osc2FineTune;

   osc1Freq = osc1Freq * pbFactor;
   osc2Freq = osc2Freq * pbFactor;

   osc1Saw.setFreq(osc1Freq); 
   osc1Squ.setFreq(osc1Freq); 
  
   osc2Saw.setFreq(osc2Freq); 
   osc2Squ.setFreq(osc2Freq);                  
}

void setLFOPitch(int lfoFreq) {
    float lfoRate = map(lfoFreq,0,127,1.0,10.0);
    lfo1.setFreq(lfoRate);
}

void updateControl(){
  // put changing controls in here
   usbMIDI.read();
}

int updateAudio(){

  Q15n16 vibrato = (Q15n16) lfoDepth * lfo1.next();

 // osc1[0]= osc1Saw.next();
 // osc1[1]= osc1Squ.next();
//  osc2[0]= osc2Saw.next();
//  osc2[1]= osc2Squ.next();

   osc1[0]= osc1Saw.phMod(vibrato);
   osc1[1]= osc1Squ.phMod(vibrato);
   osc2[0]= osc2Saw.phMod(vibrato);
   osc2[1]= osc2Squ.phMod(vibrato);
  
  int osc1Sample = osc1[osc1WavIndex];                         
  int osc2Sample = osc2[osc2WavIndex];
  
  osc1Sample = (osc1Sample * (127 - mixerVal)) >>7;
  osc2Sample = (osc2Sample * mixerVal) >>7;

  int signal = (osc1Sample + osc2Sample) >> 1;

  signal = (signal * ampLevel) >> 8;
  signal = (signal * masterVol) >> 3;
  signal = lpf.next(signal);
  return signal; 
}


void loop() {
 usbMIDI.read();
 audioHook();                             // required here
}
