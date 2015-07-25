/******************************************************************************
*
* UFTP (UDP File Transport Protocol) Client
* COMP7005 -- Data Comm Final Project
*
* Version 0.8
*
* Written By: Stephen Makonin (A00662003)
* Written On: November 23rd, 2007
*
******************************************************************************/

#include "common.h"

int recv_file(SOCKET soc, char *filename)
{
	WSADATA				wsa_data;
	SOCKET				data_socket					= INVALID_SOCKET;
	struct sockaddr_in	recv_data_addr,
						addr;
	HPACKET				packet;
	HPARTITION			partition;
	FILE				*fp;
	int					return_code;
	unsigned long		partition_id,
						at_location,
						write_amount,
						tries;					
	unsigned char		packet_count,
						packets_recv;
	unsigned int		addr_len						= sizeof(addr);
	char				control_message[MAX_INPUT_LENGTH];
	__int64				file_size;
	DWORD				timing;
	float				transfer_kb,
						transfer_sec,
						transfer_mbps;


	return_code = getsockname(soc, (struct sockaddr *)&addr, (socklen_t *)&addr_len);
	if(return_code < 0)
	{
		printf("Fetch of local port failed.\n");
		return 0;
	}

	if(fopen_s(&fp, filename, "wb") > 0)
	{
		printf("Unable to write file.\n");
		return 0;
	}

	WSAStartup(MAKEWORD(2,2), &wsa_data);
	data_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	recv_data_addr.sin_family = AF_INET;
	recv_data_addr.sin_port = htons(CONNECT_PORT_N);//addr.sin_port;
	recv_data_addr.sin_addr.s_addr = htonl(INADDR_ANY);//addr.sin_addr;
	bind(data_socket, (SOCKADDR *)&recv_data_addr, sizeof(recv_data_addr));

	file_size = partition_id = 0;
	timing = GetTickCount();
	printf("Downloading %s", filename);

	memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
	control_message[0] = CONTROL_MESSAGE_OK_START_SENDING;
	return_code = send(soc, control_message, (int)strlen(control_message), 0);
	if(return_code == SOCKET_ERROR)
	{
		printf("failed to send command: %d\n", WSAGetLastError());
		return 0;
	}

	while(1)
	{
		for(tries = 0; tries < CONTROL_MESSAGE_RECV_RETRIES; tries++)
		{
			memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
			return_code = recv(soc, control_message, MAX_INPUT_LENGTH-1, 0);
			if(return_code < 1)
			{
				printf("failed to recv command: %d\n", WSAGetLastError());
				closesocket(data_socket);
				return 0;
			}

			if(control_message[0] == CONTROL_MESSAGE_SENDING_DATA)
			{
				partition.actual_size = atol(&control_message[2]);
				break;
			}

			if(control_message[0] == CONTROL_MESSAGE_ALL_DATA_SENT)
				break;

			if(control_message[0] == CONTROL_MESSAGE_NO_SUCH_FILE)
			{
				printf("\rRemote file does not exist!\n\n");
				return 1;
			}
		}

		if(control_message[0] == CONTROL_MESSAGE_ALL_DATA_SENT)
			break;

		if(tries >= CONTROL_MESSAGE_RECV_RETRIES)
		{
			printf("\nNo CONTROL_MESSAGE_SENDING_DATA from server\n");
			closesocket(data_socket);
			return 0;
		}

		memset(partition.packet_stats, 0, MAX_PARTITION_DIVISIONS+1);
		memset(partition.data, 0, sizeof(PARTITION_LENGTH_TOTAL));
		packet_count = (unsigned char)ceil(partition.actual_size / PACKET_LENGTH_DATA);

		while(1)
		{
			for(packets_recv = 0; packets_recv < packet_count; packets_recv++)
			{
				if(partition.packet_stats[packets_recv] != 1)
					break;
			}

			if(packets_recv == packet_count)
				break;

			for(packets_recv = 0; packets_recv < packet_count; packets_recv++)
			{
				return_code = recvfrom(data_socket, (char *)&packet, sizeof(packet), 0, NULL, NULL);
				if(return_code < 1)
				{
					printf("\nfailed to recv command: %d\n", WSAGetLastError());
					closesocket(data_socket);
					return 0;
				}

				if(packet.partition_id == partition_id)
				{
					if(compute_crc((const unsigned char *)packet.data, PACKET_LENGTH_DATA) == packet.crc)
					{
						partition.packet_stats[packet.packet_id] = 1;
						at_location = packet.packet_id * PACKET_LENGTH_DATA;
						write_amount = at_location + PACKET_LENGTH_DATA < partition.actual_size ? PACKET_LENGTH_DATA : partition.actual_size - at_location;
						memcpy(&partition.data[at_location], packet.data, write_amount);
					}
				}				
			}

			for(tries = 0; tries < CONTROL_MESSAGE_RECV_RETRIES; tries++)
			{
				memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
				return_code = recv(soc, control_message, MAX_INPUT_LENGTH-1, 0);
				if(return_code < 1)
				{
					printf("\nfailed to recv command: %d\n", WSAGetLastError());
					closesocket(data_socket);
					return 0;
				}

				if(control_message[0] == CONTROL_MESSAGE_PARTITION_SENT)
					break;
			}

			if(tries >= CONTROL_MESSAGE_RECV_RETRIES)
			{
				printf("\nNo CONTROL_MESSAGE_PARTITION_SENT from server\n");
				closesocket(data_socket);
				return 0;
			}

			memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
			sprintf_s(control_message, MAX_INPUT_LENGTH, "  %s", partition.packet_stats);
			control_message[0] = CONTROL_MESSAGE_PARTITION_STATUS;
			return_code = send(soc, control_message, (int)strlen(control_message)+1, 0);
			if(return_code == SOCKET_ERROR)
			{
				printf("\nfailed to send command: %d\n", WSAGetLastError());
				closesocket(data_socket);
				return 0;
			}
		}

		fwrite(partition.data, partition.actual_size, 1, fp);
		partition_id++;
		file_size += partition.actual_size;
		printf(".");
	}
	fclose(fp);
	closesocket(data_socket);
	printf("Done!\n");

	transfer_sec = (float)(GetTickCount() - timing) / 1000.0;
	transfer_kb = (float)file_size / 1000.0;
	transfer_mbps = (file_size / 1000.0) / transfer_sec;
	printf("Transfer of %0.f KB took %0.1f seconds (%0.3f MBps)!\n\n", transfer_kb, transfer_sec, transfer_mbps);

	return 1;
}

int __cdecl main(int argc, char **argv) 
{
	WSADATA			wsa_data;
    struct addrinfo *result					= NULL,
                    *ptr					= NULL,
                    hints;
	SOCKET			conn_socket				= INVALID_SOCKET;
	char			input[MAX_INPUT_LENGTH],
					msg_buf[MAX_INPUT_LENGTH],
					*command,
					*filename,
					*next_token				= NULL,
					control_message[MAX_INPUT_LENGTH];
	int				quit					= FALSE,
					return_code;

	init_crc();

	printf("UFTP Client v0.8\n");
	printf("--------------------------\n\n");

	if(argc != 2)
	{
		printf("Usage: uftp.exe [ip address of the UFTP Server]\n\n");
		return 1;
	}

	printf("Connecting to UFTP Server %s:%s.", argv[1], CONNECT_PORT);

    // Initialize Winsock
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
    return_code = getaddrinfo(argv[1], CONNECT_PORT, &hints, &result);
    if(return_code != 0) 
	{
        printf("getaddrinfo failed: %d\n", return_code);
        WSACleanup();
        return 1;
    }
	printf(".");

    // Attempt to connect to an address until one succeeds
    for(ptr = result; ptr != NULL; ptr = ptr->ai_next) 
	{
        // Create a conn_socket for connecting to server
        conn_socket = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
        if(conn_socket == INVALID_SOCKET) 
		{
            printf("Error at conn_socket(): %ld\n", WSAGetLastError());
            freeaddrinfo(result);
            WSACleanup();
            return 1;
        }
		printf(".");

        // Connect to server.
        return_code = connect(conn_socket, ptr->ai_addr, (int)ptr->ai_addrlen);
        if(return_code == SOCKET_ERROR) 
		{
            closesocket(conn_socket);
            conn_socket = INVALID_SOCKET;
            continue;
        }
		printf(".");
        break;
    }
    freeaddrinfo(result);
    if(conn_socket == INVALID_SOCKET) 
	{
        printf("Unable to connect to server!\n");
        WSACleanup();
        return 1;
    }	
	printf(".DONE!\n\n");
	printf("COMMANDS:\n");
	printf("\t DIR  - Display directory listing.\n");
	printf("\t GET  - Download remote file.\n");
	printf("\t QUIT - Disconnect from server and exit.\n\n");

	memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
	control_message[0] = CONTROL_MESSAGE_SEND_HELLO;
	return_code = send(conn_socket, control_message, (int)strlen(control_message), 0);
	if(return_code == SOCKET_ERROR)
	{
		printf("failed to send hello: %d\n", WSAGetLastError());
		quit = TRUE;
	}
	else
	{
		memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
		return_code = recv(conn_socket, control_message, MAX_INPUT_LENGTH-1, 0);
		if(return_code == SOCKET_ERROR)
		{
			printf("failed to receive hello: %d\n", WSAGetLastError());
			quit = TRUE;
		}
	}

	switch(control_message[0])
	{
	case CONTROL_MESSAGE_HELLO_RESPONSE:
		printf("UFTP Server ready for commands:\n\n");
		quit = FALSE;
		break;

	case CONTROL_MESSAGE_SERVER_BUSY:
		printf("Server is too bust, try again later...\n\n");
		quit = TRUE;
		break;

	default:
		printf("Unknown server, may not be a UFTP Server.\n\n");
		quit = TRUE;
		break;
	}

	// interact with the server
	while(!quit)
	{
		memset(input, 0, sizeof(MAX_INPUT_LENGTH));
		printf(">");
		gets_s(input, MAX_INPUT_LENGTH);

		//process input
		command = strtok_s(input, DELIMIT_TOKENS, &next_token);
		filename = strtok_s(NULL, DELIMIT_TOKENS, &next_token);

		if(_strcmpi(command, "get") == 0)
		{
			sprintf_s(control_message, MAX_INPUT_LENGTH, "  %s", filename);
			control_message[0] = CONTROL_MESSAGE_GET_FILE;
			return_code = send(conn_socket, control_message, (int)strlen(control_message)+1, 0);
			if(return_code == SOCKET_ERROR)
			{
				printf("failed to send command: %d\n", WSAGetLastError());
				quit = TRUE;
				break;
			}

			if(!recv_file(conn_socket, filename))
			{
				printf("failed to get file: %d\n", WSAGetLastError());
				quit = TRUE;
				continue;
			}
		}
		else if(_strcmpi(command, "dir") == 0)
		{
			memset(control_message, 0, sizeof(MAX_INPUT_LENGTH));
			control_message[0] = CONTROL_MESSAGE_DISPLAY_DIR;
			return_code = send(conn_socket, control_message, (int)strlen(control_message), 0);
			if(return_code == SOCKET_ERROR)
			{
				printf("failed to send command: %d\n", WSAGetLastError());
				quit = TRUE;
				break;
			}

			do
			{				
				memset(msg_buf, 0, sizeof(MAX_INPUT_LENGTH));
				return_code = recv(conn_socket, msg_buf, MAX_INPUT_LENGTH-1, 0);
				if(return_code == SOCKET_ERROR)
				{
					printf("failed to list dir: %d\n", WSAGetLastError());
					quit = TRUE;
					break;
				}				

				printf("%s", msg_buf);

				if(strstr(msg_buf, END_OF_DYNAMIC_DATA) != NULL)
					break;

			} while(_strcmpi(msg_buf, END_OF_DYNAMIC_DATA) != 0);

			printf("\n");
		}
		else if(_strcmpi(command, "quit") == 0)
		{
			quit = TRUE;
		}
		else
		{
			printf("unknown command: %s\n", command);
		}
	}

    // shutdown the connection since no more data will be sent
    return_code = shutdown(conn_socket, SD_SEND);
    if(return_code == SOCKET_ERROR) 
	{
        printf("shutdown failed: %d\n", WSAGetLastError());
        closesocket(conn_socket);
        WSACleanup();
        return 1;
    }

    // Receive until the peer closes the connection
    do 
	{
        return_code = recv(conn_socket, msg_buf, MAX_INPUT_LENGTH-1, 0);
    } while( return_code > 0 );

    // cleanup
    closesocket(conn_socket);
    WSACleanup();

    return 0;
}

