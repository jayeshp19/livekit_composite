#include "esp_log.h"
#include "webrtc_utils_time.h"
#include "livekit_protocol.h"
#include "livekit_engine.h"
#include "livekit_signaling.h"
#include "livekit_peer.h"

static const char *TAG = "livekit_engine";

typedef struct {
    livekit_eng_options_t options;
    livekit_sig_handle_t  sig;
    livekit_eng_media_provider_t media_provider;

    livekit_peer_handle_t pub_peer;
    livekit_peer_handle_t sub_peer;
} livekit_eng_t;

/// @brief Performs one-time system initialization.
static void sys_init(void)
{
    static bool is_initialized = false;
    if (is_initialized) {
        ESP_LOGI(TAG, "System already initialized");
        return;
    }
    is_initialized = webrtc_utils_time_sync_init() == ESP_OK;
    if (!is_initialized) {
        ESP_LOGE(TAG, "System initialization failed");
        return;
    }
}

static void on_peer_pub_offer(const char *sdp, void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    ESP_LOGI(TAG, "Pub peer generated offer: %s", sdp);
    livekit_sig_send_offer(sdp, eng->sig);
}

static void on_peer_sub_answer(const char *sdp, void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    ESP_LOGI(TAG, "Sub peer generated answer: %s", sdp);
    livekit_sig_send_answer(sdp, eng->sig);
}

static void on_peer_ice_candidate(const char *candidate, void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    ESP_LOGI(TAG, "Peer generated ice candidate: %s", candidate);
}

static void on_sig_connect(void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    ESP_LOGI(TAG, "Signaling connected");
    // TODO: Implement
}

static void on_sig_disconnect(void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    ESP_LOGI(TAG, "Signaling disconnected");
    // TODO: Implement
}

static void on_sig_error(void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    ESP_LOGI(TAG, "Signaling error");
    // TODO: Implement
}

static void on_sig_join(livekit_join_response_t *join_res, void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    livekit_peer_set_ice_servers(join_res->ice_servers, join_res->ice_servers_count, eng->pub_peer);
    livekit_peer_set_ice_servers(join_res->ice_servers, join_res->ice_servers_count, eng->sub_peer);

    livekit_peer_connect_options_t connect_options = {
        .force_relay = join_res->has_client_configuration &&
                       join_res->client_configuration.force_relay == LIVEKIT_CLIENT_CONFIG_SETTING_ENABLED
    };
    livekit_peer_connect(connect_options, eng->pub_peer);
    // livekit_peer_connect(connection_options, eng->sub_peer);
}

static void on_sig_answer(const char *sdp, void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    ESP_LOGI(TAG, "Received answer: %s", sdp);
    livekit_peer_handle_sdp(sdp, eng->pub_peer);
}

static void on_sig_offer(const char *sdp, void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    ESP_LOGI(TAG, "Received offer: %s", sdp);
    livekit_peer_handle_sdp(sdp, eng->sub_peer);
}

static void on_sig_trickle(const char *ice_candidate, livekit_signal_target_t target, void *ctx)
{
    livekit_eng_t *eng = (livekit_eng_t *)ctx;
    livekit_peer_handle_t target_peer = target == LIVEKIT_SIGNAL_TARGET_SUBSCRIBER ?
        eng->sub_peer : eng->pub_peer;
    livekit_peer_handle_ice_candidate(ice_candidate, target_peer);
}

int livekit_eng_create(livekit_eng_options_t *options, livekit_eng_handle_t *handle)
{
    if (options == NULL || handle == NULL) {
        return LIVEKIT_ENG_ERR_INVALID_ARG;
    }
    livekit_eng_t *eng = (livekit_eng_t *)calloc(1, sizeof(livekit_eng_t));
    if (eng == NULL) {
        return LIVEKIT_ENG_ERR_NO_MEM;
    }
    eng->options = *options;
    livekit_sig_options_t sig_options = {
        .ctx = eng,
        .on_connect = on_sig_connect,
        .on_disconnect = on_sig_disconnect,
        .on_error = on_sig_error,
        .on_join = on_sig_join,
        .on_answer = on_sig_answer,
        .on_offer = on_sig_offer,
        .on_trickle = on_sig_trickle,
    };
    int ret = LIVEKIT_ENG_ERR_OTHER;
    do {
        if (livekit_sig_create(&sig_options, &eng->sig) != LIVEKIT_SIG_ERR_NONE) {
            ESP_LOGE(TAG, "Failed to create signaling client");
            break;
        }

        livekit_peer_options_t pub_options = {
            .target = LIVEKIT_SIGNAL_TARGET_PUBLISHER,
            .on_sdp = on_peer_pub_offer,
            .on_ice_candidate = on_peer_ice_candidate,
            .ctx = eng
        };
        if (livekit_peer_create(pub_options, &eng->pub_peer) != LIVEKIT_PEER_ERR_NONE) {
            ESP_LOGE(TAG, "Failed to create publisher peer");
            break;
        }

        livekit_peer_options_t sub_options = {
            .target = LIVEKIT_SIGNAL_TARGET_SUBSCRIBER,
            .on_sdp = on_peer_sub_answer,
            .on_ice_candidate = on_peer_ice_candidate,
            .ctx = eng
        };
        if (livekit_peer_create(sub_options, &eng->sub_peer) != LIVEKIT_PEER_ERR_NONE) {
            ESP_LOGE(TAG, "Failed to create subscriber peer");
            break;
        }
        *handle = eng;
        return LIVEKIT_ENG_ERR_NONE;
    } while (0);

    if (eng->sub_peer != NULL) livekit_peer_destroy(eng->sub_peer);
    if (eng->pub_peer != NULL) livekit_peer_destroy(eng->pub_peer);
    if (eng->sig != NULL) livekit_sig_destroy(eng->sig);
    free(eng);
    return ret;
}

int livekit_eng_destroy(livekit_eng_handle_t handle)
{
    if (handle == NULL) {
        return LIVEKIT_ENG_ERR_INVALID_ARG;
    }
    livekit_eng_t *eng = (livekit_eng_t *)handle;
    livekit_eng_close(LIVEKIT_DISCONNECT_REASON_UNKNOWN_REASON, handle);
    free(eng);
    return LIVEKIT_ENG_ERR_NONE;
}

int livekit_eng_connect(const char* server_url, const char* token, livekit_eng_handle_t handle)
{
    if (server_url == NULL || token == NULL || handle == NULL) {
        return LIVEKIT_ENG_ERR_INVALID_ARG;
    }
    livekit_eng_t *eng = (livekit_eng_t *)handle;

    sys_init();

    if (livekit_sig_connect(server_url, token, eng->sig) != LIVEKIT_SIG_ERR_NONE) {
        ESP_LOGE(TAG, "Failed to connect signaling client");
        return LIVEKIT_ENG_ERR_SIGNALING;
    }
    return LIVEKIT_ENG_ERR_NONE;
}

int livekit_eng_close(livekit_disconnect_reason_t reason, livekit_eng_handle_t handle)
{
    if (handle == NULL) {
        return LIVEKIT_ENG_ERR_INVALID_ARG;
    }
    livekit_eng_t *eng = (livekit_eng_t *)handle;

    // TODO: Send leave request

    if (livekit_sig_close(true, eng->sig) != LIVEKIT_SIG_ERR_NONE) {
        ESP_LOGE(TAG, "Failed to close signaling client");
        return LIVEKIT_ENG_ERR_SIGNALING;
    }
    return LIVEKIT_ENG_ERR_NONE;
}

int livekit_eng_publish_data(livekit_data_packet_t packet, livekit_data_packet_kind_t kind, livekit_eng_handle_t handle)
{
    // TODO: Implement
    return 0;
}

int livekit_eng_send_request(livekit_signal_request_t request, livekit_eng_handle_t handle)
{
    // TODO: Implement
    return 0;
}

int livekit_eng_set_media_provider(livekit_eng_media_provider_t* provider, livekit_eng_handle_t handle)
{
    if (handle == NULL || provider == NULL) {
        return LIVEKIT_ENG_ERR_INVALID_ARG;
    }
    livekit_eng_t *eng = (livekit_eng_t *)handle;
    eng->media_provider = *provider;
    return LIVEKIT_ENG_ERR_NONE;
}