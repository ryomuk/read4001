/*
 * read4001.c
 * A simple ROM reader for Intel 1702A EPROM using Raspberry Pi Zero
 *
 * Copyright (c) 2022 Ryo Mukai
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wiringPi.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h> // for clock_gettime()

// for disable and enable interrupt
#define BLOCK_SIZE (4 * 1024)
#define PI0_PERI_BASE 0x20000000
#define PERI_BASE PI0_PERI_BASE

typedef unsigned char byte;
typedef unsigned short word;

#define bit(x) (1<<(x))

#define DEFAULT_CHIPNUMBER 0
#define MEMSIZE 0x100

#define DOUT0 14
#define DOUT1 15
#define DOUT2 18
#define DOUT3 23
#define CLK1  24
#define CLK2  25
#define SYNC   8
#define CM     7
#define DIN0  12
#define DIN1  16
#define DIN2  20
#define DIN3  21

//#define CLOCK_kHz 570 /* 500 to 740 (kHz)*/
#define CLOCK_kHz 740 /* 500 to 740 (kHz)*/
#define Tcy_ns (1000000 / CLOCK_kHz) /* Clock Period (1.35 to 2.0 usec)*/
#define Tunit_ns (Tcy_ns / 7)

#define delay_usec(x) delayNanoseconds((x)*1000L)
void delayNanoseconds(unsigned int howLong)
{
  struct timespec tNow, tEnd;

  clock_gettime(CLOCK_MONOTONIC, &tNow);
  tEnd.tv_sec = tNow.tv_sec;
  tEnd.tv_nsec = tNow.tv_nsec + howLong;
  if(tEnd.tv_nsec >= 1000000000L){
    tEnd.tv_sec++;
    tEnd.tv_nsec -= 1000000000L;
  }
  do{
    clock_gettime(CLOCK_MONOTONIC, &tNow);
  } while ( (tNow.tv_sec == tEnd.tv_sec) ?
	    (tNow.tv_nsec < tEnd.tv_nsec)
	    : (tNow.tv_sec < tEnd.tv_sec));
}

volatile unsigned int *g_irqen1;
volatile unsigned int *g_irqen2;
volatile unsigned int *g_irqen3;
volatile unsigned int *g_irqdi1;
volatile unsigned int *g_irqdi2;
volatile unsigned int *g_irqdi3;
unsigned int g_irq1, g_irq2, g_irq3;

int initInterrupt(){
  int mem_fd;
  char *map;
  if ((mem_fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0){
    return(-1);
  }
  map = (char*) mmap(NULL,
		     BLOCK_SIZE,
		     PROT_READ | PROT_WRITE,
		     MAP_SHARED,
		     mem_fd,
		     PERI_BASE + 0xb000
		     );
  if (map == MAP_FAILED){
    return(-1);
  }
  g_irqen1 = (volatile unsigned int *) (map + 0x210);
  g_irqen2 = (volatile unsigned int *) (map + 0x214);
  g_irqen3 = (volatile unsigned int *) (map + 0x218);
  g_irqdi1 = (volatile unsigned int *) (map + 0x21c);
  g_irqdi2 = (volatile unsigned int *) (map + 0x220);
  g_irqdi3 = (volatile unsigned int *) (map + 0x224);
  return(0);
}

void disableInterrupt(){
  g_irq1 = *g_irqen1;
  g_irq2 = *g_irqen2;
  g_irq3 = *g_irqen3;

  *g_irqdi1 = 0xffffffff;
  *g_irqdi2 = 0xffffffff;
  *g_irqdi3 = 0xffffffff;
}

void enableInterrupt()
{
  *g_irqen1 = g_irq1;
  *g_irqen2 = g_irq2;
  *g_irqen3 = g_irq3;
}

inline void Assert(int pin){
  digitalWrite(pin, 1);
}

inline void Negate(int pin){
  digitalWrite(pin, 0);
}

void initGPIO(){
  // Initialize WiringPi
  wiringPiSetupGpio();

  pinMode(DOUT0, OUTPUT);
  pinMode(DOUT1, OUTPUT);
  pinMode(DOUT2, OUTPUT);
  pinMode(DOUT3, OUTPUT);
  pinMode(CLK1,  OUTPUT);
  pinMode(CLK2,  OUTPUT);
  pinMode(SYNC,  OUTPUT);
  pinMode(CM,    OUTPUT);
  pinMode(DIN0,  INPUT);
  pinMode(DIN1,  INPUT);
  pinMode(DIN2,  INPUT);
  pinMode(DIN3,  INPUT);

  Negate(CLK1);
  Negate(CLK2);
  Negate(SYNC);
  Negate(CM);
}

void setData(byte data){
  // Driver is TBD62083("H" input Sink Driver down to -10V(Vdd))
  digitalWrite(DOUT0, data & bit(0));
  digitalWrite(DOUT1, data & bit(1));
  digitalWrite(DOUT2, data & bit(2));
  digitalWrite(DOUT3, data & bit(3));
}

byte getData(){
  return( digitalRead(DIN0)
	  | (digitalRead(DIN1) << 1)
	  | (digitalRead(DIN2) << 2)
	  | (digitalRead(DIN3) << 3)
	  );	 
}

void  setPhase(int p){
  if(p == 1){
    Assert(CLK1);
  }
  if(p == 3){
    Negate(CLK1);
  }
  if(p == 5){
    Assert(CLK2);
  }
  if(p == 0){
    Negate(CLK2);
  }
  delayNanoseconds(Tunit_ns);
}

void  initCycle(){
  Assert(SYNC);
  for(int i = 0; i < 7; i++){
    setPhase(i);
  }
}

void cycleA1(byte address){
  Negate(SYNC);
  setData(address);
  for(int i = 0; i < 7; i++){
    setPhase(i);
  }
}

void cycleA2(byte address){
  setData(address >> 4);
  for(int i = 0; i < 7; i++){
    setPhase(i);
  }
}

void cycleA3(byte chipnumber){
  setData(chipnumber);
  for(int i = 0; i < 7; i++){
    setPhase(i);
    if(i == 3){
      Assert(CM);
    }
  }
}

void cycleM1(byte *data){
  for(int i = 0; i < 7; i++){
    setPhase(i);
    if(i == 0){
      setData(0);
      Negate(CM);
    }
    if(i == 5){
      *data = getData() << 4;
    }
  }
}

void cycleM2(byte *data){
  for(int i = 0; i < 7; i++){
    setPhase(i);
    if(i == 5){
      *data |= getData();
    }
  }
}

void cycleX1(){
  for(int i = 0; i < 7; i++){
    setPhase(i);
  }
}

void cycleX2(){
  for(int i = 0; i < 7; i++){
    setPhase(i);
  }
}

void cycleX3(){
  Assert(SYNC);
  for(int i = 0; i < 7; i++){
    setPhase(i);
  }
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

int main(int argc, char *argv[]){
  word address;
  byte chipnumber;
  byte buf[MEMSIZE];
  unsigned long t_start, t_total;
  
  chipnumber = DEFAULT_CHIPNUMBER;
  if(argc > 1){
    if(strncmp(argv[1], "0b", 2) == 0){
      chipnumber = strtol(&argv[1][2], NULL, 2);
    } else if(strncmp(argv[1], "0x", 2) == 0){
      chipnumber = strtol(argv[1], NULL, 16);
    } else {
      chipnumber = atoi(argv[1]);
    }
  }
  fprintf(stderr, "CLOCK=%d(kHz), %d(ns/cycle)=7*%d(ns), %.2lf(us/word)\n",
	  CLOCK_kHz, Tcy_ns, Tunit_ns,
	  (double)Tcy_ns*8/1000);
  fprintf(stderr, "Chip Number = %d\n", chipnumber);
  
  initGPIO();
  
  if(initInterrupt() !=0){
    fprintf(stderr, "initInterrupt() failed. Try sudo ./prog1702\n");
    exit(1);
  }

  //disableInterrupt();

  initCycle();
  
  t_start = micros();
  for(address = 0; address < MEMSIZE; address++){
    buf[address] = readMemory(chipnumber, address);
  }
  //enableInterrupt();
  t_total = micros() - t_start;

  fprintf(stderr, "%d(us), %lf(us/word)\n", (int)t_total, (double)t_total/MEMSIZE);
  
  for(address = 0; address < MEMSIZE; address++){
    putchar(buf[address]);
  }
}
