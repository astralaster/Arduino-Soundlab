#include <Arduino.h>
// Arduino polyphonic FM sound 
// * 31250 Hz sampling rate
// * 9-bit resolution
// * 4-fold polyphony (4 different tones can play simulatenously)
// * 2 full octaves on 24 keys
// * FM-synthesis with time-varying modulation amplitude
// * ADSR envelopes
// * 8 pots for varying instrument parameters
// * multiplexed input allows to expand to 40keys 
// Through PWM with timer1, sound is generated on pin 9
// by Rolf Oldeman May 2019 
// Licence CC BY-NC-SA 2.5 https://creativecommons.org/licenses/by-nc-sa/2.5/

#define nokey 255

#define npot 8
byte ipot=0;

//set up array with sine values in signed 8-bit numbers 
const float pi = 3.14159265;
char sine[256];
void setsine() {
  for (int i = 0; i < 256; ++i) {
    sine[i] = (sin(2 * 3.14159265 * (i + 0.5) / 256)) * 128;
  }
}

//set up array with exponential values mapping 0-255 -> 2^8 - 2^16
unsigned int exp8[256];
void setexp8(){
  for (int i = 0; i < 256; ++i) {
    exp8[i] = 256*exp(log(2.0)*(i+0.5)/32.0);
  }
}

//setup frequencies/phase increments, starting at C4=0 to B5. (A4 is defined as 440Hz)
unsigned int tone_inc[108];
void settones() {
  for (byte i=21; i<108; i++){
    tone_inc[i]= 27.5 * pow(2.0, ( (i-21) / 12.0)) * 65536.0 / (16000000.0/512) + 0.5;
  }
}

byte butstatD1=0xFF;
byte butstatD2=0xFF;
byte butstatD3=0xFF;
byte prevbutstatD1=0xFF;
byte prevbutstatD2=0xFF;
byte prevbutstatD3=0xFF;

void setup() {

  //disable all inerrupts to avoid glitches
  noInterrupts();

  //setup the array with sine values
  setsine();

  //setup the array with sine values
  setexp8();

  //setup array with tone frequency phase increments
  settones();

  //setup PORTD (pins D0-D7) for input with pull-up 
  DDRD=0B00000000; PORTD=0B11111111; 

  //setup PORTB (pins D8-D13) to high impedance, and D9 to output
  DDRB=0B00000010; PORTB=0B00000000;

  //setup PORTC (pins A0-A7) for input
  DDRC=0B00000000; PORTC=00000000;

  //Set a fast PWM signal on pin D9, TIMER1A, 9-bit resolution, 31250Hz
  TCCR1A = 0B10000010; //9-bit fast PWM
  TCCR1B = 0B00001001;

  //setup the ADC 
  ADCSRA=B11110100; // prescale 16 -> 13 mus per sample, auto trigger
  ADCSRB=B00000000; // freerun
  ADMUX =B01100000; // Vcc ref, left-align, ch0

  Serial.begin(9600);
}

// Midi Input
static byte input[3];
static int index = 0;


//initialize the main parameters of the pulse length setting
#define nch 4 //number of channels that can produce sound simultaneously
unsigned int phase[nch]  = {0,0,0,0};
int          inc[nch]    = {0,0,0,0};
byte         amp[nch]    = {0,0,0,0};
unsigned int FMphase[nch]= {0,0,0,0};
unsigned int FMinc[nch]  = {0,0,0,0};
unsigned int FMamp[nch]  = {0,0,0,0};

// main function (forced inline) to update the pulse length
inline void setPWM() __attribute__((always_inline));
inline void setPWM() {

  //wait for the timer to complete loop
  while ((TIFR1 & 0B00000001) == 0);

  //Clear(!) the overflow bit by writing a 1 to it
  TIFR1 |= 0B00000001;

  //increment the phases of the FM
  FMphase[0] += FMinc[0];
  FMphase[1] += FMinc[1];
  FMphase[2] += FMinc[2];
  FMphase[3] += FMinc[3];

  //increment the phases of the note
  phase[0] += inc[0];
  phase[1] += inc[1];
  phase[2] += inc[2];
  phase[3] += inc[3];

  //calculate the output value and set pulse width for timer2
  int val = sine[(phase[0]+sine[FMphase[0]>>8]*FMamp[0]) >> 8] * amp[0];
  val += sine[(phase[1]+sine[FMphase[1]>>8]*FMamp[1]) >> 8] * amp[1];
  val += sine[(phase[2]+sine[FMphase[2]>>8]*FMamp[2]) >> 8] * amp[2];
  val += sine[(phase[3]+sine[FMphase[3]>>8]*FMamp[3]) >> 8] * amp[3];

  //set the pulse length
  OCR1A = val/128 + 256;
}

//instrument parameters  
unsigned int ADSR_a  = 0;  
unsigned int ADSR_d  = 0;
unsigned int ADSR_s  = 0;    
unsigned int ADSR_r  = 0;
unsigned int FM_inc  = 0;
unsigned int FM_a1   = 0;
unsigned int FM_a2   = 0;
unsigned int FM_dec  = 0;

//properties of each note played
byte         iADSR[nch]     = {0, 0, 0, 0}; 
unsigned int envADSR[nch]   = {0, 0, 0, 0}; 
byte         amp_base[nch]  = {0, 0, 0, 0};
unsigned int inc_base[nch]  = {0, 0, 0, 0};
unsigned int FMexp[nch]     = {0, 0, 0, 0};
unsigned int FMval[nch]     = {0, 0, 0, 0};
byte         keych[nch]     = {0, 0, 0, 0}; 
unsigned int tch[nch]       = {0, 0, 0, 0}; 


// main loop. Duration of loop is determined by number of setPWM calls
// Each setPWMcall corresponds to 512 cycles=32mus
// Tloop= 32mus * #setPWM. #setPWM=16 gives Tloop=0.512ms
void loop() {

  //read and interpret input buttons
  prevbutstatD1 = butstatD1;
  prevbutstatD2 = butstatD2;
  prevbutstatD3 = butstatD3;
  DDRB|=0B00001000; delayMicroseconds(3); butstatD1 = PIND; DDRB&=0B11110111;
  DDRB|=0B00000100; delayMicroseconds(3); butstatD2 = PIND; DDRB&=0B11111011;
  DDRB|=0B00000001; delayMicroseconds(3); butstatD3 = PIND; DDRB&=0B11111110;

  //if((butstatD1&1)==0)PORTB|=0B00100000;
  //else PORTB&=0B11011111;
  
  byte keypressed = nokey;
  byte keyreleased = nokey;
  // if(butstatD1!=prevbutstatD1){
  //   if ( (butstatD1 & (1<<0)) == 0 and (prevbutstatD1 & (1<<0)) >  0 ) keypressed  =  0;
  //   if ( (butstatD1 & (1<<0)) >  0 and (prevbutstatD1 & (1<<0)) == 0 ) keyreleased =  0;
  //   if ( (butstatD1 & (1<<1)) == 0 and (prevbutstatD1 & (1<<1)) >  0 ) keypressed  =  1;
  //   if ( (butstatD1 & (1<<1)) >  0 and (prevbutstatD1 & (1<<1)) == 0 ) keyreleased =  1;
  //   if ( (butstatD1 & (1<<2)) == 0 and (prevbutstatD1 & (1<<2)) >  0 ) keypressed  =  2;
  //   if ( (butstatD1 & (1<<2)) >  0 and (prevbutstatD1 & (1<<2)) == 0 ) keyreleased =  2;
  //   if ( (butstatD1 & (1<<3)) == 0 and (prevbutstatD1 & (1<<3)) >  0 ) keypressed  =  3;
  //   if ( (butstatD1 & (1<<3)) >  0 and (prevbutstatD1 & (1<<3)) == 0 ) keyreleased =  3;
  //   if ( (butstatD1 & (1<<4)) == 0 and (prevbutstatD1 & (1<<4)) >  0 ) keypressed  =  4;
  //   if ( (butstatD1 & (1<<4)) >  0 and (prevbutstatD1 & (1<<4)) == 0 ) keyreleased =  4;
  //   if ( (butstatD1 & (1<<5)) == 0 and (prevbutstatD1 & (1<<5)) >  0 ) keypressed  =  5;
  //   if ( (butstatD1 & (1<<5)) >  0 and (prevbutstatD1 & (1<<5)) == 0 ) keyreleased =  5;
  //   if ( (butstatD1 & (1<<6)) == 0 and (prevbutstatD1 & (1<<6)) >  0 ) keypressed  =  6;
  //   if ( (butstatD1 & (1<<6)) >  0 and (prevbutstatD1 & (1<<6)) == 0 ) keyreleased =  6;
  //   if ( (butstatD1 & (1<<7)) == 0 and (prevbutstatD1 & (1<<7)) >  0 ) keypressed  =  7;
  //   if ( (butstatD1 & (1<<7)) >  0 and (prevbutstatD1 & (1<<7)) == 0 ) keyreleased =  7;
  // }
  // if(butstatD2!=prevbutstatD2){
  //   if ( (butstatD2 & (1<<0)) == 0 and (prevbutstatD2 & (1<<0)) >  0 ) keypressed  =  8;
  //   if ( (butstatD2 & (1<<0)) >  0 and (prevbutstatD2 & (1<<0)) == 0 ) keyreleased =  8;
  //   if ( (butstatD2 & (1<<1)) == 0 and (prevbutstatD2 & (1<<1)) >  0 ) keypressed  =  9;
  //   if ( (butstatD2 & (1<<1)) >  0 and (prevbutstatD2 & (1<<1)) == 0 ) keyreleased =  9;
  //   if ( (butstatD2 & (1<<2)) == 0 and (prevbutstatD2 & (1<<2)) >  0 ) keypressed  = 10;
  //   if ( (butstatD2 & (1<<2)) >  0 and (prevbutstatD2 & (1<<2)) == 0 ) keyreleased = 10;
  //   if ( (butstatD2 & (1<<3)) == 0 and (prevbutstatD2 & (1<<3)) >  0 ) keypressed  = 11;
  //   if ( (butstatD2 & (1<<3)) >  0 and (prevbutstatD2 & (1<<3)) == 0 ) keyreleased = 11;
  //   if ( (butstatD2 & (1<<4)) == 0 and (prevbutstatD2 & (1<<4)) >  0 ) keypressed  = 12;
  //   if ( (butstatD2 & (1<<4)) >  0 and (prevbutstatD2 & (1<<4)) == 0 ) keyreleased = 12;
  //   if ( (butstatD2 & (1<<5)) == 0 and (prevbutstatD2 & (1<<5)) >  0 ) keypressed  = 13;
  //   if ( (butstatD2 & (1<<5)) >  0 and (prevbutstatD2 & (1<<5)) == 0 ) keyreleased = 13;
  //   if ( (butstatD2 & (1<<6)) == 0 and (prevbutstatD2 & (1<<6)) >  0 ) keypressed  = 14;
  //   if ( (butstatD2 & (1<<6)) >  0 and (prevbutstatD2 & (1<<6)) == 0 ) keyreleased = 14;
  //   if ( (butstatD2 & (1<<7)) == 0 and (prevbutstatD2 & (1<<7)) >  0 ) keypressed  = 15;
  //   if ( (butstatD2 & (1<<7)) >  0 and (prevbutstatD2 & (1<<7)) == 0 ) keyreleased = 15;
  // }
  // if(butstatD3!=prevbutstatD3){
  //   if ( (butstatD3 & (1<<0)) == 0 and (prevbutstatD3 & (1<<0)) >  0 ) keypressed  = 16;
  //   if ( (butstatD3 & (1<<0)) >  0 and (prevbutstatD3 & (1<<0)) == 0 ) keyreleased = 16;
  //   if ( (butstatD3 & (1<<1)) == 0 and (prevbutstatD3 & (1<<1)) >  0 ) keypressed  = 17;
  //   if ( (butstatD3 & (1<<1)) >  0 and (prevbutstatD3 & (1<<1)) == 0 ) keyreleased = 17;
  //   if ( (butstatD3 & (1<<2)) == 0 and (prevbutstatD3 & (1<<2)) >  0 ) keypressed  = 18;
  //   if ( (butstatD3 & (1<<2)) >  0 and (prevbutstatD3 & (1<<2)) == 0 ) keyreleased = 18;
  //   if ( (butstatD3 & (1<<3)) == 0 and (prevbutstatD3 & (1<<3)) >  0 ) keypressed  = 19;
  //   if ( (butstatD3 & (1<<3)) >  0 and (prevbutstatD3 & (1<<3)) == 0 ) keyreleased = 19;
  //   if ( (butstatD3 & (1<<4)) == 0 and (prevbutstatD3 & (1<<4)) >  0 ) keypressed  = 20;
  //   if ( (butstatD3 & (1<<4)) >  0 and (prevbutstatD3 & (1<<4)) == 0 ) keyreleased = 20;
  //   if ( (butstatD3 & (1<<5)) == 0 and (prevbutstatD3 & (1<<5)) >  0 ) keypressed  = 21;
  //   if ( (butstatD3 & (1<<5)) >  0 and (prevbutstatD3 & (1<<5)) == 0 ) keyreleased = 21;
  //   if ( (butstatD3 & (1<<6)) == 0 and (prevbutstatD3 & (1<<6)) >  0 ) keypressed  = 22;
  //   if ( (butstatD3 & (1<<6)) >  0 and (prevbutstatD3 & (1<<6)) == 0 ) keyreleased = 22;
  //   if ( (butstatD3 & (1<<7)) == 0 and (prevbutstatD3 & (1<<7)) >  0 ) keypressed  = 23;
  //   if ( (butstatD3 & (1<<7)) >  0 and (prevbutstatD3 & (1<<7)) == 0 ) keyreleased = 23;
  // }
  
  setPWM(); //#1

  //setup one POT
  ADMUX &=B11111000; ADMUX |= ipot;
  
  setPWM(); //#2

  //readout one pot
  // byte potval=ADCH;

  // //interpret pot values  
  // if(ipot==7) ADSR_a  = 4096;//exp8[255-potval]>>4;     // range: 4096-256-16 = 8ms-128ms-2s  
  // if(ipot==6) ADSR_d  = 16;//exp8[255-potval]>>4;
  // if(ipot==5) ADSR_s  = 255;//potval<<8;          // range 0-255 = 0.0-0.5-1.0
  // if(ipot==4) ADSR_r  = 120;//exp8[255-potval]>>4;
  // if(ipot==3) FM_inc  = 512;//exp8[potval]>>4;         // range 16-256-4096 = fm/fc 0.06 - 1.0 - 16
  // if(ipot==2) FM_a1   = -1;//(exp8[potval]-258)>>4;   // range 0 -240 -4080 ~ beta 0 - 1 -16
  // if(ipot==1) FM_a2   = -1;//(exp8[potval]-258)>>4;
  // if(ipot==0) FM_dec  = 1;//exp8[255-potval]>>4;
  // unsigned int FM_a0=FM_a2;
  // int          FM_da=FM_a1-FM_a2;  
  // ipot++;
  // if (ipot>=npot) ipot=0;
  
  // Serial
  if(UCSR0A & (1<<RXC0))
  {
    input[index++] = UDR0;
  }

  if (index == 3) 
  {
    if(input[0] >= 0xb0 && input[0] <= 0xbf)
    {
      if(input [1] >= 0x14 && input[1] <= 0x1f)
      {
        int val = input[2]*2;
        switch(input[1] - 0x14)
        {
          case 0:
            FM_dec = exp8[255-val]>>4;
            break;
          case 1:
            FM_a2 = (exp8[val]-258)>>4;
            break;
          case 2:
            FM_a1 = (exp8[val]-258)>>4;
            break;
          case 3:
            FM_inc = exp8[val]>>4;
            break;
          case 4:
            ADSR_r = exp8[255-val]>>4;
            break;
          case 5:
            ADSR_s = val<<8;
            break;
          case 6:
            ADSR_d = exp8[255-val]>>4;
            break;
          case 7:
            ADSR_a = exp8[255-val]>>4;
            break;
        }
      }
    }
    else if(input[0] >= 0x90 && input[0] <= 0x9f)
    {
      keypressed = input[1];
    }
    else if(input[0] >= 0x80 && input[0] <= 0x8f)
    {
      keyreleased = input[1];
    }
    index = 0;
  }
  unsigned int FM_a0=FM_a2;
  int          FM_da=FM_a1-FM_a2;  

  setPWM(); //#3


  //find the best channel to start a new note
  byte nextch = 255;
  //first check if the key is still being played
  if (iADSR[0] > 0 and keypressed == keych[0])nextch = 0;
  if (iADSR[1] > 0 and keypressed == keych[1])nextch = 1;
  if (iADSR[2] > 0 and keypressed == keych[2])nextch = 2;
  if (iADSR[3] > 0 and keypressed == keych[3])nextch = 3;
  //then check for an empty channel
  if (nextch == 255) {
    if (iADSR[0] == 0)nextch = 0;
    if (iADSR[1] == 0)nextch = 1;
    if (iADSR[2] == 0)nextch = 2;
    if (iADSR[3] == 0)nextch = 3;
  }
  //otherwise use the channel with the longest playing note
  if (nextch == 255) {
    nextch = 0;
    if (tch[0] > tch[nextch])nextch = 0;
    if (tch[1] > tch[nextch])nextch = 1;
    if (tch[2] > tch[nextch])nextch = 2;
    if (tch[3] > tch[nextch])nextch = 3;
  }

  setPWM(); //#4

  //initiate new note if needed
  if (keypressed != nokey) {
    phase[nextch]=0;
    amp_base[nextch] = 64;
    inc_base[nextch] = tone_inc[keypressed];
    iADSR[nextch] = 1;
    FMphase[nextch]=0;
    FMexp[nextch]=0xFFFF;
    keych[nextch] = keypressed;
    tch[nextch] = 0;
  }

  setPWM(); //#5

  //stop a note if the button is released
  if (keyreleased != nokey) {
    if (keych[0] == keyreleased)iADSR[0] = 4;
    if (keych[1] == keyreleased)iADSR[1] = 4;
    if (keych[2] == keyreleased)iADSR[2] = 4;
    if (keych[3] == keyreleased)iADSR[3] = 4;
  }
  
  setPWM(); //#6

  //update FM decay exponential 
  FMexp[0]-=(long)FMexp[0]*FM_dec>>16;
  FMexp[1]-=(long)FMexp[1]*FM_dec>>16;
  FMexp[2]-=(long)FMexp[2]*FM_dec>>16;
  FMexp[3]-=(long)FMexp[3]*FM_dec>>16;
  
  setPWM(); //#7
  
  //adjust the ADSR envelopes
  for (byte ich = 0; ich < nch; ich++) {
    if (iADSR[ich] == 4) { // in RELEASE
      if (envADSR[ich] <= ADSR_r) {
        envADSR[ich] = 0;
        iADSR[ich] = 0;
      }
      else envADSR[ich] -= ADSR_r;
    }
    if (iADSR[ich] == 3) { // in SUSTAIN
      envADSR[ich] = ADSR_s;
    }
    if (iADSR[ich] == 2) { // in DECAY
      if (envADSR[ich] <= (ADSR_s + ADSR_d)) {
        envADSR[ich] = ADSR_s;
        iADSR[ich] = 3;
      }
      else envADSR[ich] -= ADSR_d;
    }
    if (iADSR[ich] == 1) { // in ATTACK
      if ((0xFFFF - envADSR[ich]) <= ADSR_a) {
        envADSR[ich] = 0xFFFF;
        iADSR[ich] = 2;
      }
      else envADSR[ich] += ADSR_a;
    }
    tch[ich]++;
    setPWM(); //#8-11
  }

  //update the tone for channel 0
  amp[0] = (amp_base[0] * (envADSR[0] >> 8)) >> 8;
  inc[0] = inc_base[0];
  FMamp[0] = FM_a0 + ((long)FM_da * FMexp[0]>>16);
  FMinc[0] = ((long)inc_base[0]*FM_inc)/256;
  setPWM(); //#12

  //update the tone for channel 1
  amp[1] = (amp_base[1] * (envADSR[1] >> 8)) >> 8;
  inc[1] = inc_base[1];
  FMamp[1] = FM_a0 + ((long)FM_da * FMexp[1]>>16);
  FMinc[1] = ((long)inc_base[1]*FM_inc)/256;
  setPWM(); //#13

  //update the tone for channel 2
  amp[2] = (amp_base[2] * (envADSR[2] >> 8)) >> 8;
  inc[2] = inc_base[2];
  FMamp[2] = FM_a0 + ((long)FM_da * FMexp[2]>>16);
  FMinc[2] = ((long)inc_base[2]*FM_inc)/256;
  setPWM(); //#14

  //update the tone for channel 3
  amp[3] = (amp_base[3] * (envADSR[3] >> 8)) >> 8;
  inc[3] = inc_base[3];
  FMamp[3] = FM_a0 + ((long)FM_da * FMexp[3]>>16);
  FMinc[3] = ((long)inc_base[3]*FM_inc)/256;
  setPWM(); //#15

  //update counters
  tch[0]++;
  tch[1]++;
  tch[2]++;
  tch[3]++;

  setPWM(); //#16

}
