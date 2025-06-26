#include <stdlib.h>
#include <string.h>
#include "message_protocol.h"

char* create_json_message(const char *type, const char *data) {
    cJSON *root = cJSON_CreateObject(); 
    if (!root)
        return NULL; 

    cJSON_AddStringToObject(root, "type", type);
    cJSON_AddStringToObject(root, "data", data);

    char *json_str = cJSON_PrintUnformatted(root);

    cJSON_Delete(root);

    return json_str;
}

MessageType parse_json_message(const char *json_str, char **data_out) {
    MessageType type = MSG_TYPE_UNKNOWN;
    *data_out = NULL;

    cJSON *root = cJSON_Parse(json_str);
    if (!root)
        return type;

    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    cJSON *data_item = cJSON_GetObjectItem(root, "data");
    if (cJSON_IsString(type_item) && cJSON_IsString(data_item)) {
        // Set data_out as a duplicate of the parsed string (caller must free)
        *data_out = strdup(data_item->valuestring);

        if (strcmp(type_item->valuestring, "sdp_offer") == 0)
            type = MSG_TYPE_SDP_OFFER;
        else if (strcmp(type_item->valuestring, "sdp_answer") == 0)
            type = MSG_TYPE_SDP_ANSWER;
        else if (strcmp(type_item->valuestring, "ice_candidate") == 0)
            type = MSG_TYPE_ICE_CANDIDATE;
    }

    cJSON_Delete(root);
    return type;
}

