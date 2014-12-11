#include "mcs_taglib.h"

int MCS_sendTagLibInfo(struct MCS_Item* item, int clientSocket) {
	const int SIZE = 1024;
	char* buffer = (char*) malloc((SIZE + 1) * sizeof(char));

	char* buffp = buffer;
	char* buffend = buffer + SIZE;

	int plen = snprintf(buffp, SIZE,
			"%s\n\n"
			"<mediacenter>"
			"<item id=\"%d\" type=\"%d\" label=\"%s\">",
			MCS_MSG_OK, item->id, item->type, item->label);
	
	if (plen < 0) {
		printf("MCS_sendTagLibInfo: Error writing to buffer\n");
		exit(1);
	}

	buffp += plen;

	switch (item->type) {
	case MCS_TYPE_AUDIO:
	case MCS_TYPE_VIDEO:
		taglib_set_strings_unicode(0);

		TagLib_File* file = taglib_file_new(item->filepath);

		if (file == NULL) {
			write(clientSocket, MCS_MSG_NOT_FOUND, strlen(MCS_MSG_NOT_FOUND));
			break;
		}

		TagLib_Tag* tag = taglib_file_tag(file);

		if (tag != NULL) {
			plen = snprintf(buffp, buffend - buffp,
					"<tag>"
					"<title>%s</title>"
					"<artist>%s</artist>"
					"<album>%s</album>"
					"<year>%d</year>"
					"<comment>%s</comment>"
					"<track>%d</track>"
					"<genre>%s</genre>"
					"</tag>",
					taglib_tag_title(tag),
					taglib_tag_artist(tag),
					taglib_tag_album(tag),
					taglib_tag_year(tag),
					taglib_tag_comment(tag),
					taglib_tag_track(tag),
					taglib_tag_genre(tag));
			
			if (plen < 0) {
				printf("MCS_sendTagLibInfo: Error writing to buffer (1)\n");
				exit(1);
			}

			buffp += plen;
		}

		const TagLib_AudioProperties* properties;
		properties = taglib_file_audioproperties(file);

		if (properties != NULL) {
			plen = snprintf(buffp, buffend - buffp,
					"<properties>"
					"<bitrate>%d</bitrate>"
					"<samplerate>%d</samplerate>"
					"<channels>%d</channels>"
					"<length>%d</length>"
					"</properties>",
					taglib_audioproperties_bitrate(properties),
					taglib_audioproperties_samplerate(properties),
					taglib_audioproperties_channels(properties),
					taglib_audioproperties_length(properties));
			
			if (plen < 0) {
				printf("MCS_sendTagLibInfo: Error writing to buffer (2)\n");
				exit(1);
			}

			buffp += plen;
		}

		taglib_tag_free_strings();
		taglib_file_free(file);
		break;
	default:
		write(clientSocket, MCS_MSG_NOT_FOUND, strlen(MCS_MSG_NOT_FOUND));
		printf("No info available for file type. %d\n", item->type);
		break;
	}

	plen = snprintf(buffp, buffend - buffp,
			"</item></mediacenter>");

	if (plen < 0) {
		printf("MCS_sendTagLibInfo: Error writing to buffer\n");
		exit(1);
	}

	buffp += plen;

	if (buffp > buffend) {
		write(clientSocket, MCS_MSG_TOO_LONG, strlen(MCS_MSG_TOO_LONG));
		printf("MCS_sendTagLibInfo: Buffer too small\n");
		free(buffer);
		return -1;
	}
 
	*buffp = '\0';
#ifdef MCS_DEBUG
	printf("strlen: %d %d %d\n", strlen(buffer), buffp - buffer,
			buffend - buffp);
#endif
	if (write(clientSocket, buffer, buffp - buffer) < 0) {
		printf("MCS_sendTagLibInfo: Error writing to socket.\n");
		exit(1);
	}

	free(buffer);
	return 0;
}
