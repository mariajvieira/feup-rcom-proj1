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
            control_start[packet_size++] = sizeof(fileSize); // file size L
            printf("FILE SIZE L: %d\n", (int)fileSize);
            memcpy(&control_start[packet_size], &fileSize, sizeof(fileSize)); // file size V
            packet_size += sizeof(fileSize);  //apontar para proxima posição
            printf("FILE SIZE V: ");
            for (int i = 0; i < sizeof(fileSize); i++) {
                printf("0x%02X ", control_start[packet_size - sizeof(fileSize) + i]); // Ajuste o índice
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
            for (int i = 0; i < name_length; i++) {
                printf("0x%02X ", control_start[packet_size - name_length + i]);
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

            unsigned char data_packet[MAX_PAYLOAD_SIZE + 4];
            int sequence_number = 0;
            size_t bytes_read=0;
            long bytes_remaining = fileSize;

            rewind(file);

            while (bytes_remaining > 0) {
                printf("ENTERING WHILE BYTES REMAINING\n");
                
                size_t fragment_size = (bytes_remaining > MAX_PAYLOAD_SIZE) ? MAX_PAYLOAD_SIZE : bytes_remaining;

                bytes_read = fread(data_packet + 4, 1, fragment_size, file);
                printf("Bytes read: %zu\n", bytes_read);
                if (bytes_read<fragment_size) {
                    if (feof(file)) printf("FIM DO ARQUIVO\n");
                    else if (ferror(file)) perror("erro a ler\n");
                }
                if (bytes_read <= 0) {
                    printf("Erro ao ler do ficheiro.\n");
                    fclose(file);
                    llclose(0);
                    return;
                }

                data_packet[0] = 2; // Campo C indicando "data"
                data_packet[1] = sequence_number % 100; // Número de sequência
                data_packet[2] = (bytes_read >> 8) & 0xFF; // L2 (parte alta do tamanho)
                data_packet[3] = bytes_read & 0xFF; // L1 (parte baixa do tamanho)

                // Enviar o pacote de dados
                printf("SENDING DATA PACKET...\n");
                if (llwrite(data_packet, bytes_read + 4) < 0) {
                    printf("Erro ao enviar pacote de dados.\n");
                    fclose(file);
                    llclose(0);
                    return;
                }
                sequence_number++;
                bytes_remaining-=bytes_read;
            }

            printf("All data packets sent successfully.\n");


            //CONTROL PACKET -- end of file
            printf("CONTROL END PACKET ...\n");
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



            fclose(file);
            printf("CLOSING...\n");
            llclose(0);
            break;
        }
        case (LlRx):{

            unsigned char packet[1024]={0};
            if (packet == NULL) {
                printf("Erro ao alocar memória para o pacote.\n");
                llclose(0);
                return;
            }
            int control_packet_received = 0;
            FILE *file = NULL;
            long fileSize = 0;
            char received_filename[256] = {0};

            while (1) {
                int length = llread(packet);
                if (length < 0) {
                    printf("Erro ao ler pacote.\n");
                    if (file) fclose(file);
                    llclose(0);
                    return;
                }

                unsigned char C = packet[0]; // Campo de controle

                if (C == 1 && !control_packet_received) { // Pacote de controle "start"
                    control_packet_received = 1;

                    // Processar pacote de controle de início e extrair informações
                    int index = 1;
                    while (index < length) {
                        unsigned char T = packet[index++];
                        unsigned char L = packet[index++];

                        if (T == 0) { // Tamanho do arquivo
                            memcpy(&fileSize, &packet[index], L);
                            index += L;
                        } else if (T == 1) { // Nome do arquivo
                            memcpy(received_filename, &packet[index], L);
                            received_filename[L] = '\0';
                            index += L;
                        }
                    }

                    // Abrir o arquivo para escrita
                    file = fopen(filename, "wb");
                    if (file == NULL) {
                        printf("Erro ao abrir o ficheiro %s para escrita.\n", received_filename);
                        llclose(0);
                        return;
                    }
                    printf("Pacote de controle de início recebido: ficheiro %s, tamanho %ld bytes.\n", received_filename, fileSize);

                } else if (C == 2 && control_packet_received) { // Pacote de dados
                    int sequence_number = packet[1];
                    int L2 = packet[2];
                    int L1 = packet[3];
                    int data_size = (L2 << 8) + L1;

                    // Gravar os dados no ficheiro
                    fwrite(packet + 4, 1, data_size, file);
                    printf("Pacote de dados recebido, sequência %d, tamanho %d bytes.\n", sequence_number, data_size);
                  


                } else if (C == 3 && control_packet_received) { // Pacote de controle "end"
                    printf("Pacote de controle de término recebido.\n");
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
