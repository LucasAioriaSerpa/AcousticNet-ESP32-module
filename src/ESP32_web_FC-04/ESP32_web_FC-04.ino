#include <Arduino.h>
#include <stdio.h>
#include <WiFi.h>
#include <HTTPClient.h>

//? Pino de sensor FC-04
#define FC04_ANALOG_PIN     35

//? Tempo de leitura & Envio
#define TEMPO_DE_LEITURA_MS 50
#define INTERVALO_ENVIO_MS  300
#define RECONECTAR_WIFI_MS  1000

//? Reconexão preventiva — medida imediata contra "WiFi zumbi"
//? (status() continua WL_CONNECTED mas o socket não responde mais)
#define RECONEXAO_PERIODICA_MS 300000UL  //? 5 minutos
#define MAX_FALHAS_CONSECUTIVAS 5        //? força reconexão após N POSTs falhos

//? Calibração FC-04 -> dB
//? PTP_MIN/PTP_MAX = faixa observada do pico-a-pico do sinal analógico (0-4095, ADC 12 bits)
//? DB_MIN/DB_MAX   = faixa de saída desejada em decibéis
//? Se o resultado ficar sempre colado em 0 ou em 100, ajuste PTP_MIN/PTP_MAX
//? com base em medições reais (ambiente silencioso vs. ambiente alto).
#define PTP_MIN 0
#define PTP_MAX 1024
#define DB_MIN  0.0
#define DB_MAX  100.0

//? Credenciais WiFi
#define WIFI_SSID     "definitivamente_um_WiFi" //? TP-Link_AcousticNet_Admin
#define WIFI_PASSWORD "definitivamente_uma_senha" //? _*****_**_
#define SERVER_URL    "http://192.168.0.103/api/decibels"

//? SemáForo para proteger acesso concorrente ao valor de dB
SemaphoreHandle_t xSemaphoreDecibels;

volatile float FC04valorDecibels = 0.0;

// ? Handles das tarefas
TaskHandle_t HandleParteFisica;
TaskHandle_t HandleParteConexao;

// ? Estado da conexão (usado só pela ParteConexao, sem necessidade de mutex)
unsigned long ultimaReconexaoForcada = 0;
int falhasConsecutivas = 0;

// ? Forward declarations
float mapf(float x, float in_min, float in_max, float out_min, float out_max);
float leitorDeDecibeis();
void verificarConexaoWifi();
void enviarParaServidor(float db);
void setupWifi();
void ParteFisica(void *pvParameters);
void ParteConexao(void *pvParameters);

// ! PARTE FISICA

/**
 * @brief Faz leitura do sensor FC-04.
 *
 * Esta função lé o valor analogico do sensor dentro de um periodo de tempo.
 * Com o valor analogico converte para decibeis (faixa 0-100 dB).
 */
float leitorDeDecibeis() {
  int sinalMaximo = 0;
  int sinalMinimo = 4095;
  unsigned long comesarMillis = millis();
  while (millis() - comesarMillis < TEMPO_DE_LEITURA_MS) {
    int valorAnalogico = analogRead(FC04_ANALOG_PIN);
    if (valorAnalogico > sinalMaximo) sinalMaximo = valorAnalogico;
    if (valorAnalogico < sinalMinimo) sinalMinimo = valorAnalogico;
  }
  int PTP = sinalMaximo - sinalMinimo;
  float db = mapf(PTP, PTP_MIN, PTP_MAX, DB_MIN, DB_MAX);
  db = constrain(db, DB_MIN, DB_MAX);
  return db;
}

/**
 * @brief map() com support ao float.
 */
float mapf(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + (out_min);
}

// ! PARTE CONEXÃO

/**
 * @brief Verifica e reconecta ao Wi-Fi se necessário.
 *
 * Reconecta nos seguintes casos:
 *  - status() reporta desconexão real;
 *  - passou tempo demais desde a última reconexão (reconexão preventiva);
 *  - muitos envios HTTP consecutivos falharam mesmo com status() "conectado"
 *    (sintoma de WiFi "zumbi" no ESP32).
 */
void verificarConexaoWifi() {
  unsigned long agora = millis();
  bool desconectado   = (WiFi.status() != WL_CONNECTED);
  bool tempoEsgotado  = (agora - ultimaReconexaoForcada >= RECONEXAO_PERIODICA_MS);
  bool muitasFalhas   = (falhasConsecutivas >= MAX_FALHAS_CONSECUTIVAS);
  if (!desconectado && !tempoEsgotado && !muitasFalhas) { return; }
  if (desconectado) {
    Serial.println("[WIFI] Conexão perdida. Reconectando...");
  } else if (muitasFalhas) {
    Serial.println("[WIFI] Muitas falhas consecutivas de envio. Forçando reconexão...");
  } else {
    Serial.println("[WIFI] Reconexão periódica preventiva...");
  }
  WiFi.disconnect();
  delay(100);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - inicio < 10000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n[WIFI] Reconectado!");
    Serial.print("[WIFI] Endereço IP: ");
    Serial.println(WiFi.localIP());
    falhasConsecutivas = 0;
  } else {
    Serial.println("\n[WIFI] Falha na reconexão");
  }

  ultimaReconexaoForcada = millis();
}

/**
 * @brief Envia valor de decibéis ao servidor web via HTTP POST.
 *
 * Formata payload JSON com o valor atual de dB e envia
 * para o endpoint /api/decibels do servidor web (PC-1).
 * \n Lê o valor protegido pelo semáforo antes do envio.
 * \n Contabiliza falhas consecutivas para acionar reconexão forçada.
 *
 * @param db Valor em decibéiis a ser enviado
 */
void enviarParaServidor(float db) {
  verificarConexaoWifi();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Wifi desconectado, pulando envio...");
    return;
  }
  HTTPClient http;
  http.begin(SERVER_URL);
  http.setTimeout(5000);
  http.setConnectTimeout(5000);
  http.addHeader("Content-Type", "application/json");
  String payload = "{\"value\":" + String(db, 2) + "}";
  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    Serial.printf("[HTTP] POST %s | dB %.2f | HTTP: %d\n", SERVER_URL, db, httpCode);
    falhasConsecutivas = 0;
  } else {
    Serial.printf("[HTTP] Erro no POST: %s\n", http.errorToString(httpCode).c_str());
    falhasConsecutivas++;
    Serial.printf("[HTTP] Falhas consecutivas: %d/%d\n", falhasConsecutivas, MAX_FALHAS_CONSECUTIVAS);
  }
  http.end();
}

/**
 * @brief Configura a conexão WiFi.
 *
 * Esta função inicia a conexão WiFi com as credenciais fornecidas.
 * Ela imprime o status da conexão no monitor serial e exibe o endereço IP local
 * quando a conexão é bem-sucedida.
*/
void setupWifi() {
  IPAddress local_IP(192, 168, 0, 130);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
  IPAddress secondaryDNS(8, 8, 4, 4);
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)){
    Serial.println("[WIFI] Falha ao configurar IP estático");
  }
  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("[WIFI] Conectando");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[WIFI] Endereço IP: ");
  Serial.println(WiFi.localIP());

  ultimaReconexaoForcada = millis();
  falhasConsecutivas = 0;
}

void setup() {
  Serial.begin(115200);
  pinMode(FC04_ANALOG_PIN, INPUT);
  xSemaphoreDecibels = xSemaphoreCreateBinary();
  xSemaphoreGive(xSemaphoreDecibels);
  setupWifi();
  xTaskCreatePinnedToCore(
    ParteFisica,
    "ParteFisica",
    2048,
    NULL,
    2,
    &HandleParteFisica,
    0
  );
  delay(500);
  xTaskCreatePinnedToCore(
    ParteConexao,
    "ParteConexao",
    8192,
    NULL,
    1,
    &HandleParteConexao,
    1
  );
  delay(500);
}

// ? TASK - PARTE FISICA
/**
 * @brief Função de tarefa que gerencia a leitura do sensor FC-04.
 *
 * Executa continuamente em loop, lendo o nível de decibéis (0-100 dB)
 * e armazenando o valor protegido por semáforo para a ParteConexao.
 *
 * @param pvParameters Ponteiro para os parâmetros passados para a tarefa (não utilizado).
*/
void ParteFisica(void *pvParameters) {
  Serial.printf("[TASK] ParteFisica Inicializada | CORE: %d\n", xPortGetCoreID());
  for (;;) {
    float db = leitorDeDecibeis();
    if (xSemaphoreTake(xSemaphoreDecibels, portMAX_DELAY) == pdTRUE) {
      FC04valorDecibels = db;
      xSemaphoreGive(xSemaphoreDecibels);
    }
    Serial.printf("[FC-04] %.2f dB\n", db);
    vTaskDelay(pdMS_TO_TICKS(TEMPO_DE_LEITURA_MS));
  }
}

// ? TASK - PARTE CONEXÃO
/**
 * @brief Função de tarefa que gerencia o envio dos dados ao servidor web.
 *
 * Executa continuamente em loop, lê o valor de dB protegido por semáforo
 * e envia ao backend via HTTP POST, com reconexão automática (periódica
 * e por falhas consecutivas).
 *
 * @param pvParameters Ponteiro para os parâmetros passados para a tarefa (não utilizado).
*/
void ParteConexao(void *pvParameters) {
  Serial.print("[TASK] ParteConexao Inicializada | CORE: " + String(xPortGetCoreID()) + "\n");
  for (;;) {
    float db = 0.0;
    if (xSemaphoreTake(xSemaphoreDecibels, portMAX_DELAY) == pdTRUE) {
      db = FC04valorDecibels;
      xSemaphoreGive(xSemaphoreDecibels);
    }
    enviarParaServidor(db);
    vTaskDelay(pdMS_TO_TICKS(INTERVALO_ENVIO_MS));
  }
}

void loop() {}
