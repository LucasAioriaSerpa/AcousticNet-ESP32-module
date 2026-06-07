#include <Arduino.h>
#include <stdio.h>
#include <WiFi.h>
#include <HTTPClient.h>

//? Pino de sensor FC-04
#define FC04_ANALOG_PIN     35

//? Tempo de leitura & Envio
#define TEMPO_DE_LEITURA_MS 50
#define INTERVALO_ENVIO_MS  3000

//? Credenciais WiFi
#define WIFI_SSID     "TP-Link_9D84"
#define WIFI_PASSWORD "definitivamente_uma_senha" // TODO: insira sua senha aqui!
#define SERVER_URL    "http://192.ar168.15.10/api/decibels"

//? SemáForo para proteger acesso concorrente ao valor de dB
SemaphoreHandle_t xSemaphoreDecibels;

volatile float FC04valorDecibels = 0.0;

// ? Handles das tarefas
TaskHandle_t HandleParteFisica;
TaskHandle_t HandleParteConexao;

// ! DEBUG
void debugPrint() {
  Serial.print("Valor Analogico do FC-04: ");
  Serial.println(FC04valorAnalogico);
  Serial.print("Valor em Decibeis do FC-04: ");
  Serial.println(FC04valorDecibeis);
}

// ! PARTE FISICA

/**
 * @brief Faz leitura do sensor FC-04.
 * 
 * Esta função lé o valor analogico do sensor dentro de um periodo de tempo.
 * Com o valor analogico converte para decibeis.
 */
void leitorDeDecibeis() {
  sinalMaximo = 0;
  sinalMinimo = 4095
  unsigned long comesarMillis = millis();
  while (millis() - comesarMillis < TEMPO_DE_LEITURA_MS) {
    int valorAnalogico = analogRead(FC04_ANALOG_PIN);
    if (valorAnalogico > sinalMaximo) sinalMaximo = valorAnalogico;
    if (valorAnalogico < sinalMinimo) sinalMinimo = valorAnalogico;
  }
  int PTP = sinalMaximo - sinalMinimo;
  float db = mapf(PTP, 20, 900, 49.5, 90.0);
  db = constrain(db, 49.5, 90.0);
  return db
}

/**
 * @brief map() com support ao float.
 */
float mapf(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + (out_min);
}

// ! PARTE CONEXÃO

/**
 * @brief Envia valor de decibéis ao servidor web via HTTP POST.
 * 
 * Formata payload JSON com o valor atual de dB e envia
 * para o endpoint /api/decibels do servidor web (PC-1).
 * \n Lê o valor protegido pelo semáforo antes do envio.
 * 
 * @param db Valor em decibéiis a ser enviado
 */
void enviarParaServidor(float db) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[HTTP] Wifi desconectado, pulando envio...")
    return;
  }
  HTTPClient http;
  http.begin(SERVER_URL);
  http.addHeader("Content-Type", "application/json")
  String payload = "{\"value\"": + String(db, 2) + "}";
  int httpCode = http.POST(payload);
  if (httpCode > 0) {
    Serial.printf("[HTTP] POST %s | dB %.2f | HTTP: %d\n", SERVER_URL, db, httpCode);
  } else {
    Serial.printf("[HTTP] Erro no POST: %s\n", http.errorToString(httpCode).c_str());
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
  IPAddress local_IP(192, 168, 15, 13);
  IPAddress gateway(192, 168, 15, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
  IPAddress secondaryDNS(8, 8, 4, 4);
  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)){
    Serial.println("[WIFI] Falha ao configurar IP estático");
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("[WIFI] Conectando");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("[WIFI] Endereço IP: ");
  Serial.println(WiFi.localIP());
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
 * @brief Função de tarefa que gerencia a parte física do sistema.
 *
 * Esta função deve ser executada como uma tarefa FreeRTOS. Ela executa continuamente
 * em um loop infinito, imprimindo "Tarefa Parte Física rodando!" no monitor serial a cada segundo.
 * O atraso entre as impressões é feito usando vTaskDelay.
 *
 * @param pvParameters Ponteiro para os parâmetros passados para a tarefa (não utilizado).
*/
void ParteFisica(void *pvParameters) {
  Serial.printf("[TASK] ParteFisica Inicializada | CORE: %d\n ", xPortGetCoreID);
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
 * @brief Função de tarefa que gerencia a parte de conexão do sistema.
 *
 * Esta função deve ser executada como uma tarefa FreeRTOS. Ela configura o servidor web,
 * define as rotas e aguarda requisições do cliente em um loop infinito.
 *
 * @param pvParameters Ponteiro para os parâmetros passados para a tarefa (não utilizado).
*/
void ParteConexao(void *pvParameters) {
  Serial.print("[TASK] ParteConexao Inicializada | CORE: %d\n", xPortGetCoreID());
  for (;;) {
    float db = 0.0;
    if (xSemaphoreTake(xSemaphoreDecibels, portMAX_DELAY) == pdTRUE) {
      db = FC04valorDecibels;
      xSemaphoreGive(xSemaphoreDecibeis);
    }
    enviarParaServidor(db);
    vTaskDelay(pdMS_TO_TICKS(INTERVALO_ENVIO_MS));
  }
}

void loop() {}
