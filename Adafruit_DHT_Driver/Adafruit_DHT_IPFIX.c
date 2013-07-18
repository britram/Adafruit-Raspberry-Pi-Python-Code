//  How to access GPIO registers from C-code on the Raspberry-Pi
//  Example program
//  15-January-2012
//  Dom and Gert
//


// Access from ARM Running Linux

#define BCM2708_PERI_BASE        0x20000000
#define GPIO_BASE                (BCM2708_PERI_BASE + 0x200000) /* GPIO controller */


#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>
#include <fcntl.h>
#include <assert.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <bcm2835.h>
#include <unistd.h>
#include <arpa/inet.h>

#define MAXTIMINGS 100

//#define DEBUG

#define DHT11 11
#define DHT22 22
#define AM2302 22

int bits[250], data[100];
int bitidx = 0;

int readDHT(int type, int pin, float *degc, float *rh) {
  int counter = 0;
  int laststate = HIGH;
  int j=0;

  // Set GPIO pin to output
  bcm2835_gpio_fsel(pin, BCM2835_GPIO_FSEL_OUTP);

  bcm2835_gpio_write(pin, HIGH);
  usleep(500000);  // 500 ms
  bcm2835_gpio_write(pin, LOW);
  usleep(20000);

  bcm2835_gpio_fsel(pin, BCM2835_GPIO_FSEL_INPT);

  data[0] = data[1] = data[2] = data[3] = data[4] = 0;

  // wait for pin to drop
  while (bcm2835_gpio_lev(pin) == 1) {
    usleep(1);
  }

  // read data!
  for (int i=0; i< MAXTIMINGS; i++) {
    counter = 0;
    while ( bcm2835_gpio_lev(pin) == laststate) {
	counter++;
	//nanosleep(1);		// overclocking might change this?
        if (counter == 1000)
	  break;
    }
    laststate = bcm2835_gpio_lev(pin);
    if (counter == 1000) break;
    bits[bitidx++] = counter;

    if ((i>3) && (i%2 == 0)) {
      // shove each bit into the storage bytes
      data[j/8] <<= 1;
      if (counter > 200)
        data[j/8] |= 1;
      j++;
    }
  }


#ifdef DEBUG
  for (int i=3; i<bitidx; i+=2) {
    fprintf(stderr, "bit %d: %d\n", i-3, bits[i]);
    fprintf(stderr, "bit %d: %d (%d)\n", i-2, bits[i+1], bits[i+1] > 200);
  }
  fprintf(stderr, "Data (%d): 0x%x 0x%x 0x%x 0x%x 0x%x\n", j, data[0], data[1], data[2], data[3], data[4]);
#endif

  if ((j >= 39) &&
      (data[4] == ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) ) 
  {
      // yay!
      if (type == DHT11) {
          *degc = data[2];
          *rh = data[1];
          return 1;
      } else if (type == DHT22) {
          float f, h;
          h = data[0] * 256 + data[1];
          h /= 10.0;

          f = (data[2] & 0x7F)* 256 + data[3];
          f /= 10.0;
          if (data[2] & 0x80)  f *= -1;
          
          *degc = f;
          *rh = h;
          
          return 1;
      } else {
          // logic error: bad type
          return 0;
      }
  } else {
      // checksum failure?
      return 0;      
  }

}

uint32_t seconds()
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

void export_template_message()
{
    static uint8_t template_message[] =
        {0x0,0xa,0x0,0x30,0x0,0x0,0x0,0x0,
         0xff,0xff,0xff,0xff,0x0,0x0,0x0,0x1,
         0x0,0x2,0x0,0x20,0x1,0x0,0x0,0x4,
         0x1,0x42,0x0,0x4,0x0,0x8a,0x0,0x4,
         0x80,0x2,0x0,0x4,0x0,0x0,0x8a,0xee,
         0x80,0x3,0x0,0x4,0x0,0x0,0x8a,0xee};
    
    static const size_t export_time_offset = 8;
    
    // rewrite export time in header message buffer
    uint32_t *export_time = (uint32_t *)&template_message[export_time_offset];
    *export_time = htonl(seconds());
    
    // dump buffer
    fwrite(template_message, sizeof(template_message), 1, stdout);
    fflush(stdout);
}

void export_uint32(uint32_t val)
{
    uint32_t nval = htonl(val);
    fwrite(&nval, sizeof(nval), 1, stdout);
}

void export_float(float val)
{
    uint32_t nval;
    memcpy(&nval, &val, 4);
    nval = htonl(nval);
    fwrite(&nval, sizeof(nval), 1, stdout);
}

void export_weather_message(uint32_t opid, float degc, float rh)
{
    static unsigned int weather_message_counter = 0;
    
    static uint8_t weather_message_header[] = 
        {0x0,0xa,0x0,0x24,0x0,0x0,0x0,0x0,
         0xff,0xff,0xff,0xff,0x0,0x0,0x0,0x1,
         0x1,0x0,0x0,0x14};

    static const size_t sequence_offset = 4;
    static const size_t export_time_offset = 8;

    // rewrite sequence number in weather message buffer
    uint32_t *sequence = (uint32_t *)&weather_message_header[sequence_offset];
    *sequence = htonl(weather_message_counter++);
    
    // rewrite export time in weather message buffer
    uint32_t *export_time = (uint32_t *)&weather_message_header[export_time_offset];
    *export_time = htonl(seconds());
   
    // dump header buffer
    fwrite(weather_message_header, sizeof(weather_message_header), 1, stdout);
    
    // export observation point ID
    export_uint32(opid);
    
    // export temperature and humidity
    export_float(degc);
    export_float(rh);
    
    fflush(stdout);
}

int main(int argc, char **argv)
{
  int type, dhtpin, opid;
  float degc, rh;
    
  if (!bcm2835_init())
        return 1;

  if (argc != 4) {
	fprintf(stderr, "usage: %s [11|22|2302] GPIOpin# OPid\n", argv[0]);
	fprintf(stderr, "example: %s 2302 4 - Read from an AM2302 connected to GPIO #4\n", argv[0]);
	return 2;
  }
  int type = 0;
  if (strcmp(argv[1], "11") == 0) type = DHT11;
  if (strcmp(argv[1], "22") == 0) type = DHT22;
  if (strcmp(argv[1], "2302") == 0) type = AM2302;
  if (type == 0) {
	fprintf(stderr, "Select 11, 22, 2302 as type!\n");
	return 3;
  }
  
  int dhtpin = atoi(argv[2]);

  if (dhtpin <= 0) {
	fprintf(stderr, "Please select a valid GPIO pin #\n");
	return 3;
  }

  int opid = atoi(argv[3]);

  fprintf(stderr, "Using pin #%d\n", dhtpin);
  fprintf(stderr, "Using OPid #%d\n", opid);

  // and export forever
  export_template_message();
  while (1) {
      if (readDHT(type, dhtpin, &degc, &rh)) {
          export_weather_message(opid, degc, rh);
      }
      
  }

} // main

