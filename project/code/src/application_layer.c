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
        return;
    }
    test.baudRate = baudRate;
    test.nRetransmissions = nTries;
    test.timeout = timeout;

    llopen(test);



    switch(test.role) {
        case (LlTx):
        {
            // VERIFICAR SE FICHEIRO EXISTE
            printf("READING FILE\n");
            FILE *file = fopen(filename, "rb");
            if (file == NULL) {
                printf("Erro: Ficheiro %s não encontrado.\n", filename);
                llclose(0);
                return;
            }
            long int fileSize = 0;
            // VER TAMANHO DO FICHEIRO
            fseek(file, 0, SEEK_END);
            fileSize = ftell(file);
            printf("Tamanho do arquivo: %ld bytes\n", fileSize);
            fseek(file, 0, SEEK_SET);

            //CONTROL PACKET -- start of file

            unsigned char control_start[MAX_PAYLOAD_SIZE]={0};
            int packet_size = 0;

            control_start[packet_size++] = 1; // Control field "start"

            // File size
            printf("ADDING FILE SIZE\n");
            control_start[packet_size++] = 0; // file size T
            control_start[packet_size++] = fileSize; // file size L
            printf("FILE SIZE L: 0x%02X\n", fileSize);
            memcpy(&control_start[packet_size], &fileSize, fileSize); // file size V
            packet_size += sizeof(fileSize);  //apontar para proxima posição
            printf("FILE SIZE V: ");
            for (int i = 0; i < sizeof(fileSize); i++) {
                printf("0x%02X ", control_start[packet_size + i]); // Ajuste o índice conforme necessário
            }
            printf("\n");


            // File name
            printf("ADDING FILE NAME\n");
            control_start[packet_size++] = 1; // file name T
            int name_length = strlen(filename); 
            control_start[packet_size++] = name_length;  // file name L
            printf("FILE NAME L: 0x%02X\n", name_length);
            memcpy(&control_start[packet_size], filename, name_length); // file name V
            packet_size += name_length; //apontar para proxima posição
            printf("FILE NAME V: ");
            for (int i = 0; i < sizeof(name_length); i++) {
                printf("0x%02X ", control_start[packet_size + i]); // Ajuste o índice conforme necessário
            }
            printf("\n");


            printf("SENDING START CONTROL PACKET\n");
            // Enviar o pacote de controlo de início
            if (llwrite(control_start, packet_size) < 0) {
                printf("Erro ao enviar pacote de controlo de início.\n");
                fclose(file);
                llclose(0);
                return;
            }
            printf("START CONTROL PACKET SENT\n");


            // DATA PACKET
            /*
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
                    return;
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
                return;
            }
            printf("Pacote de controlo de término enviado.\n");


*/
            fclose(file);
            printf("CLOSING...\n");
            llclose(0);
            break;
        }
        case (LlRx):
        {

            unsigned char buffer[MAX_PAYLOAD_SIZE]={0};
            int control_packet_received = 0;
            FILE *file = NULL;

            while (1) {
                printf("READING PACKET\n");
                int length = llread(buffer);
                if (length < 0) {
                    printf("Erro ao ler pacote.\n");
                    if (file) fclose(file);
                    llclose(0);
                    return;
                }

                // Identificar o tipo de pacote
                unsigned char C = buffer[0];
        
                if (C == 1) { // Pacote de controle "start"
                    control_packet_received = 1;
                    int index = 1; // Começar após o campo de controle "C"

                    long fileSize = 0;
                    char received_filename[128] = {0};

                    // Interpretar os parâmetros TLV
                    while (index < length) {
                        unsigned char T = buffer[index++];
                        unsigned char L = buffer[index++];
                        
                        if (T == 0) { // Tamanho do arquivo
                            memcpy(&fileSize, &buffer[index], L);
                            index += L;
                            printf("RECEIVED FILE SIZE L: 0x%02X\n", L);
                        } else if (T == 1) { // Nome do arquivo
                            memcpy(received_filename, &buffer[index], L);
                            received_filename[L] = '\0';  // Adicionar terminador de string
                            index += L;
                        } else {
                            printf("Parâmetro desconhecido T = %d\n", T);
                            index += L; // Ignorar valores desconhecidos
                        }
                    }
                    printf("Nome do arquivo recebido: %s\n", received_filename);

                    // Abrir o arquivo para escrita
                    file = fopen(received_filename, "wb");
                    
                    if (file == NULL) {
                        printf("Erro ao abrir o arquivo %s para escrita.\n", received_filename);
                        //llclose(0);
                        return;
                    }
                    printf("Pacote de controle de início recebido: arquivo %s, tamanho %ld bytes.\n", received_filename, fileSize);

                } /*else if (C == 2 && control_packet_received) { // Pacote de dados
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
                }*/
            }
            
            if (file) fclose(file);
            printf("CLOSING...\n");
            llclose(0);
            break;
        }


    }

}
