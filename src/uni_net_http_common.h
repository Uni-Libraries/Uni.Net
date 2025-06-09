#pragma once

//
// Includes
//

// stdlib
#include <stddef.h>
#include <stdint.h>

//
// Enums
//

typedef enum {
    UNI_NET_HTTP_COMMAND_UNKNOWN,
    UNI_NET_HTTP_COMMAND_GET,
    UNI_NET_HTTP_COMMAND_POST,
    UNI_NET_HTTP_COMMAND_HEAD,
    UNI_NET_HTTP_COMMAND_PUT,
    UNI_NET_HTTP_COMMAND_DELETE,
    UNI_NET_HTTP_COMMAND_TRACE,
    UNI_NET_HTTP_COMMAND_OPTIONS,
    UNI_NET_HTTP_COMMAND_CONNECT,
    UNI_NET_HTTP_COMMAND_PATCH,
} uni_net_http_command_type_e;


typedef enum {
    UNI_NET_HTTP_STATUS_OK              = 200,
    UNI_NET_HTTP_STATUS_NOCONTENT       = 204,
    UNI_NET_HTTP_STATUS_BADREQUEST      = 400,
    UNI_NET_HTTP_STATUS_UNAUTHORIZED    = 401,
    UNI_NET_HTTP_STATUS_NOTFOUND        = 404,
    UNI_NET_HTTP_STATUS_GONE            = 410,
    UNI_NET_HTTP_STATUS_PRECONDFAILED   = 412,
    UNI_NET_HTTP_STATUS_INTERNALSERVERR = 500,
} uni_net_http_status_e;



//
// Typedefs
//

typedef size_t (*uni_net_http_file_formatter_fn)(uint8_t* buf_out, size_t buf_out_size, const uint8_t* buf_in, size_t buf_in_len);



//
// Structs
//

typedef struct {
    const char* file_name;
    const uint8_t* file_data;
    const uint32_t file_size;
    uni_net_http_command_type_e handler_type;
    uni_net_http_file_formatter_fn formatter;
} uni_net_http_file_t;

typedef struct
{
    const char * extension;
    const char * type;
} uni_net_http_type_couple_t;



//
// Functions
//

const char* uni_net_http_get_mime_type(const char* extension);
