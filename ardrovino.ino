/*************************************************************************************
 
 
**************************************************************************************/

#include <TimerOne.h>
#include <LiquidCrystal.h>
#include <EEPROM.h>


char *fractInches[]={
"    ","1/64","1/32","3/64","1/16","5/64","3/32","7/64","1/8","9/64","5/32","11/64","3/16","13/64","7/32","15/64","1/4",	
"17/64","9/32",	"19/64","5/16",	"21/64","11/32","23/64","3/8","25/64","13/32","27/64","7/16","29/64","15/32","31/64","1/2",	
"33/64","17/32","35/64","9/16",	"37/64","19/32","39/64","5/8","41/64","21/32",	"43/64","11/16","45/64","23/32","47/64","3/4",	
"49/64","25/32","51/64","13/16","53/64","27/32","55/64","7/8","57/64","29/32","59/64","15/16","61/64","31/32","63/64"};

/* General Settings */

#define UART_BAUD_RATE 9600       //  Set this so it matches the BT module's BAUD rate 
#define UPDATE_FREQUENCY 24       //  Frequency in Hz [must be an even number] (number of timer per second the scales are read and the data is sent to the application)
#define HALF_UPDATE_FREQUENCY 12  //not sure if Arduino optimizes out division by 2, so this sure will :)


/* iGaging Clock Settins (do not change) */

#define SCALE_CLK_PULSES 21       //iGaging and Accuremote sclaes use 21 bit format
#define SCALLE_CLK_FREQUENCY 9000 //iGaging scales run at about 9-10KHz


/* GPIO Definitions */


#define SCALE_CLK_OUTPUT_PORT PORTD
#define SCALE_CLK_DIR_PORT DDRD
#define SCALE_CLK_PIN 2           //Arduino Digital pin 8

#define X_INPUT_PORT PIND

#define X_DDR DDRD

#define X_PIN 3                   //Arduino Digital pin 9


void startClkTimer();
void stopClkTimer();

/* Variable definitions */


byte loopCount = 0;

byte bitOffset = 0;
byte clockPinHigh = 0;

volatile long xValue = 0L; //X axis count
volatile bool xAvailable=false;

void setupClkTimer(int frequency){
  bitOffset = 0;

  TCCR2A = 0;// set entire TCCR2A register to 0
  TCCR2B = 0;// same for TCCR2B

  // set compare match register
  OCR2A = F_CPU / (8 * frequency) - 1; // 160 - 1;

  // turn on CTC mode
  TCCR2A |= (1 << WGM21);

  // Set CS21 bit for 8 prescaler //CS20 for no prescaler
  TCCR2B |= (1 << CS21);
}
//starts scale clock timer
void startClkTimer(){
  //initialize counter value to 0
  TCNT2  = 0;
  // enable timer compare interrupt
  TIMSK2 |= (1 << OCIE2A);
}

//stops scale clock timer
void stopClkTimer(){
  // disable timer compare interrupt
  TIMSK2 &= ~(1 << OCIE2A);
}
/* Interrupt Service Routines */

//Timer 2 interrupt (scale clock driver)
ISR(TIMER2_COMPA_vect) {
  //scale reading happens here
  if (!clockPinHigh)  {
    SCALE_CLK_OUTPUT_PORT |= _BV(SCALE_CLK_PIN);
    clockPinHigh = 1;
  }  else  {

    clockPinHigh = 0;
    SCALE_CLK_OUTPUT_PORT &= ~_BV(SCALE_CLK_PIN);

    //read the pin state and shif it into the appropriate variables
    xValue |= ((long)(X_INPUT_PORT & _BV(X_PIN) ? 1 : 0) << bitOffset);

    //read the rest of the scales only if they are enabled


    //increment the bit offset
    bitOffset++;

    if (bitOffset >= SCALE_CLK_PULSES)    {
      //stop the timer after the predefined number of pulses
      stopClkTimer();

      if (X_INPUT_PORT & _BV(X_PIN))
        xValue |= ((long)0x7ff << 21);
        
      xAvailable=true;

    }
  }
}

#define LEDPIN 13

#define btnRIGHT  0
#define btnUP     1
#define btnDOWN   2
#define btnLEFT   3
#define btnSELECT 4
#define btnNONE   5


enum droScales { DRO_MM=0,DRO_CM,DRO_INCHES,DRO_INCHES_F};

volatile unsigned int gpTimer=0;
// DRO
bool droReversed=false;
long droReverseRawVal=0;

byte droScale = DRO_MM;


float droOffset=0; // always in mm
float droStep=1;      // droStep change 
char droStr[20];

bool droZero=false;
bool doDroReversed=false;
long droZeroVal=0;
/* relative / absolute mode */
boolean doRelative=false;
boolean relative=false;
long relativeRaw=0;


struct eepromValues{
  bool droReversed;
  long droReverseRawVal;
  long droZeroVal;
  byte droScale;
  float droOffset;
  long checksum;
};

void storeEE(){
  struct eepromValues val;
  int i;
  
  val.droReversed = droReversed;
  val.droReverseRawVal = droReverseRawVal;
  val.droZeroVal = droZeroVal;
  val.droScale=droScale;
  val.droOffset = droOffset;
  
  val.checksum = -droReverseRawVal+droZeroVal;
  
  byte *pt = (byte*)&val;
  for (i=0;i<sizeof(val); i++){
    EEPROM.write (i,pt[i]);
  }
/*  Serial.println (droZeroVal);
    Serial.println (droOffset);*/
}

void retrieveEE(){
  struct eepromValues val;
  int i;
    
  byte *pt = (byte*)&val;
  for (i=0;i<sizeof(val); i++){
    pt[i] = EEPROM.read (i);
  }
    
  if (val.checksum== (-val.droReverseRawVal+val.droZeroVal)){
    droReversed = val.droReversed ;
    droReverseRawVal= val.droReverseRawVal;
    droZeroVal = val.droZeroVal;
    droScale= val.droScale;
    droOffset= val.droOffset;
  }

}


LiquidCrystal lcd(8, 9, 4, 5, 6, 7);           // select the pins used on the LCD panel
 
// define some values used by the panel and buttons
int lcd_key     = 0;
int adc_key_in  = 0;

// simple menu
void printUnit(){
  switch(droScale){
    case DRO_MM:
      lcd.print (" millimeters    ");return;      
    case DRO_CM:
      lcd.print (" centimeters    ");return;          
    case DRO_INCHES:
      lcd.print (" inches         ");return;          
    case DRO_INCHES_F:
      lcd.print (" fract inches   ");return;          
  }
}

void doScale (int k){
  lcd.setCursor(0,1);
  switch (k){
    case btnNONE:
      
      break;
    case btnUP:
      if (droScale==DRO_INCHES_F) droScale=0; else droScale++;
    
      break;
    case btnDOWN:
      if (droScale==0) droScale=DRO_INCHES_F; else droScale--;
    
      break;
  }
  printUnit();
}

void doOffset (int k){
  
  boolean disp=false;
  float dros=droStep;
  float drop;
  
  switch (droScale){
    case DRO_CM:
      dros*=10;
      break;
    case DRO_INCHES:
    case DRO_INCHES_F:
       dros*=25.4;
  }
  
  lcd.setCursor(0,1);
  
  switch (k){
    case btnNONE:

      lcd.print (" u/d change offs");

      break;
    case btnDOWN:
      droOffset+=dros;
      disp=true;
      break;
          
    case btnUP:
      droOffset-=dros;
      disp=true;
      break;
      
    case btnSELECT:
  
      if      (droStep==0.01) droStep=0.1;
      else if (droStep==0.1) droStep=1.0;
      else if (droStep==1.0) droStep=10.0;
      else droStep=0.01;
      disp=true;
    
  }
  if (disp){
    drop=droOffset;
    switch (droScale){
      case DRO_CM:
        drop/=10;
        break;
      case DRO_INCHES:
      case DRO_INCHES_F:
        drop/=25.4;
    }
    
    memset (droStr,' ',16);
    dtostrf(drop, 7, 2, droStr);droStr[7]=' ';
    switch (droScale){
       case DRO_MM:    memcpy (droStr+7," mm",3); break;
       case DRO_CM:    memcpy (droStr+7," cm",3); break;
       default:        memcpy (droStr+7," in",3); break;
    }
    
    dtostrf(droStep, 4, 2, droStr+11);droStr[15]=' ';
    
    lcd.print(droStr);
  
  }
  
  
}
void doDirection (int k){
  lcd.setCursor(0,1);
  switch (k){
    case btnNONE:
      lcd.print ("up fw / dn rev  "); 
      break;
    case btnUP:
      lcd.print ("forward selected"); 
      droReversed   = false;
      doDroReversed =true;      
      break;
    case btnDOWN:
      lcd.print ("reverse selected");        
      droReversed   =true;
      doDroReversed =true;
    
      break;
  }
}

void doClear (int k){
  lcd.setCursor(0,1);
  switch (k){
    case btnNONE:
      lcd.print ("up clear setting"); 
      break;
    case btnUP:
      droReversed=false;
      droReverseRawVal=0;
      droScale = DRO_MM;
      droOffset=0;
      lcd.print ("SETTINGS CLEARED"); 
      gpTimer=150;
      break;
    case btnDOWN:
    
      break;
  }
}

void doZero (int k){
  lcd.setCursor(0,1);
  switch (k){
    case btnNONE:
      lcd.print ("up to zero scale"); 
      break;
    case btnUP:
      droZero=true;
      lcd.print ("     DONE       "); 
      gpTimer=150;
      break;
    case btnDOWN:
    
      break;
  }
}

typedef struct smenu {
  char * title;
  void (*fn)(int);
} SimpleMenu;

// menu
SimpleMenu menuItem[]={
  {"                ", (void (*)(int)) 0},
  {"   [  UNIT  ]   ",doScale},
  {"   [  ZERO  ]   ",doZero},    
  {"   [ OFFSET ]   ",doOffset},
  {"   [ DIRECT ]   ",doDirection},
  {"   [ CLEAR  ]   ",doClear} 
};
int menuIndex=0;
int menuSIZE = sizeof (menuItem)/sizeof(*menuItem);
int menuLevel=0;


char * inches_fractional(float fract){
  static char buf[16];
  return buf;
  
}



// get formatted DRO value
// 0123456789012345
//  -1000.00     MM 
//     23.121    CM 
//    999.00     I
//    999 3/64   IF

void showDRO(long raw){
//  int raw;
  float craw;
  float ofs;
    
  memset (droStr,' ',16);
  droStr[16]=(byte)0;
  
  switch (droScale){
    case DRO_MM:
      ofs = droOffset;
      memcpy (droStr+12,"mm",2);    
      craw = (float)raw / 2560.0 * 25.4;
      craw = craw+ofs;      
      dtostrf(craw, 8, 2, droStr);droStr[8]=' ';
      break;      
    case DRO_CM:
      ofs = droOffset/10;
      memcpy (droStr+12,"cm",2);
      craw = (float)raw / 2560.0 * 2.54; 
      craw = craw+ofs;      
      dtostrf(craw, 8, 3, droStr);droStr[8]=' ';      
      break;      
      
    case DRO_INCHES:
      ofs = droOffset/25.4;
      memcpy (droStr+12,"\"",1);
      craw = (float)raw / 2560.0; 
      craw = craw+ofs;      
      dtostrf(craw, 9, 3, droStr);droStr[9]=' ';      
      break;
  
    case DRO_INCHES_F:
      ofs = droOffset/25.4;
      memcpy (droStr+12,"\"",1);      
      craw = (float)raw / 2560.0; 
      craw = craw+ofs;    
    
      boolean is_negative = craw<0;
      craw = fabs(craw);
      // integer part
      int inches_i = (int)craw;
      float inches_fra = craw-(float)inches_i;
      int inch_tbl_index = (int)( (inches_fra+1/128.0)*64) ;
      if (inch_tbl_index>63) {inch_tbl_index=0;inches_i++;}
      
      sprintf (droStr+2,"%3d",inches_i);
      droStr[5]=' ';
      strcpy (droStr+6,fractInches[inch_tbl_index]);
      droStr[6+strlen(fractInches[inch_tbl_index])]=' ';
      if (is_negative) {
        if (inches_i<10) droStr[3]='-'; else if (inches_i<100) droStr[2]='-'; else if (inches_i<1000) droStr[1]='-';
      }
      
      break;
  }  
  if (relative) {
    droStr[0]='R';
  }
}

 
byte read_LCD_buttons(){               // read the buttons
    adc_key_in = analogRead(0);       // read the value from the sensor 
  
    if (adc_key_in > 1000) return btnNONE; 
 
    if (adc_key_in < 40)   return btnRIGHT;  
    if (adc_key_in < 150)  return btnUP; 
    if (adc_key_in < 300)  return btnDOWN; 
    if (adc_key_in < 500)  return btnLEFT; 
    if (adc_key_in < 850)  return btnSELECT;  
 
    return btnNONE;                // when all others fail, return this.
}
 

void setup(){

  cli();      //stop interrupts

  //scale clock shoudl be output
  SCALE_CLK_DIR_PORT |= _BV(SCALE_CLK_PIN);         //set clock pin to output

  //set input pins as such
  X_DDR &= ~_BV(X_PIN);


//  Serial.begin(UART_BAUD_RATE);                // set up Serial library

  setupClkTimer(SCALLE_CLK_FREQUENCY);         //init the scale clock timer (don't start it yet)


  sei();                                      //allow interrupts
  
   // restore from EEProm
   retrieveEE();
   
   pinMode(LEDPIN, OUTPUT);
 
//   Timer1.initialize(10000);
  
   lcd.begin(16, 2);
   lcd.clear();
   lcd.setCursor(0,0);   
   lcd.print(" arDROvino v1.0 ");
   delay(800);
   lcd.setCursor(0,0);
   lcd.print("                ");
   
//   Timer1.attachInterrupt( keySM );
   
   attachTimerInt0Extension(keySM);
   droZero=false;
}
 
 
 
enum keyStatuss  {KEY_IDLE, KEY_PRE_PRESS,KEY_PRESS};
// time in msec
#define KEYPRESSTIME 2      // time needed to detect a keypress
#define KEYARTIME 50        // after this time autorepeat start
#define KEYARATE 10         // multiple character generation timeout

volatile int keyCount;
volatile byte keyStatus=KEY_IDLE;
volatile byte keyValue=btnNONE;
byte xc=10;

void keySM() {  
  // generate kb events
  
  if (--xc==0){ xc=10;} else return;
  
  byte kv;
  
  digitalWrite(LEDPIN, !digitalRead(LEDPIN));
  if (gpTimer>0) gpTimer--;
  
    
  if (keyCount>0) keyCount--;
  kv = read_LCD_buttons();
  if (kv!=btnNONE){
   switch(keyStatus){
     case KEY_IDLE:
       keyCount=KEYPRESSTIME;
       keyStatus=KEY_PRE_PRESS;
       break;
     case KEY_PRE_PRESS:
       if (!keyCount){
         keyValue=kv;
         keyStatus = KEY_PRESS;
         keyCount = KEYARTIME;
       }
       break;
     case KEY_PRESS:
       if (!keyCount){
         keyValue=kv;
         keyCount = KEYARATE;         
       }
       break;
   } 
  }else{
    keyStatus=KEY_IDLE;
  }
  
}
  
int kc=0;
int oMenuIndex;
void (*fnp)(int);

unsigned long t=0;

long xValues[2];
byte xValuesIndex=0;
byte xValuesCnt=0;

void loop(){
  byte key;  
  long xV;
  if (t==0) t = millis()+24;
  bool goodone=false;  
  bool xA=false;
  
  if (xAvailable){
    xAvailable=false;
    xValues[xValuesIndex++]=xValue; xValuesIndex&=1;
    if (xValuesCnt<2) {
      xValuesCnt++; 
    } else {
      if (labs(xValues[0]-xValues[1]) < 2560){
        goodone=true;
        xValue= (xValues[0]+xValues[1])/2;
      }
    }
  }
  if (goodone){
    if (doDroReversed){
      if (droReversed){
        droReverseRawVal = xValue;
      } else {
        // devo tornare  droReverseRawVal- (xValue-droReverseRawVal)
        droZeroVal = -( 2*droReverseRawVal-2*xValue-droZeroVal);
      
      }
      doDroReversed=false;
      storeEE();      
    }    
    if (droReversed) {
      xValue = droReverseRawVal- (xValue-droReverseRawVal);
    } 
    
    if (droZero) {
      droZero=false;
      droZeroVal=xValue;
      droOffset=0;
      storeEE();
    }
    xV=xValue-droZeroVal;
    if (doRelative){
      doRelative=false;
      if (relative) {
        relativeRaw=xV+(long)(droOffset/25.4*2560.0);
      }
    }
    if (relative){xV=xV-relativeRaw; }
    
    xA=true;
  }
  
  // start of conversion
  if (millis()>t){
    t=millis()+24;
    bitOffset = 0;
    xValue = 0;
    xAvailable=false;    
    startClkTimer();
  }
  
  // display data, if available
  if (xA) {
   
   lcd.setCursor(0,0);             
   showDRO(xV);
   lcd.print (droStr);
  }
   /*
   if (keyValue!=btnNONE){
     lcd.setCursor(kc,1);
     lcd.print (keyValue);
     kc++; if (kc>15) kc=0;
     keyValue=btnNONE;
     
   }*/
   
   oMenuIndex=menuIndex;
   if ( keyValue!=btnNONE ) { 
     gpTimer=1000;    // menu disappear time if no keypress
   } else {
     if (gpTimer==0){
       menuIndex=0;
       menuLevel=0;
     }
   }
   
   if (menuLevel==0){

     switch (keyValue){
       case btnSELECT:    /* switch absolute/relative mode*/     
          relative =~relative;
          doRelative=true;
          keyValue=btnNONE;break;
       case btnLEFT:
         menuIndex=0;
         keyValue=btnNONE;break;
       case btnUP:
         if (menuIndex==0) menuIndex = menuSIZE-1; else menuIndex--;
         keyValue=btnNONE;break;
       case btnDOWN:
_labDOWN:
         if ( menuIndex== (menuSIZE-1)) menuIndex=0; else menuIndex++;
         keyValue=btnNONE;break;
       case btnRIGHT:
         if (menuIndex==0) goto _labDOWN;
         fnp = menuItem[menuIndex].fn;
         if (fnp) { 
           menuLevel=1;
           fnp (btnNONE);
         }
         
         keyValue=btnNONE; break;
     }
     if (oMenuIndex!=menuIndex){
       lcd.setCursor(0,1);
       lcd.print (menuItem[menuIndex].title);
       
     }
   }
   if (menuLevel==1){
     
     // call function or return to main menu
     switch (keyValue){
       case btnLEFT:         
         menuLevel=0;
         lcd.setCursor(0,1);
         lcd.print (menuItem[menuIndex].title);
         keyValue=btnNONE;break;
      case btnUP:
      case btnDOWN:
      case btnSELECT:
      case btnRIGHT:
        fnp(keyValue);
        
        storeEE();
        keyValue=btnNONE; break;
     
     }
   } 
}

