#pragma once

#define SECRETS_WIFI_SSID     "xxx"
#define SECRETS_WIFI_PASSWORD "xxx"

#define SECRETS_OPENCLAW_HOST  "xxx.xxx.xxx.xxx"
#define SECRETS_OPENCLAW_PORT  18789
#define SECRETS_OPENCLAW_TOKEN "xxx"

/* DashScope (百炼) API — for STT */
#define SECRETS_DASHSCOPE_API_KEY "sk-xxx"
#define SECRETS_STT_MODEL         "fun-asr-realtime-2026-02-28"
#define SECRETS_STT_ENDPOINT      "wss://dashscope.aliyuncs.com/api-ws/v1/inference/"

/* MiMo TTS (Xiaomi) */
#define SECRETS_MIMO_API_KEY  "sk-xxx"
#define SECRETS_MIMO_ENDPOINT "https://api.xiaomimimo.com/v1/chat/completions"
#define SECRETS_MIMO_MODEL    "mimo-v2-tts"
#define SECRETS_MIMO_VOICE    "mimo_default"

/* Device identity — leave empty to auto-generate, or set 64 hex chars for ED25519 */
#define SECRETS_DEVICE_KEY_HEX ""