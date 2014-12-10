#ifndef MCS_H
#define MCS_H

#include <arpa/inet.h> // inet_ntoa
#include <dirent.h> // opendir
#include <signal.h> // SIGTERM, SIGKILL
#include <stdio.h> // printf
#include <stdlib.h> // exit
#include <string.h> // memset, strcpy
#include <sys/socket.h>
#include <time.h>
#include <sys/wait.h> // waitpid
#include <unistd.h> // fork, exec

// server settings
#define MCS_ADMIN_KEY "admin"
#define MCS_PORT 5002
#define MCS_MAX_ITEMS 100000
#define MCS_HASH_SIZE 10000000
#define MCP_VERSION "MCP/0.1"

// extensions, types and binaries
#define MCS_TYPE_BASE 100
#define MCS_TYPE_AUDIO 100
#define MCS_TYPE_ROM 200
#define MCS_TYPE_ROM_GB 201
#define MCS_TYPE_ROM_NES 202
#define MCS_TYPE_VIDEO 300

#define MCS_EXT_AUDIO ":flac:mp3:"
#define MCS_EXT_ROM ":gb:gbc:nes:smc:smd:"
#define MCS_EXT_ROM_GB ":gb:gbc:"
#define MCS_EXT_ROM_NES ":nes:"
#define MCS_EXT_VIDEO ":avi:mkv:mp4:"

#define MCS_BIN_AUDIO "/usr/bin/omxplayer -b %s"
#define MCS_BIN_ROM_NES "/usr/bin/fceu %s"
#define MCS_BIN_VIDEO "/usr/bin/omxplayer -b %s"
#define MCS_BIN_UNKOWN "./handleUnkownType.sh %s"

// server states
#define MCS_STATE_LISTEN 1
#define MCS_STATE_RESTART 2
#define MCS_STATE_SHUTDOWN 3

// status codes
#define MCS_MSG_OK 				MCP_VERSION " 200 OK"
#define MCS_MSG_BAD_REQUEST 	MCP_VERSION " 401 Bad Request"
#define MCS_MSG_BAD_PARAMS 		MCP_VERSION " 402 Bad Parameters"
#define MCS_MSG_UNAUTHORIZED	MCP_VERSION " 403 Unauthorized"
#define MCS_MSG_SERVER_ERROR 	MCP_VERSION " 500 Server Error"
#define MCS_MSG_ITEM_PLAYING 	MCP_VERSION " 501 Item Already Playing"
#define MCS_MSG_NOT_FOUND 		MCP_VERSION " 502 Not Found"
#define MCS_MSG_TOO_LONG 		MCP_VERSION " 503 Message Too Long"
#define MCS_MSG_NOT_IMPLEMENTED MCP_VERSION " 504 Not Implemented"

struct MCS_Item {
	unsigned int id;
	char* filepath;
	char* label;
	int type;
};

struct MCS_Context {
	// server data
	int port;	
	int state;
	char** dirs;
	int numDirs;

	// item data
	struct MCS_Item** items;
	int size;
	int capacity;
	unsigned int version;

	// only one child process should run at a time
	pid_t child;
	int wpipe; // write to child pipe
	struct MCS_Item* playingItem; // ref to item that is currenty playing
};

struct MCS_Context* MCS_createContext();
void MCS_freeContext(struct MCS_Context* mcc);
void MCS_freeItems(struct MCS_Item** items, int numItems);
int MCS_getItemType(char* filename);
int MCS_handleKillChild(struct MCS_Context* mcc);
int MCS_handlePlayItem(struct MCS_Context* mcc, struct MCS_Item* item);
void MCS_handleRequest(struct MCS_Context* mcc, int clientSocket);
struct MCS_Item* MCS_lookupItem(struct MCS_Item** items, int numItems, unsigned int itemID);
void MCS_parseDirs(struct MCS_Context* mcc);
void MCS_populateList(struct MCS_Context* mcc, int* i, char* dirpath, int dryrun);
void MCS_runServer(struct MCS_Context* mcc);
int MCS_sendInfo(struct MCS_Item* item, int clientSocket);
int MCS_sendItems(struct MCS_Context* mcc, int type, int offset, int length, int clientSocket);
int MCS_sendStatus(struct MCS_Context* mcc, int clientSocket);

#endif
