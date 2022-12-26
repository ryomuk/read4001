#include <SD.h>
#include <SPI.h>

//#define PRINTTIMINGINFO_Serial
//#define PRINTDATA_Serial
#define PRINTTIMINGINFO_DataFile

#define gpio_LED LED_BUILTIN
#define gpio_RESET 1

char DataFileName[16];
File DataFile;

#define DEFAULTCHIPNUMBER 0
#define MEMSIZE 0x100

#define DOUT0 32
#define DOUT1 31
#define DOUT2 30
#define DOUT3 29
#define SYNC  28
#define CM    27
#define CLK1  26
#define CLK2  25
#define DIN0  12
#define DIN1  11
#define DIN2  10
#define DIN3   9


#define digitalWrite digitalWriteFast
#define digitalRead digitalReadFast

// GPIO signals are Positive Logic
#define Assert(pin) digitalWrite(pin, 1)
#define Negate(pin) digitalWrite(pin, 0)

// 4001 signals are Negative Logic
#define Assert_N(pin) digitalWrite(pin, 0)
#define Negate_N(pin) digitalWrite(pin, 1)

inline void writeDataBus(byte data){
  // address/data line is negative logic
  data = ~data; 
  digitalWrite(DOUT0, data & bit(0));
  digitalWrite(DOUT1, data & bit(1));
  digitalWrite(DOUT2, data & bit(2));
  digitalWrite(DOUT3, data & bit(3));
}

// set databus to Z
inline void openDataBus(){
  digitalWrite(DOUT0, 1);
  digitalWrite(DOUT1, 1);
  digitalWrite(DOUT2, 1);
  digitalWrite(DOUT3, 1);
}

inline byte readDataBus(){
  return( digitalRead(DIN0)
	  | (digitalRead(DIN1) << 1)
	  | (digitalRead(DIN2) << 2)
	  | (digitalRead(DIN3) << 3)
	  );	 
}

  //       11   01        11       10        11
  // CLK1 ~~~~~\_________/~~~~~~~~~~~~~~~~~~~~~~~~\____
  //
  // CLK2 /~~~~~~~~~~~~~~~~~~~~~~~\_________/~~~~~~~~~
void waitForPhase01(){
  while(1){
    if(!digitalRead(CLK1) && digitalRead(CLK2)) break;
  }
}

void waitForPhase11(){
  while(1){
    if(digitalRead(CLK1) && digitalRead(CLK2)) break;
  }
}

void waitForPhase10(){
  while(1){
    if(digitalRead(CLK1) && !digitalRead(CLK2)) break;
  }
}

void cycleA1(byte address){
  waitForPhase01();
  Negate_N(SYNC);
  waitForPhase11();
  writeDataBus(address);
  waitForPhase10();
  waitForPhase11();
}

void cycleA2(byte address){
  waitForPhase01();
  waitForPhase11();
  writeDataBus(address >> 4);
  waitForPhase10();
  waitForPhase11();
}

void cycleA3(byte chipnumber){
  waitForPhase01();
  waitForPhase11();
  writeDataBus(chipnumber);
  Assert_N(CM);
  waitForPhase10();
  waitForPhase11();
}

void cycleM1(byte *data){
  waitForPhase01();
  openDataBus(); // set databus to Z
  Negate_N(CM);
  waitForPhase11();
  waitForPhase10();
  *data = readDataBus() << 4;
  waitForPhase11();
}

void cycleM2(byte *data){
  waitForPhase01();
  waitForPhase11();
  waitForPhase10();
  *data |= readDataBus();
  waitForPhase11();
}
void cycleX1(){
  waitForPhase01();
  waitForPhase11();
  waitForPhase10();
  waitForPhase11();
}

void cycleX2(){
  waitForPhase01();
  waitForPhase11();
  waitForPhase10();
  waitForPhase11();
}

void cycleX3(){
  waitForPhase01();
  Assert_N(SYNC);
  waitForPhase11();
  waitForPhase10();
  waitForPhase11();
}

void  initCycle(){
  cycleX1();
  cycleX2();
  cycleX3();
}

byte readMemory(byte chipnumber, byte address){
  byte data;

  cycleA1(address);
  cycleA2(address);
  cycleA3(chipnumber);
  cycleM1(&data);
  cycleM2(&data);
  cycleX1();
  cycleX2();
  cycleX3();

  return(data);
}

void blinkLED(int n){
  int i;
  for(i = 0; i < n; i++){
    Assert(gpio_LED); delay(150);
    Negate(gpio_LED); delay(150);
  }
}

void error_blinkLED(int n){
  while(1){
    blinkLED(n);
    delay(1000);
  }
}

void openDataFile(){
  int i;

  for(i = 0; i < 10000; i++){
    sprintf(DataFileName, "%04d.txt", i);
    if(!SD.exists(DataFileName)){
      DataFile = SD.open(DataFileName, FILE_WRITE);
      break;
    }
  }
  if(!DataFile || i == 10000){
    error_blinkLED(4);
  }
}

void setup() {
#ifdef PRINTTIMINGINFO_Serial
  Serial.begin(9600);
#endif
  
  pinMode(gpio_LED, OUTPUT); Negate(gpio_LED);
  pinMode(gpio_RESET, INPUT_PULLUP);
  
  // see if the card is present and can be initialized:
  if (!SD.begin(BUILTIN_SDCARD)) {
      // No SD card, so don't do anything more - stay stuck here
    error_blinkLED(2);
  }

  // for clock counter
  ARM_DEMCR |= ARM_DEMCR_TRCENA;
  ARM_DWT_CTRL |= ARM_DWT_CTRL_CYCCNTENA;

  pinMode(DOUT0, OUTPUT);
  pinMode(DOUT1, OUTPUT);
  pinMode(DOUT2, OUTPUT);
  pinMode(DOUT3, OUTPUT);
  pinMode(SYNC,  OUTPUT);
  pinMode(CM,    OUTPUT);
  pinMode(CLK1,  INPUT);
  pinMode(CLK2,  INPUT);
  pinMode(DIN0,  INPUT);
  pinMode(DIN1,  INPUT);
  pinMode(DIN2,  INPUT);
  pinMode(DIN3,  INPUT);

  Negate_N(SYNC);
  Negate_N(CM);
}

void loop() {
  int i;
  word address;
  byte buf[MEMSIZE];
  byte chipnumber = DEFAULTCHIPNUMBER;

  unsigned long t_start, t_total;
  char printbuf[512];

  openDataFile();

  initCycle();

  t_start = micros();
  for(address = 0; address < MEMSIZE; address++){
    buf[address] = readMemory(chipnumber, address);
  }
  t_total = micros() - t_start;
  
  sprintf(printbuf,
	  "%s: Total=%ldus(%ldus/word)\n",
	  DataFileName,  t_total, t_total/MEMSIZE);

#ifdef PRINTTIMINGINFO_DataFile
  DataFile.print(printbuf);
#endif
#ifdef PRINTTIMINGINFO_Serial
  Serial.print(printbuf);
#endif
  
  for(i = 0; i < MEMSIZE; i++){
    sprintf(printbuf, "%02X%c", buf[i], (i%16 ==15) ? '\n': ' ');
    DataFile.print(printbuf);
#ifdef PRINTDATA_Serial
    Serial.print(printbuf);
#endif    
  }

  DataFile.close();

  blinkLED(2);
  while(digitalRead(gpio_RESET) == HIGH){
    delay(100);
  }
  blinkLED(1);
  while(digitalRead(gpio_RESET) == LOW){
    delay(100);
  }
  delay(1000);
}
