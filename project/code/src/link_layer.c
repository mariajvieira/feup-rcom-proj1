// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#define BUF_SIZE 5 
#define FLAG 0x7E
#define A_T 0x03
#define A_R 0x01
#define C_SET 0X03
#define C_UA 0X07
#define C_RR0 0xAA
#define C_RR1 0xAB
#define C_REJ0 0x54
#define C_DISC 0x0B

int alarmcount = 0;
int alarmEnabled = FALSE;
volatile int STOP = FALSE;

void alarmHandler(int signal)
{
   alarmEnabled = FALSE;
   alarmcount++;

   printf("Alarm #%d\n", alarmcount);
}

// MISC
#define _POSIX_SOURCE 1 // POSIX compliant source

////////////////////////////////////////////////
// LLOPEN
////////////////////////////////////////////////
int llopen(LinkLayer connectionParameters)
{
    if (openSerialPort(connectionParameters.serialPort,
                       connectionParameters.baudRate) < 0)
    {
        return -1;
    }

   unsigned char buf[BUF_SIZE] = {0};

   buf[0] = 0x7E;
   buf[1] = 0x03;
   buf[2] = 0x03;
   buf[3] = (buf[1])^(buf[2]);
   buf[4] = 0x7E;

   (void)signal(SIGALRM, alarmHandler);

   int bytes = writeBytesSerialPort(buf, 5);
   alarm(3);
   alarmEnabled = TRUE;
   printf("%d bytes written\n", bytes);

   unsigned char bufr[BUF_SIZE] = {0};

   
   while (STOP == FALSE && alarmcount<4)
   {
       // Returns after 5 chars have been input
       bytes = readByteSerialPort(bufr);
       if (bytes>0) {
           alarmEnabled=FALSE;
           printf("Received :)\n");

                   
           if (buf[0]==0X7E && buf[4]==0X7E && buf[2]==0X07 && (buf[3]==(buf[1]^buf[2]))) {
               
               STOP = TRUE;
           
               printf("UA received\n");
           }

           STOP=TRUE;

       } else if (alarmEnabled==FALSE && alarmcount<4) {

           writeBytesSerialPort(buf, 5);
           alarm(3);
           alarmEnabled=TRUE;
       }

   }
   if (!STOP) {
       printf("Error :(");
       exit(1);
   }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////
int llwrite(const unsigned char *buf, int bufSize)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    // TODO

    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    // TODO

    int clstat = closeSerialPort();
    return clstat;
}
