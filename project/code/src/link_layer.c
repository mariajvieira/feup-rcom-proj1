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
int ret, timeout;
int fd;

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
    fd = openSerialPort(connectionParameters.serialPort,
                       connectionParameters.baudRate);
    
    if (fd < 0 ){
        perror(connectionParameters.serialPort);
        exit(-1);
    }
    timeout = connectionParameters.timeout;
    ret = connectionParameters.nRetransmissions;

    printf("Serial port open");

switch (connectionParameters.role){
    
    case(LlTx):
    {
        (void) signal (SIGALRM, alarmHandler); //set alarm function handler

        unsigned char buf [BUF_SIZE] = {0}; 
        unsigned char pass [BUF_SIZE] = {0};
        unsigned char receive[BUF_SIZE] = {0};

        buf[0] = 0x7E;
        buf[1] = 0x03;
        buf[2] = 0x03;
        buf[3] = (buf[1])^(buf[2]);
        buf[4] = 0x7E;
        buf[5] = '\n';

        int count = 0 ; 
        int flag = 0;
        int final = 0;

        while (alarmcount < ret) {
            if (!alarmEnabled){
                int bytesW = writeBytesSerialPort (buf, 5); //serial port
                printf("%d bytes written\n", bytesW);
                alarm(timeout);            
                alarmEnabled = TRUE;
            }
            while (alarmEnabled && !STOP){
                int bytesR = readByteSerialPort (pass); //serial port
                if (bytesR == 0) continue ; //if no byte was received, continue

                if (receive[0] == 0) { //if receive[0] = 0 , new message 
                    count = 0; 
                }

                if (flag) { // reception error , will start again
                    count = 1;
                    receive [0] = 0x7E; // start flag
                    flag = 0 ;
                }

                receive [count] = pass[0]; // read buffer (.79) to receive buffer

                printf("buf[%d] = 0x%02X\n", count, (unsigned int)(receive[count]));

                switch (count) {
                    case 0 : 
                        if (receive[count] != 0x7E){
                            receive[0] = 0;
                        }
                        break;
                    
                    case 1: 
                        if (receive[count] != 0x03){
                            if (receive[count] == 0x7E){
                                flag = 1;
                            }
                            else{
                                receive[0] = 0;
                            }
                        }
                        break;

                    case 2:
                        if (receive[count] != 0x07){
                            if (receive[count] == 0x07){
                                flag = 1;
                            }
                            else {
                                receive[0] = 0;
                            }
                        }
                        break;

                    case 3:
                        if (receive[count] != (receive[1] ^ receive [2])){
                            if (receive[count] == 0x7E) {
                                flag = 1;
                            }
                            else{
                                receive[0] = 0;
                            }
                        }
                        break;

                    case 4:
                        if (receive[count] == 0x7E){
                            STOP = TRUE;
                            final = 1;
                        }
                        else{
                            if (receive[count] = 0x7E) {
                                flag = 1;
                            }
                            else{
                                receive[0] = 0;
                            }
                        }
                        break;
                    
                    default:
                        break;
                    
                }
                count++;
            }

            if (final){
                break;
            }

        }
        if (STOP) return fd ; 
        
    }

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
