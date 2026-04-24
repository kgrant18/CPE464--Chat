/******************************************************************************
* cclient.c
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
#define PACKET_MAX 1400
#define MAX_HANDLE_LEN 101
#define DEBUG_FLAG 1

int readFromStdin(uint8_t * buffer);
void checkArgs(int argc, char * argv[]);
int sendHandleName(int socketNum, char *handle_name);
int blockUntilFlagReceived(int socketNum, char *handle_name);
int clientControl(int socketNum, char *handle_name);
int processMessage(int socketNum, uint8_t *buffer, char *handle_name);
int createMessagePacket(uint8_t *buffer, char *sender, char *destination, char *txt_message);
int parseMessagePacket(uint8_t *packet);
int processMulticast(int socketNum, uint8_t *buffer, char *handle_name);
int createMulticastPacket(uint8_t *buffer, int num_handles, char *sender, char *destination[], char *txt_msg);
int parseMulticastPacket(uint8_t *packet);
int processListHandles(int socketNum, uint8_t *buffer, char *handle_name);
void printNumHandles(uint8_t *packet);
void printHandleNames(uint8_t *packet);
int processBroadcast(int socketNum, uint8_t *buffer, char *handle_name);
int createBroadcastPacket(uint8_t *bPacket, char *sender, char *message);
void parseBroadcastPacket(uint8_t *packet);
void printDollar(void);

int main(int argc, char * argv[])
{
	int socketNum = 0;         //socket descriptor
	
	checkArgs(argc, argv);

	/* set up the TCP Client socket  */
	socketNum = tcpClientSetup(argv[2], argv[3], DEBUG_FLAG);

	char *handle_name = argv[1]; 
	if ((sendHandleName(socketNum, handle_name)) < 0) {
		return -1;
	}

	if (blockUntilFlagReceived(socketNum, handle_name) < 0) {
		return -1; 
	}

	clientControl(socketNum, handle_name);
	
	close(socketNum);
	
	return 0;
}

int blockUntilFlagReceived(int socketNum, char *handle_name) {
	uint8_t buffer[MAXBUF];

	int bytes_recv = recvPDU(socketNum, buffer, sizeof(buffer));
	if (bytes_recv < 0) {
		fprintf(stderr, "flag2/3 recvPDU failed\n");
		return -1; 
	}
	else if (bytes_recv == 0) {
		printf("Server terminated\n");
		exit(1); 
	}

	int flag = buffer[0]; 

	if (flag == 2) {
		return 0;
	}

	else if (flag == 3) {
		printf("Handle already in use: %s\n", handle_name);
		return -1; 
	}
	else {
		fprintf(stderr, "Expected flag 2 or flag 3\n");
		return -1; 
	}
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
			int print_dollar = 1; 
			
			uint8_t buffer[MAXBUF];

			readFromStdin(buffer); 
			if (buffer[0] == '%' && (buffer[1] == 'M' || buffer[1] == 'm')) {
				//send message to one handle
				processMessage(socketNum, buffer, handle_name); 
			}
			else if (buffer[0] == '%' && (buffer[1] == 'C' || buffer[1] == 'c')) {
				//send a message to multiple handles
				processMulticast(socketNum, buffer, handle_name); 
			}
			else if (buffer[0] == '%' && (buffer[1] == 'L' || buffer[1] == 'l')) {
				processListHandles(socketNum, buffer, handle_name);
				print_dollar = 0;
			}
			else if (buffer[0] == '%' && (buffer[1] == 'B' || buffer[1] == 'b')) {
				processBroadcast(socketNum, buffer, handle_name); 
			}
			
			if (print_dollar == 1) {
				printDollar(); 
			}
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
				
				if (flag == 4) {
					//broadcast 
					parseBroadcastPacket(dataBuffer);
					printDollar(); 
				}

				else if (flag == 5) {
					//regular message
					parseMessagePacket(dataBuffer); 
					printDollar(); 
				}
				else if (flag == 6) {
					//multicast
					parseMulticastPacket(dataBuffer);
					printDollar(); 
				}
				else if (flag == 7) {
					//error paccket
					printf("\nAn unknown handle was entered\n");
					printDollar(); 
				}
				else if (flag == 11) {
					//received num handles for %l
					printNumHandles(dataBuffer);
				}
				else if (flag == 12) {
					//print each handle hame on %l
					printHandleNames(dataBuffer);
				}
				else if (flag == 13) {
					//%l is finished
					printDollar(); 
				}	
			}

			else {
				printf("Server terminated\n");
				exit(1);
			}
		}
	}

	return 0; 
}

void parseBroadcastPacket(uint8_t *packet) {
	/**
	 * Display the broadcast message
	 */

	 //bypass type
	 int index = 1;
	 
	 //sender length
	 int sender_len = packet[index++];

	 //sender name
	 uint8_t sender_name[MAX_HANDLE_LEN];
	 memcpy(sender_name, packet + index, sender_len);
	 sender_name[sender_len] = '\0';
	 index += sender_len; 

	 char *txt_message = (char *)(packet + index);
	 
	 printf("\n%s: %s\n", sender_name, txt_message);
	
}

int processBroadcast(int socketNum, uint8_t *buffer, char *handle_name) {
	//tokenize the buffer to extract message
	char *command = strtok((char *)buffer, " ");
	if (command == NULL) {
		fprintf(stderr, "format %%B [message]\n");
		return -1; 
	}

	char *message = strtok(NULL, "");
	if (message == NULL) {
		fprintf(stderr, "format %%B [message]\n");
		return -1; 
	}

	uint8_t bPacket[PACKET_MAX];
	int num_bytes = 0; 
	if ((num_bytes = createBroadcastPacket(bPacket, handle_name, message)) < 0) {
		fprintf(stderr, "failed to make broadcast packet\n");
		return -1;
	} 

	int sent_bytes = sendPDU(socketNum, bPacket, num_bytes); 
	if (sent_bytes < 0) {
		fprintf(stderr, "failed to send broadcast packet\n");
		return -1;
	}

	return sent_bytes; 
}

int createBroadcastPacket(uint8_t *bPacket, char *sender, char *message) {
	int index = 0; 
	
	//flag = 4 for broadcast
	bPacket[index++] = 4; 

	//1 byte for length of sender
	bPacket[index++] = strlen(sender);

	//sender handle
	memcpy(bPacket + index, sender, strlen(sender));
	index += strlen(sender);

	//text message (null terminated)
	memcpy(bPacket + index, message, strlen(message) + 1);
	index += (strlen(message) + 1);

	return index;
}

void printHandleNames(uint8_t *packet) {

	//skip the flag byte
	int index = 1; 

	int handle_len = packet[index++];

	uint8_t handle_name[MAX_HANDLE_LEN];
	memcpy(&handle_name, packet + index, handle_len);
	handle_name[handle_len] = '\0';

	printf("\t%s\n", handle_name);
}

void printNumHandles(uint8_t *packet) {
	
	//skip the flag byte
	uint32_t num_handles = 0;
	memcpy(&num_handles, packet + 1, 4);

	//convert to host order
	int num_handles_ho = ntohl(num_handles);

	printf("Number of clients: %d\n", num_handles_ho);
	
}

int processListHandles(int socketNum, uint8_t *buffer, char *handle_name) {

	//flag = 10 to tell server it wants a list of handles
	buffer[0] = 10;  

	//only the flag is needed in format to know what is being requested
	int sent_bytes = sendPDU(socketNum, buffer, 1);
	if (sent_bytes < 0) {
		fprintf(stderr, "sendPDU for listing handles");
		return -1;
	}

	return 0;
}

int parseMulticastPacket(uint8_t *packet) {
	/**
	 * Display the multicast text to appropriate clients
	 */

	//extract the fields similarly to how it was done by the client
	int index = 1; 

	//sending handle length
	int sender_len = packet[index++];

	//handle name of sending client
	char sender_name[MAX_HANDLE_LEN];
	memcpy(sender_name, packet + index, sender_len);
	sender_name[sender_len] = '\0';
	index += sender_len; 

	//number of destination handles
	int num_handles = packet[index++];

	//need to find the index where text starts
	int i = 0;
	for (i = 0; i < num_handles; i++) {
		int dest_len = packet[index++];
		index += dest_len;
	}

	char *txt_message = (char *)(packet + index);
	
	//display  sent message
	printf("\n%s: %s\n", sender_name, txt_message);

	
	 
	return 0; 
}

int processMulticast(int socketNum, uint8_t *buffer, char *handle_name) {
	//tokenize the buffer to extract destination handles and text message
	char *command = strtok((char *)buffer, " ");
	if (command == NULL) {
		fprintf(stderr, "format: %%C [num_handles] [dest_handles] [txt_message]\n");
		return -1; 
	}

	char *numHandles = strtok(NULL, " ");
	if (numHandles == NULL) {
		fprintf(stderr, "format: %%C [num_handles] [dest_handles] [txt_message]\n");
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
			fprintf(stderr, "dest_handle[i] is NULL\n");
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

	uint8_t message[PACKET_MAX];
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
		fprintf(stderr, "send handle name failed\n");
		exit(-1); 
	}

	return bytes_sent; 
}

void printDollar(void) {
	printf("$: ");
	fflush(stdout);
}


int readFromStdin(uint8_t *buffer) {
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

void checkArgs(int argc, char * argv[]) {
	/* check command line arguments  */
	if (argc != 4)
	{
		printf("usage: %s [handle] [server-name] [server-port] \n", argv[0]);
		exit(1);
	}
}
