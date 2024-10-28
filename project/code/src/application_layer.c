// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <stdlib.h>


void applicationLayer(const char *serialPort, const char *role, int baudRate,
                      int nTries, int timeout, const char *filename)
{
    
    LinkLayer test;    
    strcpy(test.serialPort, serialPort);
    if (strcmp(role, "rx") == 0) {
        test.role = LlRx;
    } 
    else if(strcmp(role, "tx") == 0) {
        test.role = LlTx;
    }
    else{
        exit(-1);
    }
    test.baudRate = baudRate;
    test.nRetransmissions = nTries;
    test.timeout = timeout;

    llopen(test);
    printf("CLOSING...\n");

    switch(test.role) {
        case (LlTx):
        {


            llclose(0);
            break;
        }
        case (LlRx):
        {



            llclose(0);
            break;
        }


    }





    llclose(1);
}
