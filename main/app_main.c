#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <sys/param.h>
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mdns.h"

#include "esp_log.h"
#include "mqtt_client.h"
#include "esp_tls.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"

#include "cJSON.h"
#include "audio.h"
#include "peer_connection.h"


static const char *TAG = "webrtc";

static TaskHandle_t xAudioTaskHandle = NULL;
static TaskHandle_t xPcTaskHandle = NULL;
static TaskHandle_t xCameraTaskHandle = NULL;

extern esp_err_t camera_init();
extern void camera_task(void *pvParameters);
extern void wifi_init_sta();

static char remote_description[2048];
char base64_str[2048];

static const char *pcRemoteDescription = NULL;
static const char *pcLocalDescription = NULL;
static PeerConnectionState eState = PEER_CONNECTION_CLOSED;

PeerConnection *g_pc;
int gDataChannelOpened = 0;

const char subtopic[] = "webrtc/hello666/jsonrpc";
const char device_id[] = "hello666";

#if CONFIG_BROKER_CERTIFICATE_OVERRIDDEN == 1
static const uint8_t mqtt_eclipseprojects_io_pem_start[]  = "-----BEGIN CERTIFICATE-----\n" CONFIG_BROKER_CERTIFICATE_OVERRIDE "\n-----END CERTIFICATE-----";
#else
extern const uint8_t mqtt_eclipseprojects_io_pem_start[]   asm("_binary_mqtt_eclipseprojects_io_pem_start");
#endif
extern const uint8_t mqtt_eclipseprojects_io_pem_end[]   asm("_binary_mqtt_eclipseprojects_io_pem_end");

static void handle_mqtt_data(const char *payload, int len) {

  cJSON *root = cJSON_Parse(payload);
  if (root == NULL) {
    ESP_LOGE(TAG, "JSON Parse Error");
    return;
  }

  cJSON *method = cJSON_GetObjectItem(root, "method");
  if (method == NULL || method->type != cJSON_String) {
    ESP_LOGE(TAG, "JSON Parse Error");
    return;
  }

  if (strcmp(method->valuestring, "request_offer") == 0) {

    ESP_LOGI(TAG, "peer_connection_create_offer");
    peer_connection_create_offer(g_pc);

    vTaskDelay(pdMS_TO_TICKS(1000));

  } else if (strcmp(method->valuestring, "response_answer") == 0) {

    memset(remote_description, 0, sizeof(remote_description));
    char *base64_str = cJSON_GetObjectItem(root, "params")->valuestring;
    base64_decode(base64_str, strlen(base64_str), (unsigned char *)remote_description, sizeof(remote_description));
    pcRemoteDescription = remote_description;
    printf("remote description: %s\n", remote_description);
    peer_connection_set_remote_description(g_pc, pcRemoteDescription);
  }

  cJSON_Delete(root);

}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {

  ESP_LOGD(TAG, "Event dispatched from event loop base=%s, event_id=%d", base, event_id);
  esp_mqtt_event_handle_t event = event_data;
  esp_mqtt_client_handle_t client = event->client;
  int msg_id;

  switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
      ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
      msg_id = esp_mqtt_client_subscribe(client, subtopic, 0);
      ESP_LOGI(TAG, "sent subscribe successful, msg_id=%d", msg_id);
      break;
    case MQTT_EVENT_DISCONNECTED:
      ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
      break;
    case MQTT_EVENT_SUBSCRIBED:
      ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
      break;
    case MQTT_EVENT_UNSUBSCRIBED:
      ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
      break;
    case MQTT_EVENT_PUBLISHED:
      ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
      break;
    case MQTT_EVENT_DATA:

      ESP_LOGI(TAG, "MQTT_EVENT_DATA");

      //printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
      //printf("DATA=%.*s\r\n", event->data_len, event->data);
      handle_mqtt_data(event->data, event->data_len);

      if (pcLocalDescription != NULL) {

        base64_encode((const unsigned char*)pcLocalDescription, strlen(pcLocalDescription), base64_str, sizeof(base64_str));
        cJSON *response = cJSON_CreateObject();
        cJSON_AddStringToObject(response, "jsonrpc", "2.0");
        cJSON_AddStringToObject(response, "result", base64_str);
        cJSON_AddNumberToObject(response, "id", 1);
        char *response_str = cJSON_PrintUnformatted(response);
        esp_mqtt_client_publish(client, "webrtc/hello666/jsonrpc-reply", response_str, 0, 0, 0);
        free(response_str);
        cJSON_Delete(response);
        pcLocalDescription = NULL;
      }
      break;

    case MQTT_EVENT_ERROR:
      ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
      if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
        ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
        ESP_LOGI(TAG, "Last captured errno : %d (%s)",  event->error_handle->esp_transport_sock_errno,
         strerror(event->error_handle->esp_transport_sock_errno));
      } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
      } else {
        ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
      }
      break;
    default:
      ESP_LOGI(TAG, "Other event id:%d", event->event_id);
      break;
  }
}

static void mqtt_app_start(void) {

  const esp_mqtt_client_config_t mqtt_cfg = {
    .broker = {
      .address.uri = CONFIG_BROKER_URI,
      .verification.certificate = (const char *)mqtt_eclipseprojects_io_pem_start
    },
    .buffer.size = 2048,
    .task.stack_size = 4096,
  };

  ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
  esp_mqtt_client_start(client);
}

static void on_icecandidate(char *sdp_text, void *data) {
  
  pcLocalDescription = sdp_text;
  ESP_LOGI(TAG, "pcLocalDescription: %s", pcLocalDescription); 

}

static void oniceconnectionstatechange(PeerConnectionState state, void *user_data) {

  ESP_LOGI(TAG, "PeerConnectionState: %d", state);
  eState = state;
}

static void onmessasge(char *msg, size_t len, void *userdata) {

}

void onopen(void *userdata) {
 
  ESP_LOGI(TAG, "Datachannel opened");
  gDataChannelOpened = 1;
}

static void onclose(void *userdata) {
 
}


void peer_connection_task(void *arg) {

  ESP_LOGI(TAG, "peer_connection_task started");

  for(;;) {

    peer_connection_loop(g_pc);

    vTaskDelay(pdMS_TO_TICKS(5));

  }
}

void app_main(void) {

    PeerOptions options = {0};

    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("esp-tls", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE);
    esp_log_level_set("MQTT_EXAMPLE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT_BASE", ESP_LOG_VERBOSE);
    esp_log_level_set("TRANSPORT", ESP_LOG_VERBOSE);
    esp_log_level_set("OUTBOX", ESP_LOG_VERBOSE);

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(mdns_init());

    wifi_init_sta();

    g_pc = (PeerConnection*)malloc(sizeof(PeerConnection));
    options.b_datachannel = 1;
    options.audio_codec = CODEC_PCMA;
    peer_connection_configure(g_pc, &options);
    peer_connection_init(g_pc);
    peer_connection_onicecandidate(g_pc, on_icecandidate);
    peer_connection_oniceconnectionstatechange(g_pc, oniceconnectionstatechange);
    peer_connection_ondatachannel(g_pc, onmessasge, onopen, onclose);

    camera_init();

//    xTaskCreate(audio_task, "audio", 2048, NULL, 5, &xAudioTaskHandle);

    xTaskCreatePinnedToCore(camera_task, "camera", 4096, NULL, 6, &xCameraTaskHandle, 0);

    xTaskCreatePinnedToCore(peer_connection_task, "peer_connection", 10240, NULL, 5, &xPcTaskHandle, 1);

    mqtt_app_start();

    ESP_LOGI(TAG, "[APP] Free memory: %d bytes", esp_get_free_heap_size());
}
