#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "message_protocol.h"

int main(void) {
    // Define test inputs.
    const char *original_type = "sdp_offer";
    const char *original_data = "v=0\r\no=- 4611735268880197856 2 IN IP4 127.0.0.1\ns=-\nc=IN IP4 127.0.0.1\n";

    // Create a JSON message.
    char *json_str = create_json_message(original_type, original_data);
    if (!json_str) {
        fprintf(stderr, "Failed to create JSON message\n");
        return EXIT_FAILURE;
    }
    printf("Created JSON message:\n%s\n\n", json_str);

    // Parse the JSON message.
    char *parsed_data = NULL;
    MessageType msgType = parse_json_message(json_str, &parsed_data);

    // Print the parsed message type.
    printf("Parsed message type: ");
    switch (msgType) {
        case MSG_TYPE_SDP_OFFER:
            printf("SDP Offer");
            break;
        case MSG_TYPE_SDP_ANSWER:
            printf("SDP Answer");
            break;
        case MSG_TYPE_ICE_CANDIDATE:
            printf("ICE Candidate");
            break;
        default:
            printf("Unknown");
    }
    printf("\n");

    // Print the parsed data.
    if (parsed_data) {
        printf("Parsed data:\n%s\n", parsed_data);
    } else {
        printf("No data parsed.\n");
    }

    // Compare original and parsed data.
    if (strcmp(original_data, parsed_data) == 0) {
        printf("\nTest passed: Parsed data matches the original.\n");
    } else {
        printf("\nTest failed: Parsed data does not match the original.\n");
    }

    // Clean up.
    free(json_str);
    free(parsed_data);

    return EXIT_SUCCESS;
}
