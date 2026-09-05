/*
 * Vocal-HUD: Smart Wearable Audio-Visual Communication System
 * -------------------------------------------------------------
 * REFERENCE / RECONSTRUCTED FIRMWARE
 *
 * This sketch is NOT the original submitted project code. It is a
 * reconstruction written from the documented architecture, pin mapping,
 * and design decisions in the project report, to demonstrate the
 * communication and control logic described there. It has not been
 * re-validated on hardware. Treat it as a design reference, not a
 * drop-in replacement for the original.
 *
 * Target: Seeed XIAO ESP32-C3
 * Peripherals: INMP441 (I2S mic) + MAX98357A (I2S amp) + TTP223 (touch)
 * Wireless: ESP-NOW, half-duplex, symmetric (same sketch runs on both nodes)
 *
 * Flow:
 *   Touch press  -> capture audio from INMP441 over I2S
 *                -> send buffer to peer node via ESP-NOW
 *   Packet in    -> play buffer out to MAX98357A over I2S
 *
 * Both nodes run this identical sketch; each node's MAC address must be
 * registered as the peer on the other node (see PEER_MAC_ADDR below).
 */

#include <WiFi.h>
#include <esp_now.h>
#include <driver/i2s.h>

// ---------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------

// MAC address of the *other* node. Replace with the paired device's MAC.
static uint8_t PEER_MAC_ADDR[6] = { 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF };

// I2S pin mapping (shared BCLK/LRCLK between mic input and amp output —
// see docs/hardware-schematic.md)
#define I2S_BCLK_PIN   4
#define I2S_LRCLK_PIN  5
#define I2S_MIC_DIN    6   // INMP441 SD  -> ESP32-C3 (mic data in)
#define I2S_AMP_DOUT   7   // ESP32-C3 -> MAX98357A DIN (amp data out)

#define TOUCH_PIN      3   // TTP223 output, pulled down externally (10k)

// Audio format
#define SAMPLE_RATE       16000
#define BITS_PER_SAMPLE   I2S_BITS_PER_SAMPLE_16BIT

// Smaller buffer = lower latency, more frequent transfers.
// The project report's largest latency win came from shrinking this.
#define DMA_BUFFER_SAMPLES  256
#define AUDIO_CHUNK_BYTES   (DMA_BUFFER_SAMPLES * sizeof(int16_t))

#define I2S_PORT_MIC  I2S_NUM_0
#define I2S_PORT_AMP  I2S_NUM_1

// ---------------------------------------------------------------------
// State
// ---------------------------------------------------------------------

volatile bool touchActive = false;
int16_t audioBuffer[DMA_BUFFER_SAMPLES];

// ---------------------------------------------------------------------
// I2S setup
// ---------------------------------------------------------------------

void setupI2SMic() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = BITS_PER_SAMPLE,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = DMA_BUFFER_SAMPLES,
    .use_apll = false,
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRCLK_PIN,
    .data_out_num = I2S_PIN_NO_CHANGE,
    .data_in_num = I2S_MIC_DIN,
  };
  i2s_driver_install(I2S_PORT_MIC, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT_MIC, &pins);
}

void setupI2SAmp() {
  i2s_config_t cfg = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = SAMPLE_RATE,
    .bits_per_sample = BITS_PER_SAMPLE,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 4,
    .dma_buf_len = DMA_BUFFER_SAMPLES,
    .use_apll = false,
  };
  i2s_pin_config_t pins = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRCLK_PIN,
    .data_out_num = I2S_AMP_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE,
  };
  i2s_driver_install(I2S_PORT_AMP, &cfg, 0, NULL);
  i2s_set_pin(I2S_PORT_AMP, &pins);
}

// ---------------------------------------------------------------------
// ESP-NOW callbacks
// ---------------------------------------------------------------------

void onDataRecv(const uint8_t *mac, const uint8_t *data, int len) {
  // Incoming audio chunk from the peer node — push straight to the amp.
  size_t bytesWritten = 0;
  i2s_write(I2S_PORT_AMP, data, len, &bytesWritten, portMAX_DELAY);
}

void onDataSent(const uint8_t *mac, esp_now_send_status_t status) {
  // Optional: monitor for dropped packets under continuous transmission.
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW init failed");
    return;
  }

  esp_now_register_recv_cb(onDataRecv);
  esp_now_register_send_cb(onDataSent);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, PEER_MAC_ADDR, 6);
  peer.channel = 0;
  peer.encrypt = false;
  esp_now_add_peer(&peer);
}

// ---------------------------------------------------------------------
// Touch handling
// ---------------------------------------------------------------------

void IRAM_ATTR onTouch() {
  touchActive = !touchActive; // toggle push-to-talk state on each press
}

// ---------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------

void setup() {
  Serial.begin(115200);

  pinMode(TOUCH_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(TOUCH_PIN), onTouch, RISING);

  setupI2SMic();
  setupI2SAmp();
  setupEspNow();

  Serial.println("Vocal-HUD node ready.");
}

void loop() {
  if (touchActive) {
    size_t bytesRead = 0;
    i2s_read(I2S_PORT_MIC, audioBuffer, AUDIO_CHUNK_BYTES, &bytesRead, portMAX_DELAY);

    if (bytesRead > 0) {
      esp_now_send(PEER_MAC_ADDR, (uint8_t *)audioBuffer, bytesRead);
    }
  }
}
