#include <WiFiNINA.h>
#include <ArduinoHttpClient.h>
#include <Servo.h>

// WiFi y Telegram
const char* ssid = "TotalPLAY";
const char* password = "Terminator3";
const char* BOT_TOKEN = "7800356049:AAFKVSJwTDSK2p3eS_6GWIpIMLhqotMZ4PA";
const char* CHAT_ID = "-1002527647327";

// Pines y control EMG
const int pinEMG = A0;
const float fs = 1000.0;
const float fc = 30.0;
const float notchFreq = 50.0;
const float notch_bw = 1.0;
const int ventana = 100;
float buffer[ventana];
int i_buffer = 0;
float suma_rms = 0.0;
float rms_suavizado = 0.0;
const float alpha_rms = 0.2;

// Filtro pasa bajos RC
float alpha_lp;
float prev_lp_output = 0.0;

// Filtro Notch
bool usar_notch = true;
float notch_a1, notch_a2, notch_b1, notch_b2;
float notch_in_1 = 0.0, notch_in_2 = 0.0;
float notch_out_1 = 0.0, notch_out_2 = 0.0;

// Servo
Servo myServo;
int pinServo = 2;
const int VUELTAS_COMPLETAS = 5;
const int TIEMPO_VUELTA = 1000; // ms por vuelta
const int VELOCIDAD_ABRIR = 0;   // Dirección 1
const int VELOCIDAD_CERRAR = 180; // Dirección 2
const int SERVO_PARADO = 90; // Ajusta según tu servo

// Estado y temporizador
unsigned long ultimaAccion = 0;
const unsigned long intervaloAcciones = 2500;
bool focoEncendido = false;
bool cortinaAbierta = false;
float ultimoRMS = 0;

// Cliente Shelly y Telegram
const char* shelly_ip = "192.168.100.37";
WiFiClient client;
WiFiSSLClient wifi;
HttpClient telegram = HttpClient(wifi, "api.telegram.org", 443);

void setup() {
  Serial.begin(115200);
  pinMode(LED_BUILTIN, OUTPUT);
  myServo.attach(pinServo);
  delay(100); // estabilización
  myServo.detach(); // Evita que se mueva solo

  // Inicializar buffer
  for (int i = 0; i < ventana; i++) buffer[i] = 0;

  // Filtro pasa bajos
  float RC = 1.0 / (2.0 * PI * fc);
  float dt = 1.0 / fs;
  alpha_lp = dt / (RC + dt);

  // Filtro Notch
  if (usar_notch) {
    float w0 = 2.0 * PI * notchFreq / fs;
    float cos_w0 = cos(w0);
    float sin_w0 = sin(w0);
    float Q = notchFreq / notch_bw;
    float alpha = sin_w0 / (2.0 * Q);
    float a0 = 1.0 + alpha;
    notch_a1 = -2.0 * cos_w0 / a0;
    notch_a2 = 1.0 / a0;
    notch_b1 = -2.0 * cos_w0 / a0;
    notch_b2 = (1.0 - alpha) / a0;
  }

  conectarWiFi();
  Serial.println("Sistema iniciado. Monitoreando EMG...");
}

void loop() {
  int lectura = analogRead(pinEMG);
  float valor = lectura - 512;

  // Filtro pasa bajos
  float filtrada = alpha_lp * valor + (1 - alpha_lp) * prev_lp_output;
  prev_lp_output = filtrada;

  // Filtro Notch
  float procesada = filtrada;
  if (usar_notch) {
    procesada = filtrada + notch_a1 * notch_in_1 + notch_a2 * notch_in_2
                - notch_b1 * notch_out_1 - notch_b2 * notch_out_2;

    notch_in_2 = notch_in_1;
    notch_in_1 = filtrada;
    notch_out_2 = notch_out_1;
    notch_out_1 = procesada;
  }

  // Cálculo RMS
  suma_rms -= buffer[i_buffer];
  buffer[i_buffer] = procesada * procesada;
  suma_rms += buffer[i_buffer];
  i_buffer = (i_buffer + 1) % ventana;
  float rms = sqrt(suma_rms / ventana);

  // RMS suavizado
  rms_suavizado = alpha_rms * rms + (1 - alpha_rms) * rms_suavizado;
  ultimoRMS = rms_suavizado;

  // Imprimir en serial
  unsigned long tiempoActual = millis();
  Serial.print(tiempoActual);
  Serial.print(",");
  Serial.print(valor);
  Serial.print(",");
  Serial.print(filtrada);
  Serial.print(",");
  Serial.println(rms);

  digitalWrite(LED_BUILTIN, rms_suavizado > 10.0 ? HIGH : LOW);

  if (millis() - ultimaAccion >= intervaloAcciones) {
    ultimaAccion = millis();

    Serial.println("\n-------------------------------");
    Serial.print("RMS detectado: ");
    Serial.println(ultimoRMS, 2);
    ejecutarAccion(ultimoRMS);
    Serial.println("-------------------------------");

    delay(2000); // Esperar antes de reiniciar
  }

  delay(1);  // para mantener 1000 Hz
}

void ejecutarAccion(float rms) {
  if (rms >= 49.00 && rms <= 51.99) {
    Serial.println("ACCION: Cambiar estado del foco");
    controlarFoco();
  }
  else if (rms >= 52.00 && rms <= 54.99 && !cortinaAbierta) {
    Serial.println("ACCION: Abrir cortina (5 vueltas)");
    moverCortina(true);
  }
  else if (rms >= 55.00 && rms <= 57.99 && cortinaAbierta) {
    Serial.println("ACCION: Cerrar cortina (5 vueltas)");
    moverCortina(false);
  }
  else if (rms >= 58.00 && rms <= 61.99) {
    Serial.println("ACCION: Enviar alerta a Telegram");
    enviarAlertaTelegram(rms);
  }
  else {
    Serial.print("ACCION: Ninguna (RMS ");
    Serial.print(rms, 2);
    Serial.println(" fuera de los rangos válidos)");
  }
}

void moverCortina(bool abrir) {
  myServo.attach(pinServo); // activar servo justo antes
  int velocidad = abrir ? VELOCIDAD_ABRIR : VELOCIDAD_CERRAR;
  String direccion = abrir ? "abriendo" : "cerrando";

  for (int i = 0; i < VUELTAS_COMPLETAS; i++) {
    Serial.print("Vuelta ");
    Serial.print(i + 1);
    Serial.print(" - Dirección: ");
    Serial.println(direccion);
    myServo.write(velocidad);
    delay(TIEMPO_VUELTA);
  }

  myServo.write(SERVO_PARADO);
  delay(300); // dar tiempo a detenerse
  myServo.detach(); // cortar señal
  cortinaAbierta = abrir;

  Serial.print("Cortina ");
  Serial.print(abrir ? "abierta" : "cerrada");
  Serial.println(" (5 vueltas completas)");
}

void controlarFoco() {
  focoEncendido = !focoEncendido;
  String estado = focoEncendido ? "on" : "off";
  sendShellyCommand("/light/0?turn=" + estado);
  Serial.print("Foco: ");
  Serial.println(focoEncendido ? "ENCENDIDO" : "APAGADO");
}


void enviarAlertaTelegram(float valorRMS) {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  String mensaje = "⚠️ Alerta, el usuario necesita ayuda, se detectó un RMS: " + String(valorRMS, 2);
  String url = "/bot" + String(BOT_TOKEN) + "/sendMessage?chat_id=" + String(CHAT_ID) + "&text=" + mensaje;

  telegram.get(url);
  Serial.println("Alerta enviada a Telegram");
}

void sendShellyCommand(String command) {
  if (client.connect(shelly_ip, 80)) {
    client.print("GET " + command + " HTTP/1.1\r\nHost: " + String(shelly_ip) + "\r\nConnection: close\r\n\r\n");
    delay(200);
    client.stop();
  } else {
    Serial.println("Error al conectar con Shelly");
  }
}

void conectarWiFi() {
  Serial.print("Conectando WiFi...");
  while (WiFi.begin(ssid, password) != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println(" ¡Conectado!");
}
