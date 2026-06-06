#include <Arduino.h>
#include <stdio.h>
#include <WiFi.h>
#include <WebServer.h>

//? Pino de sensor FC-04
#define FC04_ANALOG_PIN 35

//? Credenciais WiFi
#define WIFI_SSID     "TP-Link_9D84"
#define WIFI_PASSWORD "definitivamente_uma_senha" // TODO: insira sua senha aqui!
#define SERVER_URL    "http://192/168.15.18/api/decibels"

// ? Handles das tarefas
TaskHandle_t HandleParteFisica;
TaskHandle_t HandleParteConexao;

// ? Instâncias
WebServer servidor(80);

const int tempoDeLeitura = 50; //? tempo de leitura por micro-segundos (ms)
volatile int FC04valorAnalogico = 0, FC04valorDecibeis = 0;
volatile int sinalMaximo = 0, sinalMinimo = 1023;

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
long leitorDeDecibeis() {
  unsigned long comesarMillis = millis();
  while (millis() - comesarMillis < tempoDeLeitura) {
    FC04valorAnalogico = analogRead(FC04_ANALOG_PIN);
    if (FC04valorAnalogico > sinalMaximo) sinalMaximo = FC04valorAnalogico;
    if (FC04valorAnalogico < sinalMinimo) sinalMinimo = FC04valorAnalogico;
  }
  int PTP = sinalMaximo - sinalMinimo;
  int FC04valorDecibeis = map(PTP, 20, 900, 49.5, 90); //? Ajustar após calibração
}

// ! PARTE CONEXÃO

/**
 * @brief Manipula a rota raiz do servidor web.
 *
 * Esta função é chamada quando a rota raiz ("/") é acessada. Ela coleta dados dos sensores,
 * formata esses dados em JSON e os envia como resposta HTTP.
*/
void handleRoot() {
  /* // * json:
  {
    "FC04decibeis": xDb,
  }
  */
  String json = "{";
  json += "\"FC04decibeis\":" + String(FC04valorAnalogico);
  json += "}";
  servidor.sendHeader("Access-Control-Allow-Origin", "*");
  servidor.sendHeader("Access-Control-Allow-Methods", "GET");
  servidor.sendHeader("Access-Control-Allow-Headers", "Content-Type");
  servidor.send(200, "application/json", json);
}

//? static IP config

IPAddress local_IP(192, 168, 15,13);
IPAddress gateway(192, 168, 15, 1);
IPAddress subnet(255,255,255,0);
IPAddress primaryDNS(8,8,8,8);
IPAddress secondaryDNS(8,8,4,4);

/**
 * @brief Configura a conexão WiFi.
 *
 * Esta função inicia a conexão WiFi com as credenciais fornecidas.
 * Ela imprime o status da conexão no monitor serial e exibe o endereço IP local
 * quando a conexão é bem-sucedida.
*/
void setupWifi() {
  if (!WIFI.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)){
    Serial.println("Falha ao configurar IP estático")
  }
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado ao WiFi!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());
}

void setup() {
  Serial.begin(115200);
  pinMode(FC04_ANALOG_PIN, OUTPUT);
  setupWifi();
  xTaskCreatePinnedToCore(
    ParteFisica,
    "ParteFisica",
    2048,
    NULL,
    1,
    &HandleParteFisica,
    0
  );
  delay(500);
  xTaskCreatePinnedToCore(
    ParteConexao,
    "ParteConexao",
    4096,
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
  Serial.print("TASK: ParteFisica | CORE: ");
  Serial.println(xPortGetCoreID());
  for (;;) {
    leitorDeDecibeis();
    vTaskDelay(pdMS_TO_TICKS(20));
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
  Serial.print("TASK: ParteConexao | CORE: ");
  Serial.println(xPortGetCoreID());
  // ? Configuração do servidor web
  servidor.on("/", HTTP_GET, handleRoot);     // ? Rota root
  servidor.begin();                           // ? Inicia o servidor web
  Serial.println("Servidor web iniciado!");
  for (;;) {
    servidor.handleClient();                  // ? Aguarda requisições do client
    vTaskDelay(pdMS_TO_TICKS(10));            // ? Pequeno delay para evitar uso excessivo da CPU :]
  }
}

void loop() {}
