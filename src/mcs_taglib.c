#include "mcs_taglib.h"

int MCS_sendTagLibInfo(struct MCS_Item* item, int clientSocket) {
	int base = item->type - (item->type % MCS_TYPE_BASE);

	if (base != MCS_TYPE_AUDIO && base != MCS_TYPE_VIDEO)
		return MCS_ERR_NOT_FOUND;

	const int SIZE = 1024;
	char* buffer = (char*) malloc((SIZE + 1) * sizeof(char));

	char* buffp = buffer;
	char* buffend = buffer + SIZE;

	int plen = snprintf(buffp, SIZE,
			"%s %d %s\n\n"
			"<mediacenter>"
			"<item id=\"%d\" type=\"%d\" label=\"%s\">",
			MCP_VERSION, MCS_ERR_OK, MCS_MSG_OK,
			item->id, item->type, item->label);
	
	if (plen < 0) {
		printf("MCS_sendTagLibInfo: Error writing to buffer\n");
		free(buffer);
		return MCS_ERR_SERVER_ERROR;
	}

	buffp += plen;

	// get tag data and properties
	taglib_set_strings_unicode(0);

	TagLib_File* file = taglib_file_new(item->filepath);

	if (file == NULL) {
		printf("MCS_sendTagLibInfo: File not found. %s\n", item->filepath);
		free(buffer);
		return MCS_ERR_NOT_FOUND;
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
			free(buffer);
			return MCS_ERR_SERVER_ERROR;
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
			free(buffer);
			return MCS_ERR_SERVER_ERROR;
		}

		buffp += plen;
	}

	taglib_tag_free_strings();
	taglib_file_free(file);

	// close XML tags and send to client
	plen = snprintf(buffp, buffend - buffp,
			"</item></mediacenter>");

	if (plen < 0) {
		printf("MCS_sendTagLibInfo: Error writing to buffer\n");
		free(buffer);
		return MCS_ERR_SERVER_ERROR;
	}

	buffp += plen;

	if (buffp > buffend) {
		printf("MCS_sendTagLibInfo: Buffer too small\n");
		free(buffer);
		return MCS_ERR_TOO_LONG;
	}
 
	*buffp = '\0';
#ifdef MCS_DEBUG
	printf("strlen: %d %d %d\n", strlen(buffer), buffp - buffer,
			buffend - buffp);
#endif
	if (write(clientSocket, buffer, buffp - buffer) < 0) {
		printf("MCS_sendTagLibInfo: Error writing to socket.\n");
		free(buffer);
		return -1;
	}

	free(buffer);
	return MCS_ERR_OK;
}
