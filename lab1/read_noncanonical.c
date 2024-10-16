#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

// Baudrate settings are defined in <asm/termbits.h>, which is
// included by <termios.h>
#define BAUDRATE B38400
#define _POSIX_SOURCE 1 // POSIX compliant source

#define FALSE 0
#define TRUE 1

#define BUF_SIZE 256
#define FLAG 0x7E
#define A_SET 0x03    
#define C_SET 0x03
typedef enum {START,FLAG_RCV, A_RCV, C_RCV, BCC_OK,STOPP} States;
States s = START; 


volatile int STOP = FALSE;

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

   // Open serial port device for reading and writing and not as controlling tty
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
   newtio.c_cc[VMIN] = 5;  // Blocking read until 5 chars received

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

   // Loop for input
   unsigned char buf[BUF_SIZE + 1] = {0}; // +1: Save space for the final '\0' char

 
   while (STOP == FALSE)
   {
       // Returns after 5 chars have been input
       int bytes = read(fd, buf, 1);
       if (bytes==0) continue;
       unsigned char byte = buf[0];

        switch (s){
            case START: 
                if (byte == FLAG) {
                    s = FLAG_RCV;
                }
                break;

            case FLAG_RCV:
                if (byte == FLAG) s = FLAG_RCV;
                else if (byte == A_SET) s = A_RCV;
                else s = START;  
                break; 

            case A_RCV:
                if (byte == FLAG) s = FLAG_RCV;
                else if (byte == C_SET) s = C_RCV;
                else s = START;   
                break;

            case C_RCV:
                if (byte == FLAG) s = FLAG_RCV;
                else if (byte == (A_SET^C_SET)) s = BCC_OK;
                else s = START;  
                break;

            case BCC_OK:
                if (byte == FLAG) {
                    s = STOPP;
                    STOP = TRUE;
                    printf("MESSAGE RECEIVED!\n");

                    unsigned char ua[5] = {0};
                    ua[0] = 0x7E;
                    ua[1] = 0x01;
                    ua[2] = 0x07;
                    ua[3] = ua[1]^ua[2];
                    ua[4] = 0x7E;

                    write(fd,ua,BUF_SIZE);
                    printf("UA SENDED\n");


                } else s = START; 
                break;
        }      
   }


   // The while() cycle should be changed in order to respect the specifications
   // of the protocol indicated in the Lab guide

   // Restore the old port settings
   if (tcsetattr(fd, TCSANOW, &oldtio) == -1)
   {
       perror("tcsetattr");
       exit(-1);
   }

   close(fd);

   return 0;
}
