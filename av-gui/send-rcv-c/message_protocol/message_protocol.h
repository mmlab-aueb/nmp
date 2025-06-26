#ifndef MESSAGE_PROTOCOL_H
#define MESSAGE_PROTOCOL_H

#include <cJSON.h>

// Enum for message types
typedef enum {
    MSG_TYPE_SDP_OFFER,
    MSG_TYPE_SDP_ANSWER,
    MSG_TYPE_ICE_CANDIDATE,
    MSG_TYPE_ROLE,
    MSG_TYPE_UNKNOWN
} MessageType;

/**
 * Creates a JSON string representing a message with a type and data.
 *
 * @param type   The message type as a string (e.g., "sdp_offer", "sdp_answer", "ice_candidate").
 * @param data   The message data as a string (e.g., an SDP description or ICE candidate).
 *
 * @return       A dynamically allocated JSON string in the format:
 *               {"type":"<type>","data":"<data>"}
 *               - If memory allocation or JSON creation fails, the function returns `NULL`.
 *               - The caller is responsible for freeing the returned string using `free()`.
 *
 * Notes:
 * - This function uses `cJSON` to create and format the JSON string.
 * - The returned JSON string is unformatted (no extra whitespace for readability).
 */
char* create_json_message(const char *type, const char *data);

/**
 * Parses a JSON string and extracts the message type and data.
 *
 * @param json_str   The input JSON string to be parsed (e.g., '{"type":"sdp_offer","data":"..."}').
 * @param data_out   A pointer to a `char*` that will hold the value of the "data" field in the JSON.
 *                   The memory for this string is dynamically allocated, and the caller is responsible
 *                   for freeing it using `free()`.
 * 
 * @return           A `MessageType` enum indicating the type of the message:
 *                   - MSG_TYPE_SDP_OFFER: If the "type" field is "sdp_offer"
 *                   - MSG_TYPE_SDP_ANSWER: If the "type" field is "sdp_answer"
 *                   - MSG_TYPE_ICE_CANDIDATE: If the "type" field is "ice_candidate"
 *                   - MSG_TYPE_UNKNOWN: If the "type" field is missing, invalid, or unrecognized.
 *
 * Notes:
 * - If the JSON string is invalid, the function returns MSG_TYPE_UNKNOWN and sets `*data_out` to NULL.
 * - The caller must free the memory for `*data_out` if it is not NULL.
 */
MessageType parse_json_message(const char *json_str, char **data_out);

#endif

