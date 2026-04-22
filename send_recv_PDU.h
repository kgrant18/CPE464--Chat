#ifndef SEND_RECV_PDU_H
#define SEND_RECV_PDU_H

int sendPDU(int clientSocket, uint8_t *dataBuffer, int lengthOfData); 
int recvPDU(int socketNumber, uint8_t *dataBuffer, int bufferSize); 


#endif