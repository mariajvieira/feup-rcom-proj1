#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>
#include "alarm.c"

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256
#define FLAG 0x7E
#define A_ 0x03    
#define C_SET 0x03

typedef enum {START,FLAG_RCV, A_RCV, C_RCV, BCC_OK, STOPP} States;
States s = START; 

volatile int STOP = FALSE;
extern int alarmEnabled;
extern int alarmCount;

int main(int argc, char *argv[])
{
   // Program usage: Uses either COM1 or COM2
   const char *serialPortName = argv[1];


   if (argc < 2)
   {
       printf("Incorrect program usage\n"
              "Usage: %s <SerialPort>\n"
              "Example: %s /dev/ttyS1\n",
              argv[0],
              argv[0]);
       exit(1);
   
   }

   // Open serial port device for reading and writing, and not as controlling tty
   // because we don't want to get killed if linenoise sends CTRL-C.
   int fd = open(serialPortName, O_RDWR | O_NOCTTY);

   if (fd < 0)
   {
       perror(serialPortName);
       exit(-1);
   }

   struct termios oldtio;
   struct termios newtio;

   // Save current port settings
   if (tcgetattr(fd, &oldtio) == -1)
   {
       perror("tcgetattr");
       exit(-1);
   }

   // Clear struct for new port settings
   memset(&newtio, 0, sizeof(newtio));

   newtio.c_cflag = BAUDRATE | CS8 | CLOCAL | CREAD;
   newtio.c_iflag = IGNPAR;
   newtio.c_oflag = 0;

   // Set input mode (non-canonical, no echo,...)
   newtio.c_lflag = 0;
   newtio.c_cc[VTIME] = 0; // Inter-character timer unused
   newtio.c_cc[VMIN] = 0;  // Blocking read until 5 chars received

   // VTIME e VMIN should be changed in order to protect with a
   // timeout the reception of the following character(s)

   // Now clean the line and activate the settings for the port
   // tcflush() discards data written to the object referred to
   // by fd but not transmitted, or data received but not read,
   // depending on the value of queue_selector:
   //   TCIFLUSH - flushes data received but not read.
   tcflush(fd, TCIOFLUSH);

   // Set new port settings
   if (tcsetattr(fd, TCSANOW, &newtio) == -1)
   {
       perror("tcsetattr");
       exit(-1);
   }

   printf("New termios structure set\n");

   // Create string to send (SET)
   unsigned char buf[BUF_SIZE] = {0};


   buf[0] = 0x7E;
   buf[1] = 0x03;
   buf[2] = 0x03;
   buf[3] = (buf[1])^(buf[2]);
   buf[4] = 0x7E;

   (void)signal(SIGALRM, alarmHandler);

   int bytes = write(fd, buf, 5);
   alarm(3);
   alarmEnabled = TRUE;
   printf("%d bytes written\n", bytes);

   unsigned char bufr[BUF_SIZE] = {0};

   
   while (STOP == FALSE && alarmCount<4)
   {
       // Returns after 5 chars have been input
       bytes = read(fd, bufr, 5);
       if (bytes>0) {
           alarmEnabled=FALSE;
           printf("Received :)\n");

                   
           if (buf[0]==0X7E && buf[4]==0X7E && buf[2]==0X07 && (buf[3]==(buf[1]^buf[2]))) {
               
               STOP = TRUE;
           
               printf("UA received\n");
           }

           STOP=TRUE;

       } else if (alarmEnabled==FALSE && alarmCount<4) {

           write(fd, buf, 5);
           alarm(3);
           alarmEnabled=TRUE;
       }

   }
   if (!STOP) {
       printf("Error :(");
       exit(1);
   }

   // Restore the old port settings
   if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
   {
       perror("tcsetattr");
       exit(-1);
   }

   close(fd);

   return 0;
}