/******************************************************************************
* myClient.c
*
* Writen by Prof. Smith, updated Jan 2023
* Use at your own risk.  
*
*****************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdint.h>
#include <string.h>

#include "networks.h"
#include "safeUtil.h"
#include "send_recv_PDU.h"
#include "pollLib.h"

#define MAXBUF 1024
#define MAXMSG 1400
#define DEBUG_FLAG 1

void sendToServer(int socketNum);
int readFromStdin(uint8_t * buffer);
void checkArgs(int argc, char * argv[]);
int sendHandleName(int socketNum, char *handle_name);
int clientControl(int socketNum, char *handle_name);
int processMessage(int socketNum, uint8_t *buffer, char *handle_name);
int createMessagePacket(uint8_t *buffer, char *sender, char *destination, char *txt_message);
int parseMessagePacket(uint8_t *packet);
int processMulticast(int socketNum, uint8_t *buffer, char *handle_name);
int createMulticastPacket(uint8_t *buffer, int num_handles, char *sender, char *destination[], char *txt_msg);

int main(int argc, char * argv[])
{
	int socketNum = 0;         //socket descriptor
	
	checkArgs(argc, argv);

	/* set up the TCP Client socket  */
	socketNum = tcpClientSetup(argv[2], argv[3], DEBUG_FLAG);

	char *handle_name = argv[1]; 
	sendHandleName(socketNum, handle_name);

	clientControl(socketNum, handle_name);
	
	close(socketNum);
	
	return 0;
}

int clientControl(int socketNum, char *handle_name) {
	setupPollSet(); 
	//add STDIN
	addToPollSet(STDIN_FILENO);
	addToPollSet(socketNum);

	printf("$: ");
	fflush(stdout); 

	//poll indefinitely 
	while (1) {
		int poll_socket = pollCall(-1); 
		if (poll_socket < 0) {
			perror("pollCall");
			exit(-1); 
		}

		if (poll_socket == STDIN_FILENO) {
			//STDIN
			uint8_t buffer[MAXBUF];
			int sendLen = 0; 

			sendLen = readFromStdin(buffer); 
			if (buffer[0] == '%' && (buffer[1] == 'M' || buffer[1] == 'm')) {
				//send message to one handle
				processMessage(socketNum, buffer, handle_name); 
			}
			else if (buffer[0] == '%' && (buffer[1] == 'C' || buffer[1] == 'c')) {
				//send a message to multiple handles
				processMulticast(socketNum, buffer, handle_name); 
			}
			
			printf("$: ");
			fflush(stdout); 

		}

		else {
			//process message
			uint8_t dataBuffer[MAXBUF]; 
			int messageLen = 0; 

			if ((messageLen = recvPDU(socketNum, dataBuffer, MAXBUF)) < 0) {
				perror("recv call");
				exit(-1);
			}
			
			if (messageLen > 0) {
				int flag = dataBuffer[0]; 
				if (flag == 5) {
					parseMessagePacket(dataBuffer); 
				}
				else if (flag == 6) {
					printf("got something\n");
				}

				printf("$: ");
				fflush(stdout); 
			}
			else {
				printf("Server terminated\n");
				exit(1);
			}

		}
	}

	return 0; 
}

int processMulticast(int socketNum, uint8_t *buffer, char *handle_name) {
	//tokenize the buffer to extract destination handles and text message
	char *command = strtok((char *)buffer, " ");
	if (command == NULL) {
		fprintf(stderr, "format: [cmd] [num_handles] [dest_handles] [txt_message]\n");
		return -1; 
	}

	char *numHandles = strtok(NULL, " ");
	if (numHandles == NULL) {
		fprintf(stderr, "format: [cmd] [num_handles] [dest_handles] [txt_message]\n");
		return -1; 
	}

	int num_handles = atoi(numHandles);
	
	//num handles is between 2 and 9
	if (num_handles < 2 || num_handles > 9) {
		fprintf(stderr, "number of handles must be between 2 and 9\n");
		return -1; 
	} 

	//add destination handle names to buffer
	char *dest_handles[num_handles];

	int i = 0;
	for (i = 0; i < num_handles; i++) {
		dest_handles[i] = strtok(NULL, " ");
		if (dest_handles[i] == NULL) {
			fprintf(stderr, "number of handles must be between 2 and 9\n");
			return -1;
		}
	}

	//get text message
	char *txt_message = strtok(NULL, ""); 
	if (txt_message == NULL) {
		fprintf(stderr, "format: [cmd] [num_handles] [dest_handles] [txt_message]\n");
		return -1; 
	}

	//create multicast packet
	uint8_t multiPacket[MAXBUF];
	int num_bytes = 0;
	if ((num_bytes = createMulticastPacket(multiPacket, num_handles, handle_name, dest_handles, txt_message)) < 0) {
		fprintf(stderr, "multicast packet creation failed\n");
		return -1; 
	} 

	//send multicast packet
	int bytes_sent = 0; 
	if ((bytes_sent = sendPDU(socketNum, multiPacket, num_bytes)) < 0) {
		fprintf(stderr, "sendPDU failed for multicast packet\n");
		return -1; 
	}

	return bytes_sent; 
}

int createMulticastPacket(uint8_t *buffer, int num_handles, char *sender, char *destination[], char *txt_msg) {
	int index = 0; 

	//flag = 6 for multicast
	buffer[index++] = 6;

	//next byte is length of sending client's handle
	buffer[index++] = strlen(sender); 

	//add sender handle name
	memcpy(buffer + index, sender, strlen(sender)); 
	index += strlen(sender); 

	//1 byte for num of destination handles 
	buffer[index++] = num_handles; 

	//for each destination handle
	int i = 0; 
	for (i = 0; i < num_handles; i++) {
		//1 byte for length of destination handle name
		buffer[index++] = strlen(destination[i]);
		//add destination handle name
		memcpy(buffer + index, destination[i], strlen(destination[i]));
		index += strlen(destination[i]); 

	}

	//add text message
	memcpy(buffer + index, txt_msg, strlen(txt_msg) + 1);
	index += (strlen(txt_msg) + 1);

	return index; 
}

int parseMessagePacket(uint8_t *packet) {
	/**
	 * Display the text message sent from other client
	 */

	int index = 1; 

	int sender_len = packet[index++];
	
	//get sender name and make a string
	uint8_t sender_name[101];
	memcpy(sender_name, packet + index, sender_len);
	sender_name[sender_len] = '\0';
	index += sender_len;

	int one_client = packet[index++];
	if (one_client != 1) {
		fprintf(stderr, "%%M only accepts one destination\n");
		return -1; 
	}

	int dest_len = packet[index++];

	//get destination name and make a string
	uint8_t dest_name[101];
	memcpy(dest_name, packet + index, dest_len);
	dest_name[dest_len] = '\0';
	index += dest_len;

	//now get the text msg
	char *txt_message = (char *)(packet + index);
    
	//display  sent message
	printf("\n%s: %s\n", sender_name, txt_message);

	return 0; 
}

int createMessagePacket(uint8_t *buffer, char *sender, char *destination, char *txt_message) {

	int index = 0; 

	//flag = 5 for messages
	buffer[index++] = 5;
	//1 byte for length of sending clients handle
	buffer[index++] = strlen(sender); 
	//handle name of sending client
	memcpy(buffer + index, sender, strlen(sender));
	index += strlen(sender);

	//1 = only one dest
	buffer[index++] = 1;
	//1 byte for dest handle length
	buffer[index++] = strlen(destination); 
	//handle name of destination client
	memcpy(buffer + index, destination, strlen(destination)); 
	//update index after copying dest name
	index += strlen(destination);

	//add text message field
	memcpy(buffer + index, txt_message, strlen(txt_message)+1);
	index += strlen(txt_message)+1;

	//return index = number of bytes
	return index; 
}

int processMessage(int socketNum, uint8_t *buffer, char *handle_name) {
	//tokenize the buffer to extract destination handle and text message
	char *command = strtok((char *)buffer, " ");
	if (command == NULL) {
		fprintf(stderr, "format: [cmd] [dest_handle] [txt_message]\n");
		return -1; 
	}

	char *dest_handle = strtok(NULL, " ");
	if (dest_handle == NULL) {
		fprintf(stderr, "format: [cmd] [dest_handle] [txt_message]\n");
		return -1; 
	}

	char *txt_message = strtok(NULL, ""); 
	if (txt_message == NULL) {
		fprintf(stderr, "format: [cmd] [dest_handle] [txt_message]\n");
		return -1; 
	}

	if (strlen(txt_message) > 200) {
		fprintf(stderr, "max message length is 200 bytes\n");
		return -1;
	}

	uint8_t message[MAXMSG];
	int msg_pkt_bytes = 0; 
	if ((msg_pkt_bytes = createMessagePacket(message, handle_name, dest_handle, txt_message)) < 0) {
		fprintf(stderr, "error creating message packet\n");
		return -1; 
	}

	//send message PDU to client
	int sent_bytes = 0;
	if ((sent_bytes = sendPDU(socketNum, message, msg_pkt_bytes)) < 0) {
		fprintf(stderr, "sendPDU message packet\n");
		return -1; 
	}

	return sent_bytes; 
}

int sendHandleName(int socketNum, char *handle_name) {
	uint8_t buffer[MAXBUF];
	int handleLen = strlen(handle_name); 

	if (handleLen > 100) {
		fprintf(stderr, "Handle name can have max 100 characters\n");
		return -1;
	}

	//make flag = 1
	buffer[0] = 1;
	//1 byte handle length
	buffer[1] = handleLen;
	//rest is the data
	memcpy(buffer + 2, handle_name, handleLen);

	//send the data to server
	int bytes_sent = sendPDU(socketNum, buffer, handleLen + 2);
	if (bytes_sent < 0) {
		perror("send call");
		exit(-1); 
	}

	return bytes_sent; 
}






void sendToServer(int socketNum)
{
	uint8_t buffer[MAXBUF];   //data buffer
	int sendLen = 0;        //amount of data to send
	int sent = 0;            //actual amount of data sent/* get the data and send it   */
	int recvBytes = 0;
	
	sendLen = readFromStdin(buffer);
	printf("read: %s string len: %d (including null)\n", buffer, sendLen);
	
	sent =  safeSend(socketNum, buffer, sendLen, 0);
	if (sent < 0)
	{
		perror("send call");
		exit(-1);
	}

	printf("Socket:%d: Sent, Length: %d msg: %s\n", socketNum, sent, buffer);
	
	// just for debugging, recv a message from the server to prove it works.
	recvBytes = safeRecv(socketNum, buffer, MAXBUF, 0);
	printf("Socket %d: Byte recv: %d message: %s\n", socketNum, recvBytes, buffer);
	
}

int readFromStdin(uint8_t *buffer)
{
	char aChar = 0;
	int inputLen = 0;        
	
	// Important you don't input more characters than you have space 
	buffer[0] = '\0';
	while (inputLen < (MAXBUF - 1) && aChar != '\n')
	{
		aChar = getchar();
		if (aChar != '\n')
		{
			buffer[inputLen] = aChar;
			inputLen++;
		}
	}
	
	// Null terminate the string
	buffer[inputLen] = '\0';
	inputLen++;
	
	return inputLen;
}

void checkArgs(int argc, char * argv[])
{
	/* check command line arguments  */
	if (argc != 4)
	{
		printf("usage: %s [handle] [server-name] [server-port] \n", argv[0]);
		exit(1);
	}
}
