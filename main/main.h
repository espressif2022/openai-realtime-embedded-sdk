#include <peer.h>

#define LOG_TAG "realtimeapi-sdk"
#define MAX_HTTP_OUTPUT_BUFFER 2048

#define CONFIG_OPENAI_BOARD_ESP_BOX     1
#define CONFIG_OPENAI_BOARD_M5_ATOMS3R  0

#define ESP_SPARKBOT                    0 //enable M5_ATOMS3R first.


#define ENABLE_AEC                      1
#define USE_GMF                         0

void oai_wifi(void);
void oai_init_audio_capture(void);
void oai_init_audio_decoder(void);
void oai_init_audio_encoder();
void oai_send_audio(PeerConnection *peer_connection);
void oai_audio_decode(uint8_t *data, size_t size, void* userdata);
void oai_webrtc();
void oai_http_request(char *offer, char *answer);
void oai_completed_event();
