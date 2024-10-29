// Application layer protocol implementation

#include "application_layer.h"
#include "link_layer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


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



    switch(test.role) {
        case (LlTx):
        {
            // VERIFICAR SE FICHEIRO EXISTE
            FILE *file = fopen(filename, "rb");
            if (file == NULL) {
                printf("Erro: Ficheiro %s não encontrado.\n", filename);
                llclose(0);
                exit(-1);
            }

            // VER TAMANHO DO FICHEIRO
            fseek(file, 0, SEEK_END);
            long fileSize = ftell(file);
            fseek(file, 0, SEEK_SET);

            //CONTROL PACKET -- start of file

            unsigned char control_start[256]={0};
            int packet_size = 0;

            control_start[packet_size++] = 1; // Campo C indicando "start"
            control_start[packet_size++] = 0; // Tipo 0 para tamanho do ficheiro
            control_start[packet_size++] = sizeof(long); // Comprimento do valor (tamanho do ficheiro)
            memcpy(&control_start[packet_size], &fileSize, sizeof(long));
            packet_size += sizeof(long);

            // Nome do ficheiro
            control_start[packet_size++] = 1; // Tipo 1 para nome do ficheiro
            int name_length = strlen(filename);
            control_start[packet_size++] = name_length; // Comprimento do nome do ficheiro
            memcpy(&control_start[packet_size], filename, name_length);
            packet_size += name_length;

            // Enviar o pacote de controlo de início
            if (llwrite(control_start, packet_size) < 0) {
                printf("Erro ao enviar pacote de controlo de início.\n");
                fclose(file);
                llclose(0);
                exit(-1);
            }
            printf("Pacote de controlo de início enviado.\n");


            // DATA PACKET
            unsigned char data_packet[256];
            int sequence_number = 0;
            size_t bytes_read;
            while ((bytes_read = fread(data_packet + 4, 1, 256 - 4, file)) > 0) {
                data_packet[0] = 2; // Campo C indicando "data"
                data_packet[1] = sequence_number % 100; // Número de sequência
                data_packet[2] = (bytes_read >> 8) & 0xFF; // L2 (parte alta do tamanho)
                data_packet[3] = bytes_read & 0xFF; // L1 (parte baixa do tamanho)

                // Enviar o pacote de dados
                if (llwrite(data_packet, bytes_read + 4) < 0) {
                    printf("Erro ao enviar pacote de dados.\n");
                    fclose(file);
                    llclose(0);
                    exit(-1);
                }
                sequence_number++;
            }


            //CONTROL PACKET -- end of file
            unsigned char control_end[256];
            memcpy(control_end, control_start, packet_size);
            control_end[0] = 3; // Campo C indicando "end"
            if (llwrite(control_end, packet_size) < 0) {
                printf("Erro ao enviar pacote de controlo de término.\n");
                fclose(file);
                llclose(0);
                exit(-1);
            }
            printf("Pacote de controlo de término enviado.\n");



            fclose(file);
            printf("CLOSING...\n");
            llclose(0);
            break;
        }
        case (LlRx):
        {

            unsigned char buffer[256];
            int control_packet_received = 0;
            FILE *file = NULL;

            while (1) {
                int length = llread(buffer);
                if (length < 0) {
                    printf("Erro ao ler pacote.\n");
                    if (file) fclose(file);
                    llclose(0);
                    exit(-1);
                }

                // Identificar o tipo de pacote
                unsigned char C = buffer[0];
                
                if (C == 1) { // Pacote de controlo "start"
                    control_packet_received = 1;

                    // Extrair o tamanho do ficheiro
                    long fileSize = 0;
                    int index = 2; // Começar após C e T do tamanho
                    int size_length = buffer[index++];
                    memcpy(&fileSize, &buffer[index], size_length);
                    index += size_length;

                    // Extrair o nome do ficheiro
                    int name_length = buffer[index + 1];
                    char received_filename[128];
                    memcpy(received_filename, &buffer[index + 2], name_length);
                    received_filename[name_length] = '\0';

                    // Abrir o ficheiro para escrita
                    file = fopen(received_filename, "wb");
                    if (file == NULL) {
                        printf("Erro ao abrir o ficheiro %s para escrita.\n", received_filename);
                        llclose(0);
                        exit(-1);
                    }
                    printf("Pacote de controlo de início recebido: ficheiro %s, tamanho %ld bytes.\n", received_filename, fileSize);

                } else if (C == 2 && control_packet_received) { // Pacote de dados
                    int sequence_number = buffer[1];
                    int L2 = buffer[2];
                    int L1 = buffer[3];
                    int data_size = (L2 << 8) + L1;

                    // Gravar os dados no ficheiro
                    fwrite(buffer + 4, 1, data_size, file);
                    printf("Pacote de dados recebido, sequência %d, tamanho %d bytes.\n", sequence_number, data_size);

                } else if (C == 3 && control_packet_received) { // Pacote de controlo "end"
                    printf("Pacote de controlo de término recebido.\n");
                    break;
                }
            }
            
            if (file) fclose(file);
            printf("CLOSING...\n");
            llclose(0);
            break;
        }


    }

}
