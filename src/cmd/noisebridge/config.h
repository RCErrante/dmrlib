#ifndef _NOISEBRIDGE_CONFIG_H
#define _NOISEBRIDGE_CONFIG_H

#include <dmr/log.h>
#include <dmr/proto.h>
#include <dmr/proto/homebrew.h>
#include <dmr/proto/mbe.h>
#include <dmr/proto/mmdvm.h>
#include <dmr/proto/repeater.h>
#include <dmr/payload/voice.h>

extern const char *software_id, *package_id;

typedef enum {
    PEER_NONE,
    PEER_HOMEBREW,
    PEER_MBE,
    PEER_MMDVM
} peer_t;

typedef struct config_s {
    const char         *filename;
    peer_t             upstream, modem;
    dmr_homebrew_t     *homebrew;
    char               *homebrew_host_s;
    struct hostent     *homebrew_host;
    int                homebrew_port;
    char               *homebrew_auth;
    char               *homebrew_call;
    struct in_addr     homebrew_bind;
    dmr_id_t           homebrew_id;
    dmr_color_code_t   homebrew_cc;
    dmr_mmdvm_t        *mmdvm;
    char               *mmdvm_port;
    int                mmdvm_rate;
    dmr_mbe_t          *mbe;
    uint8_t            mbe_quality;
    char               *audio_device;
    dmr_log_priority_t log_level;
} config_t;

config_t *configure(const char *filename);

#endif // _NOISEBRIDGE_CONFIG_H