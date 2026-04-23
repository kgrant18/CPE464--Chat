/******************************************************************************
* myServer.c
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
int receive_handle_name(int clientSocket, handle_table *h_table);
int serverControl(int mainServerSocket, handle_table *h_table);
void addNewSocket(int mainServerSocket, handle_table *h_table);
void processClient(int clientSocket, handle_table *h_table);
void callFunctionBasedOnFlag(int clientSocket, uint8_t *dataBuffer, int dataLen, handle_table *h_table, int flag);
int serverProcessMessage(uint8_t *packet, int packetLen, handle_table *h_table);
int serverProcessMulticast(uint8_t *packet, int packetLen, handle_table *h_table);
int serverSendFlag11Packet(int clientSocket, uint8_t *flag11_packet, int num_handles);
int serverSendFlag12Packet(int clientSocket, char *handle_name);

int main(int argc, char *argv[])
{
	int mainServerSocket = 0;   //socket descriptor for the server socket
	int clientSocket = 0;   //socket descriptor for the client socket
	int portNumber = 0;
	
	handle_table *h_table = create_handle_table(); 

	portNumber = checkArgs(argc, argv);
	
	//create the server socket
	mainServerSocket = tcpServerSetup(portNumber);
	
	serverControl(mainServerSocket, h_table); 
	


	/* close the sockets */
	close(clientSocket);
	close(mainServerSocket);

	destroy_handle_table(h_table); 

	return 0;
}

int serverControl(int mainServerSocket, handle_table *h_table) {
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
			addNewSocket(mainServerSocket, h_table);
		}
		else {
			//process client
			processClient(poll_socket, h_table);
		}

	}
}

void processClient(int clientSocket, handle_table *h_table) {

	uint8_t dataBuffer[MAXBUF];
	int messageLen = 0; 

	//receive data that client sent
	messageLen = recvPDU(clientSocket, dataBuffer, MAXBUF);
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
		print_handle_table(h_table);
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
	if (flag == 5) {
		//message from client
		serverProcessMessage(dataBuffer, dataLen, h_table);
	}
	else if (flag == 6) {
		serverProcessMulticast(dataBuffer, dataLen, h_table);
	}
	else if (flag == 10) {
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
	}
}

int serverSendFlag12Packet(int clientSocket, char *handle_name) {
	uint8_t flag12_packet[MAXBUF];

	//set flag = 12
	flag12_packet[0] = 12;

	//next byte is length of handle
	flag12_packet[1] = strlen(handle_name);

	//put name into buffer
	memcpy(flag12_packet + 2, handle_name, strlen(handle_name));

	int bytes_sent = sendPDU(clientSocket, flag12_packet, strlen(handle_name) + 2);
	if (bytes_sent < 0) {
		fprintf(stderr, "sendPDU for flag12 failed\n");
		return -1;
	}

	return bytes_sent;	
	
}

int serverSendFlag11Packet(int clientSocket, uint8_t *flag11_packet, int num_handles) {

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
				return -1; 
			}
		}

		//send message to each handle if valid
		int dst_socket = h_table->entries[dest_index].socket_num;

		if (sendPDU(dst_socket, packet, packetLen) < 0) {
			fprintf(stderr, "message failed to send for multicast\n");
			return -1; 
		}
	}

	return 0; 
}

int serverProcessMessage(uint8_t *packet, int packetLen, handle_table *h_table) {
	//extract the message fields

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
		fprintf(stderr, "lookup_name failed\n");
		return -1; 
	} 

	int dst_socket = h_table->entries[dest_index].socket_num; 

	//send to the proper client

	if (sendPDU(dst_socket, packet, packetLen) < 0) {
		fprintf(stderr, "message failed to send for message\n");
		return -1; 
	}

	

	return 0; 
}

void addNewSocket(int mainServerSocket, handle_table *h_table) {
	int clientSocket = 0; 

	clientSocket = tcpAccept(mainServerSocket, DEBUG_FLAG);
	addToPollSet(clientSocket);
	receive_handle_name(clientSocket, h_table);

	print_handle_table(h_table);

}

int receive_handle_name(int clientSocket, handle_table *h_table) {
	uint8_t buffer[MAXMSG];

	//receive from client
	int bytes_recv = recvPDU(clientSocket, buffer, sizeof(buffer));
	if (bytes_recv < 0) {
		fprintf(stderr, "recvPDU failed");
		return -1;
	} 
	else if (bytes_recv == 0) {
		fprintf(stderr, "client's connection closed");
		return -1; 
	}

	//check that flag is 1
	if (buffer[0] != 1) {
		fprintf(stderr, "flag should be 1 here");
		return -1; 
	}

	uint8_t handleLen = buffer[1]; 
	if (handleLen > 100) {
		fprintf(stderr, "handle len should not be more than 100 chars");
		return -1; 
	}

	char handle_name[MAX_HANDLE_LEN];
	memcpy(handle_name, buffer + 2, handleLen);
	
	//null terminate to make a string
	handle_name[handleLen] = '\0';

	//add to the handle table
	if (add_handle(h_table, handle_name, clientSocket) < 0) {
		return -1;
	}

	return 0; 
}



void recvFromClient(int clientSocket)
{
	uint8_t dataBuffer[MAXBUF];
	int messageLen = 0;
	
	//now get the data from the client_socket
	if ((messageLen = safeRecv(clientSocket, dataBuffer, MAXBUF, 0)) < 0)
	{
		perror("recv call");
		exit(-1);
	}

	if (messageLen > 0)
	{
		printf("Socket %d: Message received, length: %d Data: %s\n", clientSocket, messageLen, dataBuffer);
		
		// send it back to client (just to test sending is working... e.g. debugging)
		messageLen = safeSend(clientSocket, dataBuffer, messageLen, 0);
		printf("Socket %d: msg sent: %d bytes, text: %s\n", clientSocket, messageLen, dataBuffer);
	}
	else
	{
		printf("Socket %d: Connection closed by other side\n", clientSocket);
	}

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

