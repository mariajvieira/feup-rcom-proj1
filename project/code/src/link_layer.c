// Link layer protocol implementation

#include "link_layer.h"
#include "serial_port.h"
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

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
#define DATA_SIZE 1024

int alarmcount = 0;
int alarmEnabled = FALSE;
volatile int STOP = FALSE;
int ret, timeout;
int fd2;
LinkLayer info;
int frame_number;
int retransmissions = 0, timeout_v ;
typedef enum {START,FLAG_RCV, A_RCV, C_RCV, BCC_OK, DATA, STOPP} States;
States s;

struct timespec start, end;


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

            clock_gettime(CLOCK_REALTIME,&start);

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
                    timeout_v++;          
                    alarmEnabled = TRUE;
                }


                while (alarmEnabled && !STOP){
                    int bytesR_T = readByteSerialPort(receiveT); //read in serial port
                    if (bytesR_T <= 0) continue ; //if no byte was received, continue

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
                if (bytesR_R<=0) continue;

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
int stuff (unsigned char *stuffed, const unsigned char *helper, int size2) {
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
    
int destuff(unsigned char *destuffed, const unsigned char *helper, int size2) {
    int size = 0; // Inicializa o tamanho do pacote desinflado

    // Percorre os bytes do pacote de entrada, exceto o último que é o BCC2
    for (size_t i = 0; i < size2 ; i++) {
        if (helper[i] == 0x7D) { // Byte de escape encontrado
            i++; // Avança para o próximo byte
            if (helper[i] == 0x5E) {
                destuffed[size++] = 0x7E; // Desinflação do byte original
            } else if (helper[i] == 0x5D) {
                destuffed[size++] = 0x7D; // Desinflação do byte original
            }
        } else {
            destuffed[size++] = helper[i]; // Byte não escapado
        }
    }

    // Retorna o tamanho do pacote desinflado
    return size;
}



int llwrite(const unsigned char *buf, int bufSize){

    printf("\nLLWRITE() \n");
    alarmcount = 0;
    int size = 6 + bufSize;
    unsigned char frame[size];
    frame[0] = FLAG;
    frame[1] = A_T;
    frame[2] = C_I0; 
    frame[3] = frame[1] ^ frame[2]; //BCC1
    int pos=4;

    unsigned char bcc2_tx = buf[0];
    for (int i = 0 ; i < bufSize; i++){
        frame[pos] = buf[i];   //adiciona data
        pos++;     
        if (i > 0) bcc2_tx ^= buf[i];  // cria BCC2
    }

    printf("BCC2 É 0x%02x\n", bcc2_tx);

    frame[pos] = bcc2_tx;  //adiciona BCC2 à frame
    
    unsigned char stuffed[size*2];  
    size = stuff(stuffed,frame,size);
    stuffed[size] = FLAG;
    size++;

    int ack = 0;
    int rej = 0 ;
    unsigned char helper = {0};
    unsigned char read = {0};
    s = START;

    (void) signal (SIGALRM, alarmHandler);

    while (alarmcount < ret){
        s = START;
        alarmEnabled = TRUE;
        alarm(timeout);
        timeout_v++;
        ack = 0;
        rej = 0;

        STOP = FALSE;
        int bytesW = writeBytesSerialPort(stuffed, size); //send information frame
        if (bytesW==-1) {
            printf("ERROR\n");
        }
        if (bytesW==0) {
            printf("0 BYTES WRITTEN\n");
        }
        printf("Written %d bytes \n", bytesW);

        while((ack == 0 && rej == 0 && alarmEnabled) && !STOP) {
            //printf("ENTERED WHILE\n");
            int bytesR = readByteSerialPort(&read);  //receive feedback
       
            if (bytesR == 0) {
                //printf("NO BYTES READ\n");
                continue;
            }
            
            switch(s) {
                case START:
                    //printf("ENTERED SWITCH, STATE START\n");
                    if (read == FLAG) s = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (read == FLAG) s = FLAG_RCV;
                    else if (read == A_T) s = A_RCV;                        
                    else s = START;
                    break;
                case A_RCV:
                    if (read == C_REJ0 || read == C_REJ1){
                        rej=1;
                        STOP =TRUE;
                    } else if (read == C_RR0 || read == C_RR1) { 
                        ack=1;
                        alarmEnabled = FALSE;
                        s = C_RCV;
                        helper = read;
                    } else if (read == C_DISC) {
                        
                    } else if (read == FLAG) s = FLAG_RCV;
                    else s = START;
                    break;
                case C_RCV:
                    if (read == (A_T ^ helper)) s = BCC_OK;
                    else if (read == FLAG) s = FLAG_RCV;
                    else s = START;
                    break;
                case BCC_OK:
                    if (read == FLAG) {
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

        printf("REJ IS %d\n", rej);
        printf("ACK IS %d\n", ack);
        if (ack==1){
            return size;
        } 
        if (rej==1 || alarmcount > 0) {
            alarmcount++;
            retransmissions++;
        }
    }
    return -1;
}


////////////////////////////////////////////////
// LLREAD
////////////////////////////////////////////////

unsigned char generatebcc2(const unsigned char* data, int data_size){
    unsigned char bcc2 = data[0];
    //printf("BCC2 POSIÇÃO 0: 0x%02X\n", bcc2);
    for(int i = 0 ; i < data_size-1 ; i++){
        
        //printf("BCC2 POSIÇÃO %d: 0x%02X  XOR COM PACKET 0x%02X\n", i, bcc2, data[i]);
        if (i>0) bcc2 ^= data[i];
    }
    printf("RESULTADO BCC2: 0x%02X\n", bcc2);
    return bcc2;
}

int llread(unsigned char *packet)
{
    printf("Entered llread\n");
    alarmcount = 0;
    int packet_size = 0;
    unsigned char read[MAX_PAYLOAD_SIZE] = {0};
    unsigned char RR1[5] = {FLAG, A_T, C_RR1, A_T ^ C_RR1, FLAG};
    unsigned char RR0[5] = {FLAG, A_T, C_RR0, A_T ^ C_RR0, FLAG};
    unsigned char REJ1[5] = {FLAG, A_T, C_REJ1, A_T ^ C_REJ1, FLAG};
    unsigned char REJ0[5] = {FLAG, A_T, C_REJ0, A_T ^ C_REJ0, FLAG};
    unsigned char n;
    unsigned char bcc2_packet = 0;
    unsigned char bcc2 = 0;
    int size = 0;
    s = START;

    (void)signal(SIGALRM, alarmHandler);
    
    while (1) {  // Loop to retry reading packets
        alarmEnabled = TRUE;
        STOP = FALSE;
        alarm(timeout);
        size = 0;

        while (alarmEnabled && !STOP) {
            int bytesR = readByteSerialPort(read);
            if (bytesR <= 0) {
                continue; // No bytes read
            }

            unsigned char byteR = read[0];

            switch (s) {
                case START:
                    if (byteR == FLAG) s = FLAG_RCV;
                    break;
                case FLAG_RCV:
                    if (byteR == FLAG) s = FLAG_RCV;
                    else if (byteR == A_T) s = A_RCV;                        
                    else s = START;
                    break;       
                case A_RCV:
                    if (byteR == C_I0 || byteR == C_I1) {
                        s = C_RCV;
                        frame_number = (byteR == C_I0) ? 0 : 1;
                        n = byteR;
                    } else if (byteR == FLAG) s = FLAG_RCV;
                    else s = START;
                    break;    
                case C_RCV:
                    if (byteR == (A_T ^ n)) s = DATA;
                    else if (byteR == FLAG) s = FLAG_RCV;
                    else s = START;
                    break;
                case DATA:
                    if (byteR == FLAG) {  // Frame completed
                        printf("Frame detected as complete, size: %d\n", size);

                        if (size < 1) { 
                            printf("Error: Incomplete packet, size: %d\n", size);
                            return -1;
                        }

                        unsigned char destuffed[MAX_PAYLOAD_SIZE + 4];
                        packet_size = destuff(destuffed, packet, size);
                        memcpy(packet, destuffed, packet_size);

                        bcc2 = packet[packet_size - 2];
                        packet_size--;
                        packet[size] = '\0';

                        bcc2_packet = generatebcc2(packet, packet_size);

                        printf("BCC2 calculated: 0x%02X, BCC2 received: 0x%02X\n", bcc2_packet, bcc2);

                        if (bcc2 == bcc2_packet) { // Valid packet
                            printf("BCC2 matches, accepting packet, sending RR\n");
                            if (frame_number == 0) {
                                writeBytesSerialPort(RR1, 5);
                                frame_number = 1;
                                printf("RR1\n");
                            } else {
                                writeBytesSerialPort(RR0, 5);
                                frame_number = 0;
                                printf("RR0\n");
                            }
                            return packet_size; // Return valid packet size
                        } else { // Invalid packet, send REJ
                            printf("BCC2 mismatch, packet rejected, sending REJ\n");
                            if (frame_number == 0) {
                                writeBytesSerialPort(REJ0, 5);
                            } else {
                                writeBytesSerialPort(REJ1, 5);
                            }
                            break; // Exit to retry receiving a packet
                        }
                    } else {
                        packet[size++] = byteR; // Collect data bytes
                        s = DATA;
                    }
                    break;
                default:
                    break;
            }
        }

        // Handle timeout or other conditions if needed
        if (alarmcount >= ret) {
            printf("Timeout reached, retrying...\n");
            alarmcount = 0; // Reset for next attempt
        }
    }
    
    return -1; // In case of failure after all retries
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
                    timeout_v++;
                    alarmEnabled=TRUE;
                    printf("DISC SENT first time!\n");
                }
                

                while (alarmEnabled==TRUE && !STOP) {
                    int bytesR_DISC = readByteSerialPort(receiveDISC);
                    //printf("\nDISC message sent, %d bytes written\n", bytesR_DISC);
                    if (bytesR_DISC <= 0) continue;
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
                    if (bytesR_DISC<=0) continue;

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
                    if (bytesR_UA<=0) continue;

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

    clock_gettime(CLOCK_REALTIME, &end);
    double elapsed = end.tv_sec-start.tv_sec + (end.tv_nsec-start.tv_nsec)/1e9;
    printf("----Show Statistics---- \n");
    printf("Elapsed time: %f seconds \n", elapsed);
    printf("Number of retransmissions: %d\n", retransmissions);


    return clstat;
}
