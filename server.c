/******************************************************************************
* server.c
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
#include <arpa/inet.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdint.h>

#include "networks.h"
#include "safeUtil.h"
#include "handle_table.h"
#include "send_recv_PDU.h"
#include "pollLib.h"

#define MAXBUF 1024
#define MAXMSG 1400
#define L_PACKET_LEN 5
#define MAX_HANDLE_LEN 101
#define DEBUG_FLAG 1

void recvFromClient(int clientSocket);
int checkArgs(int argc, char *argv[]);
int processFlag1Packet(int clientSocket, uint8_t *buffer, handle_table *h_table);
void serverControl(int mainServerSocket, handle_table *h_table);
void addNewSocket(int mainServerSocket, handle_table *h_table);
void processClient(int clientSocket, handle_table *h_table);
void callFunctionBasedOnFlag(int clientSocket, uint8_t *dataBuffer, int dataLen, handle_table *h_table, int flag);
int serverProcessMessage(int clientSocket, uint8_t *packet, int packetLen, handle_table *h_table);
int serverProcessMulticast(uint8_t *packet, int packetLen, handle_table *h_table);
int serverSendFlag7Packet(int clientSocket, uint8_t *flag7_packet, char *handle_name);
int serverSendFlag11Packet(int clientSocket, uint8_t *flag11_packet, int num_handles);
int serverSendFlag12Packet(int clientSocket, char *handle_name);
int broadcastToAllHandles(int clientSocket, uint8_t *packet, int packetLen, handle_table *h_table);

int main(int argc, char *argv[])
{
	int mainServerSocket = 0;   //socket descriptor for the server socket
	int portNumber = 0;
	handle_table *h_table = create_handle_table();
	portNumber = checkArgs(argc, argv);
	//create the server socket
	mainServerSocket = tcpServerSetup(portNumber);
	serverControl(mainServerSocket, h_table); 
	close(mainServerSocket);
	destroy_handle_table(h_table); 

	return 0;
}

void serverControl(int mainServerSocket, handle_table *h_table) {
	//add main server socket to poll set
	setupPollSet(); 
	addToPollSet(mainServerSocket);

	//poll indefinitely until socket is ready
	while (1) {
		int poll_socket = pollCall(-1);
		if (poll_socket < 0) {
			perror("PollCall");
			exit(1);
		}
		else if (poll_socket == mainServerSocket) {
			//incoming socket
			addNewSocket(mainServerSocket, h_table);
		}
		else {
			//process client
			processClient(poll_socket, h_table);
		}
	}
}

void processClient(int clientSocket, handle_table *h_table) {

	uint8_t dataBuffer[MAXMSG];
	int messageLen = 0; 

	//receive data that client sent
	messageLen = recvPDU(clientSocket, dataBuffer, MAXMSG);
	if (messageLen > 0) {
		//successful data read
		int flag = dataBuffer[0];
		callFunctionBasedOnFlag(clientSocket, dataBuffer, messageLen, h_table, flag);

	}
	else if (messageLen == 0) {
		//client colsed connection
		printf("Socket %d: Connection closed by other side\n", clientSocket);
		removeFromPollSet(clientSocket);
		remove_handle(h_table, clientSocket);
		//print_handle_table(h_table);
		close(clientSocket);
	}
	else {
		//something went wrong
		perror("recv failed");
		removeFromPollSet(clientSocket);
		remove_handle(h_table, clientSocket);
		close(clientSocket);
	}
}

void callFunctionBasedOnFlag(int clientSocket, uint8_t *dataBuffer, int dataLen, handle_table *h_table, int flag) {
	/**
	 * Based on the flag, the server will call a function accordingly
	 */
	if (flag == 1) {
		processFlag1Packet(clientSocket, dataBuffer, h_table);
	}
	
	if (flag == 4) {
		//broadcast
		broadcastToAllHandles(clientSocket, dataBuffer, dataLen, h_table);
	} 
	else if (flag == 5) {
		//message
		serverProcessMessage(clientSocket, dataBuffer, dataLen, h_table);
	}
	else if (flag == 6) {
		//multicast
		serverProcessMulticast(dataBuffer, dataLen, h_table);
	}
	else if (flag == 10) {
		//list handles
		int num_handles = get_num_handles(h_table);
		
		//server responds with flag = 11 packet
		uint8_t flag11_packet[L_PACKET_LEN];
		serverSendFlag11Packet(clientSocket, flag11_packet, num_handles); 

		//server sends flag= 12 packet for each handle in table
		int i = 0; 
		for (i = 0; i < num_handles; i++) {
			char *handle_name = get_handle_by_index(h_table, i);	
			serverSendFlag12Packet(clientSocket, handle_name);
		}

		//send flag = 13 to indicate done with %l 
		uint8_t flag13_packet[1]; 
		flag13_packet[0] = 13;
		sendPDU(clientSocket, flag13_packet, 1);
	}
}

int broadcastToAllHandles(int clientSocket, uint8_t *packet, int packetLen, handle_table *h_table) {
	/** 
	 * Send the PDU to all handles besides the sender
	 */

	int num_handles = get_num_handles(h_table); 

	int i = 0; 
	for (i = 0; i < num_handles; i++) {
		int dest_socket = get_socket_by_index(h_table, i); 
		//don't send to the sender
		if (dest_socket == clientSocket) {
			continue; 
		}

		if (sendPDU(dest_socket, packet, packetLen) < 0) {
			fprintf(stderr, "broadcast sendPDU failed\n");
			return -1; 
		}
	}

	return 0; 
}

int serverSendFlag12Packet(int clientSocket, char *handle_name) {
	/**
	 * Package the Flag12 packet to send
	 */
	
	uint8_t flag12_packet[MAXBUF];

	//set flag = 12
	flag12_packet[0] = 12;

	//next byte is length of handle
	flag12_packet[1] = strlen(handle_name);

	//put name into packet
	memcpy(flag12_packet + 2, handle_name, strlen(handle_name));

	int bytes_sent = sendPDU(clientSocket, flag12_packet, strlen(handle_name) + 2);
	if (bytes_sent < 0) {
		fprintf(stderr, "sendPDU for flag12 failed\n");
		return -1;
	}

	return bytes_sent;	
}

int serverSendFlag11Packet(int clientSocket, uint8_t *flag11_packet, int num_handles) {
	/**
	 * Package the Flag11 packet to send
	 */

	//set flag = 11
	flag11_packet[0] = 11; 

	//convert to network order (32 bits)
	uint32_t num_handles_no = htonl(num_handles);

	//place after flag
	memcpy(flag11_packet + 1, &num_handles_no, 4);

	//send flag11 packet to client
	int send_len = 0;
	if ((send_len = sendPDU(clientSocket, flag11_packet, L_PACKET_LEN)) < 0) {
		fprintf(stderr, "flag11 packet failed to send");
		return -1;
	}

	return send_len;
}

int serverSendFlag7Packet(int clientSocket, uint8_t *flag7_packet, char *handle_name) {
	/**
	 * Error packet- if destination handle in a messsags does not exist
	 */
	
	int index = 0; 
	
	//flag = 7 for error packet 
	flag7_packet[index++] = 7; 

	//len of dest handle
	flag7_packet[index++] = strlen(handle_name); 

	//dest handle name
	memcpy(flag7_packet + index, handle_name, strlen(handle_name)); 
	index += strlen(handle_name); 

	int sent_bytes = sendPDU(clientSocket, flag7_packet, index);
	if (sent_bytes < 0) {
		fprintf(stderr, "error sending flag7 packet\n");
		return -1; 
	}

	return sent_bytes;
}

int serverProcessMulticast(uint8_t *packet, int packetLen, handle_table *h_table) {
	/**
	 * extract the multicast fields and send to given clients
	 */
	
	//start at index = 1 to bypass the flag in first byte
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

	//check destination names are valid and send if so
	int i = 0;
	for (i = 0; i < num_handles; i++) {
		int dest_len = packet[index++];

		char dest_handle[MAX_HANDLE_LEN];
		memcpy(dest_handle, packet + index, dest_len);
		dest_handle[dest_len] = '\0';
		index += dest_len; 

		int dest_index = lookup_name(h_table, dest_handle); 
		if (dest_index < 0) {
			int sender_index = lookup_name(h_table, sender_name);
			if (sender_index >= 0) {
				//send invalid message back to caller (flag 7)!!
				uint8_t flag7_packet[MAXBUF];
				int sender_socket = get_socket_by_index(h_table, sender_index); 
				serverSendFlag7Packet(sender_socket, flag7_packet, dest_handle);
				return -1; 
			}
		}

		//send message to each handle if valid
		int dst_socket = get_socket_by_index(h_table, dest_index); 

		if (sendPDU(dst_socket, packet, packetLen) < 0) {
			fprintf(stderr, "message failed to send for multicast\n");
			return -1; 
		}
	}

	return 0; 
}

int serverProcessMessage(int clientSocket, uint8_t *packet, int packetLen, handle_table *h_table) {
	/**
	 * extract the message fields and send to the given client
	 */

	//start at index = 1 to bypass the flag in first byte
	int index = 1; 

	int sender_len = packet[index++];
	
	//get sender name and make a string
	char sender_name[MAX_HANDLE_LEN];
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
	char dest_name[MAX_HANDLE_LEN];
	memcpy(dest_name, packet + index, dest_len);
	dest_name[dest_len] = '\0';
	index += dest_len;

	int dest_index = 0; 
	if ((dest_index = lookup_name(h_table, (char *)dest_name)) < 0) {
		//maybe send message to sender?? 
		uint8_t flag7_packet[MAXBUF]; 
		serverSendFlag7Packet(clientSocket, flag7_packet, dest_name);
		return -1; 
	} 

	//retrieve socket number
	int dst_socket = get_socket_by_index(h_table, dest_index);

	//send to the proper client
	if (sendPDU(dst_socket, packet, packetLen) < 0) {
		fprintf(stderr, "message failed to send for message\n");
		return -1; 
	}

	return 0; 
}

void addNewSocket(int mainServerSocket, handle_table *h_table) {
	/**
	 * Accpet a new socket and add it to the handle table
	 */

	int clientSocket = 0; 

	clientSocket = tcpAccept(mainServerSocket, DEBUG_FLAG);
	addToPollSet(clientSocket);
	
	//print_handle_table(h_table);

}

int processFlag1Packet(int clientSocket, uint8_t *buffer, handle_table *h_table) {
	/**
	 * New clients will send their handle name to the server to process and manage
	 */

	if (buffer[0] != 1) {
		fprintf(stderr, "flag 1 should be here\n");
		return -1; 
	}


	uint8_t handleLen = buffer[1]; 
	if (handleLen > 100) {
		fprintf(stderr, "handle len should not be more than 100 chars\n");
		return -1; 
	}

	char handle_name[MAX_HANDLE_LEN];
	memcpy(handle_name, buffer + 2, handleLen);
	
	//null terminate to make a string
	handle_name[handleLen] = '\0';

	//add to the handle table
	if (add_handle(h_table, handle_name, clientSocket) < 0) {
		//if name taken, send flag3 packet to client
		uint8_t flag3_packet[1];
		flag3_packet[0] = 3;

		if (sendPDU(clientSocket, flag3_packet, 1) < 0) {
			fprintf(stderr, "failed to send flag3 packet\n");
			return -1; 
		}
		return -1; 
	}

	//send flag2 packet to client confirming good handle
	uint8_t flag2_packet[1]; 
	flag2_packet[0] = 2; 
	if (sendPDU(clientSocket, flag2_packet, 1) < 0) {
		fprintf(stderr, "failed to send flag2 packet\n");
		return -1; 
	}

	return 0; 
}

int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;

	if (argc > 2)
	{
		fprintf(stderr, "Usage %s [optional port number]\n", argv[0]);
		exit(-1);
	}
	
	if (argc == 2)
	{
		portNumber = atoi(argv[1]);
	}
	
	return portNumber;
}

