#include <stdio.h>
#include <stdlib.h> 
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "networks.h" 
#include "safeUtil.h"
#include "send_recv_PDU.h"


int sendPDU(int clientSocket, uint8_t *dataBuffer, int lengthOfData) {
    /**
     * Adds PDU length to the PDU
     */

    uint8_t new_PDU[lengthOfData + 2];

    uint16_t NO_dataLength = htons(lengthOfData + 2);

    //copy the data length into first 2 bytes of buffer
    memcpy(new_PDU, &NO_dataLength, 2); 

    //copy rest of PDU into buffer
    memcpy(new_PDU + 2, dataBuffer, lengthOfData); 

    //send the new PDU
    ssize_t bytes_sent; 
    if ((bytes_sent = send(clientSocket, new_PDU, lengthOfData + 2, 0)) < 0) {
        perror("sendPDU send failure\n");
        exit(1); 
    }

    return (bytes_sent - 2); 
}

int recvPDU(int socketNumber, uint8_t *dataBuffer, int bufferSize) {
    /**
     * Reads the application PDU and returns number of bytes received
     */
    uint16_t PDU_length; 

    //first recv reads the PDU length (first 2 bytes)
    ssize_t bytes_recv; 
    if ((bytes_recv = recv(socketNumber, &PDU_length, 2, MSG_WAITALL)) <= 0) {
        return bytes_recv; 
    }

    //convert to host order
    uint16_t ho_dataLength = ntohs(PDU_length);
    int payloadLength = ho_dataLength - 2; 

    if (payloadLength > bufferSize) {
        fprintf(stderr, "bufferSize is too small for the PDU\n");
        return -1; 
    }

    //second recv to read the original PDU 
    if ((bytes_recv = recv(socketNumber, dataBuffer, payloadLength, MSG_WAITALL)) <= 0) {
        perror("connection could be closed\n");
        return bytes_recv; 
    }

    //return number of data bytes received
    return payloadLength; 
}