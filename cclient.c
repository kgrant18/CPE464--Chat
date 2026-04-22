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
void addFlagToBuf(uint8_t *buffer, int flag);

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
				//process message
				processMessage(socketNum, buffer, handle_name); 
			}
			
			printf("$: ");
			fflush(stdout); 

		}

		// else {
		// 	//process message
		// 	uint8_t dataBuffer[MAXBUF]; 
		// 	int messageLen = 0; 
		// }
	}


	
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

	printf("message sent to client, bytes sent: %d\n", sent_bytes);

	return 0; 
}

void addFlagToBuf(uint8_t *buffer, int flag) {
	buffer[0] = flag;
}

int sendHandleName(int socketNum, char *handle_name) {
	uint8_t buffer[MAXBUF];
	int handleLen = strlen(handle_name); 

	if (handleLen > 100) {
		fprintf(stderr, "Handle name can have max 100 characters\n");
		return -1;
	}

	//make flag = 1
	addFlagToBuf(buffer, 1); 
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
