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
#define C_I0 0x00
#define C_I1 0x80
#define ESC 0x7D

int alarmcount = 0;
int alarmEnabled = FALSE;
volatile int STOP = FALSE;
int ret, timeout;
int fd2;
LinkLayer info;
int frame_number=1;

typedef enum {START,FLAG_RCV, A_RCV, C_RCV, BCC_OK, DATA, STOPP} States;
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
                        case STOPP:
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
    stuffed[size] = helper[0];
    size++;

    for (int i = 1 ; i < size2 ; i++){
        if (helper[i] == FLAG || helper[i] == ESC){
            stuffed[size] = ESC;
            size++;
            stuffed[size] = helper[i]^0x20;
            size++;
        } else {
            stuffed[size] = helper[i];
            size++;
        } 
    }
    return size;
}
    
int destuff(unsigned char *destuffed, const unsigned char *helper, int size2){
    int size = 0 ;
    int i=0;

    destuffed[size] = helper[0];
    size++;

    for (int i = 1; i < size2 ; i++){
        if (helper[i]==ESC) {
            if (i+1 <size2) {
                if (helper[i+1] == (FLAG^0x20)){
                    destuffed[size] = FLAG;
                    size++;
                } else if (helper[i+1] == (ESC^0x20)){
                    destuffed[size] = ESC;
                    size++;
                }
                i++;
            }
        } else {
            destuffed[size]=helper[i];
            size++;
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
    dm[2] = (0 << 6); //information frame 0
    dm[3] = dm[1] ^ dm[2]; //BCC1

    unsigned char BCC2 = buf[0];
    for (int i = 0 ; i < bufSize; i++){
        dm [i + 4] = buf[i];        //adiciona data
        if (i > 0) BCC2 ^= buf[i];  // cria BCC2
    }

    dm[bufSize + 4] = BCC2;  //adiciona BCC2 à frame
    
    unsigned char stuffed[size * 2];  
    size = stuff(stuffed,dm,size);
    stuffed[size] = FLAG;
    size++;

    int count = 0 ; 
    int acc = 0;
    int rej = 0 ;
    unsigned char helper = {0};
    unsigned char read = {0};
    s = START;

    (void) signal (SIGALRM, alarmHandler);

    while (alarmcount < ret){
        alarmEnabled = TRUE;
        alarm(timeout);
        acc = 0;
        rej = 0;

        while (acc = 0 & rej == 0 && alarmEnabled){
            STOP = FALSE;
            int bytesW = writeBytesSerialPort(stuffed, size); //send information frame
            printf("Written %d bytes \n", bytesW);

            while(alarmEnabled && !STOP) {
                int bytesR = readByteSerialPort(&read);  //receive feedback
                if (bytesR == 0) continue;

                switch(s) {
                    case START:
                        if (read == FLAG) s = FLAG_RCV;
                        break;
                    case FLAG_RCV:
                        if (read == FLAG) s = FLAG_RCV;
                        else if (read == A_R) s = A_RCV;                        
                        else s = START;
                        break;
                    case A_RCV:
                        if (read == C_REJ0 || read == C_REJ1 || 
                            read == C_RR0 || read == C_RR1 || read == C_DISC) {
                            s = C_RCV;
                            helper = read;
                        } else if (read == FLAG) s = FLAG_RCV;
                        else s = START;
                        break;
                    case C_RCV:
                        if (read == (A_R ^ helper)) s = BCC_OK;
                        else if (read == FLAG) s = FLAG_RCV;
                        else s = START;
                        break;
                    case BCC_OK:
                        if (read == FLAG) {
                            s = STOPP;
                            STOP = TRUE;
                        } else {
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
    alarmcount = 0;
    int packet_size=0;
    unsigned char read = {0};
    unsigned char b[MAX_PAYLOAD_SIZE];
    unsigned char RR1[5]={FLAG, A_T, C_RR1, A_T^C_RR1, FLAG};
    unsigned char RR0[5]={FLAG, A_T, C_RR0, A_T^C_RR0, FLAG};;
    unsigned char REJ1[5]={FLAG, A_T, C_REJ1, A_T^C_REJ1, FLAG};;
    unsigned char REJ0[5]={FLAG, A_T, C_REJ0, A_T^C_REJ0, FLAG};
    unsigned char n;
    unsigned char bcc2=0;
    int size=0;
    s = START;


    (void) signal (SIGALRM, alarmHandler);

    while (alarmcount < ret){
        alarmEnabled = TRUE;
        alarm(timeout);

        while (alarmEnabled && !STOP){
            STOP = FALSE;
            int bytesR = readByteSerialPort(&read);
            if (bytesR == 0) continue;
            printf("Read %d bytes \n", bytesR);

 // START, FLAG, A, CONTROL, BCC1, DATA, BCC2, FLAG
            switch (s) {
                case START:
                    if (read == FLAG) s = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (read == FLAG) s = FLAG_RCV;
                    else if (read == A_T) s = A_RCV;                        
                    else s = START;
                    break;       
                case A_RCV:
                    if (read == C_I0 || read == C_I1) {
                        s = C_RCV;
                        frame_number = read;
                    } else if (read == FLAG) s = FLAG_RCV;
                    else s = START;
                    break;    
                case C_RCV:
                    if (read == (A_T ^ n)) s = DATA;
                    else if (read == FLAG) s = FLAG_RCV;
                    else s = START;
                    break;
                case DATA:

                    if (read == FLAG) {  //frame acabou
                        bcc2 = b[size-1];
                        size--;

                        unsigned char bcc2_ = b[0];
                        for (int i=1; i<size; i++) {
                            bcc2_ = bcc2_ ^ b[i];
                        }

                        if (bcc2 = bcc2_) { //aceite
                            s=STOPP;
                            if (frame_number == 0) {
                                //mandar RR1
                                int bytesW = writeBytesSerialPort(RR1, 5); 
                                printf("\nRR1 SENT\n");
                                frame_number=1;

                            } else if (frame_number==1) {
                                //mandar RR0
                                int bytesW = writeBytesSerialPort(RR0, 5);
                                printf("\nRR0 SENT\n");
                                frame_number=0; 
                            }
                            
                        } else {  //rejeitado
                            if (frame_number == 0) {
                                //mandar RJ0
                                int bytesW = writeBytesSerialPort(REJ0, 5); 
                                printf("\nREJ0 SENT\n");
                            } else if (frame_number==1) {
                                //mandar RJ1
                                int bytesW = writeBytesSerialPort(REJ1, 5); 
                                printf("\nREJ1 SENT\n");
                            }
                            return -1;

                        }
                    } else if (read == ESC) { //fazer destuff e guardar no packet
                        s = DATA;
                        unsigned char destuffed[MAX_PAYLOAD_SIZE];
                        packet_size = destuff(destuffed, b, size);
                        memcpy(packet, destuffed, packet_size);
                        return packet_size;

                    } 
                    break;
                

            }
        }

    }






    return 0;
}

////////////////////////////////////////////////
// LLCLOSE
////////////////////////////////////////////////
int llclose(int showStatistics)
{
    alarmEnabled=FALSE;
    STOP = FALSE;
    unsigned char DISC[5] = {FLAG, A_T, C_DISC, A_T^C_DISC, FLAG};
    unsigned char ua[BUF_SIZE] = {FLAG, A_R, C_UA, A_R ^ C_UA, FLAG};

    printf("role: %d\n", info.role);

    switch (info.role) {
        
        case(LlTx):
        {
            s=START;
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
                                STOP = TRUE;
                                printf("DISC RECEIVED!\n");
                            } else {
                                s=START;
                            }
                            break;
                        case STOPP:
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
                } else {
                    alarmcount++;
                    if (alarmcount >= ret) return -1;
                }
            }
            break;
        }
        case (LlRx):
        {
            s=START;
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
                                STOP = TRUE;
                                printf("DISC RECEIVED!\n");
                            } else {
                                s=START;
                            }
                            break;
                        case STOPP:
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
                } else {
                    alarmcount++;
                    if (alarmcount >= ret) return -1;
                    if (!alarmEnabled) alarmEnabled = TRUE;
                }
                alarmcount++;
            }
            alarmcount = 0;
            s = START;
            STOP = FALSE;

            while (alarmcount<ret && !STOP) {
                while (!alarmEnabled && !STOP) {
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
                                STOP = TRUE;
                            } else {
                                s=START;
                            }
                            break;
                        case STOPP:
                            break;

                        default:
                            s=START;
                            break;

                    }
                        
                }
                if (STOP) {
                    printf("UA RECEIVED\n");    
                    alarm(0);
                    alarmEnabled = FALSE;
                    return 0;             
                } else {
                    alarmcount++;
                    if (alarmcount >= ret) return 1;
                    if (!alarmEnabled) alarmEnabled = TRUE;
                }
            }
            break;

        }

    }

    int clstat = closeSerialPort();
    return clstat;
}

