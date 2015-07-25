/******************************************************************************
*
* UFTP (UDP File Transport Protocol) Common Library
* COMP7005 -- Data Comm Final Project
*
* Version 0.8
*
* Written By: Stephen Makonin (A00662003)
* Written On: November 23rd, 2007
*
******************************************************************************/

#include <winsock2.h>
#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>
#include <windows.h>
#include <tchar.h>
#include <strsafe.h>
#include <sys/stat.h>
#include <direct.h>
#include <math.h>
#include "crc.h"

/*
	Socket defaults and threading
*/
#define CONNECT_PORT				"3333"
#define CONNECT_PORT_N				3333
#define MAX_INPUT_LENGTH			80
#define MAX_CONNECTIONS				32
#define DELIMIT_TOKENS				" \n\r\t\0"
#define END_OF_DYNAMIC_DATA			"\t\t\t\0"
#define ERROR_FOR_DYNAMIC_DATA		"Unable to complete operation\n\t\t\t\0"


typedef struct _HTHREAD
{
	int					in_use;
    DWORD				id;
    HANDLE				handle; 
	SOCKET				socket;
	struct sockaddr_in	address;
} HTHREAD, *HTHREAD_PTR;

int find_next_free_thread(HTHREAD_PTR threads)
{
	int i;

	for(i = 0; i < MAX_CONNECTIONS; i++)
	{
		if(!threads[i].in_use)
		{
			if(CloseHandle(threads[i].handle))
				CloseHandle(threads[i].handle);
			memset(&threads[i], 0, sizeof(HTHREAD));
			return i;
		}
	}

	return -1;
}

/*
	Control messages between client and server
*/
#define CONTROL_MESSAGE_NO_MESSAGE			'\0'
#define CONTROL_MESSAGE_SEND_HELLO			'a'
#define CONTROL_MESSAGE_HELLO_RESPONSE		'b'
#define CONTROL_MESSAGE_SERVER_BUSY			'c'
#define CONTROL_MESSAGE_CLIENT_BYE			'd'
#define CONTROL_MESSAGE_SERVER_BYE			'e'
#define CONTROL_MESSAGE_GET_FILE			'f'
#define CONTROL_MESSAGE_DISPLAY_DIR			'g'
#define CONTROL_MESSAGE_CHANGE_DIR			'h'
#define CONTROL_MESSAGE_OK_START_SENDING	'i'
#define CONTROL_MESSAGE_SENDING_DATA		'j'
#define CONTROL_MESSAGE_PARTITION_SENT		'k'
#define CONTROL_MESSAGE_PARTITION_STATUS	'l'
#define CONTROL_MESSAGE_ALL_DATA_SENT		'm'
#define CONTROL_MESSAGE_NO_SUCH_FILE		'n'
#define CONTROL_MESSAGE_UNKNOWN_COMMAND		'o'
#define CONTROL_MESSAGE_UNKNOWN_ERROR		'p'
#define CONTROL_MESSAGE_RECV_RETRIES		5

/*
	Things to do with packets
*/
#define PACKET_LENGTH_TOTAL			1500
#define PACKET_LENGTH_HEADER		8
#define PACKET_LENGTH_DATA			(PACKET_LENGTH_TOTAL - PACKET_LENGTH_HEADER)

typedef struct _HPACKET
{
	unsigned short	partition_id;
	unsigned char	packet_id;
	unsigned char	reserved;
    unsigned long	crc;
	unsigned char	data[PACKET_LENGTH_DATA];
} HPACKET, *HPACKET_PTR;

/*
	Things to do with partitions
*/
#define MAX_PARTITION_DIVISIONS		64
#define MAX_PARTITIONS				0xFFFF
#define PARTITION_LENGTH_TOTAL		(MAX_PARTITION_DIVISIONS * PACKET_LENGTH_DATA)

typedef struct _HPARTITION
{
	unsigned long	actual_size;
	unsigned char	packet_stats[MAX_PARTITION_DIVISIONS+1];
	unsigned char	data[PARTITION_LENGTH_TOTAL];
} HPARTITION, *HPARTITION_PTR;

/*
	File handling functions
*/
int file_size(char* filename)
{
  struct _stat stbuf;
  memset(&stbuf, 0, sizeof(stbuf));
  _stat(filename, &stbuf);
  if(stbuf.st_size > 0)
	return stbuf.st_size;
  else
	  return -1;
}

void get_dir_listing(SOCKET soc)
{
	WIN32_FIND_DATA fd;
	HANDLE			fh							= INVALID_HANDLE_VALUE;
	char			file[MAX_INPUT_LENGTH];
	char			line[MAX_INPUT_LENGTH];
	LPTSTR			dir;
	char			size[8];
	unsigned long	file_size;
	int				return_code;
	size_t			converted_ch;

	dir = (LPTSTR)malloc(MAX_PATH);
	_wgetdcwd(_getdrive(), dir, MAX_PATH);
	StringCbCatN(dir, MAX_PATH, TEXT("\\*"), 2*sizeof(TCHAR));

	fh = FindFirstFile(dir, &fd);
	if(fh != INVALID_HANDLE_VALUE)
	{
		do
		{
			if(!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			{
				file_size = ((fd.nFileSizeHigh * MAXDWORD) + fd.nFileSizeLow);
				memset(size, 0, sizeof(8));
				if(file_size < 1000)
					sprintf_s(size, 8, "%3u", file_size);
				else if(file_size > 999 && file_size < 1000000 )
					sprintf_s(size, 8, "%3uK", (file_size/1000));
				else if(file_size > 999999 && file_size < 1000000000 )
					sprintf_s(size, 8, "%3uM", (file_size/1000000));
				else if(file_size > 999999999 && file_size < 1000000000000 )
					sprintf_s(size, 8, "%3uG", (file_size/1000000000));
				else if(file_size > 999999999999 && file_size < 1000000000000000 )
					sprintf_s(size, 8, "%3uT", (file_size/1000000000000));

				converted_ch = 0;
				if(fd.cAlternateFileName[0] == 0)
					wcstombs_s(&converted_ch, file, MAX_INPUT_LENGTH, fd.cFileName, MAX_INPUT_LENGTH);
				else
					wcstombs_s(&converted_ch, file, MAX_INPUT_LENGTH, fd.cAlternateFileName, MAX_INPUT_LENGTH);
				memset(line, 0, sizeof(MAX_INPUT_LENGTH));
				sprintf_s(line, MAX_INPUT_LENGTH, "%-16s %4s\n", file, size);
				return_code = send(soc, line, (int)strlen(line)+1, 0);
			}

		} while(FindNextFile(fh, &fd));
	}

	FindClose(fh);
	return_code = send(soc,  END_OF_DYNAMIC_DATA, (int)strlen(END_OF_DYNAMIC_DATA)+1, 0);
} 

/*
	Redef of function names from 3rd party libraries
	for nmaing consistancy/convention
*/
#define init_crc					crcInit
#define compute_crc					crcFast
