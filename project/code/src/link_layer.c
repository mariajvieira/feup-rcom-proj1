// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#define BUF_SIZE 5
#define FLAG 0x7E
#define A_T 0x03
#define A_R 0x01
#define C_SET 0X03
#define C_UA 0X07
#define C_RR0 0xAA
#define C_RR1 0xAB
#define C_REJ0 0x54
#define C_REJ1 0X55
#define C_DISC 0x0B
#define ESC 0x7D

int alarmcount = 0;
int alarmEnabled = FALSE;
volatile int STOP = FALSE;
int ret, timeout;
int fd2;
LinkLayer info;

typedef enum {START,FLAG_RCV, A_RCV, C_RCV, BCC_OK,STOPP} States;
States s;

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
    info=connectionParameters;
    fd2 = openSerialPort(connectionParameters.serialPort,
                       connectionParameters.baudRate);
    
    if (fd2 < 0 ){
        perror(connectionParameters.serialPort);
        exit(-1);
    }

    timeout = connectionParameters.timeout;
    ret = connectionParameters.nRetransmissions;

    printf("Serial port open\n") ;
    int final = 0;

    switch (connectionParameters.role) {
        
        case(LlTx):
        {
            s=START;
            (void) signal (SIGALRM, alarmHandler); //set alarm function handler

            unsigned char buf [BUF_SIZE] = {0};
            unsigned char receiveT[BUF_SIZE] = {0};

            buf[0] = FLAG;
            buf[1] = A_T;
            buf[2] = C_SET;
            buf[3] = (buf[1])^(buf[2]);
            buf[4] = FLAG;
    
            while (alarmcount < ret) {
                if (!alarmEnabled){
                    int bytesW = writeBytesSerialPort(buf, 5); //send SET in serial port
                    printf("SET SENT: %d bytes written\n", bytesW);
                    alarm(timeout);            
                    alarmEnabled = TRUE;
                }


                while (alarmEnabled && !STOP){
                    int bytesR_T = readByteSerialPort(receiveT); //read in serial port
                    if (bytesR_T == 0) continue ; //if no byte was received, continue

                    unsigned char byteT = receiveT[0];
                    switch (s) 
                    {
                        case START: 
                            if (byteT==FLAG) s=FLAG_RCV;
                            break;
                        
                        case FLAG_RCV:
                            if (byteT == FLAG) s = FLAG_RCV;
                            else if (byteT == A_R) s = A_RCV;
                            else s = START;  
                            break; 

                        case A_RCV:
                            if (byteT == FLAG) s = FLAG_RCV;
                            else if (byteT == C_UA) s = C_RCV;
                            else s = START;   
                            break;

                        case C_RCV:
                            if (byteT == FLAG) s = FLAG_RCV;
                            else if (byteT == (A_R^C_UA)) s = BCC_OK;
                            else s = START;  
                            break;

                        case BCC_OK:
                            if (byteT == FLAG) {
                                s = STOPP;
                                final=1;
                                STOP = TRUE;
                                printf("UA RECEIVED!\n");
                            } else {
                                s=START;
                            }
                            break;
                        default:
                        s=START;
                        break;
                    }
                
                    if (final){
                        STOP=TRUE;
                    }

                }
                if (STOP) {
                    break;
                }
                alarmcount++;
            
            }
            if (STOP) return 1;
            else return 0;
        }
        case (LlRx):
        {
            s=START;
            final=0;
            unsigned char receiveR[BUF_SIZE] = {0};
            unsigned char ua[BUF_SIZE] = {FLAG, A_R, C_UA, A_R ^ C_UA, FLAG};
            while (!STOP) {
                int bytesR_R = readByteSerialPort(receiveR);
                if (bytesR_R==0) continue;

                unsigned char byteR = receiveR[0]; 
                switch (s) 
                {
                    case START: 
                        if (byteR==FLAG) s=FLAG_RCV;
                        break;
                    
                    case FLAG_RCV:
                        if (byteR == FLAG) s = FLAG_RCV;
                        else if (byteR == A_T) s = A_RCV;
                        else s = START;  
                        break; 

                    case A_RCV:
                        if (byteR == FLAG) s = FLAG_RCV;
                        else if (byteR == C_SET) s = C_RCV;
                        else s = START;   
                        break;

                    case C_RCV:
                        if (byteR == FLAG) s = FLAG_RCV;
                        else if (byteR == (A_T^C_SET)) s = BCC_OK;
                        else s = START;  
                        break;

                    case BCC_OK:
                        if (byteR == FLAG) {
                            s = STOPP;
                            final=1;
                            STOP = TRUE;
                            printf("SET RECEIVED!\n");
                        } else {
                            s=START;
                        }
                        break;

                    default:
                        s=START;
                        break;

                } 
            
            }
            if (STOP) {
                printf("SET RECEIVED, SENDING UA\n");
                int bytesW_R = writeBytesSerialPort(ua, BUF_SIZE);
                printf("UA SENT: %d BYTES WRITTEN\n", bytesW_R);
                return 1;
            } else return 0;
        }
    }

    return 1;
}

////////////////////////////////////////////////
// LLWRITE
////////////////////////////////////////////////

    //byte stuffing 
    int stuff (unsigned char *stuffed, const unsigned char *helper, int size2){
        int size = 0 ;
        stuffed[size ++] = helper[0];

        for (int i = 1 ; i < size2 ; i++){
            if (helper[i] == FLAG || helper[i] == ESC){
                stuffed[size++] = ESC;
                stuffed[size++] = helper[i]^0x20;
            }
            else{
                stuffed[size++] = helper[i];
            } 
        }
        return size;
    }
    
    int destuff(unsigned char *destuffed, const unsigned char *helper, int size2){
        int size = 0 ;
        destuffed[size++] = helper[0];

        for (int i = 1; i < size2 ; i++){
            if (helper[i] == ESC){
                destuffed[size++] = helper[i+1]^0x20;
                i++;
            }
            else{
                destuffed[size++] = helper[i];
            }
        }
        return size;
    }
    int llwrite(const unsigned char *buf, int bufSize){

    printf("Entered llwrite %d\n",ret);
    alarmcount = 0;
    int size = 6 + bufSize;
    unsigned char dm[size];
    dm[0] = FLAG;
    dm[1] = A_T;
    dm[2] = (0 << 6); 
    dm[3] = dm[1] ^ dm[2]; //BCC1

    unsigned char BCC2 = buf[0];
    for (int i = 0 ; i < bufSize; i++){
        dm [i + 4] = buf[i];
        if (i > 0) BCC2 ^= buf[i];
    }

    dm[bufSize + 4] = BCC2;
    unsigned char stuffed[size * 2];
    size = stuff(stuffed,dm,size);
    stuffed[size] = FLAG;
    size++;

    int count = 0 ; 
    int acc = 0;
    int rej = 0 ;
    unsigned char helper = {0};
    unsigned char read = {0};

    (void) signal (SIGALRM, alarmHandler);

    while (alarmcount < ret){
        alarmEnabled = TRUE;
        alarm(timeout);
        acc = 0;
        rej = 0;

        while (acc = 0 & rej == 0 && alarmEnabled){
            STOP = FALSE;
            int bytesW = writeBytesSerialPort(stuffed, size); 
            printf("Written %d bytes \n", bytesW);

                while(alarmEnabled && (!STOP)){
                    int bytesR = readByteSerialPort(&read);
                    if (bytesR == 0) continue;

                    switch(s){
                        case START:
                        if (read == FLAG) {
                            s = FLAG_RCV;
                            }
                        break;
                        case FLAG_RCV:
                        if (read == A_R) {
                            s = A_RCV;
                            } 
                        else if (read == FLAG) {
                        break;
                            } 
                        else {
                            s = START;
                            }
                        break;
                        case A_RCV:
                        if (read == ((0 << 7) | C_REJ0) || read == ((1 << 7) | C_REJ1) || 
                            read == ((0 << 7) | A_R) || read == ((1 << 7) | A_R) || read == C_DISC) {
                            s = C_RCV;
                            helper = read;
                            } 
                        else if (read == FLAG) {
                            s = FLAG_RCV;
                            } 
                        else {
                            s = START;
                            }
                        break;
                        case C_RCV:
                        if (read == (A_T ^ helper)) {
                            s = BCC_OK;
                            } 
                        else if (read == FLAG) {
                            s = FLAG_RCV;
                            } 
                        else {
                            s = START;
                            }
                        break;
                        case BCC_OK:
                        if (read == FLAG) {
                            s = STOPP;
                            STOP = TRUE;
                            } 
                        else {
                            s = START;
                            }
                        break;
                        default:
                            s = START;
                        break;
                            }
                }
        
        }
    }
    return 0;
}

////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////
int llread(unsigned char *packet)
{
    printf("Entered llread\n");
    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    alarmEnabled=FALSE;
    STOP = FALSE;
    int final=0;
    unsigned char DISC[5] = {FLAG, A_T, C_DISC, A_T^C_DISC, FLAG};
    unsigned char ua[BUF_SIZE] = {FLAG, A_R, C_UA, A_R ^ C_UA, FLAG};

    printf("role: %d\n", info.role);

    switch (info.role) {
        
        case(LlTx):
        {
            s=START;
            final=0;
            alarmcount=0;
            
            (void) signal (SIGALRM, alarmHandler); 
            unsigned char receiveDISC[BUF_SIZE] = {0};
            while (alarmcount<ret && !STOP) {
                if (!alarmEnabled) {
                    int bytesW_DISC = writeBytesSerialPort(DISC, 5);  //SEND DISC
                    printf("Written %d bytes \n", bytesW_DISC);
                    alarm(timeout);
                    alarmEnabled=TRUE;
                    printf("DISC SENT first time!\n");
                }
                

                while (alarmEnabled==TRUE && !STOP) {
                int bytesR_DISC = readByteSerialPort(receiveDISC);
                //printf("\nDISC message sent, %d bytes written\n", bytesR_DISC);
                    if (bytesR_DISC == 0) continue;
                    unsigned char byteR_DISC = receiveDISC[0]; 

                    switch (s) 
                    {
                        case START: 
                            if (byteR_DISC==FLAG) s=FLAG_RCV;
                            break;
                        
                        case FLAG_RCV:
                            if (byteR_DISC == FLAG) s = FLAG_RCV;
                            else if (byteR_DISC == A_T) s = A_RCV;
                            else s = START;  
                            break; 

                        case A_RCV:
                            if (byteR_DISC == FLAG) s = FLAG_RCV;
                            else if (byteR_DISC == C_DISC) s = C_RCV;
                            else s = START;   
                            break;

                        case C_RCV:
                            if (byteR_DISC == FLAG) s = FLAG_RCV;
                            else if (byteR_DISC == (A_T^C_DISC)) s = BCC_OK;
                            else s = START;  
                            break;

                        case BCC_OK:
                            if (byteR_DISC == FLAG) {
                                s = STOPP;
                                final=1;
                                STOP = TRUE;
                                printf("DISC RECEIVED!\n");
                            } else {
                                s=START;
                                break;
                            }
                            break;

                        default:
                            s=START;
                            break;

                    }
                        
                }
                if (STOP) {
                    printf("DISC RECEIVED, SENDING UA\n");
                    int bytesW_UA = writeBytesSerialPort(ua, BUF_SIZE);
                    alarmEnabled = FALSE;
                    printf("UA SENT: %d BYTES WRITTEN\n", bytesW_UA);                  
                } else return -1;

                alarmcount++;
            }
        }
        case (LlRx):
        {
            s=START;
            final=0;
            alarmcount=0;
     
            (void) signal (SIGALRM, alarmHandler); 

            unsigned char receiveDISC[BUF_SIZE] = {0};
            unsigned char receiveUA[BUF_SIZE] = {0};

            while (alarmcount<ret && !STOP) {
                while (alarmEnabled==FALSE && !STOP) {
                    int bytesR_DISC = readByteSerialPort(receiveDISC);
                    if (bytesR_DISC==0) continue;

                    unsigned char byteR_DISC = receiveDISC[0]; 

                    switch (s) 
                    {
                        case START: 
                            if (byteR_DISC==FLAG) s=FLAG_RCV;
                            break;
                        
                        case FLAG_RCV:
                            if (byteR_DISC == FLAG) s = FLAG_RCV;
                            else if (byteR_DISC == A_T) s = A_RCV;
                            else s = START;  
                            break; 

                        case A_RCV:
                            if (byteR_DISC == FLAG) s = FLAG_RCV;
                            else if (byteR_DISC == C_DISC) s = C_RCV;
                            else s = START;   
                            break;

                        case C_RCV:
                            if (byteR_DISC == FLAG) s = FLAG_RCV;
                            else if (byteR_DISC == (A_T^C_DISC)) s = BCC_OK;
                            else s = START;  
                            break;

                        case BCC_OK:
                            if (byteR_DISC == FLAG) {
                                s = STOPP;
                                final=1;
                                STOP = TRUE;
                                printf("DISC RECEIVED!\n");
                            } else {
                                s=START;
                            }
                            break;

                        default:
                            s=START;
                            break;

                    }     
                }
                if (STOP) {
                    printf("DISC RECEIVED, SENDING DISC\n");
                    int bytesW_DISC = writeBytesSerialPort(DISC, 5);
                    printf("DISC SENT: %d BYTES WRITTEN\n", bytesW_DISC);  
                    break;             
                } else return -1;
                if (alarmEnabled == FALSE){
                    alarmEnabled = TRUE;
                }  
                alarmcount++;
            }
            alarmcount = 0;
            s = START;
            STOP = FALSE;

            while (alarmcount<ret && !STOP) {
                while (alarmEnabled==FALSE && !STOP) {
                    int bytesR_UA = readByteSerialPort(receiveUA);
                    if (bytesR_UA==0) continue;

                    unsigned char byteR_UA = receiveUA[0]; 

                    switch (s) 
                    {
                        case START: 
                            if (byteR_UA==FLAG) s=FLAG_RCV;
                            break;
                        
                        case FLAG_RCV:
                            if (byteR_UA == FLAG) s = FLAG_RCV;
                            else if (byteR_UA == A_R) s = A_RCV;
                            else s = START;  
                            break; 

                        case A_RCV:
                            if (byteR_UA == FLAG) s = FLAG_RCV;
                            else if (byteR_UA == C_UA) s = C_RCV;
                            else s = START;   
                            break;

                        case C_RCV:
                            if (byteR_UA== FLAG) s = FLAG_RCV;
                            else if (byteR_UA == (A_R^C_UA)) s = BCC_OK;
                            else s = START;  
                            break;

                        case BCC_OK:
                            if (byteR_UA == FLAG) {
                                s = STOPP;
                                final=1;
                                STOP = TRUE;
                            } else {
                                s=START;
                            }
                            break;

                        default:
                            s=START;
                            break;

                    }
                        
                }
                if (STOP) {
                    printf("UA RECEIVED\n");    
                    return 0;             
                } else return 1;
                if (alarmEnabled == FALSE){
                    alarmEnabled = TRUE;
                } 
                alarmcount++;
            }

        }


    }

    int clstat = closeSerialPort();
    return clstat;
}

