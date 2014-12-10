#include "mcs.h"

#ifdef MCS_TAGLIB
#include "mcs_taglib.h"
#endif

#ifdef MCS_DEBUG
int MCS_checkIDs(struct MCS_Item** items, int numItems) {
	int i;
	int j;
	for (i = 0; i < numItems - 1; i++) {
		for (j = i + 1; j < numItems; j++) {
			if (items[i]->id == items[j]->id) {
				printf("MCS_checkIDs: %d %s %s\n", items[i]->id,
						items[i]->filepath, items[j]->filepath);
				return -1;
			}
		}
	}

	return 0;
}

long MCS_getSize(struct MCS_Context* mcc) {
	long size = sizeof(struct MCS_Context*) + sizeof(struct MCS_Context);
	
	size += mcc->capacity * sizeof(struct MCS_Item*);
	size += mcc->size * sizeof(struct MCS_Item);

	int i;
	for (i = 0; i < mcc->size; i++) {
		size += strlen(mcc->items[i]->filepath) * sizeof(char);
	}

	size += mcc->numDirs * sizeof(char*);

	return size; 
}
#endif

// SOURCE: http://www.eternallyconfuzzled.com/tuts/algorithms/jsw_tut_hashing.aspx
unsigned int sax_hash(char* msg, int len, int modn) {
	unsigned int h = 0;

	int i;
	for (i = 0; i < len; i++) {
		h ^= (h << 5) + (h >> 2) + msg[i];
	}

	return (h % modn) + modn;
}

struct MCS_Context* MCS_createContext() {
	struct MCS_Context* mcc;
	mcc = (struct MCS_Context*) malloc(sizeof(struct MCS_Context));
	memset(mcc, 0, sizeof(struct MCS_Context)); // just in case

	// server data
	mcc->port = MCS_PORT;
	mcc->state = 0;
	mcc->dirs = NULL;
	mcc->numDirs = 0;

	// item data
	mcc->items = NULL;
	mcc->size = 0;
	mcc->capacity = 0;
	mcc->version = 0;

	// only one child process should run at a time
	mcc->child = 0;
	mcc->wpipe = 0; // write to child pipe
	mcc->playingItem = NULL; // ref to item that is currenty playing

	return mcc;
}

void MCS_freeContext(struct MCS_Context* mcc) {
	MCS_freeItems(mcc->items, mcc->capacity);

	free(mcc->dirs);
	free(mcc);
}

void MCS_freeItems(struct MCS_Item** items, int numItems) {
	int i;
	for (i = 0; i < numItems; i++) {
		struct MCS_Item* item = items[i];

		if (item != NULL) {
			free(item->filepath);
			// free(item->label); // reference
		}

		free(item);
	}

	free(items);
}

int MCS_getItemType(char* filename) {
	char* p = strrchr(filename, '.');

	if (p == NULL)
		return -1;

	int filelen = strlen(filename);
	int extlen = filelen - (p - filename) - 1;
	
	char ext[extlen + 3];
	ext[0] = ':';
	ext[extlen + 1] = ':';
	ext[extlen + 2] = '\0';

	strncpy(ext + 1, p + 1, extlen);

	if (strstr(MCS_EXT_AUDIO, ext) != NULL)
		return MCS_TYPE_AUDIO;	

	if (strstr(MCS_EXT_ROM, ext) != NULL) {
		if (strstr(MCS_EXT_ROM_GB, ext) != NULL)
			return MCS_TYPE_ROM_GB;

		if (strstr(MCS_EXT_ROM_NES, ext) != NULL)
			return MCS_TYPE_ROM_NES;

		return -1;
	}

	if (strstr(MCS_EXT_VIDEO, ext) != NULL)
		return MCS_TYPE_VIDEO;

	return -1;
}

int MCS_handleKillChild(struct MCS_Context* mcc) {
	// if fork wasn't called
	if (mcc->child == 0)
		return 0;

	// close pipe to child process
	close(mcc->wpipe);
	mcc->wpipe = 0;

	if (kill(-mcc->child, SIGINT) < 0) {
		if (kill(-mcc->child, SIGTERM) < 0) {
			if (kill(-mcc->child, SIGKILL) < 0) {
				printf("Process %d could not be killed\n", mcc->child);
				return -1;
			} else {
				printf("Process %d killed by SIGKILL\n", mcc->child);
			}
		} else {
			printf("Process %d killed by SIGTERM\n", mcc->child);
		}
	} else {
		printf("Process %d killed by SIGINT\n", mcc->child);
	}

	wait(0); // collect zombies

	// after this point the child should not exist anymore
	mcc->child = 0;
	mcc->playingItem = NULL;

	return 0;
}

int MCS_handlePlayItem(struct MCS_Context* mcc, struct MCS_Item* item) {
	// check if file exists
	// file can still disappear between access and execv but at least we
	// don't pipe and fork
	if (access(item->filepath, F_OK) < 0) {
		printf("File does not exist: %s\n", item->filepath);
		return -1;
	}

	// create a pipe so that we can pass commands to the child process
    // see http://tldp.org/LDP/lpg/node11.html
	int fds[2];
	
	if (pipe(fds) < 0) {
		printf("Error piping\n");
		exit(1);
	}

	int pid = fork();
	
	if (pid < 0) {
		printf("Failed to fork\n");
		exit(1);
	}

	if (pid == 0) {
		// child
		int r = 0;

		// configure child side of the pipe
		close(fds[1]); // close write to parent
		close(0); // close STDIN
		dup(fds[0]); // connect pipe from exec to STDIN

		// set the process group
		setpgid(0, 0);

		char* bin = NULL;

		switch(item->type) {
		case MCS_TYPE_AUDIO:
			bin = MCS_BIN_AUDIO;
			break;
		case MCS_TYPE_ROM_NES:
			bin = MCS_BIN_ROM_NES;
			break;
		case MCS_TYPE_VIDEO:
			bin = MCS_BIN_VIDEO;
			break;
		default:
#ifdef MCS_BIN_UNKOWN
			bin = MCS_BIN_UNKOWN;
#else
			printf("Unkown item type %d\n", item->type);
			r = 2;
#endif
			break;
		}

		if (bin != NULL) {
			mcc->playingItem = item;

			// split the binary string (with options) into argvs using the
			// the same memory by replacing ' ' with '\0' and setting the
			// pointer correctly
			int slen = strlen(bin);

			char sbin[slen + 1];
			strncpy(sbin, bin, slen);
			sbin[slen] = '\0';

			char* sbinp = sbin;
			char* sbinend = sbin + slen;

			int count = 0;
			while (sbinp < sbinend) {
				if (*sbinp == ' ') {
					count++;
				}

				sbinp++;
			}

			char* argv[count + 2];
			argv[0] = sbin;
			argv[count + 1] = (char*) NULL;

			if (count > 0) {
				int skip = 0;

				count = 1;
				sbinp = sbin;
				while (sbinp < sbinend) {
					// for arguments that are enclosed in '"' ignore ' '
					if (*sbinp == '"') {
						skip = !skip;
					}

					if (!skip && *sbinp == ' ') {
						if (strncmp(sbinp + 1, "%s", 2) == 0) {
							// replace %s with the filepath
							argv[count] = item->filepath;
						} else {
							argv[count] = sbinp + 1;
						}
						
						*sbinp = '\0';
						count++;
					}

					sbinp++;
				}
			}

			r = execv(argv[0], argv);
		} else {
			printf("BIN is null\n");
		}

		close(fds[0]); // close read from parent
		close(fds[1]); // close write to parent

		mcc->playingItem = NULL;

		_exit(r);
	} else {
		// parent
		// configure parent side of the pipe
		close(fds[0]); // close read from child 
	
		mcc->wpipe = fds[1]; // write to child
		mcc->child = pid;
	}

	return 0;	
}

void MCS_handleRequest(struct MCS_Context* mcc, int clientSocket) {
	const int SIZE = 128;
	char* buffer = (char*) malloc((SIZE + 1) * sizeof(char));

	int len = read(clientSocket, buffer, SIZE);

	if (len < 0) {
		printf("Error reading from socket.\n");
		exit(1);
	}

	if (len == 0) {
		free(buffer);
		return;
	}

	// escape the buffer just in case
	buffer[len] = '\0';
	printf("%s (%d)\n", buffer, len);

	if (strncmp("CTRL ", buffer, 5) == 0 && len == 6) {
		if (mcc->wpipe == 0) {
			write(clientSocket, MCS_MSG_SERVER_ERROR, strlen(MCS_MSG_SERVER_ERROR));
			goto free_and_return;
		}

		// only send one char at a time through the pipe
		if (write(mcc->wpipe, buffer + 5, 1) < 0) {
			write(clientSocket, MCS_MSG_SERVER_ERROR, strlen(MCS_MSG_SERVER_ERROR));
		} else {
			write(clientSocket, MCS_MSG_OK, strlen(MCS_MSG_OK));
		}
	} else if (strncmp("INFO ", buffer, 5) == 0 && len > 5) {
		int itemID = atoi(buffer + 5);

		struct MCS_Item* item = MCS_lookupItem(mcc->items, mcc->size, itemID);

		if (item == NULL) {
			write(clientSocket, MCS_MSG_NOT_FOUND, strlen(MCS_MSG_NOT_FOUND));
			goto free_and_return;
		}

		if (MCS_sendInfo(item, clientSocket) < 0) {
			write(clientSocket, MCS_MSG_SERVER_ERROR, strlen(MCS_MSG_SERVER_ERROR));
		} else {
			write(clientSocket, MCS_MSG_OK, strlen(MCS_MSG_OK));
		}
	} else if (strncmp("LIST ", buffer, 5) == 0 && len > 5) {
		int type, offset, length;

		if (sscanf(buffer, "LIST %d %d %d", &type, &offset, &length) != 3) {
			write(clientSocket, MCS_MSG_BAD_REQUEST, strlen(MCS_MSG_BAD_REQUEST));
			goto free_and_return;
		}

		MCS_sendItems(mcc, type, offset, length, clientSocket);
	} else if (strncmp("PLAY ", buffer, 5) == 0 && len > 5) {
		if (mcc->child != 0 || mcc->playingItem != NULL) {
			write(clientSocket, MCS_MSG_ITEM_PLAYING, strlen(MCS_MSG_ITEM_PLAYING));
			goto free_and_return;
		}

		int itemID = atoi(buffer + 5);

		struct MCS_Item* item = MCS_lookupItem(mcc->items, mcc->size, itemID);

		if (item == NULL) {
			write(clientSocket, MCS_MSG_NOT_FOUND, strlen(MCS_MSG_NOT_FOUND));
			goto free_and_return;
		}

		if (MCS_handlePlayItem(mcc, item) < 0) {
			write(clientSocket, MCS_MSG_SERVER_ERROR, strlen(MCS_MSG_SERVER_ERROR));
		} else {
			write(clientSocket, MCS_MSG_OK, strlen(MCS_MSG_OK));
		}
	} else if (strncmp("RESTART ", buffer, 8) == 0 && len > 8) {
		char* p = strchr(buffer, ' ');

		if (len != (8 + strlen(MCS_ADMIN_KEY))
				|| strncmp(p + 1, MCS_ADMIN_KEY, strlen(MCS_ADMIN_KEY)) != 0) {
			write(clientSocket, MCS_MSG_UNAUTHORIZED, strlen(MCS_MSG_UNAUTHORIZED));
			goto free_and_return;
		}

		write(clientSocket, MCS_MSG_OK, strlen(MCS_MSG_OK));
		mcc->state = MCS_STATE_RESTART;
	} else if (strncmp("SHUTDOWN ", buffer, 9) == 0 && len > 9) {
		char* p = strchr(buffer, ' ');

		if (len != (9 + strlen(MCS_ADMIN_KEY))
				|| strncmp(p + 1, MCS_ADMIN_KEY, strlen(MCS_ADMIN_KEY)) != 0) {
			write(clientSocket, MCS_MSG_UNAUTHORIZED, strlen(MCS_MSG_UNAUTHORIZED));
			goto free_and_return;
		} 

		write(clientSocket, MCS_MSG_OK, strlen(MCS_MSG_OK));
		mcc->state = MCS_STATE_SHUTDOWN;
	} else if (strncmp("STAT", buffer, 4) == 0) {
		MCS_sendStatus(mcc, clientSocket);
	} else if (strncmp("STOP", buffer, 4) == 0) {
		if (MCS_handleKillChild(mcc) < 0) {
			write(clientSocket, MCS_MSG_SERVER_ERROR, strlen(MCS_MSG_SERVER_ERROR));
		} else {
			write(clientSocket, MCS_MSG_OK, strlen(MCS_MSG_OK));
		}
	} else {
		write(clientSocket, MCS_MSG_BAD_REQUEST, strlen(MCS_MSG_BAD_REQUEST));
	}

free_and_return:
	free(buffer);
}

struct MCS_Item* MCS_lookupItem(struct MCS_Item** items, int numItems,
			unsigned int itemID) {
	// TODO consider benefits of a hash table lookup
	// this function is only called when the server receives INFO and PLAY
	// commands, which should be rare. in addition, it is very likely that
	// the item count is low, i.e. for movies, it probably wouldn't exceed
	// 1000-10000 items.
	int i;
	for (i = 0; i < numItems; i++) {
		if (items[i]->id == itemID) {
			return items[i];
		}
	}

	return NULL;
}

void MCS_parseDirs(struct MCS_Context* mcc) {
	if (mcc->dirs == NULL)
		return;

	int i = 0;

	// do a dry-run to determine the required list size
	int j;
	for (j = 0; j <	mcc->numDirs; j++) {
		if (mcc->dirs[j] != NULL) {
			MCS_populateList(NULL, &i, mcc->dirs[j], 1);
		}
	}

	if (i > MCS_MAX_ITEMS) {
		i = MCS_MAX_ITEMS;
		printf("Item count capped to %d items.\n", MCS_MAX_ITEMS);
	}

	mcc->capacity = i;
	mcc->items = (struct MCS_Item**) malloc(mcc->capacity *
			sizeof(struct MCS_Item*));
	memset(mcc->items, 0, mcc->capacity * sizeof(struct MCS_Item*));

	printf("Collecting data from:\n");

	i = 0;
	for (j = 0; j < mcc->numDirs; j++) {
		if (mcc->dirs[j] != NULL) {
			printf("%d %s\n", j, mcc->dirs[j]);
			MCS_populateList(mcc, &i, mcc->dirs[j], 0);
		}
	}
	mcc->size = i;
	mcc->version = time(NULL);
}

void MCS_populateList(struct MCS_Context* mcc, int* i, char* dirpath,
		int dryrun) {
	// TODO might be bad "opening" dozens of dirs while going down the
	// directory hierarchy
	DIR* dir = opendir(dirpath);

	if (dir == NULL) {
		printf("Error opening directory. \"%s\"\n", dirpath);
		return;
	}

	int dirlen = strlen(dirpath);
	
	const int SIZE = 256;
	char filepath[SIZE];

	strncpy(filepath, dirpath, dirlen);

	struct dirent* entry;

	while ((entry = readdir(dir))) {
		int type = entry->d_type;
		char* filename = entry->d_name;

		// skip "." and ".." directory entries
		if (type == DT_DIR && filename[0] == '.' && filename[1] == '\0')
			continue;

		if (type == DT_DIR && filename[0] == '.' && filename[1] == '.'
				&& filename[2] == '\0')
			continue;
	
		int filelen = strlen(filename);
		int pathlen = dirlen + filelen;

		if (pathlen >= SIZE) {
			printf("Buffer too small for filename. %d %d\n", pathlen, SIZE);
			exit(1);
		}

		// expand file name
		strncpy(filepath + dirlen, filename, SIZE - dirlen);
		filepath[pathlen] = '\0';
	
		if (type == DT_DIR) {
			// recurse down the hierarchy
			filepath[pathlen] = '/';
			filepath[pathlen + 1] = '\0'; 

			MCS_populateList(mcc, i, filepath, dryrun);
		} else if (type == DT_REG) {
			int type = MCS_getItemType(filename);

			if (type < 0)
				continue;

			if (!dryrun) {
				// calculate ID
				// FIXME buffer size not clear!
				char hashMessage[33 + pathlen + 1];
	
				if (sprintf(hashMessage, "%ld", entry->d_ino) < 0) {
					printf("MCS_populateList: Converting int to string failed\n");
					exit(1);
				}
				
				int nodelen = strlen(hashMessage);
				strncpy(hashMessage + nodelen, strrchr(filepath, '/'),
						pathlen);
				hashMessage[nodelen + pathlen] = '\0';

				// create item and add to list
				struct MCS_Item* item;
				item = (struct MCS_Item*) malloc(sizeof(struct MCS_Item));
				memset(item, 0, sizeof(struct MCS_Item));

				item->id = sax_hash(hashMessage, strlen(hashMessage),
						MCS_HASH_SIZE);

				item->filepath = (char*) malloc((pathlen + 1)
						* sizeof(char));
				strncpy(item->filepath, filepath, pathlen);
				item->filepath[pathlen] = '\0';

				item->label = strrchr(item->filepath, '/') + 1;
				item->type = type;

				mcc->items[*i] = item;
			}

			(*i)++; // important
		}
	
		if (!dryrun && ((*i) == mcc->capacity))
			break;
	}

	closedir(dir);
}

void MCS_runServer(struct MCS_Context* mcc) {
	int serverSocket = socket(AF_INET, SOCK_STREAM, 0);

	if (serverSocket < 0) {
		printf("Error opening socket.\n");
		exit(1);
	}

	struct sockaddr_in serverAddress;
	memset(&serverAddress, 0, sizeof(serverAddress));

	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(mcc->port);
	
	if (bind(serverSocket, (struct sockaddr*) &serverAddress,
			sizeof(serverAddress)) < 0) {
		close(serverSocket);
		printf("Error binding to port %d\n", mcc->port);
		return;
	}

	printf("Listening on port %d\n", mcc->port);
	mcc->state = MCS_STATE_LISTEN;

	while (mcc->state == MCS_STATE_LISTEN) {
		listen(serverSocket, 5);
		struct sockaddr_in clientAddress;

		socklen_t clen = sizeof(clientAddress);
		int clientSocket = accept(serverSocket,
				(struct sockaddr*) &clientAddress, &clen);

		if (clientSocket < 0) {
			printf("Error accepting connection.\n");
			exit(1);
		}

		printf("Handling client %s\n", inet_ntoa(clientAddress.sin_addr));

		MCS_handleRequest(mcc, clientSocket);

		if (close(clientSocket) < 0) {
			printf("Error closing client socket.\n");
		}

		// the idea is to update the item list without closing the socket
		// or else we'll have to wait before we can open it again
		if (mcc->state == MCS_STATE_RESTART) {
			MCS_handleKillChild(mcc);
			MCS_freeItems(mcc->items, mcc->capacity);
			MCS_parseDirs(mcc);

			mcc->state = MCS_STATE_LISTEN;
		}
	}

	// kills the child process if there is one
	MCS_handleKillChild(mcc);

	if (close(serverSocket) < 0) {
		printf("Error closing server socket.\n");
		exit(1);
	}

	printf("Server stopped\n");
	return;
}

int MCS_sendInfo(struct MCS_Item* item,	int clientSocket) {
#ifdef MCS_TAGLIB
	return MCS_sendTagLibInfo(item, clientSocket);
#else
	write(clientSocket, MCS_MSG_NOT_IMPLEMENTED, strlen(MCS_MSG_NOT_IMPLEMENTED));
	return 0; 
#endif
}

int MCS_sendItems(struct MCS_Context* mcc, int type, int offset, int length,
		int clientSocket) {
	if (type < 0 || offset < 0 || length < 1 || offset >= mcc->size
			|| length > MCS_MAX_ITEMS) {
		write(clientSocket, MCS_MSG_BAD_PARAMS, strlen(MCS_MSG_BAD_PARAMS));
		return -1;
	}

	const int SIZE = 10000;
	char* buffer = (char*) malloc((SIZE + 1) * sizeof(char));

	char* buffp = buffer;
	char* buffend = buffer + SIZE;

	int plen = snprintf(buffp, SIZE,
			"%s\n\n"
			"<mediacenter>"
			"<items version=\"%d\" type=\"%d\" offset=\"%d\" length=\"%d\">",
			MCS_MSG_OK, mcc->version, type, offset, length);
	
	if (plen < 0) {
		printf("Error writing to buffer\n");
		exit(1);
	}

	buffp += plen;

	int i;
	for (i = 0; i < mcc->size; i++) {
		if (length == 0 || (type == 0 && i + offset >= mcc->size))
			break;

		struct MCS_Item* item;

		if (type == 0) {
			// if the type is 0, meaning list all types of items
			// the offset is real
			item = mcc->items[i + offset];
		} else {
			int itemType = mcc->items[i]->type;
			int base = itemType - (itemType % MCS_TYPE_BASE);

			if (type != 0 && type != itemType && type != base)
				continue;

			if (offset > 0) {
				offset--;
				continue;
			}

			item = mcc->items[i];
		}

		plen = snprintf(buffp, buffend - buffp,
				"<item id=\"%d\" type=\"%d\" label=\"%s\"/>", item->id,
				item->type, item->label);

		if (plen < 0) {
			printf("Error writing to buffer\n");
			exit(1);
		}

		buffp += plen;

		if (buffp > buffend) {
			write(clientSocket, MCS_MSG_TOO_LONG, strlen(MCS_MSG_TOO_LONG));
			printf("MCS_sendItems: Buffer too small\n");
			free(buffer);
			return -1;
		}

		// count down number of items
		length--;
	}

	plen = snprintf(buffp, buffend - buffp, "</items></mediacenter>");
#ifdef MCS_DEBUG
	printf(">> %d %d\n", plen, buffend - buffp);
#endif	
	if (plen < 0) {
		printf("Error writing to buffer\n");
		exit(1);
	}

	buffp += plen;

	if (buffp > buffend) {
		write(clientSocket, MCS_MSG_TOO_LONG, strlen(MCS_MSG_TOO_LONG));
		printf("MCS_sendItems: Buffer too small\n");
		free(buffer);
		return -1;
	}

	*buffp = '\0';

	if (write(clientSocket, buffer, buffp - buffer) < 0) {
		printf("Error writing to socket.\n");
		exit(1);
	}

	free(buffer);
	return 0;
}

int MCS_sendStatus(struct MCS_Context* mcc, int clientSocket) {
	const int SIZE = 512;
	char* buffer = (char*) malloc((SIZE + 1) * sizeof(char));

	int len = snprintf(buffer, SIZE,
			"%s\n\n"
			"<mediacenter><status>"
			"<items version=\"%d\" size=\"%d\"/>"
			"<types>"
			"<type id=\"%d\" name=\"audio\"/>"
			"<type id=\"%d\" name=\"rom\"/>"
			"<type id=\"%d\" name=\"rom/gb\">"
			"<type id=\"%d\" name=\"rom/nes\">"
			"<type id=\"%d\" name=\"video\"/>"
			"</types>"
			"</status></mediacenter>",
			MCS_MSG_OK, mcc->version, mcc->size,
			MCS_TYPE_AUDIO, MCS_TYPE_ROM, MCS_TYPE_ROM_GB,
			MCS_TYPE_ROM_NES, MCS_TYPE_VIDEO);

	if (len < 0) {
		printf("MCS_sendStatus: Failed to write to buffer\n");
		exit(1);
	}

	if (len > SIZE) {
		write(clientSocket, MCS_MSG_TOO_LONG, strlen(MCS_MSG_TOO_LONG));
		printf("Status buffer size too small %d. Needed %d\n", SIZE, len);
		free(buffer);
		return -1;
	}

	buffer[len] = '\0';

	if (write(clientSocket, buffer, len) < 0) {
		printf("MCS_sendStatus: Failed to write to socket.\n");
		free(buffer);
		return -1;
	}
	
	free(buffer);
	return 0;
}

int main(int argc, char* argv[]) {
	if (argc <= 1) {
		printf("Not enough arguments provided\n");
		return -1;
	}

	struct MCS_Context* mcc = MCS_createContext();

	mcc->port = MCS_PORT;

	mcc->numDirs = argc - 1;
	mcc->dirs = (char**) malloc(mcc->numDirs * sizeof(char*));
	memcpy(mcc->dirs, &argv[1], mcc->numDirs * sizeof(char*));

	MCS_parseDirs(mcc);

	printf("Items: %d/%d\n", mcc->size, mcc->capacity);
#ifdef MCS_DEBUG
	printf("Unique: %d\n", MCS_checkIDs(mcc->items, mcc->size));
	printf("Alloc'd %ld bytes\n", MCS_getSize(mcc));
#endif
	MCS_runServer(mcc);

	MCS_freeContext(mcc);

	return 0;
}
