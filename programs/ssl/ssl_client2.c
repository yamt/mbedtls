/*
 *  SSL client with certificate authentication
 *
 *  Copyright (C) 2006-2015, ARM Limited, All Rights Reserved
 *  SPDX-License-Identifier: Apache-2.0
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may
 *  not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 *  This file is part of mbed TLS (https://tls.mbed.org)
 */

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_PLATFORM_C)
#include "mbedtls/platform.h"
#else
#include <stdio.h>
#include <stdlib.h>
#define mbedtls_time       time
#define mbedtls_time_t     time_t
#define mbedtls_printf     printf
#define mbedtls_fprintf    fprintf
#define mbedtls_snprintf   snprintf
#define mbedtls_exit            exit
#define mbedtls_calloc      calloc
#define mbedtls_free        free
#define MBEDTLS_EXIT_SUCCESS    EXIT_SUCCESS
#define MBEDTLS_EXIT_FAILURE    EXIT_FAILURE
#endif

#if !defined(MBEDTLS_ENTROPY_C) || \
    !defined(MBEDTLS_SSL_TLS_C) || \
    !defined(MBEDTLS_SSL_CLI_C) || \
    !defined(MBEDTLS_NET_C)     || \
    !( defined(MBEDTLS_CTR_DRBG_C) || defined(MBEDTLS_HMAC_DRBG_C) )
int main( void )
{
    mbedtls_printf("MBEDTLS_ENTROPY_C and/or "
           "MBEDTLS_SSL_TLS_C and/or MBEDTLS_SSL_CLI_C and/or "
           "MBEDTLS_NET_C not defined, or "
           "neither MBEDTLS_CTR_DRBG_C nor MBEDTLS_HMAC_DRBG_C defined.\n");
    mbedtls_exit( 0 );
}
#else

#include "mbedtls/net_sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/ssl_ciphersuites.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/hmac_drbg.h"
#include "mbedtls/certs.h"
#include "mbedtls/x509.h"
#include "mbedtls/error.h"
#include "mbedtls/debug.h"
#include "mbedtls/timing.h"

#include "mbedtls/ssl_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_REQUEST_SIZE      20000
#define MAX_REQUEST_SIZE_STR "20000"

#define DFL_SERVER_NAME         "localhost"
#define DFL_SERVER_ADDR         NULL
#define DFL_SERVER_PORT         "4433"
#define DFL_REQUEST_PAGE        "/"
#define DFL_REQUEST_SIZE        -1
#define DFL_DEBUG_LEVEL         0
#define DFL_NBIO                0
#define DFL_EVENT               0
#define DFL_READ_TIMEOUT        0
#define DFL_MAX_RESEND          0
#define DFL_CA_FILE             ""
#define DFL_CA_PATH             ""
#define DFL_CRT_FILE            ""
#define DFL_KEY_FILE            ""
#define DFL_PSK                 ""
#define DFL_PSK_IDENTITY        "Client_identity"
#define DFL_ECJPAKE_PW          NULL
#define DFL_EC_MAX_OPS          -1
#define DFL_FORCE_CIPHER        0
#define DFL_RENEGOTIATION       MBEDTLS_SSL_RENEGOTIATION_DISABLED
#define DFL_ALLOW_LEGACY        -2
#define DFL_RENEGOTIATE         0
#define DFL_EXCHANGES           1
#define DFL_MIN_VERSION         -1
#define DFL_MAX_VERSION         -1
#define DFL_ARC4                -1
#define DFL_SHA1                -1
#define DFL_AUTH_MODE           -1
#define DFL_MFL_CODE            MBEDTLS_SSL_MAX_FRAG_LEN_NONE
#define DFL_TRUNC_HMAC          -1
#define DFL_RECSPLIT            -1
#define DFL_DHMLEN              -1
#define DFL_RECONNECT           0
#define DFL_RECO_DELAY          0
#define DFL_RECO_MODE           1
#define DFL_CID_ENABLED         0
#define DFL_CID_VALUE           ""
#define DFL_CID_ENABLED_RENEGO  -1
#define DFL_CID_VALUE_RENEGO    NULL
#define DFL_RECONNECT_HARD      0
#define DFL_TICKETS             MBEDTLS_SSL_SESSION_TICKETS_ENABLED
#define DFL_ALPN_STRING         NULL
#define DFL_CURVES              NULL
#define DFL_TRANSPORT           MBEDTLS_SSL_TRANSPORT_STREAM
#define DFL_HS_TO_MIN           0
#define DFL_HS_TO_MAX           0
#define DFL_DTLS_MTU            -1
#define DFL_DGRAM_PACKING        1
#define DFL_FALLBACK            -1
#define DFL_EXTENDED_MS         -1
#define DFL_ETM                 -1
#define DFL_SERIALIZE           0
#define DFL_EXTENDED_MS_ENFORCE -1
#define DFL_SKIP_CLOSE_NOTIFY   0

#define GET_REQUEST "GET %s HTTP/1.0\r\nExtra-header: "
#define GET_REQUEST_END "\r\n\r\n"

#if defined(MBEDTLS_X509_CRT_PARSE_C)
#if defined(MBEDTLS_FS_IO)
#define USAGE_IO \
    "    ca_file=%%s          The single file containing the top-level CA(s) you fully trust\n" \
    "                        default: \"\" (pre-loaded)\n" \
    "                        use \"none\" to skip loading any top-level CAs.\n" \
    "    ca_path=%%s          The path containing the top-level CA(s) you fully trust\n" \
    "                        default: \"\" (pre-loaded) (overrides ca_file)\n" \
    "                        use \"none\" to skip loading any top-level CAs.\n" \
    "    crt_file=%%s         Your own cert and chain (in bottom to top order, top may be omitted)\n" \
    "                        default: \"\" (pre-loaded)\n" \
    "    key_file=%%s         default: \"\" (pre-loaded)\n"
#else
#define USAGE_IO \
    "    No file operations available (MBEDTLS_FS_IO not defined)\n"
#endif /* MBEDTLS_FS_IO */
#else
#define USAGE_IO ""
#endif /* MBEDTLS_X509_CRT_PARSE_C */

#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
#define USAGE_CID \
    "    cid=%%d             Disable (0) or enable (1) the use of the DTLS Connection ID extension.\n" \
    "                       default: 0 (disabled)\n"     \
    "    cid_renego=%%d      Disable (0) or enable (1) the use of the DTLS Connection ID extension during renegotiation.\n" \
    "                       default: same as 'cid' parameter\n"     \
    "    cid_val=%%s          The CID to use for incoming messages (in hex, without 0x).\n"  \
    "                        default: \"\"\n" \
    "    cid_val_renego=%%s   The CID to use for incoming messages (in hex, without 0x) after renegotiation.\n"  \
    "                        default: same as 'cid_val' parameter\n"
#else /* MBEDTLS_SSL_DTLS_CONNECTION_ID */
#define USAGE_CID ""
#endif /* MBEDTLS_SSL_DTLS_CONNECTION_ID */

#if defined(MBEDTLS_KEY_EXCHANGE__SOME__PSK_ENABLED)
#define USAGE_PSK                                                   \
    "    psk=%%s              default: \"\" (in hex, without 0x)\n" \
    "    psk_identity=%%s     default: \"Client_identity\"\n"
#else
#define USAGE_PSK ""
#endif /* MBEDTLS_KEY_EXCHANGE__SOME__PSK_ENABLED */

#if defined(MBEDTLS_SSL_SESSION_TICKETS)
#define USAGE_TICKETS                                       \
    "    tickets=%%d          default: 1 (enabled)\n"
#else
#define USAGE_TICKETS ""
#endif /* MBEDTLS_SSL_SESSION_TICKETS */

#if defined(MBEDTLS_SSL_TRUNCATED_HMAC)
#define USAGE_TRUNC_HMAC                                    \
    "    trunc_hmac=%%d       default: library default\n"
#else
#define USAGE_TRUNC_HMAC ""
#endif /* MBEDTLS_SSL_TRUNCATED_HMAC */

#if defined(MBEDTLS_SSL_MAX_FRAGMENT_LENGTH)
#define USAGE_MAX_FRAG_LEN                                      \
    "    max_frag_len=%%d     default: 16384 (tls default)\n"   \
    "                        options: 512, 1024, 2048, 4096\n"
#else
#define USAGE_MAX_FRAG_LEN ""
#endif /* MBEDTLS_SSL_MAX_FRAGMENT_LENGTH */

#if defined(MBEDTLS_SSL_CBC_RECORD_SPLITTING)
#define USAGE_RECSPLIT \
    "    recsplit=0/1        default: (library default: on)\n"
#else
#define USAGE_RECSPLIT
#endif

#if defined(MBEDTLS_DHM_C)
#define USAGE_DHMLEN \
    "    dhmlen=%%d           default: (library default: 1024 bits)\n"
#else
#define USAGE_DHMLEN
#endif

#if defined(MBEDTLS_SSL_ALPN)
#define USAGE_ALPN \
    "    alpn=%%s             default: \"\" (disabled)\n"   \
    "                        example: spdy/1,http/1.1\n"
#else
#define USAGE_ALPN ""
#endif /* MBEDTLS_SSL_ALPN */

#if defined(MBEDTLS_ECP_C) && !defined(MBEDTLS_SSL_CONF_SINGLE_EC)
#define USAGE_CURVES \
    "    curves=a,b,c,d      default: \"default\" (library default)\n"  \
    "                        example: \"secp521r1,brainpoolP512r1\"\n"  \
    "                        - use \"none\" for empty list\n"           \
    "                        - see mbedtls_ecp_curve_list()\n"          \
    "                          for acceptable curve names\n"
#else
#define USAGE_CURVES ""
#endif

#if defined(MBEDTLS_SSL_PROTO_DTLS)
#define USAGE_DTLS \
    "    dtls=%%d             default: 0 (TLS) (if both enabled)\n" \
    "    hs_timeout=%%d-%%d    default: (library default: 1000-60000)\n"    \
    "                        range of DTLS handshake timeouts in millisecs\n" \
    "    mtu=%%d              default: (library default: unlimited)\n"  \
    "    dgram_packing=%%d    default: 1 (allowed)\n"                   \
    "                        allow or forbid packing of multiple\n" \
    "                        records within a single datgram.\n"
#else
#define USAGE_DTLS ""
#endif

#if defined(MBEDTLS_SSL_FALLBACK_SCSV)
#define USAGE_FALLBACK \
    "    fallback=0/1        default: (library default: off)\n"
#else
#define USAGE_FALLBACK ""
#endif

#if defined(MBEDTLS_SSL_EXTENDED_MASTER_SECRET) &&       \
    !defined(MBEDTLS_SSL_CONF_EXTENDED_MASTER_SECRET) && \
    !defined(MBEDTLS_SSL_CONF_ENFORCE_EXTENDED_MASTER_SECRET)
#define USAGE_EMS \
    "    extended_ms=0/1     default: (library default: on)\n" \
    "    enforce_extended_master_secret=0/1 default: (library default: off)\n"
#else
#define USAGE_EMS ""
#endif

#if defined(MBEDTLS_SSL_ENCRYPT_THEN_MAC)
#define USAGE_ETM \
    "    etm=0/1             default: (library default: on)\n"
#else
#define USAGE_ETM ""
#endif

#if defined(MBEDTLS_SSL_RENEGOTIATION)
#define USAGE_RENEGO \
    "    renegotiation=%%d    default: 0 (disabled)\n"      \
    "    renegotiate=%%d      default: 0 (disabled)\n"
#else
#define USAGE_RENEGO ""
#endif

#if defined(MBEDTLS_KEY_EXCHANGE_ECJPAKE_ENABLED)
#define USAGE_ECJPAKE \
    "    ecjpake_pw=%%s       default: none (disabled)\n"
#else
#define USAGE_ECJPAKE ""
#endif

#if defined(MBEDTLS_ECP_RESTARTABLE)
#define USAGE_ECRESTART \
    "    ec_max_ops=%%s       default: library default (restart disabled)\n"
#else
#define USAGE_ECRESTART ""
#endif

#if defined(MBEDTLS_SSL_CONTEXT_SERIALIZATION)
#define USAGE_SERIALIZATION \
    "    serialize=%%d        default: 0 (do not serialize/deserialize)\n" \
    "                        options: 1 (serialize)\n"                    \
    "                                 2 (serialize with re-initialization)\n"
#else
#define USAGE_SERIALIZATION ""
#endif

#if !defined(MBEDTLS_SSL_CONF_AUTHMODE)
#define USAGE_AUTH_MODE \
    "    auth_mode=%%s        default: (library default: none)\n" \
    "                        options: none, optional, required\n"
#else
#define USAGE_AUTH_MODE ""
#endif

#if !defined(MBEDTLS_SSL_CONF_ALLOW_LEGACY_RENEGOTIATION)
#define USAGE_ALLOW_LEGACY_RENEGO    "    allow_legacy=%%d     default: (library default: no)\n"
#else
#define USAGE_ALLOW_LEGACY_RENEGO ""
#endif

#if !defined(MBEDTLS_SSL_CONF_READ_TIMEOUT)
#define USAGE_READ_TIMEOUT                              \
    "    read_timeout=%%d     default: 0 ms (no timeout)\n"
#else
#define USAGE_READ_TIMEOUT ""
#endif

#if !defined(MBEDTLS_SSL_CONF_MIN_MINOR_VER) || \
    !defined(MBEDTLS_SSL_CONF_MIN_MAJOR_VER) || \
    !defined(MBEDTLS_SSL_CONF_MAX_MINOR_VER) || \
    !defined(MBEDTLS_SSL_CONF_MAX_MAJOR_VER)
#define USAGE_MAX_VERSION    "    max_version=%%s      default: (library default: tls1_2)\n"
#define USAGE_MIN_VERSION    "    min_version=%%s      default: (library default: tls1)\n"
#define USAGE_FORCE_VERSION    "    force_version=%%s    default: \"\" (none)\n"       \
           "                        options: ssl3, tls1, tls1_1, tls1_2, dtls1, dtls1_2\n"
#else
#define USAGE_MAX_VERSION   ""
#define USAGE_MIN_VERSION   ""
#define USAGE_FORCE_VERSION ""
#endif

/* USAGE is arbitrarily split to stay under the portable string literal
 * length limit: 4095 bytes in C99. */
#define USAGE1 \
    "\n usage: ssl_client2 param=<>...\n"                   \
    "\n acceptable parameters:\n"                           \
    "    server_name=%%s      default: localhost\n"         \
    "    server_addr=%%s      default: given by name\n"     \
    "    server_port=%%d      default: 4433\n"              \
    "    request_page=%%s     default: \".\"\n"             \
    "    request_size=%%d     default: about 34 (basic request)\n"           \
    "                        (minimum: 0, max: " MAX_REQUEST_SIZE_STR ")\n"  \
    "                        If 0, in the first exchange only an empty\n"    \
    "                        application data message is sent followed by\n" \
    "                        a second non-empty message before attempting\n" \
    "                        to read a response from the server\n"           \
    "    debug_level=%%d      default: 0 (disabled)\n"             \
    "    nbio=%%d             default: 0 (blocking I/O)\n"         \
    "                        options: 1 (non-blocking), 2 (added delays)\n"   \
    "    event=%%d            default: 0 (loop)\n"                            \
    "                        options: 1 (level-triggered, implies nbio=1),\n" \
    USAGE_READ_TIMEOUT                                                  \
    "    max_resend=%%d       default: 0 (no resend on timeout)\n" \
    "    skip_close_notify=%%d default: 0 (send close_notify)\n" \
    "\n"                                                    \
    USAGE_DTLS                                              \
    USAGE_CID                                               \
    "\n"

#define USAGE2 \
    USAGE_AUTH_MODE                                         \
    USAGE_IO                                                \
    "\n"                                                    \
    USAGE_PSK                                               \
    USAGE_ECJPAKE                                           \
    USAGE_ECRESTART                                         \
    "\n"

#define USAGE3 \
    USAGE_ALLOW_LEGACY_RENEGO                               \
    USAGE_RENEGO                                            \
    "    exchanges=%%d        default: 1\n"                 \
    "    reconnect=%%d        number of reconnections using session resumption\n" \
    "                        default: 0 (disabled)\n"      \
    "    reco_delay=%%d       default: 0 seconds\n"         \
    "    reco_mode=%%d        0: copy session, 1: serialize session\n" \
    "                        default: 1\n"      \
    "    reconnect_hard=%%d   default: 0 (disabled)\n"      \
    USAGE_TICKETS                                           \
    USAGE_MAX_FRAG_LEN                                      \
    USAGE_TRUNC_HMAC                                        \
    USAGE_ALPN                                              \
    USAGE_FALLBACK                                          \
    USAGE_EMS                                               \
    USAGE_ETM                                               \
    USAGE_CURVES                                            \
    USAGE_RECSPLIT                                          \
    USAGE_DHMLEN                                            \
    "\n"
#define USAGE4 \
    "    arc4=%%d             default: (library default: 0)\n" \
    "    allow_sha1=%%d       default: 0\n"                             \
    USAGE_MIN_VERSION                                                   \
    USAGE_MAX_VERSION                                                   \
    USAGE_FORCE_VERSION                                                 \
    "\n"                                                    \
    "    force_ciphersuite=<name>    default: all enabled\n"\
    "    query_config=<name>         return 0 if the specified\n"       \
    "                                configuration macro is defined and 1\n"  \
    "                                otherwise. The expansion of the macro\n" \
    "                                is printed if it is defined\n"     \
    USAGE_SERIALIZATION                                     \
    " acceptable ciphersuite names:\n"

#define ALPN_LIST_SIZE  10
#define CURVE_LIST_SIZE 20


/*
 * global options
 */
struct options
{
    const char *server_name;    /* hostname of the server (client only)     */
    const char *server_addr;    /* address of the server (client only)      */
    const char *server_port;    /* port on which the ssl service runs       */
    int debug_level;            /* level of debugging                       */
    int nbio;                   /* should I/O be blocking?                  */
    int event;                  /* loop or event-driven IO? level or edge triggered? */
    uint32_t read_timeout;      /* timeout on mbedtls_ssl_read() in milliseconds     */
    int max_resend;             /* DTLS times to resend on read timeout     */
    const char *request_page;   /* page on server to request                */
    int request_size;           /* pad request with header to requested size */
    const char *ca_file;        /* the file with the CA certificate(s)      */
    const char *ca_path;        /* the path with the CA certificate(s) reside */
    const char *crt_file;       /* the file with the client certificate     */
    const char *key_file;       /* the file with the client key             */
    const char *psk;            /* the pre-shared key                       */
    const char *psk_identity;   /* the pre-shared key identity              */
    const char *ecjpake_pw;     /* the EC J-PAKE password                   */
    int ec_max_ops;             /* EC consecutive operations limit          */
    int force_ciphersuite[2];   /* protocol/ciphersuite to use, or all      */
    int renegotiation;          /* enable / disable renegotiation           */
    int allow_legacy;           /* allow legacy renegotiation               */
    int renegotiate;            /* attempt renegotiation?                   */
    int renego_delay;           /* delay before enforcing renegotiation     */
    int exchanges;              /* number of data exchanges                 */
    int min_version;            /* minimum protocol version accepted        */
    int max_version;            /* maximum protocol version accepted        */
    int arc4;                   /* flag for arc4 suites support             */
    int allow_sha1;             /* flag for SHA-1 support                   */
    int auth_mode;              /* verify mode for connection               */
    unsigned char mfl_code;     /* code for maximum fragment length         */
    int trunc_hmac;             /* negotiate truncated hmac or not          */
    int recsplit;               /* enable record splitting?                 */
    int dhmlen;                 /* minimum DHM params len in bits           */
    int reconnect;              /* attempt to resume session                */
    int reco_delay;             /* delay in seconds before resuming session */
    int reco_mode;              /* how to keep the session around           */
    int reconnect_hard;         /* unexpectedly reconnect from the same port */
    int tickets;                /* enable / disable session tickets         */
    const char *curves;         /* list of supported elliptic curves        */
    const char *alpn_string;    /* ALPN supported protocols                 */
    int transport;              /* TLS or DTLS?                             */
    uint32_t hs_to_min;         /* Initial value of DTLS handshake timer    */
    uint32_t hs_to_max;         /* Max value of DTLS handshake timer        */
    int dtls_mtu;               /* UDP Maximum tranport unit for DTLS       */
    int fallback;               /* is this a fallback connection?           */
    int dgram_packing;          /* allow/forbid datagram packing            */
    int extended_ms;            /* negotiate extended master secret?        */
    int enforce_extended_master_secret; /* Enforce the usage of extended
                                         * master secret */
    int etm;                    /* negotiate encrypt then mac?              */
    int cid_enabled;            /* whether to use the CID extension or not  */
    int cid_enabled_renego;     /* whether to use the CID extension or not
                                 * during renegotiation                     */
    const char *cid_val;        /* the CID to use for incoming messages     */
    int serialize;              /* serialize/deserialize connection         */
    const char *cid_val_renego; /* the CID to use for incoming messages
                                 * after renegotiation                      */
    int skip_close_notify;      /* skip sending the close_notify alert      */
} opt;

int query_config( const char *config );

#if defined(MBEDTLS_DEBUG_C)
static void my_debug( void *ctx, int level,
                      const char *file, int line,
                      const char *str )
{
    const char *p, *basename;

    /* Extract basename from file */
    for( p = basename = file; *p != '\0'; p++ )
        if( *p == '/' || *p == '\\' )
            basename = p + 1;

    mbedtls_fprintf( (FILE *) ctx, "%s:%04d: |%d| %s",
                     basename, line, level, str );
    fflush(  (FILE *) ctx  );
}
#endif /* MBEDTLS_DEBUG_C */


#if !defined(MBEDTLS_SSL_CONF_RECV) && \
    !defined(MBEDTLS_SSL_CONF_SEND) && \
    !defined(MBEDTLS_SSL_CONF_RECV_TIMEOUT)
/*
 * Test recv/send functions that make sure each try returns
 * WANT_READ/WANT_WRITE at least once before sucesseding
 */

static int delayed_recv( void *ctx, unsigned char *buf, size_t len )
{
    static int first_try = 1;
    int ret;

    if( first_try )
    {
        first_try = 0;
        return( MBEDTLS_ERR_SSL_WANT_READ );
    }

    ret = mbedtls_net_recv( ctx, buf, len );
    if( ret != MBEDTLS_ERR_SSL_WANT_READ )
        first_try = 1; /* Next call will be a new operation */
    return( ret );
}

static int delayed_send( void *ctx, const unsigned char *buf, size_t len )
{
    static int first_try = 1;
    int ret;

    if( first_try )
    {
        first_try = 0;
        return( MBEDTLS_ERR_SSL_WANT_WRITE );
    }

    ret = mbedtls_net_send( ctx, buf, len );
    if( ret != MBEDTLS_ERR_SSL_WANT_WRITE )
        first_try = 1; /* Next call will be a new operation */
    return( ret );
}

typedef struct
{
    mbedtls_ssl_context *ssl;
    mbedtls_net_context *net;
} io_ctx_t;

#if defined(MBEDTLS_SSL_RECORD_CHECKING)
static int ssl_check_record( mbedtls_ssl_context const *ssl,
                             unsigned char const *buf, size_t len )
{
    int ret;
    unsigned char *tmp_buf;

    tmp_buf = mbedtls_calloc( 1, len );
    if( tmp_buf == NULL )
        return( MBEDTLS_ERR_SSL_ALLOC_FAILED );
    memcpy( tmp_buf, buf, len );

    ret = mbedtls_ssl_check_record( ssl, tmp_buf, len );
    if( ret != MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE )
    {
        int ret_repeated;

        /* Test-only: Make sure that mbedtls_ssl_check_record()
         *            doesn't alter state. */
        memcpy( tmp_buf, buf, len ); /* Restore buffer */
        ret_repeated = mbedtls_ssl_check_record( ssl, tmp_buf, len );
        if( ret != ret_repeated )
        {
            mbedtls_printf( "mbedtls_ssl_check_record() returned inconsistent results.\n" );
            return( -1 );
        }

        switch( ret )
        {
            case 0:
                break;

            case MBEDTLS_ERR_SSL_INVALID_RECORD:
                if( opt.debug_level > 1 )
                    mbedtls_printf( "mbedtls_ssl_check_record() detected invalid record.\n" );
                break;

            case MBEDTLS_ERR_SSL_INVALID_MAC:
                if( opt.debug_level > 1 )
                    mbedtls_printf( "mbedtls_ssl_check_record() detected unauthentic record.\n" );
                break;

            case MBEDTLS_ERR_SSL_UNEXPECTED_RECORD:
                if( opt.debug_level > 1 )
                    mbedtls_printf( "mbedtls_ssl_check_record() detected unexpected record.\n" );
                break;

            default:
                mbedtls_printf( "mbedtls_ssl_check_record() failed fatally with -%#04x.\n", -ret );
                return( -1 );
        }

        /* Regardless of the outcome, forward the record to the stack. */
    }

    mbedtls_free( tmp_buf );

    return( 0 );
}
#endif /* MBEDTLS_SSL_RECORD_CHECKING */

static int recv_cb( void *ctx, unsigned char *buf, size_t len )
{
    io_ctx_t *io_ctx = (io_ctx_t*) ctx;
    size_t recv_len;
    int ret;

    if( opt.nbio == 2 )
        ret = delayed_recv( io_ctx->net, buf, len );
    else
        ret = mbedtls_net_recv( io_ctx->net, buf, len );
    if( ret < 0 )
        return( ret );
    recv_len = (size_t) ret;

    if( opt.transport == MBEDTLS_SSL_TRANSPORT_DATAGRAM )
    {
        /* Here's the place to do any datagram/record checking
         * in between receiving the packet from the underlying
         * transport and passing it on to the TLS stack. */
#if defined(MBEDTLS_SSL_RECORD_CHECKING)
        if( ssl_check_record( io_ctx->ssl, buf, recv_len ) != 0 )
            return( -1 );
#endif /* MBEDTLS_SSL_RECORD_CHECKING */
    }

    return( (int) recv_len );
}

static int recv_timeout_cb( void *ctx, unsigned char *buf, size_t len,
                            uint32_t timeout )
{
    io_ctx_t *io_ctx = (io_ctx_t*) ctx;
    int ret;
    size_t recv_len;

    ret = mbedtls_net_recv_timeout( io_ctx->net, buf, len, timeout );
    if( ret < 0 )
        return( ret );
    recv_len = (size_t) ret;

    if( opt.transport == MBEDTLS_SSL_TRANSPORT_DATAGRAM )
    {
        /* Here's the place to do any datagram/record checking
         * in between receiving the packet from the underlying
         * transport and passing it on to the TLS stack. */
#if defined(MBEDTLS_SSL_RECORD_CHECKING)
        if( ssl_check_record( io_ctx->ssl, buf, recv_len ) != 0 )
            return( -1 );
#endif /* MBEDTLS_SSL_RECORD_CHECKING */
    }

    return( (int) recv_len );
}

static int send_cb( void *ctx, unsigned char const *buf, size_t len )
{
    io_ctx_t *io_ctx = (io_ctx_t*) ctx;

    if( opt.nbio == 2 )
        return( delayed_send( io_ctx->net, buf, len ) );

    return( mbedtls_net_send( io_ctx->net, buf, len ) );
}
#endif /* !MBEDTLS_SSL_CONF_RECV &&
          !MBEDTLS_SSL_CONF_SEND &&
          !MBEDTLS_SSL_CONF_RECV_TIMEOUT */

#if defined(MBEDTLS_X509_CRT_PARSE_C)

#if !defined(MBEDTLS_X509_REMOVE_VERIFY_CALLBACK)
static unsigned char peer_crt_info[1024];

/*
 * Enabled if debug_level > 1 in code below
 */
static int my_verify( void *data, mbedtls_x509_crt *crt,
                      int depth, uint32_t *flags )
{
#if !defined(MBEDTLS_X509_REMOVE_INFO)
    char buf[1024];
#endif
    ((void) data);

#if !defined(MBEDTLS_X509_REMOVE_INFO)
    mbedtls_x509_crt_info( buf, sizeof( buf ) - 1, "", crt );
    if( depth == 0 )
        memcpy( peer_crt_info, buf, sizeof( buf ) );

    if( opt.debug_level == 0 )
        return( 0 );

    mbedtls_printf( "\nVerify requested for (Depth %d):\n", depth );
    mbedtls_printf( "%s", buf );
#else
    ((void) crt);
    ((void) depth);
#endif

    if ( ( *flags ) == 0 )
        mbedtls_printf( "  This certificate has no flags\n" );
    else
    {
#if !defined(MBEDTLS_X509_REMOVE_INFO)
        mbedtls_x509_crt_verify_info( buf, sizeof( buf ), "  ! ", *flags );
        mbedtls_printf( "%s\n", buf );
#endif
    }

    return( 0 );
}
#endif /* MBEDTLS_X509_REMOVE_VERIFY_CALLBACK */

#endif /* MBEDTLS_X509_CRT_PARSE_C */

#if ( defined(MBEDTLS_X509_CRT_PARSE_C) &&        \
      !defined(MBEDTLS_SSL_CONF_SINGLE_HASH) ) || \
    !defined(MBEDTLS_CTR_DRBG_C)
static int available_hashes[] = {
#if defined(MBEDTLS_SHA512_C)
    MBEDTLS_MD_SHA512,
    MBEDTLS_MD_SHA384,
#endif
#if defined(MBEDTLS_SHA256_C)
    MBEDTLS_MD_SHA256,
    MBEDTLS_MD_SHA224,
#endif
#if defined(MBEDTLS_SHA1_C)
    /* Allow SHA-1 as we use it extensively in tests. */
    MBEDTLS_MD_SHA1,
#endif
    MBEDTLS_MD_NONE
};
#endif /* ( MBEDTLS_X509_CRT_PARSE_C && !MBEDTLS_SSL_CONF_SINGLE_HASH ) ||
          !MBEDTLS_CTR_DRBG_C */

/*
 * Wait for an event from the underlying transport or the timer
 * (Used in event-driven IO mode).
 */
#if !defined(MBEDTLS_TIMING_C)
int idle( mbedtls_net_context *fd,
          int idle_reason )
#else
int idle( mbedtls_net_context *fd,
          mbedtls_timing_delay_context *timer,
          int idle_reason )
#endif
{

    int ret;
    int poll_type = 0;

    if( idle_reason == MBEDTLS_ERR_SSL_WANT_WRITE )
        poll_type = MBEDTLS_NET_POLL_WRITE;
    else if( idle_reason == MBEDTLS_ERR_SSL_WANT_READ )
        poll_type = MBEDTLS_NET_POLL_READ;
#if !defined(MBEDTLS_TIMING_C)
    else
        return( 0 );
#endif

    while( 1 )
    {
        /* Check if timer has expired */
#if defined(MBEDTLS_TIMING_C)
        if( timer != NULL &&
            mbedtls_timing_get_delay( timer ) == 2 )
        {
            break;
        }
#endif /* MBEDTLS_TIMING_C */

        /* Check if underlying transport became available */
        if( poll_type != 0 )
        {
            ret = mbedtls_net_poll( fd, poll_type, 0 );
            if( ret < 0 )
                return( ret );
            if( ret == poll_type )
                break;
        }
    }

    return( 0 );
}

/* Unhexify `hex` into `dst`. `dst` must have
 * size at least `strlen( hex ) / 2`. */
int unhexify( char const *hex, unsigned char *dst )
{
    unsigned char c;
    size_t j;
    size_t len = strlen( hex );

    if( len % 2 != 0 )
        return( -1 );

    for( j = 0; j < len; j += 2 )
    {
        c = hex[j];
        if( c >= '0' && c <= '9' )
            c -= '0';
        else if( c >= 'a' && c <= 'f' )
            c -= 'a' - 10;
        else if( c >= 'A' && c <= 'F' )
            c -= 'A' - 10;
        else
            return( -1 );
        dst[ j / 2 ] = c << 4;

        c = hex[j + 1];
        if( c >= '0' && c <= '9' )
            c -= '0';
        else if( c >= 'a' && c <= 'f' )
            c -= 'a' - 10;
        else if( c >= 'A' && c <= 'F' )
            c -= 'A' - 10;
        else
            return( -1 );
        dst[ j / 2 ] |= c;
    }

    return( 0 );
}

#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
int report_cid_usage( mbedtls_ssl_context *ssl,
                      const char *additional_description )
{
    int ret;
    unsigned char peer_cid[ MBEDTLS_SSL_CID_OUT_LEN_MAX ];
    size_t peer_cid_len;
    int cid_negotiated;

    if( opt.transport != MBEDTLS_SSL_TRANSPORT_DATAGRAM )
        return( 0 );

    /* Check if the use of a CID has been negotiated,
     * but don't ask for the CID value and length.
     *
     * Note: Here and below, we're demonstrating the various ways
     *       in which mbedtls_ssl_get_peer_cid() can be called,
     *       depending on whether or not the length/value of the
     *       peer's CID is needed.
     *
     *       An actual application, however, should use
     *       just one call to mbedtls_ssl_get_peer_cid(). */
    ret = mbedtls_ssl_get_peer_cid( ssl, &cid_negotiated,
                                    NULL, NULL );
    if( ret != 0 )
    {
        mbedtls_printf( " failed\n  ! mbedtls_ssl_get_peer_cid returned -0x%x\n\n",
                        -ret );
        return( ret );
    }

    if( cid_negotiated == MBEDTLS_SSL_CID_DISABLED )
    {
        if( opt.cid_enabled == MBEDTLS_SSL_CID_ENABLED )
        {
            mbedtls_printf( "(%s) Use of Connection ID was rejected by the server.\n",
                            additional_description );
        }
    }
    else
    {
        size_t idx=0;
        mbedtls_printf( "(%s) Use of Connection ID has been negotiated.\n",
                        additional_description );

        /* Ask for just the length of the peer's CID. */
        ret = mbedtls_ssl_get_peer_cid( ssl, &cid_negotiated,
                                        NULL, &peer_cid_len );
        if( ret != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_get_peer_cid returned -0x%x\n\n",
                            -ret );
            return( ret );
        }

        /* Ask for just length + value of the peer's CID. */
        ret = mbedtls_ssl_get_peer_cid( ssl, &cid_negotiated,
                                        peer_cid, &peer_cid_len );
        if( ret != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_get_peer_cid returned -0x%x\n\n",
                            -ret );
            return( ret );
        }
        mbedtls_printf( "(%s) Peer CID (length %u Bytes): ",
                        additional_description,
                        (unsigned) peer_cid_len );
        while( idx < peer_cid_len )
        {
            mbedtls_printf( "%02x ", peer_cid[ idx ] );
            idx++;
        }
        mbedtls_printf( "\n" );
    }

    return( 0 );
}
#endif /* MBEDTLS_SSL_DTLS_CONNECTION_ID */

#if defined(MBEDTLS_SSL_CONF_RNG)
int rng_wrap( void *ctx, unsigned char *dst, size_t len );

#if defined(MBEDTLS_CTR_DRBG_C)
mbedtls_ctr_drbg_context *rng_ctx_global = NULL;
#else
mbedtls_hmac_drbg_context *rng_ctx_global = NULL;
#endif /* MBEDTLS_CTR_DRBG_C */

int rng_wrap( void *ctx, unsigned char *dst, size_t len )
{
    /* We expect the NULL parameter here. */
    if( ctx != NULL )
        return( -1 );

#if defined(MBEDTLS_CTR_DRBG_C)
    return( mbedtls_ctr_drbg_random( rng_ctx_global, dst, len ) );
#else
    return( mbedtls_hmac_drbg_random( rng_ctx_global, dst, len ) );
#endif
}
#endif /* MBEDTLS_SSL_CONF_RNG */

#if defined(MBEDTLS_ENTROPY_HARDWARE_ALT)
int mbedtls_hardware_poll( void *data, unsigned char *output,
                           size_t len, size_t *olen )
{
    size_t i;
    (void) data;
    for( i = 0; i < len; ++i )
        output[i] = rand();
    *olen = len;
    return( 0 );
}
#endif

int main( int argc, char *argv[] )
{
    int ret = 0, len, tail_len, i, written, frags, retry_left;
    mbedtls_net_context server_fd;
#if !defined(MBEDTLS_SSL_CONF_RECV) && \
    !defined(MBEDTLS_SSL_CONF_SEND) && \
    !defined(MBEDTLS_SSL_CONF_RECV_TIMEOUT)
    io_ctx_t io_ctx;
#endif

    unsigned char *buf = NULL;
    unsigned int main_buf_len = 0;

#if defined(MBEDTLS_KEY_EXCHANGE__SOME__PSK_ENABLED)
    unsigned char psk[MBEDTLS_PSK_MAX_LEN];
    size_t psk_len = 0;
#endif

#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
    unsigned char cid[MBEDTLS_SSL_CID_IN_LEN_MAX];
    unsigned char cid_renego[MBEDTLS_SSL_CID_IN_LEN_MAX];
    size_t cid_len = 0;
    size_t cid_renego_len = 0;
#endif

#if defined(MBEDTLS_SSL_ALPN)
    const char *alpn_list[ALPN_LIST_SIZE];
#endif
#if defined(MBEDTLS_ECP_C) && !defined(MBEDTLS_SSL_CONF_SINGLE_EC)
    mbedtls_ecp_group_id curve_list[CURVE_LIST_SIZE];
    const mbedtls_ecp_curve_info *curve_cur;
#endif

    const char *pers = "ssl_client2";

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_x509_crt_profile crt_profile_for_test = mbedtls_x509_crt_profile_default;
#endif
    mbedtls_entropy_context *entropy = NULL;
#if defined(MBEDTLS_CTR_DRBG_C)
    mbedtls_ctr_drbg_context *ctr_drbg = NULL;
#else
    mbedtls_hmac_drbg_context *hmac_drbg = NULL;
#endif
    mbedtls_ssl_context *ssl;
    mbedtls_ssl_config *conf;
    mbedtls_ssl_session saved_session;
    unsigned char *session_data = NULL;
    size_t session_data_len = 0;
#if defined(MBEDTLS_TIMING_C)
    mbedtls_timing_delay_context timer;
#endif
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    uint32_t flags;
    mbedtls_x509_crt *cacert = NULL;
    mbedtls_x509_crt *clicert = NULL;
    mbedtls_pk_context *pkey = NULL;
#endif
    char *p, *q;
    const int *list;
#if defined(MBEDTLS_SSL_CONTEXT_SERIALIZATION)
    unsigned char *context_buf = NULL;
    size_t context_buf_len;
#endif

    ssl       = mbedtls_calloc( 1, sizeof( *ssl ) );
    conf      = mbedtls_calloc( 1, sizeof( *conf ) );
    entropy   = mbedtls_calloc( 1, sizeof( *entropy ) );
#if defined(MBEDTLS_CTR_DRBG_C)
    ctr_drbg  = mbedtls_calloc( 1, sizeof( *ctr_drbg ) );
#else
    hmac_drbg = mbedtls_calloc( 1, sizeof( *hmac_drbg ) );
#endif
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    cacert    = mbedtls_calloc( 1, sizeof( *cacert ) );
    clicert   = mbedtls_calloc( 1, sizeof( *clicert ) );
    pkey      = mbedtls_calloc( 1, sizeof( *pkey ) );
#endif

    if( ssl       == NULL || entropy   == NULL ||
#if defined(MBEDTLS_X509_CRT_PARSE_C)
           cacert == NULL ||
           clicert== NULL || pkey      == NULL ||
#endif
#if defined(MBEDTLS_CTR_DRBG_C)
        ctr_drbg  == NULL ||
#else
        hmac_drbg == NULL ||
#endif

        conf   == NULL)
    {
        goto exit;
    }

    /*
     * Make sure memory references are valid.
     */
    mbedtls_net_init( &server_fd );
    mbedtls_ssl_init( ssl );
    mbedtls_ssl_config_init( conf );
    memset( &saved_session, 0, sizeof( mbedtls_ssl_session ) );
#if defined(MBEDTLS_CTR_DRBG_C)
    mbedtls_ctr_drbg_init( ctr_drbg );
#else
    mbedtls_hmac_drbg_init( hmac_drbg );
#endif /* MBEDTLS_CTR_DRBG_C */
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_x509_crt_init( cacert );
    mbedtls_x509_crt_init( clicert );
    mbedtls_pk_init( pkey );
#endif
#if defined(MBEDTLS_SSL_ALPN)
    memset( (void * ) alpn_list, 0, sizeof( alpn_list ) );
#endif

    if( argc == 0 )
    {
    usage:
        if( ret == 0 )
            ret = 1;

        mbedtls_printf( USAGE1 );
        mbedtls_printf( USAGE2 );
        mbedtls_printf( USAGE3 );
        mbedtls_printf( USAGE4 );

        list = mbedtls_ssl_list_ciphersuites();
        while( *list )
        {
            mbedtls_printf(" %-42s", mbedtls_ssl_get_ciphersuite_name( *list ) );
            list++;
            if( !*list )
                break;
            mbedtls_printf(" %s\n", mbedtls_ssl_get_ciphersuite_name( *list ) );
            list++;
        }
        mbedtls_printf("\n");
        goto exit;
    }

    opt.server_name         = DFL_SERVER_NAME;
    opt.server_addr         = DFL_SERVER_ADDR;
    opt.server_port         = DFL_SERVER_PORT;
    opt.debug_level         = DFL_DEBUG_LEVEL;
    opt.cid_enabled         = DFL_CID_ENABLED;
    opt.cid_val             = DFL_CID_VALUE;
    opt.cid_enabled_renego  = DFL_CID_ENABLED_RENEGO;
    opt.cid_val_renego      = DFL_CID_VALUE_RENEGO;
    opt.nbio                = DFL_NBIO;
    opt.event               = DFL_EVENT;
    opt.read_timeout        = DFL_READ_TIMEOUT;
    opt.max_resend          = DFL_MAX_RESEND;
    opt.request_page        = DFL_REQUEST_PAGE;
    opt.request_size        = DFL_REQUEST_SIZE;
    opt.ca_file             = DFL_CA_FILE;
    opt.ca_path             = DFL_CA_PATH;
    opt.crt_file            = DFL_CRT_FILE;
    opt.key_file            = DFL_KEY_FILE;
    opt.psk                 = DFL_PSK;
    opt.psk_identity        = DFL_PSK_IDENTITY;
    opt.ecjpake_pw          = DFL_ECJPAKE_PW;
    opt.ec_max_ops          = DFL_EC_MAX_OPS;
    opt.force_ciphersuite[0]= DFL_FORCE_CIPHER;
    opt.renegotiation       = DFL_RENEGOTIATION;
    opt.allow_legacy        = DFL_ALLOW_LEGACY;
    opt.renegotiate         = DFL_RENEGOTIATE;
    opt.exchanges           = DFL_EXCHANGES;
    opt.min_version         = DFL_MIN_VERSION;
    opt.max_version         = DFL_MAX_VERSION;
    opt.arc4                = DFL_ARC4;
    opt.allow_sha1          = DFL_SHA1;
    opt.auth_mode           = DFL_AUTH_MODE;
    opt.mfl_code            = DFL_MFL_CODE;
    opt.trunc_hmac          = DFL_TRUNC_HMAC;
    opt.recsplit            = DFL_RECSPLIT;
    opt.dhmlen              = DFL_DHMLEN;
    opt.reconnect           = DFL_RECONNECT;
    opt.reco_delay          = DFL_RECO_DELAY;
    opt.reco_mode           = DFL_RECO_MODE;
    opt.reconnect_hard      = DFL_RECONNECT_HARD;
    opt.tickets             = DFL_TICKETS;
    opt.alpn_string         = DFL_ALPN_STRING;
    opt.curves              = DFL_CURVES;
    opt.transport           = DFL_TRANSPORT;
    opt.hs_to_min           = DFL_HS_TO_MIN;
    opt.hs_to_max           = DFL_HS_TO_MAX;
    opt.dtls_mtu            = DFL_DTLS_MTU;
    opt.fallback            = DFL_FALLBACK;
    opt.extended_ms         = DFL_EXTENDED_MS;
    opt.enforce_extended_master_secret = DFL_EXTENDED_MS_ENFORCE;
    opt.etm                 = DFL_ETM;
    opt.dgram_packing       = DFL_DGRAM_PACKING;
    opt.serialize           = DFL_SERIALIZE;
    opt.skip_close_notify   = DFL_SKIP_CLOSE_NOTIFY;

    for( i = 1; i < argc; i++ )
    {
        p = argv[i];
        if( ( q = strchr( p, '=' ) ) == NULL )
            goto usage;
        *q++ = '\0';

        if( strcmp( p, "server_name" ) == 0 )
            opt.server_name = q;
        else if( strcmp( p, "server_addr" ) == 0 )
            opt.server_addr = q;
        else if( strcmp( p, "server_port" ) == 0 )
            opt.server_port = q;
        else if( strcmp( p, "dtls" ) == 0 )
        {
            int t = atoi( q );
            if( t == 0 )
                opt.transport = MBEDTLS_SSL_TRANSPORT_STREAM;
            else if( t == 1 )
                opt.transport = MBEDTLS_SSL_TRANSPORT_DATAGRAM;
            else
                goto usage;
        }
        else if( strcmp( p, "debug_level" ) == 0 )
        {
            opt.debug_level = atoi( q );
            if( opt.debug_level < 0 || opt.debug_level > 65535 )
                goto usage;
        }
        else if( strcmp( p, "nbio" ) == 0 )
        {
            opt.nbio = atoi( q );
            if( opt.nbio < 0 || opt.nbio > 2 )
                goto usage;
        }
        else if( strcmp( p, "event" ) == 0 )
        {
            opt.event = atoi( q );
            if( opt.event < 0 || opt.event > 2 )
                goto usage;
        }
#if !defined(MBEDTLS_SSL_CONF_READ_TIMEOUT)
        else if( strcmp( p, "read_timeout" ) == 0 )
            opt.read_timeout = atoi( q );
#endif
        else if( strcmp( p, "max_resend" ) == 0 )
        {
            opt.max_resend = atoi( q );
            if( opt.max_resend < 0 )
                goto usage;
        }
        else if( strcmp( p, "request_page" ) == 0 )
            opt.request_page = q;
        else if( strcmp( p, "request_size" ) == 0 )
        {
            opt.request_size = atoi( q );
            if( opt.request_size < 0 ||
                opt.request_size > MAX_REQUEST_SIZE )
                goto usage;
        }
        else if( strcmp( p, "ca_file" ) == 0 )
            opt.ca_file = q;
        else if( strcmp( p, "ca_path" ) == 0 )
            opt.ca_path = q;
        else if( strcmp( p, "crt_file" ) == 0 )
            opt.crt_file = q;
        else if( strcmp( p, "key_file" ) == 0 )
            opt.key_file = q;
#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
        else if( strcmp( p, "cid" ) == 0 )
        {
            opt.cid_enabled = atoi( q );
            if( opt.cid_enabled != 0 && opt.cid_enabled != 1 )
                goto usage;
        }
        else if( strcmp( p, "cid_renego" ) == 0 )
        {
            opt.cid_enabled_renego = atoi( q );
            if( opt.cid_enabled_renego != 0 && opt.cid_enabled_renego != 1 )
                goto usage;
        }
        else if( strcmp( p, "cid_val" ) == 0 )
        {
            opt.cid_val = q;
        }
        else if( strcmp( p, "cid_val_renego" ) == 0 )
        {
            opt.cid_val_renego = q;
        }
#endif /* MBEDTLS_SSL_DTLS_CONNECTION_ID */
        else if( strcmp( p, "psk" ) == 0 )
            opt.psk = q;
        else if( strcmp( p, "psk_identity" ) == 0 )
            opt.psk_identity = q;
        else if( strcmp( p, "ecjpake_pw" ) == 0 )
            opt.ecjpake_pw = q;
        else if( strcmp( p, "ec_max_ops" ) == 0 )
            opt.ec_max_ops = atoi( q );
        else if( strcmp( p, "force_ciphersuite" ) == 0 )
        {
            opt.force_ciphersuite[0] = mbedtls_ssl_get_ciphersuite_id( q );

            if( opt.force_ciphersuite[0] == 0 )
            {
                ret = 2;
                goto usage;
            }
            opt.force_ciphersuite[1] = 0;
        }
        else if( strcmp( p, "renegotiation" ) == 0 )
        {
            opt.renegotiation = (atoi( q )) ?
                MBEDTLS_SSL_RENEGOTIATION_ENABLED :
                MBEDTLS_SSL_RENEGOTIATION_DISABLED;
        }
#if !defined(MBEDTLS_SSL_CONF_ALLOW_LEGACY_RENEGOTIATION)
        else if( strcmp( p, "allow_legacy" ) == 0 )
        {
            switch( atoi( q ) )
            {
                case -1:
                    opt.allow_legacy = MBEDTLS_SSL_LEGACY_BREAK_HANDSHAKE;
                    break;
                case 0:
                    opt.allow_legacy = MBEDTLS_SSL_LEGACY_NO_RENEGOTIATION;
                    break;
                case 1:
                    opt.allow_legacy = MBEDTLS_SSL_LEGACY_ALLOW_RENEGOTIATION;
                    break;
                default: goto usage;
            }
        }
#endif /* !MBEDTLS_SSL_CONF_ALLOW_LEGACY_RENEGOTIATION */
        else if( strcmp( p, "renegotiate" ) == 0 )
        {
            opt.renegotiate = atoi( q );
            if( opt.renegotiate < 0 || opt.renegotiate > 1 )
                goto usage;
        }
        else if( strcmp( p, "exchanges" ) == 0 )
        {
            opt.exchanges = atoi( q );
            if( opt.exchanges < 1 )
                goto usage;
        }
        else if( strcmp( p, "reconnect" ) == 0 )
        {
            opt.reconnect = atoi( q );
            if( opt.reconnect < 0 || opt.reconnect > 2 )
                goto usage;
        }
        else if( strcmp( p, "reco_delay" ) == 0 )
        {
            opt.reco_delay = atoi( q );
            if( opt.reco_delay < 0 )
                goto usage;
        }
        else if( strcmp( p, "reco_mode" ) == 0 )
        {
            opt.reco_mode = atoi( q );
            if( opt.reco_mode < 0 )
                goto usage;
        }
        else if( strcmp( p, "reconnect_hard" ) == 0 )
        {
            opt.reconnect_hard = atoi( q );
            if( opt.reconnect_hard < 0 || opt.reconnect_hard > 1 )
                goto usage;
        }
        else if( strcmp( p, "tickets" ) == 0 )
        {
            opt.tickets = atoi( q );
            if( opt.tickets < 0 || opt.tickets > 2 )
                goto usage;
        }
        else if( strcmp( p, "alpn" ) == 0 )
        {
            opt.alpn_string = q;
        }
        else if( strcmp( p, "fallback" ) == 0 )
        {
            switch( atoi( q ) )
            {
                case 0: opt.fallback = MBEDTLS_SSL_IS_NOT_FALLBACK; break;
                case 1: opt.fallback = MBEDTLS_SSL_IS_FALLBACK; break;
                default: goto usage;
            }
        }
        else if( strcmp( p, "extended_ms" ) == 0 )
        {
            switch( atoi( q ) )
            {
                case 0:
                    opt.extended_ms = MBEDTLS_SSL_EXTENDED_MS_DISABLED;
                    break;
                case 1:
                    opt.extended_ms = MBEDTLS_SSL_EXTENDED_MS_ENABLED;
                    break;
                default: goto usage;
            }
        }
        else if( strcmp( p, "enforce_extended_master_secret" ) == 0 )
        {
            switch( atoi( q ) )
            {
                case 0:
                    opt.enforce_extended_master_secret =
                        MBEDTLS_SSL_EXTENDED_MS_ENFORCE_DISABLED;
                    break;
                case 1:
                    opt.enforce_extended_master_secret =
                        MBEDTLS_SSL_EXTENDED_MS_ENFORCE_ENABLED;
                    break;
                default: goto usage;
            }
        }
#if !defined(MBEDTLS_SSL_CONF_SINGLE_EC)
        else if( strcmp( p, "curves" ) == 0 )
            opt.curves = q;
#endif /* !MBEDTLS_SSL_CONF_SINGLE_EC */
        else if( strcmp( p, "etm" ) == 0 )
        {
            switch( atoi( q ) )
            {
                case 0: opt.etm = MBEDTLS_SSL_ETM_DISABLED; break;
                case 1: opt.etm = MBEDTLS_SSL_ETM_ENABLED; break;
                default: goto usage;
            }
        }
#if !defined(MBEDTLS_SSL_CONF_MIN_MINOR_VER) || \
    !defined(MBEDTLS_SSL_CONF_MIN_MAJOR_VER) || \
    !defined(MBEDTLS_SSL_CONF_MAX_MINOR_VER) || \
    !defined(MBEDTLS_SSL_CONF_MAX_MAJOR_VER)
        else if( strcmp( p, "min_version" ) == 0 )
        {
            if( strcmp( q, "ssl3" ) == 0 )
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_0;
            else if( strcmp( q, "tls1" ) == 0 )
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_1;
            else if( strcmp( q, "tls1_1" ) == 0 ||
                     strcmp( q, "dtls1" ) == 0 )
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_2;
            else if( strcmp( q, "tls1_2" ) == 0 ||
                     strcmp( q, "dtls1_2" ) == 0 )
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_3;
            else
                goto usage;
        }
        else if( strcmp( p, "max_version" ) == 0 )
        {
            if( strcmp( q, "ssl3" ) == 0 )
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_0;
            else if( strcmp( q, "tls1" ) == 0 )
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_1;
            else if( strcmp( q, "tls1_1" ) == 0 ||
                     strcmp( q, "dtls1" ) == 0 )
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_2;
            else if( strcmp( q, "tls1_2" ) == 0 ||
                     strcmp( q, "dtls1_2" ) == 0 )
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_3;
            else
                goto usage;
        }
        else if( strcmp( p, "force_version" ) == 0 )
        {
            if( strcmp( q, "ssl3" ) == 0 )
            {
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_0;
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_0;
            }
            else if( strcmp( q, "tls1" ) == 0 )
            {
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_1;
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_1;
            }
            else if( strcmp( q, "tls1_1" ) == 0 )
            {
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_2;
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_2;
            }
            else if( strcmp( q, "tls1_2" ) == 0 )
            {
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_3;
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_3;
            }
            else if( strcmp( q, "dtls1" ) == 0 )
            {
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_2;
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_2;
                opt.transport = MBEDTLS_SSL_TRANSPORT_DATAGRAM;
            }
            else if( strcmp( q, "dtls1_2" ) == 0 )
            {
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_3;
                opt.max_version = MBEDTLS_SSL_MINOR_VERSION_3;
                opt.transport = MBEDTLS_SSL_TRANSPORT_DATAGRAM;
            }
            else
                goto usage;
        }
#endif
        else if( strcmp( p, "arc4" ) == 0 )
        {
            switch( atoi( q ) )
            {
                case 0:     opt.arc4 = MBEDTLS_SSL_ARC4_DISABLED;   break;
                case 1:     opt.arc4 = MBEDTLS_SSL_ARC4_ENABLED;    break;
                default:    goto usage;
            }
        }
        else if( strcmp( p, "allow_sha1" ) == 0 )
        {
            switch( atoi( q ) )
            {
                case 0:     opt.allow_sha1 = 0;   break;
                case 1:     opt.allow_sha1 = 1;    break;
                default:    goto usage;
            }
        }
#if !defined(MBEDTLS_SSL_CONF_AUTHMODE)
        else if( strcmp( p, "auth_mode" ) == 0 )
        {
            if( strcmp( q, "none" ) == 0 )
                opt.auth_mode = MBEDTLS_SSL_VERIFY_NONE;
            else if( strcmp( q, "optional" ) == 0 )
                opt.auth_mode = MBEDTLS_SSL_VERIFY_OPTIONAL;
            else if( strcmp( q, "required" ) == 0 )
                opt.auth_mode = MBEDTLS_SSL_VERIFY_REQUIRED;
            else
                goto usage;
        }
#endif
        else if( strcmp( p, "max_frag_len" ) == 0 )
        {
            if( strcmp( q, "512" ) == 0 )
                opt.mfl_code = MBEDTLS_SSL_MAX_FRAG_LEN_512;
            else if( strcmp( q, "1024" ) == 0 )
                opt.mfl_code = MBEDTLS_SSL_MAX_FRAG_LEN_1024;
            else if( strcmp( q, "2048" ) == 0 )
                opt.mfl_code = MBEDTLS_SSL_MAX_FRAG_LEN_2048;
            else if( strcmp( q, "4096" ) == 0 )
                opt.mfl_code = MBEDTLS_SSL_MAX_FRAG_LEN_4096;
            else
                goto usage;
        }
        else if( strcmp( p, "trunc_hmac" ) == 0 )
        {
            switch( atoi( q ) )
            {
                case 0: opt.trunc_hmac = MBEDTLS_SSL_TRUNC_HMAC_DISABLED; break;
                case 1: opt.trunc_hmac = MBEDTLS_SSL_TRUNC_HMAC_ENABLED; break;
                default: goto usage;
            }
        }
        else if( strcmp( p, "hs_timeout" ) == 0 )
        {
            if( ( p = strchr( q, '-' ) ) == NULL )
                goto usage;
            *p++ = '\0';
            opt.hs_to_min = atoi( q );
            opt.hs_to_max = atoi( p );
            if( opt.hs_to_min == 0 || opt.hs_to_max < opt.hs_to_min )
                goto usage;
        }
        else if( strcmp( p, "mtu" ) == 0 )
        {
            opt.dtls_mtu = atoi( q );
            if( opt.dtls_mtu < 0 )
                goto usage;
        }
        else if( strcmp( p, "dgram_packing" ) == 0 )
        {
            opt.dgram_packing = atoi( q );
            if( opt.dgram_packing != 0 &&
                opt.dgram_packing != 1 )
            {
                goto usage;
            }
        }
        else if( strcmp( p, "recsplit" ) == 0 )
        {
            opt.recsplit = atoi( q );
            if( opt.recsplit < 0 || opt.recsplit > 1 )
                goto usage;
        }
        else if( strcmp( p, "dhmlen" ) == 0 )
        {
            opt.dhmlen = atoi( q );
            if( opt.dhmlen < 0 )
                goto usage;
        }
        else if( strcmp( p, "query_config" ) == 0 )
        {
            mbedtls_exit( query_config( q ) );
        }
        else if( strcmp( p, "skip_close_notify" ) == 0 )
        {
            opt.skip_close_notify = atoi( q );
            if( opt.skip_close_notify < 0 || opt.skip_close_notify > 1 )
                goto usage;
        }
        else if( strcmp( p, "serialize") == 0 )
        {
            opt.serialize = atoi( q );
            if( opt.serialize < 0 || opt.serialize > 2)
                goto usage;
        }
        else
            goto usage;
    }

    /* try to use as small buf from the heap as possible */
    if( opt.request_size <= 0 )
    {
        main_buf_len = MBEDTLS_SSL_MAX_CONTENT_LEN + 1;
    }
    else if( opt.request_size < (int)sizeof(GET_REQUEST) )
    {
        main_buf_len = sizeof(GET_REQUEST) + 1;
    }
    else
    {
        main_buf_len = opt.request_size + 1;
    }

    buf = mbedtls_calloc( 1, main_buf_len );
    if( buf == NULL )
    {
        mbedtls_printf( "buf allocation failed!\n" );
        goto exit;
    }

    /* Event-driven IO is incompatible with the above custom
     * receive and send functions, as the polling builds on
     * refers to the underlying net_context. */
    if( opt.event == 1 && opt.nbio != 1 )
    {
        mbedtls_printf( "Warning: event-driven IO mandates nbio=1 - overwrite\n" );
        opt.nbio = 1;
    }

#if defined(MBEDTLS_DEBUG_C)
    mbedtls_debug_set_threshold( opt.debug_level );
#endif

    if( opt.force_ciphersuite[0] > 0 )
    {
        mbedtls_ssl_ciphersuite_handle_t ciphersuite_info;
        ciphersuite_info =
            mbedtls_ssl_ciphersuite_from_id( opt.force_ciphersuite[0] );

        if( opt.max_version != -1 &&
            mbedtls_ssl_ver_gt(
                mbedtls_ssl_suite_get_min_minor_ver( ciphersuite_info ),
                opt.max_version ) )
        {
            mbedtls_printf( "forced ciphersuite not allowed with this protocol version\n" );
            ret = 2;
            goto usage;
        }
        if( opt.min_version != -1 &&
            mbedtls_ssl_ver_lt(
                mbedtls_ssl_suite_get_max_minor_ver( ciphersuite_info ),
                opt.min_version ) )
        {
            mbedtls_printf( "forced ciphersuite not allowed with this protocol version\n" );
            ret = 2;
            goto usage;
        }

        /* If the server selects a version that's not supported by
         * this suite, then there will be no common ciphersuite... */
        if( opt.max_version == -1 ||
            mbedtls_ssl_ver_gt(
                opt.max_version,
                mbedtls_ssl_suite_get_max_minor_ver( ciphersuite_info ) ) )
        {
            opt.max_version = mbedtls_ssl_suite_get_max_minor_ver( ciphersuite_info );
        }
        if( mbedtls_ssl_ver_lt(
                opt.min_version,
                mbedtls_ssl_suite_get_min_minor_ver( ciphersuite_info ) ) )
        {
            opt.min_version = mbedtls_ssl_suite_get_min_minor_ver( ciphersuite_info );
            /* DTLS starts with TLS 1.1 */
            if( opt.transport == MBEDTLS_SSL_TRANSPORT_DATAGRAM &&
                mbedtls_ssl_ver_lt( opt.min_version,
                                    MBEDTLS_SSL_MINOR_VERSION_2 ) )
            {
                opt.min_version = MBEDTLS_SSL_MINOR_VERSION_2;
            }
        }

        /* Enable RC4 if needed and not explicitly disabled */
        if( mbedtls_ssl_suite_get_cipher( ciphersuite_info ) == MBEDTLS_CIPHER_ARC4_128 )
        {
            if( opt.arc4 == MBEDTLS_SSL_ARC4_DISABLED )
            {
                mbedtls_printf( "forced RC4 ciphersuite with RC4 disabled\n" );
                ret = 2;
                goto usage;
            }

            opt.arc4 = MBEDTLS_SSL_ARC4_ENABLED;
        }
    }

#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
    cid_len = strlen( opt.cid_val ) / 2;
    if( cid_len > sizeof( cid ) )
    {
        mbedtls_printf( "CID too long\n" );
        goto exit;
    }

    if( unhexify( opt.cid_val, cid ) != 0 )
    {
        mbedtls_printf( "CID not valid hex\n" );
        goto exit;
    }

    /* Keep CID settings for renegotiation unless
     * specified otherwise. */
    if( opt.cid_enabled_renego == DFL_CID_ENABLED_RENEGO )
        opt.cid_enabled_renego = opt.cid_enabled;
    if( opt.cid_val_renego == DFL_CID_VALUE_RENEGO )
        opt.cid_val_renego = opt.cid_val;

    cid_renego_len = strlen( opt.cid_val_renego ) / 2;
    if( cid_renego_len > sizeof( cid_renego ) )
    {
        mbedtls_printf( "CID too long\n" );
        goto exit;
    }

    if( unhexify( opt.cid_val_renego, cid_renego ) != 0 )
    {
        mbedtls_printf( "CID not valid hex\n" );
        goto exit;
    }
#endif /* MBEDTLS_SSL_DTLS_CONNECTION_ID */

#if defined(MBEDTLS_KEY_EXCHANGE__SOME__PSK_ENABLED)
    /*
     * Unhexify the pre-shared key if any is given
     */
    if( strlen( opt.psk ) )
    {
        psk_len = strlen( opt.psk ) / 2;
        if( psk_len > sizeof( psk ) )
        {
            mbedtls_printf( "pre-shared key too long\n" );
            goto exit;
        }

        if( unhexify( opt.psk, psk ) != 0 )
        {
            mbedtls_printf( "pre-shared key not valid hex\n" );
            goto exit;
        }
    }
#endif /* MBEDTLS_KEY_EXCHANGE__SOME__PSK_ENABLED */

#if defined(MBEDTLS_ECP_C) && !defined(MBEDTLS_SSL_CONF_SINGLE_EC)
    if( opt.curves != NULL )
    {
        p = (char *) opt.curves;
        i = 0;

        if( strcmp( p, "none" ) == 0 )
        {
            curve_list[0] = MBEDTLS_ECP_DP_NONE;
        }
        else if( strcmp( p, "default" ) != 0 )
        {
            /* Leave room for a final NULL in curve list */
            while( i < CURVE_LIST_SIZE - 1 && *p != '\0' )
            {
                q = p;

                /* Terminate the current string */
                while( *p != ',' && *p != '\0' )
                    p++;
                if( *p == ',' )
                    *p++ = '\0';

                if( ( curve_cur = mbedtls_ecp_curve_info_from_name( q ) ) != NULL )
                {
                    curve_list[i++] = curve_cur->grp_id;
                }
                else
                {
                    mbedtls_printf( "unknown curve %s\n", q );
                    mbedtls_printf( "supported curves: " );
                    for( curve_cur = mbedtls_ecp_curve_list();
                         curve_cur->grp_id != MBEDTLS_ECP_DP_NONE;
                         curve_cur++ )
                    {
                        mbedtls_printf( "%s ", curve_cur->name );
                    }
                    mbedtls_printf( "\n" );
                    goto exit;
                }
            }

            mbedtls_printf("Number of curves: %d\n", i );

            if( i == CURVE_LIST_SIZE - 1 && *p != '\0' )
            {
                mbedtls_printf( "curves list too long, maximum %d",
                                CURVE_LIST_SIZE - 1 );
                goto exit;
            }

            curve_list[i] = MBEDTLS_ECP_DP_NONE;
        }
    }
#endif /* MBEDTLS_ECP_C && !MBEDTLS_SSL_CONF_SINGLE_EC */

#if defined(MBEDTLS_SSL_ALPN)
    if( opt.alpn_string != NULL )
    {
        p = (char *) opt.alpn_string;
        i = 0;

        /* Leave room for a final NULL in alpn_list */
        while( i < ALPN_LIST_SIZE - 1 && *p != '\0' )
        {
            alpn_list[i++] = p;

            /* Terminate the current string and move on to next one */
            while( *p != ',' && *p != '\0' )
                p++;
            if( *p == ',' )
                *p++ = '\0';
        }
    }
#endif /* MBEDTLS_SSL_ALPN */

    /*
     * 0. Initialize the RNG and the session data
     */
    mbedtls_printf( "\n  . Seeding the random number generator..." );
    fflush( stdout );

    mbedtls_entropy_init( entropy );
#if defined(MBEDTLS_CTR_DRBG_C)
    if( ( ret = mbedtls_ctr_drbg_seed( ctr_drbg, mbedtls_entropy_func,
                                       entropy, (const unsigned char *) pers,
                                       strlen( pers ) ) ) != 0 )
    {
        mbedtls_printf( " failed\n  ! mbedtls_ctr_drbg_seed returned -0x%x\n",
                        -ret );
        goto exit;
    }
#else /* MBEDTLS_CTR_DRBG_C */
    if( ( ret = mbedtls_hmac_drbg_seed( hmac_drbg,
                                        mbedtls_md_info_from_type(
                                            available_hashes[0] ),
                                        mbedtls_entropy_func,
                                        entropy, (const unsigned char *) pers,
                                        strlen( pers ) ) ) != 0 )
    {
        mbedtls_printf( " failed\n  ! mbedtls_ctr_drbg_seed returned -0x%x\n",
                        -ret );
        goto exit;
    }
#endif /* MBEDTLS_CTR_DRBG */

    mbedtls_printf( " ok\n" );

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    /*
     * 1.1. Load the trusted CA
     */
    mbedtls_printf( "  . Loading the CA root certificate ..." );
    fflush( stdout );

    if( strcmp( opt.ca_path, "none" ) == 0 ||
        strcmp( opt.ca_file, "none" ) == 0 )
    {
        ret = 0;
    }
    else
#if defined(MBEDTLS_FS_IO)
    if( strlen( opt.ca_path ) )
        ret = mbedtls_x509_crt_parse_path( cacert, opt.ca_path );
    else if( strlen( opt.ca_file ) )
        ret = mbedtls_x509_crt_parse_file( cacert, opt.ca_file );
    else
#endif
#if defined(MBEDTLS_CERTS_C)
    {
#if defined(MBEDTLS_PEM_PARSE_C)
        for( i = 0; mbedtls_test_cas[i] != NULL; i++ )
        {
            ret = mbedtls_x509_crt_parse( cacert,
                                  (const unsigned char *) mbedtls_test_cas[i],
                                  mbedtls_test_cas_len[i] );
            if( ret != 0 )
                break;
        }
        if( ret == 0 )
#endif /* MBEDTLS_PEM_PARSE_C */
        for( i = 0; mbedtls_test_cas_der[i] != NULL; i++ )
        {
            ret = mbedtls_x509_crt_parse_der_nocopy( cacert,
                         (const unsigned char *) mbedtls_test_cas_der[i],
                         mbedtls_test_cas_der_len[i] );
            if( ret != 0 )
                break;
        }
    }
#else
    {
        ret = 1;
        mbedtls_printf( "MBEDTLS_CERTS_C not defined." );
    }
#endif /* MBEDTLS_CERTS_C */
    if( ret < 0 )
    {
        mbedtls_printf( " failed\n  !  mbedtls_x509_crt_parse returned -0x%x\n\n",
                        -ret );
        goto exit;
    }

    mbedtls_printf( " ok (%d skipped)\n", ret );

    /*
     * 1.2. Load own certificate and private key
     *
     * (can be skipped if client authentication is not required)
     */
    mbedtls_printf( "  . Loading the client cert. and key..." );
    fflush( stdout );

    if( strcmp( opt.crt_file, "none" ) == 0 )
        ret = 0;
    else
#if defined(MBEDTLS_FS_IO)
    if( strlen( opt.crt_file ) )
        ret = mbedtls_x509_crt_parse_file( clicert, opt.crt_file );
    else
#endif
#if defined(MBEDTLS_CERTS_C)
#if defined(MBEDTLS_PEM_PARSE_C)
        ret = mbedtls_x509_crt_parse( clicert,
                (const unsigned char *) mbedtls_test_cli_crt,
                mbedtls_test_cli_crt_len );
#else
        ret = mbedtls_x509_crt_parse_der_nocopy( clicert,
                (const unsigned char *) mbedtls_test_cli_crt,
                mbedtls_test_cli_crt_len );
#endif
#else
    {
        ret = 1;
        mbedtls_printf( "MBEDTLS_CERTS_C not defined." );
    }
#endif
    if( ret != 0 )
    {
        mbedtls_printf( " failed\n  !  mbedtls_x509_crt_parse returned -0x%x\n\n",
                        -ret );
        goto exit;
    }

    if( strcmp( opt.key_file, "none" ) == 0 )
        ret = 0;
    else
#if defined(MBEDTLS_FS_IO)
    if( strlen( opt.key_file ) )
        ret = mbedtls_pk_parse_keyfile( pkey, opt.key_file, "" );
    else
#endif
#if defined(MBEDTLS_CERTS_C)
        ret = mbedtls_pk_parse_key( pkey,
                (const unsigned char *) mbedtls_test_cli_key,
                mbedtls_test_cli_key_len, NULL, 0 );
#else
    {
        ret = 1;
        mbedtls_printf( "MBEDTLS_CERTS_C not defined." );
    }
#endif
    if( ret != 0 )
    {
        mbedtls_printf( " failed\n  !  mbedtls_pk_parse_key returned -0x%x\n\n",
                        -ret );
        goto exit;
    }

    mbedtls_printf( " ok\n" );
#endif /* MBEDTLS_X509_CRT_PARSE_C */

    /*
     * 2. Start the connection
     */
    if( opt.server_addr == NULL)
        opt.server_addr = opt.server_name;

    mbedtls_printf( "  . Connecting to %s/%s/%s...",
            opt.transport == MBEDTLS_SSL_TRANSPORT_STREAM ? "tcp" : "udp",
            opt.server_addr, opt.server_port );
    fflush( stdout );

    if( ( ret = mbedtls_net_connect( &server_fd,
                       opt.server_addr, opt.server_port,
                       opt.transport == MBEDTLS_SSL_TRANSPORT_STREAM ?
                       MBEDTLS_NET_PROTO_TCP : MBEDTLS_NET_PROTO_UDP ) ) != 0 )
    {
        mbedtls_printf( " failed\n  ! mbedtls_net_connect returned -0x%x\n\n",
                        -ret );
        goto exit;
    }

    if( opt.nbio > 0 )
        ret = mbedtls_net_set_nonblock( &server_fd );
    else
        ret = mbedtls_net_set_block( &server_fd );
    if( ret != 0 )
    {
        mbedtls_printf( " failed\n  ! net_set_(non)block() returned -0x%x\n\n",
                        -ret );
        goto exit;
    }

    mbedtls_printf( " ok\n" );

    /*
     * 3. Setup stuff
     */
    mbedtls_printf( "  . Setting up the SSL/TLS structure..." );
    fflush( stdout );

    if( ( ret = mbedtls_ssl_config_defaults( conf,
                    MBEDTLS_SSL_IS_CLIENT,
                    opt.transport,
                    MBEDTLS_SSL_PRESET_DEFAULT ) ) != 0 )
    {
        mbedtls_printf( " failed\n  ! mbedtls_ssl_config_defaults returned -0x%x\n\n",
                        -ret );
        goto exit;
    }

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    /* The default algorithms profile disables SHA-1, but our tests still
       rely on it heavily. */
    if( opt.allow_sha1 > 0 )
    {
        crt_profile_for_test.allowed_mds |= MBEDTLS_X509_ID_FLAG( MBEDTLS_MD_SHA1 );
        mbedtls_ssl_conf_cert_profile( conf, &crt_profile_for_test );
#if !defined(MBEDTLS_SSL_CONF_SINGLE_HASH)
        mbedtls_ssl_conf_sig_hashes( conf, available_hashes );
#endif
    }

#if !defined(MBEDTLS_X509_REMOVE_VERIFY_CALLBACK)
    mbedtls_ssl_conf_verify( conf, my_verify, NULL );
    memset( peer_crt_info, 0, sizeof( peer_crt_info ) );
#endif /* MBEDTLS_X509_REMOVE_VERIFY_CALLBACK */
#endif /* MBEDTLS_X509_CRT_PARSE_C */

#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID) && \
    !defined(MBEDTLS_SSL_CONF_CID_LEN) &&      \
    !defined(MBEDTLS_SSL_CONF_IGNORE_UNEXPECTED_CID)
    if( opt.cid_enabled == 1 || opt.cid_enabled_renego == 1 )
    {
        if( opt.cid_enabled == 1        &&
            opt.cid_enabled_renego == 1 &&
            cid_len != cid_renego_len )
        {
            mbedtls_printf( "CID length must not change during renegotiation\n" );
            goto usage;
        }

        if( opt.cid_enabled == 1 )
            ret = mbedtls_ssl_conf_cid( conf, cid_len,
                                        MBEDTLS_SSL_UNEXPECTED_CID_IGNORE );
        else
            ret = mbedtls_ssl_conf_cid( conf, cid_renego_len,
                                        MBEDTLS_SSL_UNEXPECTED_CID_IGNORE );

        if( ret != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_conf_cid_len returned -%#04x\n\n",
                            -ret );
            goto exit;
        }
    }
#endif /* MBEDTLS_SSL_DTLS_CONNECTION_ID &&
          !MBEDTLS_SSL_CONF_CID_LEN &&
          !MBEDTLS_SSL_CONF_IGNORE_UNEXPECTED_CID */

    if( opt.auth_mode != DFL_AUTH_MODE )
        mbedtls_ssl_conf_authmode( conf, opt.auth_mode );

#if defined(MBEDTLS_SSL_PROTO_DTLS)
    if( opt.hs_to_min != DFL_HS_TO_MIN || opt.hs_to_max != DFL_HS_TO_MAX )
        mbedtls_ssl_conf_handshake_timeout( conf, opt.hs_to_min,
                                            opt.hs_to_max );

    if( opt.dgram_packing != DFL_DGRAM_PACKING )
        mbedtls_ssl_set_datagram_packing( ssl, opt.dgram_packing );
#endif /* MBEDTLS_SSL_PROTO_DTLS */

#if defined(MBEDTLS_SSL_MAX_FRAGMENT_LENGTH)
    if( ( ret = mbedtls_ssl_conf_max_frag_len( conf, opt.mfl_code ) ) != 0 )
    {
        mbedtls_printf( " failed\n  ! mbedtls_ssl_conf_max_frag_len returned %d\n\n",
                        ret );
        goto exit;
    }
#endif

#if defined(MBEDTLS_SSL_TRUNCATED_HMAC)
    if( opt.trunc_hmac != DFL_TRUNC_HMAC )
        mbedtls_ssl_conf_truncated_hmac( conf, opt.trunc_hmac );
#endif

#if defined(MBEDTLS_SSL_EXTENDED_MASTER_SECRET) &&       \
    !defined(MBEDTLS_SSL_CONF_EXTENDED_MASTER_SECRET) && \
    !defined(MBEDTLS_SSL_CONF_ENFORCE_EXTENDED_MASTER_SECRET)
    if( opt.extended_ms != DFL_EXTENDED_MS )
        mbedtls_ssl_conf_extended_master_secret( conf, opt.extended_ms );
    if( opt.enforce_extended_master_secret != DFL_EXTENDED_MS_ENFORCE )
        mbedtls_ssl_conf_extended_master_secret_enforce( conf,
            opt.enforce_extended_master_secret );
#endif

#if defined(MBEDTLS_SSL_ENCRYPT_THEN_MAC)
    if( opt.etm != DFL_ETM )
        mbedtls_ssl_conf_encrypt_then_mac( conf, opt.etm );
#endif

#if defined(MBEDTLS_SSL_CBC_RECORD_SPLITTING)
    if( opt.recsplit != DFL_RECSPLIT )
        mbedtls_ssl_conf_cbc_record_splitting( conf, opt.recsplit
                                  ? MBEDTLS_SSL_CBC_RECORD_SPLITTING_ENABLED
                                  : MBEDTLS_SSL_CBC_RECORD_SPLITTING_DISABLED );
#endif

#if defined(MBEDTLS_DHM_C)
    if( opt.dhmlen != DFL_DHMLEN )
        mbedtls_ssl_conf_dhm_min_bitlen( conf, opt.dhmlen );
#endif

#if defined(MBEDTLS_SSL_ALPN)
    if( opt.alpn_string != NULL )
        if( ( ret = mbedtls_ssl_conf_alpn_protocols( conf, alpn_list ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_conf_alpn_protocols returned %d\n\n",
                            ret );
            goto exit;
        }
#endif

#if defined(MBEDTLS_CTR_DRBG_C)
#if !defined(MBEDTLS_SSL_CONF_RNG)
    mbedtls_ssl_conf_rng( conf, mbedtls_ctr_drbg_random, ctr_drbg );
#else
    rng_ctx_global = ctr_drbg;
#endif
#else /* MBEDTLS_CTR_DRBG_C */
#if !defined(MBEDTLS_SSL_CONF_RNG)
    mbedtls_ssl_conf_rng( conf, mbedtls_hmac_drbg_random, hmac_drbg );
#else
    rng_ctx_global = hmac_drbg;
#endif
#endif /* MBEDTLS_CTR_DRBG_C */

#if defined(MBEDTLS_DEBUG_C)
    mbedtls_ssl_conf_dbg( conf, my_debug, stdout );
#endif

#if !defined(MBEDTLS_SSL_CONF_READ_TIMEOUT)
    mbedtls_ssl_conf_read_timeout( conf, opt.read_timeout );
#endif

#if defined(MBEDTLS_SSL_SESSION_TICKETS)
    mbedtls_ssl_conf_session_tickets( conf, opt.tickets );
#endif

#if !defined(MBEDTLS_SSL_CONF_SINGLE_CIPHERSUITE)
    if( opt.force_ciphersuite[0] != DFL_FORCE_CIPHER )
        mbedtls_ssl_conf_ciphersuites( conf, opt.force_ciphersuite );
#endif /* MBEDTLS_SSL_CONF_SINGLE_CIPHERSUITE */

#if defined(MBEDTLS_ARC4_C)
    if( opt.arc4 != DFL_ARC4 )
        mbedtls_ssl_conf_arc4_support( conf, opt.arc4 );
#endif

#if !defined(MBEDTLS_SSL_CONF_ALLOW_LEGACY_RENEGOTIATION)
    if( opt.allow_legacy != DFL_ALLOW_LEGACY )
        mbedtls_ssl_conf_legacy_renegotiation( conf, opt.allow_legacy );
#endif /* !MBEDTLS_SSL_CONF_ALLOW_LEGACY_RENEGOTIATION */
#if defined(MBEDTLS_SSL_RENEGOTIATION)
    mbedtls_ssl_conf_renegotiation( conf, opt.renegotiation );
#endif

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    if( strcmp( opt.ca_path, "none" ) != 0 &&
        strcmp( opt.ca_file, "none" ) != 0 )
    {
        mbedtls_ssl_conf_ca_chain( conf, cacert, NULL );
    }
    if( strcmp( opt.crt_file, "none" ) != 0 &&
        strcmp( opt.key_file, "none" ) != 0 )
    {
        if( ( ret = mbedtls_ssl_conf_own_cert( conf, clicert, pkey ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_conf_own_cert returned %d\n\n",
                            ret );
            goto exit;
        }
    }
#endif

#if defined(MBEDTLS_ECP_C)
#if !defined(MBEDTLS_SSL_CONF_SINGLE_EC)
    if( opt.curves != NULL &&
        strcmp( opt.curves, "default" ) != 0 )
    {
        mbedtls_ssl_conf_curves( conf, curve_list );
    }
#endif /* !MBEDTLS_SSL_CONF_SINGLE_EC */
#endif /* MBEDTLS_ECP_C */

#if defined(MBEDTLS_KEY_EXCHANGE__SOME__PSK_ENABLED)
    if( ( ret = mbedtls_ssl_conf_psk( conf, psk, psk_len,
                             (const unsigned char *) opt.psk_identity,
                             strlen( opt.psk_identity ) ) ) != 0 )
    {
        mbedtls_printf( " failed\n  ! mbedtls_ssl_conf_psk returned %d\n\n",
                        ret );
        goto exit;
    }
#endif

#if !defined(MBEDTLS_SSL_CONF_MIN_MINOR_VER) || \
    !defined(MBEDTLS_SSL_CONF_MIN_MAJOR_VER) || \
    !defined(MBEDTLS_SSL_CONF_MAX_MINOR_VER) || \
    !defined(MBEDTLS_SSL_CONF_MAX_MAJOR_VER)
    if( opt.min_version != DFL_MIN_VERSION )
        mbedtls_ssl_conf_min_version( conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                      opt.min_version );

    if( opt.max_version != DFL_MAX_VERSION )
        mbedtls_ssl_conf_max_version( conf, MBEDTLS_SSL_MAJOR_VERSION_3,
                                      opt.max_version );
#endif

#if defined(MBEDTLS_SSL_FALLBACK_SCSV)
    if( opt.fallback != DFL_FALLBACK )
        mbedtls_ssl_conf_fallback( conf, opt.fallback );
#endif

    if( ( ret = mbedtls_ssl_setup( ssl, conf ) ) != 0 )
    {
        mbedtls_printf( " failed\n  ! mbedtls_ssl_setup returned -0x%x\n\n",
                        -ret );
        goto exit;
    }

#if defined(MBEDTLS_X509_CRT_PARSE_C) && !defined(MBEDTLS_X509_REMOVE_HOSTNAME_VERIFICATION)
    if( ( ret = mbedtls_ssl_set_hostname( ssl, opt.server_name ) ) != 0 )
    {
        mbedtls_printf( " failed\n  ! mbedtls_ssl_set_hostname returned %d\n\n",
                        ret );
        goto exit;
    }
#endif

#if defined(MBEDTLS_KEY_EXCHANGE_ECJPAKE_ENABLED)
    if( opt.ecjpake_pw != DFL_ECJPAKE_PW )
    {
        if( ( ret = mbedtls_ssl_set_hs_ecjpake_password( ssl,
                        (const unsigned char *) opt.ecjpake_pw,
                                        strlen( opt.ecjpake_pw ) ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_set_hs_ecjpake_password returned %d\n\n",
                            ret );
            goto exit;
        }
    }
#endif

#if !defined(MBEDTLS_SSL_CONF_RECV) && \
    !defined(MBEDTLS_SSL_CONF_SEND) && \
    !defined(MBEDTLS_SSL_CONF_RECV_TIMEOUT)
    io_ctx.ssl = ssl;
    io_ctx.net = &server_fd;
    mbedtls_ssl_set_bio( ssl, &io_ctx, send_cb, recv_cb,
                         opt.nbio == 0 ? recv_timeout_cb : NULL );
#else
     mbedtls_ssl_set_bio_ctx( ssl, &server_fd );
#endif

#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
    if( opt.transport == MBEDTLS_SSL_TRANSPORT_DATAGRAM )
    {
        if( ( ret = mbedtls_ssl_set_cid( ssl, opt.cid_enabled,
                                         cid, cid_len ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_set_cid returned %d\n\n",
                            ret );
            goto exit;
        }
    }
#endif /* MBEDTLS_SSL_DTLS_CONNECTION_ID */

#if defined(MBEDTLS_SSL_PROTO_DTLS)
    if( opt.dtls_mtu != DFL_DTLS_MTU )
        mbedtls_ssl_set_mtu( ssl, opt.dtls_mtu );
#endif

#if defined(MBEDTLS_TIMING_C)
#if !defined(MBEDTLS_SSL_CONF_SET_TIMER) && \
    !defined(MBEDTLS_SSL_CONF_GET_TIMER)
    mbedtls_ssl_set_timer_cb( ssl, &timer, mbedtls_timing_set_delay,
                                            mbedtls_timing_get_delay );
#else
    mbedtls_ssl_set_timer_cb_ctx( ssl, &timer );
#endif
#endif

#if defined(MBEDTLS_ECP_RESTARTABLE)
    if( opt.ec_max_ops != DFL_EC_MAX_OPS )
        mbedtls_ecp_set_max_ops( opt.ec_max_ops );
#endif

    mbedtls_printf( " ok\n" );

    /*
     * 4. Handshake
     */
    mbedtls_printf( "  . Performing the SSL/TLS handshake..." );
    fflush( stdout );

    while( ( ret = mbedtls_ssl_handshake( ssl ) ) != 0 )
    {
        if( ret != MBEDTLS_ERR_SSL_WANT_READ &&
            ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
            ret != MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_handshake returned -0x%x\n",
                            -ret );
            if( ret == MBEDTLS_ERR_X509_CERT_VERIFY_FAILED )
                mbedtls_printf(
                    "    Unable to verify the server's certificate. "
                        "Either it is invalid,\n"
                    "    or you didn't set ca_file or ca_path "
                        "to an appropriate value.\n"
                    "    Alternatively, you may want to use "
                        "auth_mode=optional for testing purposes.\n" );
            mbedtls_printf( "\n" );
            goto exit;
        }

#if defined(MBEDTLS_ECP_RESTARTABLE)
        if( ret == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
            continue;
#endif

        /* For event-driven IO, wait for socket to become available */
        if( opt.event == 1 /* level triggered IO */ )
        {
#if defined(MBEDTLS_TIMING_C)
            ret = idle( &server_fd, &timer, ret );
#else
            ret = idle( &server_fd, ret );
#endif
            if( ret != 0 )
                goto exit;
        }
    }

    mbedtls_printf( " ok\n    [ Protocol is %s ]\n    [ Ciphersuite is %s ]\n",
                    mbedtls_ssl_get_version( ssl ),
                    mbedtls_ssl_get_ciphersuite( ssl ) );

    if( ( ret = mbedtls_ssl_get_record_expansion( ssl ) ) >= 0 )
        mbedtls_printf( "    [ Record expansion is %d ]\n", ret );
    else
        mbedtls_printf( "    [ Record expansion is unknown (compression) ]\n" );

#if defined(MBEDTLS_SSL_MAX_FRAGMENT_LENGTH)
    mbedtls_printf( "    [ Maximum input fragment length is %u ]\n",
                    (unsigned int) mbedtls_ssl_get_input_max_frag_len( ssl ) );
    mbedtls_printf( "    [ Maximum output fragment length is %u ]\n",
                    (unsigned int) mbedtls_ssl_get_output_max_frag_len( ssl ) );
#endif

#if defined(MBEDTLS_SSL_ALPN)
    if( opt.alpn_string != NULL )
    {
        const char *alp = mbedtls_ssl_get_alpn_protocol( ssl );
        mbedtls_printf( "    [ Application Layer Protocol is %s ]\n",
                alp ? alp : "(none)" );
    }
#endif

    if( opt.reconnect != 0 )
    {
        mbedtls_printf("  . Saving session for reuse..." );
        fflush( stdout );

        if( opt.reco_mode == 1 )
        {
            /* free any previously saved data */
            if( session_data != NULL )
            {
                mbedtls_platform_zeroize( session_data, session_data_len );
                mbedtls_free( session_data );
                session_data = NULL;
            }

            /* get size of the buffer needed */
            mbedtls_ssl_session_save( mbedtls_ssl_get_session_pointer( ssl ),
                                      NULL, 0, &session_data_len );
            session_data = mbedtls_calloc( 1, session_data_len );
            if( session_data == NULL )
            {
                mbedtls_printf( " failed\n  ! alloc %u bytes for session data\n",
                                (unsigned) session_data_len );
                ret = MBEDTLS_ERR_SSL_ALLOC_FAILED;
                goto exit;
            }

            /* actually save session data */
            if( ( ret = mbedtls_ssl_session_save( mbedtls_ssl_get_session_pointer( ssl ),
                                                  session_data, session_data_len,
                                                  &session_data_len ) ) != 0 )
            {
                mbedtls_printf( " failed\n  ! mbedtls_ssl_session_saved returned -0x%04x\n\n",
                                -ret );
                goto exit;
            }
        }
        else
        {
            if( ( ret = mbedtls_ssl_get_session( ssl, &saved_session ) ) != 0 )
            {
                mbedtls_printf( " failed\n  ! mbedtls_ssl_get_session returned -0x%x\n\n",
                                -ret );
                goto exit;
            }
        }

        mbedtls_printf( " ok\n" );

        if( opt.reco_mode == 1 )
        {
            mbedtls_printf( "    [ Saved %u bytes of session data]\n",
                            (unsigned) session_data_len );
        }
    }

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    /*
     * 5. Verify the server certificate
     */
    mbedtls_printf( "  . Verifying peer X.509 certificate..." );

    if( ( flags = mbedtls_ssl_get_verify_result( ssl ) ) != 0 )
    {
#if !defined(MBEDTLS_X509_REMOVE_INFO)
        char vrfy_buf[512];
#endif

        mbedtls_printf( " failed\n" );

#if !defined(MBEDTLS_X509_REMOVE_INFO)
        mbedtls_x509_crt_verify_info( vrfy_buf, sizeof( vrfy_buf ),
                                      "  ! ", flags );

        mbedtls_printf( "%s\n", vrfy_buf );
#endif
    }
    else
        mbedtls_printf( " ok\n" );

#if !defined(MBEDTLS_X509_REMOVE_INFO) && \
    !defined(MBEDTLS_X509_REMOVE_VERIFY_CALLBACK)
    mbedtls_printf( "  . Peer certificate information    ...\n" );
    mbedtls_printf( "%s\n", peer_crt_info );
#endif /* !MBEDTLS_X509_REMOVE_INFO && !MBEDTLS_X509_REMOVE_VERIFY_CALLBACK */
#endif /* MBEDTLS_X509_CRT_PARSE_C */

#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
    ret = report_cid_usage( ssl, "initial handshake" );
    if( ret != 0 )
        goto exit;

    if( opt.transport == MBEDTLS_SSL_TRANSPORT_DATAGRAM )
    {
        if( ( ret = mbedtls_ssl_set_cid( ssl, opt.cid_enabled_renego,
                                         cid_renego,
                                         cid_renego_len ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_set_cid returned %d\n\n",
                            ret );
            return( ret );
        }
    }
#endif /* MBEDTLS_SSL_DTLS_CONNECTION_ID */

#if defined(MBEDTLS_SSL_RENEGOTIATION)
    if( opt.renegotiate )
    {
        /*
         * Perform renegotiation (this must be done when the server is waiting
         * for input from our side).
         */
        mbedtls_printf( "  . Performing renegotiation..." );
        fflush( stdout );
        while( ( ret = mbedtls_ssl_renegotiate( ssl ) ) != 0 )
        {
            if( ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
                ret != MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
            {
                mbedtls_printf( " failed\n  ! mbedtls_ssl_renegotiate returned %d\n\n",
                                ret );
                goto exit;
            }

#if defined(MBEDTLS_ECP_RESTARTABLE)
            if( ret == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
                continue;
#endif

            /* For event-driven IO, wait for socket to become available */
            if( opt.event == 1 /* level triggered IO */ )
            {
#if defined(MBEDTLS_TIMING_C)
                idle( &server_fd, &timer, ret );
#else
                idle( &server_fd, ret );
#endif
            }

        }
        mbedtls_printf( " ok\n" );
    }
#endif /* MBEDTLS_SSL_RENEGOTIATION */

#if defined(MBEDTLS_SSL_DTLS_CONNECTION_ID)
    ret = report_cid_usage( ssl, "after renegotiation" );
    if( ret != 0 )
        goto exit;
#endif /* MBEDTLS_SSL_DTLS_CONNECTION_ID */

    /*
     * 6. Write the GET request
     */
    retry_left = opt.max_resend;
send_request:
    mbedtls_printf( "  > Write to server:" );
    fflush( stdout );

    len = mbedtls_snprintf( (char *) buf, main_buf_len - 1, GET_REQUEST,
                            opt.request_page );
    tail_len = (int) strlen( GET_REQUEST_END );

    /* Add padding to GET request to reach opt.request_size in length */
    if( opt.request_size != DFL_REQUEST_SIZE &&
        len + tail_len < opt.request_size )
    {
        memset( buf + len, 'A', opt.request_size - len - tail_len );
        len += opt.request_size - len - tail_len;
    }

    strncpy( (char *) buf + len, GET_REQUEST_END, main_buf_len - len - 1 );
    len += tail_len;

    /* Truncate if request size is smaller than the "natural" size */
    if( opt.request_size != DFL_REQUEST_SIZE &&
        len > opt.request_size )
    {
        len = opt.request_size;

        /* Still end with \r\n unless that's really not possible */
        if( len >= 2 ) buf[len - 2] = '\r';
        if( len >= 1 ) buf[len - 1] = '\n';
    }

    if( opt.transport == MBEDTLS_SSL_TRANSPORT_STREAM )
    {
        written = 0;
        frags = 0;

        do
        {
            while( ( ret = mbedtls_ssl_write( ssl, buf + written,
                                              len - written ) ) < 0 )
            {
                if( ret != MBEDTLS_ERR_SSL_WANT_READ &&
                    ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
                    ret != MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
                {
                    mbedtls_printf( " failed\n  ! mbedtls_ssl_write returned -0x%x\n\n",
                                    -ret );
                    goto exit;
                }

                /* For event-driven IO, wait for socket to become available */
                if( opt.event == 1 /* level triggered IO */ )
                {
#if defined(MBEDTLS_TIMING_C)
                    idle( &server_fd, &timer, ret );
#else
                    idle( &server_fd, ret );
#endif
                }
            }

            frags++;
            written += ret;
        }
        while( written < len );
    }
    else /* Not stream, so datagram */
    {
        while( 1 )
        {
            ret = mbedtls_ssl_write( ssl, buf, len );

#if defined(MBEDTLS_ECP_RESTARTABLE)
            if( ret == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
                continue;
#endif

            if( ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE )
                break;

            /* For event-driven IO, wait for socket to become available */
            if( opt.event == 1 /* level triggered IO */ )
            {
#if defined(MBEDTLS_TIMING_C)
                idle( &server_fd, &timer, ret );
#else
                idle( &server_fd, ret );
#endif
            }
        }

        if( ret < 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_write returned %d\n\n",
                            ret );
            goto exit;
        }

        frags = 1;
        written = ret;

        if( written < len )
        {
            mbedtls_printf( " warning\n  ! request didn't fit into single datagram and "
                            "was truncated to size %u", (unsigned) written );
        }
    }

    buf[written] = '\0';
    mbedtls_printf( " %d bytes written in %d fragments\n\n%s\n",
                    written, frags, (char *) buf );

    /* Send a non-empty request if request_size == 0 */
    if ( len == 0 )
    {
        opt.request_size = DFL_REQUEST_SIZE;
        goto send_request;
    }

    /*
     * 7. Read the HTTP response
     */
    mbedtls_printf( "  < Read from server:" );
    fflush( stdout );

    /*
     * TLS and DTLS need different reading styles (stream vs datagram)
     */
    if( opt.transport == MBEDTLS_SSL_TRANSPORT_STREAM )
    {
        do
        {
            len = main_buf_len - 1;
            memset( buf, 0, main_buf_len );
            ret = mbedtls_ssl_read( ssl, buf, len );

#if defined(MBEDTLS_ECP_RESTARTABLE)
            if( ret == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
                continue;
#endif

            if( ret == MBEDTLS_ERR_SSL_WANT_READ ||
                ret == MBEDTLS_ERR_SSL_WANT_WRITE )
            {
                /* For event-driven IO, wait for socket to become available */
                if( opt.event == 1 /* level triggered IO */ )
                {
#if defined(MBEDTLS_TIMING_C)
                    idle( &server_fd, &timer, ret );
#else
                    idle( &server_fd, ret );
#endif
                }
                continue;
            }

            if( ret <= 0 )
            {
                switch( ret )
                {
                    case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
                        mbedtls_printf( " connection was closed gracefully\n" );
                        ret = 0;
                        goto close_notify;

                    case 0:
                    case MBEDTLS_ERR_NET_CONN_RESET:
                        mbedtls_printf( " connection was reset by peer\n" );
                        ret = 0;
                        goto reconnect;

                    default:
                        mbedtls_printf( " mbedtls_ssl_read returned -0x%x\n",
                                        -ret );
                        goto exit;
                }
            }

            len = ret;
            buf[len] = '\0';
            mbedtls_printf( " %d bytes read\n\n%s", len, (char *) buf );

            /* End of message should be detected according to the syntax of the
             * application protocol (eg HTTP), just use a dummy test here. */
            if( ret > 0 && buf[len-1] == '\n' )
            {
                ret = 0;
                break;
            }
        }
        while( 1 );
    }
    else /* Not stream, so datagram */
    {
        len = main_buf_len - 1;
        memset( buf, 0, main_buf_len );

        while( 1 )
        {
            ret = mbedtls_ssl_read( ssl, buf, len );

#if defined(MBEDTLS_ECP_RESTARTABLE)
            if( ret == MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
                continue;
#endif

            if( ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE )
                break;

            /* For event-driven IO, wait for socket to become available */
            if( opt.event == 1 /* level triggered IO */ )
            {
#if defined(MBEDTLS_TIMING_C)
                idle( &server_fd, &timer, ret );
#else
                idle( &server_fd, ret );
#endif
            }
        }

        if( ret <= 0 )
        {
            switch( ret )
            {
                case MBEDTLS_ERR_SSL_TIMEOUT:
                    mbedtls_printf( " timeout\n" );
                    if( retry_left-- > 0 )
                        goto send_request;
                    goto exit;

                case MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY:
                    mbedtls_printf( " connection was closed gracefully\n" );
                    ret = 0;
                    goto close_notify;

                default:
                    mbedtls_printf( " mbedtls_ssl_read returned -0x%x\n", -ret );
                    goto exit;
            }
        }

        len = ret;
        buf[len] = '\0';
        mbedtls_printf( " %d bytes read\n\n%s", len, (char *) buf );
        ret = 0;
    }

    /*
     * 7b. Simulate hard reset and reconnect from same port?
     */
    if( opt.reconnect_hard != 0 )
    {
        opt.reconnect_hard = 0;

        mbedtls_printf( "  . Restarting connection from same port..." );
        fflush( stdout );

#if defined(MBEDTLS_X509_CRT_PARSE_C) && \
    !defined(MBEDTLS_X509_REMOVE_VERIFY_CALLBACK)
        memset( peer_crt_info, 0, sizeof( peer_crt_info ) );
#endif /* MBEDTLS_X509_CRT_PARSE_C && !MBEDTLS_X509_REMOVE_VERIFY_CALLBACK */

        if( ( ret = mbedtls_ssl_session_reset( ssl ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_session_reset returned -0x%x\n\n",
                            -ret );
            goto exit;
        }

        while( ( ret = mbedtls_ssl_handshake( ssl ) ) != 0 )
        {
            if( ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
                ret != MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
            {
                mbedtls_printf( " failed\n  ! mbedtls_ssl_handshake returned -0x%x\n\n",
                                -ret );
                goto exit;
            }

            /* For event-driven IO, wait for socket to become available */
            if( opt.event == 1 /* level triggered IO */ )
            {
#if defined(MBEDTLS_TIMING_C)
                idle( &server_fd, &timer, ret );
#else
                idle( &server_fd, ret );
#endif
            }
        }

        mbedtls_printf( " ok\n" );

        goto send_request;
    }

    /*
     * 7c. Simulate serialize/deserialize and go back to data exchange
     */
#if defined(MBEDTLS_SSL_CONTEXT_SERIALIZATION)
    if( opt.serialize != 0 )
    {
        size_t buf_len;

        mbedtls_printf( "  . Serializing live connection..." );

        ret = mbedtls_ssl_context_save( ssl, NULL, 0, &buf_len );
        if( ret != MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_context_save returned "
                            "-0x%x\n\n", -ret );

            goto exit;
        }

        if( ( context_buf = mbedtls_calloc( 1, buf_len ) ) == NULL )
        {
            mbedtls_printf( " failed\n  ! Couldn't allocate buffer for "
                            "serialized context" );

            goto exit;
        }
        context_buf_len = buf_len;

        if( ( ret = mbedtls_ssl_context_save( ssl, context_buf,
                                              buf_len, &buf_len ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_context_save returned "
                            "-0x%x\n\n", -ret );

            goto exit;
        }

        mbedtls_printf( " ok\n" );

        if( opt.serialize == 1 )
        {
            /* nothing to do here, done by context_save() already */
            mbedtls_printf( "  . Context has been reset... ok" );
        }

        if( opt.serialize == 2 )
        {
            mbedtls_printf( "  . Freeing and reinitializing context..." );

            mbedtls_ssl_free( ssl );

            mbedtls_ssl_init( ssl );

            if( ( ret = mbedtls_ssl_setup( ssl, conf ) ) != 0 )
            {
                mbedtls_printf( " failed\n  ! mbedtls_ssl_setup returned "
                                "-0x%x\n\n", -ret );
                goto exit;
            }

#if !defined(MBEDTLS_SSL_CONF_RECV) &&          \
    !defined(MBEDTLS_SSL_CONF_SEND) &&          \
    !defined(MBEDTLS_SSL_CONF_RECV_TIMEOUT)
            mbedtls_ssl_set_bio( ssl, &io_ctx, send_cb, recv_cb,
                                 opt.nbio == 0 ? recv_timeout_cb : NULL );
#else
            mbedtls_ssl_set_bio_ctx( ssl, &server_fd );
#endif

#if defined(MBEDTLS_TIMING_C)
#if !defined(MBEDTLS_SSL_CONF_SET_TIMER) && \
    !defined(MBEDTLS_SSL_CONF_GET_TIMER)
            mbedtls_ssl_set_timer_cb( ssl, &timer,
                                      mbedtls_timing_set_delay,
                                      mbedtls_timing_get_delay );
#else
            mbedtls_ssl_set_timer_cb_ctx( ssl, &timer );
#endif
#endif /* MBEDTLS_TIMING_C */

            mbedtls_printf( " ok\n" );
        }

        mbedtls_printf( "  . Deserializing connection..." );

        if( ( ret = mbedtls_ssl_context_load( ssl, context_buf,
                                              buf_len ) ) != 0 )
        {
            mbedtls_printf( "failed\n  ! mbedtls_ssl_context_load returned "
                            "-0x%x\n\n", -ret );

            goto exit;
        }

        mbedtls_free( context_buf );
        context_buf = NULL;
        context_buf_len = 0;

        mbedtls_printf( " ok\n" );
    }
#endif /* MBEDTLS_SSL_CONTEXT_SERIALIZATION */

    /*
     * 7d. Continue doing data exchanges?
     */
    if( --opt.exchanges > 0 )
        goto send_request;

    /*
     * 8. Done, cleanly close the connection
     */
close_notify:
    mbedtls_printf( "  . Closing the connection..." );
    fflush( stdout );

    /*
     * Most of the time sending a close_notify before closing is the right
     * thing to do. However, when the server already knows how many messages
     * are expected and closes the connection by itself, this alert becomes
     * redundant. Sometimes with DTLS this redundancy becomes a problem by
     * leading to a race condition where the server might close the connection
     * before seeing the alert, and since UDP is connection-less when the
     * alert arrives it will be seen as a new connection, which will fail as
     * the alert is clearly not a valid ClientHello. This may cause spurious
     * failures in tests that use DTLS and resumption with ssl_server2 in
     * ssl-opt.sh, avoided by enabling skip_close_notify client-side.
     */
    if( opt.skip_close_notify == 0 )
    {
        /* No error checking, the connection might be closed already */
        do ret = mbedtls_ssl_close_notify( ssl );
        while( ret == MBEDTLS_ERR_SSL_WANT_WRITE );
        ret = 0;
    }

    mbedtls_printf( " done\n" );

    /*
     * 9. Reconnect?
     */
reconnect:
    if( opt.reconnect != 0 )
    {
        --opt.reconnect;

        mbedtls_net_free( &server_fd );

#if defined(MBEDTLS_TIMING_C)
        if( opt.reco_delay > 0 )
            mbedtls_net_usleep( 1000000 * opt.reco_delay );
#endif

        mbedtls_printf( "  . Reconnecting with saved session..." );

#if defined(MBEDTLS_X509_CRT_PARSE_C) && \
    !defined(MBEDTLS_X509_REMOVE_VERIFY_CALLBACK)
        memset( peer_crt_info, 0, sizeof( peer_crt_info ) );
#endif /* MBEDTLS_X509_CRT_PARSE_C && !MBEDTLS_X509_REMOVE_VERIFY_CALLBACK */

        if( ( ret = mbedtls_ssl_session_reset( ssl ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_session_reset returned -0x%x\n\n",
                            -ret );
            goto exit;
        }

        if( opt.reco_mode == 1 )
        {
            if( ( ret = mbedtls_ssl_session_load( &saved_session,
                                                  session_data,
                                                  session_data_len ) ) != 0 )
            {
                mbedtls_printf( " failed\n  ! mbedtls_ssl_session_load returned -0x%x\n\n",
                                -ret );
                goto exit;
            }
        }

#if !defined(MBEDTLS_SSL_NO_SESSION_RESUMPTION)
        if( ( ret = mbedtls_ssl_set_session( ssl, &saved_session ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_ssl_set_session returned -0x%x\n\n",
                            -ret );
            goto exit;
        }
#endif /* !MBEDTLS_SSL_NO_SESSION_RESUMPTION */

        if( ( ret = mbedtls_net_connect( &server_fd,
                        opt.server_addr, opt.server_port,
                        opt.transport == MBEDTLS_SSL_TRANSPORT_STREAM ?
                        MBEDTLS_NET_PROTO_TCP : MBEDTLS_NET_PROTO_UDP ) ) != 0 )
        {
            mbedtls_printf( " failed\n  ! mbedtls_net_connect returned -0x%x\n\n",
                            -ret );
            goto exit;
        }

        if( opt.nbio > 0 )
            ret = mbedtls_net_set_nonblock( &server_fd );
        else
            ret = mbedtls_net_set_block( &server_fd );
        if( ret != 0 )
        {
            mbedtls_printf( " failed\n  ! net_set_(non)block() returned -0x%x\n\n",
                            -ret );
            goto exit;
        }

        while( ( ret = mbedtls_ssl_handshake( ssl ) ) != 0 )
        {
            if( ret != MBEDTLS_ERR_SSL_WANT_READ &&
                ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
                ret != MBEDTLS_ERR_SSL_CRYPTO_IN_PROGRESS )
            {
                mbedtls_printf( " failed\n  ! mbedtls_ssl_handshake returned -0x%x\n\n",
                                -ret );
                goto exit;
            }
        }

        mbedtls_printf( " ok\n" );

        goto send_request;
    }

    /*
     * Cleanup and exit
     */
exit:

#ifdef MBEDTLS_ERROR_C
    if( ret != 0 )
    {
        char error_buf[100];
        mbedtls_strerror( ret, error_buf, 100 );
        mbedtls_printf("Last error was: -0x%X - %s\n\n", -ret, error_buf );
    }
#endif

    mbedtls_net_free( &server_fd );

#if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_x509_crt_free( clicert );
    mbedtls_x509_crt_free( cacert );
    mbedtls_pk_free( pkey );
#endif
    mbedtls_ssl_session_free( &saved_session );
    mbedtls_ssl_free( ssl );
    mbedtls_ssl_config_free( conf );
#if defined(MBEDTLS_CTR_DRBG_C)
    mbedtls_ctr_drbg_free( ctr_drbg );
#else
    mbedtls_hmac_drbg_free( hmac_drbg );
#endif
    mbedtls_entropy_free( entropy );
    if( session_data != NULL )
        mbedtls_platform_zeroize( session_data, session_data_len );
    mbedtls_free( session_data );
#if defined(MBEDTLS_SSL_CONTEXT_SERIALIZATION)
    if( context_buf != NULL )
        mbedtls_platform_zeroize( context_buf, context_buf_len );
    mbedtls_free( context_buf );
#endif

    mbedtls_free( ssl );
    mbedtls_free( conf );
    mbedtls_free( entropy );
    mbedtls_free( buf );
#if defined(MBEDTLS_CTR_DRBG_C)
    mbedtls_free( ctr_drbg );
#else
    mbedtls_free( hmac_drbg );
#endif
#if defined(MBEDTLS_X509_CRT_PARSE_C)
    mbedtls_free( cacert );
    mbedtls_free( clicert );
    mbedtls_free( pkey );
#endif
#if defined(_WIN32)
    mbedtls_printf( "  + Press Enter to exit this program.\n" );
    fflush( stdout ); getchar();
#endif

    // Shell can not handle large exit numbers -> 1 for errors
    if( ret < 0 )
        ret = 1;

    mbedtls_exit( ret );
}
#endif /* MBEDTLS_BIGNUM_C && MBEDTLS_ENTROPY_C && MBEDTLS_SSL_TLS_C &&
          MBEDTLS_SSL_CLI_C && MBEDTLS_NET_C && MBEDTLS_RSA_C &&
          ( MBEDTLS_CTR_DRBG_C || MBEDTLS_HMAC_DRBG_C ) && MBEDTLS_TIMING_C */
