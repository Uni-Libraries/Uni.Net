//
// Includes
//

// stdlib
#include <string.h>
#include <ctype.h>

// Uni.net
#include "uni_net_http_common.h"



//
// Defines
//

#ifndef ARRAY_SIZE
    #define  ARRAY_SIZE( x )    ( sizeof( x ) / sizeof( x )[ 0 ] )
#endif

//
// Globals
//

uni_net_http_type_couple_t g_uni_net_http_types[] =
{
    { "html", "text/html"              },
    { "json", "application/json"       },
    { "css",  "text/css"               },
    { "js",   "text/javascript"        },
    { "png",  "image/png"              },
    { "jpg",  "image/jpeg"             },
    { "gif",  "image/gif"              },
    { "txt",  "text/plain"             },
    { "mp3",  "audio/mpeg3"            },
    { "wav",  "audio/wav"              },
    { "flac", "audio/ogg"              },
    { "pdf",  "application/pdf"        },
    { "ttf",  "application/x-font-ttf" },
    { "ttc",  "application/x-font-ttf" }
};



//
// Functions
//

const char* uni_net_http_get_mime_type(const char* extension)
{
    const char* result = "application/octet-stream";
    if (extension)
    {
        for (size_t x = 0; x < ARRAY_SIZE(g_uni_net_http_types); x++) {
            if (strcasecmp(extension, g_uni_net_http_types[x].extension) == 0) {
                result = g_uni_net_http_types[x].type;
                break;
            }
        }
    }

    return result;
}
