/******************************************************************************
*
* UFTP (UDP File Transport Protocol) Daemon
* COMP7005 -- Data Comm Final Project
*
* Version 0.8
*
* Written By: Stephen Makonin (A00662003)
* Written On: November 23rd, 2007
*
******************************************************************************/

#include "common.h"

int send_file(HTHREAD_PTR descr, char *filename)
{
	WSADATA				wsa_data;
	SOCKET				data_socket					= INVALID_SOCKET;
	struct sockaddr_in	send_data_addr;
	HPACKET				packet;
	HPARTITION			partition;
	FILE				*fp;
	int					return_code;
	unsigned long		at_location,
						read_amount,
						tries;					
	unsigned char		packet_count;
	char				control_message[MAX_INPUT_LENGTH];

	if(fopen_s(&fp, filename, "rb") > 0)
	{
		memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
		control_message[0] = CONTROL_MESSAGE_NO_SUCH_FILE;
		return_code = send(descr->socket, control_message, (int)strlen(control_message), 0);		
		return 1;
	}

	for(tries = 0; tries < CONTROL_MESSAGE_RECV_RETRIES; tries++)
	{
		memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
		return_code = recv(descr->socket, control_message, MAX_INPUT_LENGTH-1, 0);
		if(return_code < 1)
		{
			printf("failed to recv command: %d\n", WSAGetLastError());
			closesocket(data_socket);
			return 0;
		}

		if(control_message[0] == CONTROL_MESSAGE_OK_START_SENDING)
			break;
	}

	if(tries >= CONTROL_MESSAGE_RECV_RETRIES)
	{
		printf("No CONTROL_MESSAGE_OK_START_SENDING from %s\n", inet_ntoa(descr->address.sin_addr));
		closesocket(data_socket);
		return 0;
	}

	WSAStartup(MAKEWORD(2,2), &wsa_data);
	data_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	send_data_addr.sin_family = AF_INET;
	send_data_addr.sin_port = htons(CONNECT_PORT_N);//descr->address.sin_port;
	send_data_addr.sin_addr = descr->address.sin_addr;//inet_addr("123.456.789.1");

	packet.partition_id = 0;
	packet.reserved = 0;
	while(!feof(fp))
	{
		memset(partition.packet_stats, 0, MAX_PARTITION_DIVISIONS+1);
		partition.actual_size = (unsigned long)fread(partition.data, 1, PARTITION_LENGTH_TOTAL, fp);
		packet_count = (unsigned char)ceil(partition.actual_size / PACKET_LENGTH_DATA);

		memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
		sprintf_s(control_message, MAX_INPUT_LENGTH, "  %u", partition.actual_size);
		control_message[0] = CONTROL_MESSAGE_SENDING_DATA;
		return_code = send(descr->socket, control_message, (int)strlen(control_message)+1, 0);
		if(return_code == SOCKET_ERROR)
		{
			printf("failed to send command: %d\n", WSAGetLastError());
			closesocket(data_socket);
			return 0;
		}		

		while(1)
		{	
			for(packet.packet_id = 0; packet.packet_id < packet_count; packet.packet_id++)
			{
				if(partition.packet_stats[packet.packet_id] != 1)
					break;
			}

			if(packet.packet_id == packet_count)
				break;

			for(packet.packet_id = 0; packet.packet_id < packet_count; packet.packet_id++)
			{
				if(partition.packet_stats[packet.packet_id] != 1)
				{					
					memset(packet.data, 0, sizeof(PARTITION_LENGTH_TOTAL));
					at_location = packet.packet_id * PACKET_LENGTH_DATA;
					read_amount = at_location + PACKET_LENGTH_DATA < partition.actual_size ? PACKET_LENGTH_DATA : partition.actual_size - at_location;
					memcpy(packet.data, &partition.data[at_location], read_amount);
					packet.crc = compute_crc((const unsigned char *)packet.data, PACKET_LENGTH_DATA);

					return_code = sendto(data_socket, (char *)&packet, sizeof(packet), 0, (SOCKADDR *)&send_data_addr, sizeof(send_data_addr));
					if(return_code == SOCKET_ERROR)
					{
						printf("failed to send command: %d\n", WSAGetLastError());
						closesocket(data_socket);
						return 0;
					}
					Sleep(5);
				}
			}

			memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
			control_message[0] = CONTROL_MESSAGE_PARTITION_SENT;
			return_code = send(descr->socket, control_message, (int)strlen(control_message)+1, 0);
			if(return_code == SOCKET_ERROR)
			{
				printf("failed to send command: %d\n", WSAGetLastError());
				closesocket(data_socket);
				return 0;
			}

			for(tries = 0; tries < CONTROL_MESSAGE_RECV_RETRIES; tries++)
			{
				memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
				return_code = recv(descr->socket, control_message, MAX_INPUT_LENGTH-1, 0);
				if(return_code < 1)
				{
					printf("failed to recv command: %d\n", WSAGetLastError());
					closesocket(data_socket);
					return 0;
				}

				if(control_message[0] == CONTROL_MESSAGE_PARTITION_STATUS)
				{
					memset(partition.packet_stats, 0, MAX_PARTITION_DIVISIONS+1);
					memcpy(partition.packet_stats, &control_message[2], MAX_PARTITION_DIVISIONS);
					break;
				}
			}

			if(tries >= CONTROL_MESSAGE_RECV_RETRIES)
			{
				printf("No CONTROL_MESSAGE_PARTITION_STATUS from %s\n", inet_ntoa(descr->address.sin_addr));
				closesocket(data_socket);
				return 0;
			}
		}

		packet.partition_id++;
	}
	fclose(fp);

	memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
	control_message[0] = CONTROL_MESSAGE_ALL_DATA_SENT;
	return_code = send(descr->socket, control_message, (int)strlen(control_message)+1, 0);
	if(return_code == SOCKET_ERROR)
	{
		printf("failed to send command: %d\n", WSAGetLastError());
		closesocket(data_socket);
		return 1;
	}			

	closesocket(data_socket);
	return 1;
}

DWORD WINAPI handle_client_thread(LPVOID lpParam) 
{
	HTHREAD_PTR	descr								= (HTHREAD_PTR)lpParam;
	int			return_code;
	char		control_message[MAX_INPUT_LENGTH];

	printf("Thread %d servicing %s\n", descr->id, inet_ntoa(descr->address.sin_addr));
	memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
	control_message[0] = CONTROL_MESSAGE_HELLO_RESPONSE;
	return_code = send(descr->socket, control_message, (int)strlen(control_message), 0);

	do
	{
		// wait form command
		memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
		return_code = recv(descr->socket, control_message, MAX_INPUT_LENGTH-1, 0);
		if(return_code < 1)
			break;

		switch(control_message[0])
		{
		case CONTROL_MESSAGE_PARTITION_STATUS:
		case CONTROL_MESSAGE_SEND_HELLO:
		case CONTROL_MESSAGE_NO_MESSAGE:
		case CONTROL_MESSAGE_CLIENT_BYE:
			// do nothing catch all
			break;

		case CONTROL_MESSAGE_DISPLAY_DIR:
			get_dir_listing(descr->socket);
			break;

		case CONTROL_MESSAGE_GET_FILE:
			printf("Client %s wants file %s\n", inet_ntoa(descr->address.sin_addr), &control_message[2]);
			return_code = send_file(descr, &control_message[2]);
			break;

		default:
			memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
			control_message[0] = CONTROL_MESSAGE_UNKNOWN_COMMAND;
			return_code = send(descr->socket, control_message, (int)strlen(control_message), 0);
			break;
		}
	} while(return_code > 0 && control_message[0] != CONTROL_MESSAGE_CLIENT_BYE);

	if (return_code < 0)
		printf("send/recv failed for %s: %d\n", inet_ntoa(descr->address.sin_addr), WSAGetLastError());
	else
		printf("Connection closing for %s\n", inet_ntoa(descr->address.sin_addr));

    // shutdown the connection, done
    return_code = shutdown(descr->socket, SD_SEND);
    if(return_code == SOCKET_ERROR) 
	{
        printf("shutdown failed for %s: %d\n", inet_ntoa(descr->address.sin_addr), WSAGetLastError());
        closesocket(descr->socket);
        return 1;
    }

	// cleanup
    closesocket(descr->socket);
	descr->in_use = 0;
	return 0;
}

int main(int argc, char **argv)
{
	WSADATA				wsa_data;
	SOCKET				listening_socket			= INVALID_SOCKET,
						client_socket				= INVALID_SOCKET;
    struct addrinfo		*result						= NULL, 
						hints;
	struct sockaddr		address;
	int					return_code,
						index,
						address_length,
						shutdown_signal				= 0;
	char				control_message[MAX_INPUT_LENGTH];
	HTHREAD	threads[MAX_CONNECTIONS];

	init_crc();

	printf("UFTP Daemon v0.8\n");
	printf("--------------------------\n\n");
	printf("On fixed port: %s\n", CONNECT_PORT);
	printf("Max. Connurent Connections: %d\n", MAX_CONNECTIONS);
	printf("\nServer staring up.");

	// Clean up thread lookup
	memset(threads, 0, sizeof(HTHREAD) * MAX_CONNECTIONS);
	printf(".");
    
    // Initialize Winsock sub-system
    return_code = WSAStartup(MAKEWORD(2,2), &wsa_data);
    if(return_code != 0) 
	{
        printf("WSAStartup failed: %d\n", return_code);
        return 1;
    }
	printf(".");

    // Resolve the server address and port
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;
    return_code = getaddrinfo(NULL, CONNECT_PORT, &hints, &result);
    if(return_code != 0) 
	{
        printf("getaddrinfo failed: %d\n", return_code);
        WSACleanup();
        return 1;
    }
	printf(".");

    // Create a SOCKET for connecting to server
    listening_socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if(listening_socket == INVALID_SOCKET) 
	{
        printf("socket failed: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }
	printf(".");

    // Setup the TCP listening socket
    return_code = bind(listening_socket, result->ai_addr, (int)result->ai_addrlen);
    if(return_code == SOCKET_ERROR) 
	{
        printf("bind failed: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(listening_socket);
        WSACleanup();
        return 1;
    }
    freeaddrinfo(result);
	printf(".");

    return_code = listen(listening_socket, SOMAXCONN);
    if(return_code == SOCKET_ERROR) 
	{
        printf("listen failed: %d\n", WSAGetLastError());
        closesocket(listening_socket);
        WSACleanup();
        return 1;
    }
	printf(".DONE!\n\n");

	// Loop until shutdown
	while(!shutdown_signal)
	{
		// Accept a client socket
		memset(&address, 0, sizeof(address));
		client_socket = INVALID_SOCKET;
		address_length = sizeof(address);
		client_socket = accept(listening_socket, &address, &address_length);
		if(client_socket == INVALID_SOCKET) 
			printf("accept failed: %d\n", WSAGetLastError());

		memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
		return_code = recv(client_socket, control_message, MAX_INPUT_LENGTH-1, 0);
		if(control_message[0] != CONTROL_MESSAGE_SEND_HELLO)
		{
			return_code = shutdown(client_socket, SD_BOTH);
			closesocket(client_socket);
			continue;
		}

		// Client socket valid start tread to handle client
		index = find_next_free_thread(threads);
		if(index == -1)
		{
			printf("max connections reached: %d\n", MAX_CONNECTIONS);
			
			memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
			control_message[0] = CONTROL_MESSAGE_SERVER_BUSY;
			return_code = send(client_socket, control_message, (int)strlen(control_message), 0);
			return_code = shutdown(client_socket, SD_BOTH);
			closesocket(client_socket);
			continue;
		}

		threads[index].in_use = 1;
		threads[index].socket = client_socket;
		memcpy(&threads[index].address, &address, sizeof(address));
		threads[index].handle = CreateThread(NULL, 0, handle_client_thread, &threads[index], 0, &threads[index].id);

        if(threads[index].handle == NULL) 
			printf("unable to create thread: %d\n", index);

		// TODO: Add clean way to close down server
		//if(tolower(getchar()) == (int)'q')
		//	shutdown_signal = 1;
	}

    // Close to stop new connections
	closesocket(listening_socket);

    // Make sure all threads are complete and exited
	for(index = 0; index < MAX_CONNECTIONS; index++)
	{
		WaitForSingleObject(threads[index].handle, INFINITE);
		CloseHandle(threads[index].handle);
	}
	
	// Close down socket sub-system
    WSACleanup();

    return 0;
}
