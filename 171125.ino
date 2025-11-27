// --- Librerías ---
#include <ESP8266WiFi.h>      // Para la conexión WiFi
#include <PubSubClient.h>     // Para la conexión MQTT

// --- Configuración WiFi ---
const char* ssid = "Redmi";            // Reemplaza con el nombre de tu red WiFi
const char* password = "05052005";    // Reemplaza con tu contraseña de WiFi

// --- Configuración MQTT ---
const char* mqtt_server = "20.151.96.240"; // IP Pública
const int mqtt_port = 1884; // Puerto MQTT

// --- PINES Y CONSTANTES (Configuración de dos sensores: Izquierda/Centro) ---
#define PIN_TRIG D1
#define PIN_ECHO D2
#define PIN_TRIG3 D5  // Sensor Centro
#define PIN_ECHO3 D6  // Sensor Centro

// Definición para el pin del buzzer
#define PIN_BUZZER D7 

// Pin del acelerómetro (Eje Z)a
#define PIN_ACCEL_Z A0 

// --- FRECUENCIAS DE TONO PARA CADA SENSOR ---
const int FREC_IZQUIERDA = 1000; // Tono alto (1000 Hz)
const int FREC_CENTRO    = 200;  // Tono bajo (200 Hz)
const int TONE_DURATION  = 200;  // Duración del tono en ms

float distancia;  // Sensor Izquierda
float distancia3; // Sensor Centro

// Variables para el acelerómetro
int val_z_raw; 
float accel_z; 

// Constantes de Calibración/Umbrales (Tomadas del código original)
const int OFFSET_Z = 500; 
const float SENSITIVITY_Z = 100.0; 
const float HORIZONTAL_UMBRAL_MIN = 0.8; 
const float HORIZONTAL_UMBRAL_MAX = 1.2; 
const float ALARM_THRESHOLD_CM = 20.0; // Umbral de 20.0 cm

// --- Objetos WiFi y MQTT ---
WiFiClient espClient;
PubSubClient client(espClient);

// ----------------------------------------------------
// Función de Callback (para RECIBIR datos de MQTT)
// ----------------------------------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaje recibido [");
  Serial.print(topic);
  Serial.print("] ");
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);

  // Ejemplo: Si recibes "ALARMA_REMOTA", suena el buzzer
  if (message == "ALARMA_REMOTA") {
    Serial.println("¡Alarma remota activada!");
    tone(PIN_BUZZER, 500, 2000); // Tono de 500Hz por 2s
  }
}

// ----------------------------------------------------
// Función para reconectar a WiFi y MQTT
// ----------------------------------------------------
void reconnect() {
  // Intenta conectar a WiFi si está desconectado
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print("Conectando a WiFi...");
    delay(500);
  }

  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");
    String clientId = "Sentinel-";
    clientId += String(random(0xffff), HEX);
    
    // Conectar con ID aleatorio
    if (client.connect(clientId.c_str())) {
      Serial.println("¡Conectado a MQTT!");
      // Suscribirse al tópico de control
      client.subscribe("sentinel/control");
      Serial.println("Suscrito a 'sentinel/control'");
    } else {
      Serial.print("falló, rc=");
      Serial.print(client.state());
      Serial.println(" intentando de nuevo en 5 segundos");
      delay(5000);
    }
  }
}

// ----------------------------------------------------
// Función para medir la distancia de un solo sensor
// ----------------------------------------------------
float ICACHE_FLASH_ATTR medirDistancia(int trigPin, int echoPin) {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(4);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    long duration = pulseIn(echoPin, HIGH); // 30ms timeout (máx 5m)

    if (duration == 0) {
        yield();
        return 0.0;
    }
    float distance_cm = duration / 58.3;
    
    yield();
    
    return distance_cm; 
}


void setup() {
    Serial.begin(115200);
    
    // 1. Configuración de pines 
    pinMode(PIN_TRIG, OUTPUT);
    pinMode(PIN_ECHO, INPUT);
    pinMode(PIN_TRIG3, OUTPUT); // Sensor Centro
    pinMode(PIN_ECHO3, INPUT);  // Sensor Centro
    pinMode(PIN_BUZZER, OUTPUT);
    digitalWrite(PIN_BUZZER, LOW); 

    // 2. Iniciar conexión WiFi
    Serial.println();
    Serial.print("Conectando a ");
    Serial.println(ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
    }
    Serial.println("");
    Serial.println("¡WiFi conectado!");
    Serial.print("Dirección IP: ");
    Serial.println(WiFi.localIP());

    // 3. Configurar servidor MQTT
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
}


void loop() {
    // 1. Mantenimiento de Conexión MQTT
    if (!client.connected()) {
      reconnect();
    }
    client.loop(); // Permite recibir y mantener viva la conexión

    // 2. Lectura de Sensores Ultrasónicos (SECUENCIAL)
    distancia = medirDistancia(PIN_TRIG, PIN_ECHO);  // Izquierda
    delay(10);
    distancia3 = medirDistancia(PIN_TRIG3, PIN_ECHO3); // Centro
    delay(10);

    // 3. Lectura del Acelerómetro GY-61 (Eje Z)
    val_z_raw = analogRead(PIN_ACCEL_Z);
    accel_z = ((float)val_z_raw - OFFSET_Z) / SENSITIVITY_Z;

    // 4. Lógica de activación del Buzzer y Tonos (Lógica original de prioridad)
    bool alarma_activa = false;
    
    bool alarma_izquierda = (distancia > 0.0 && distancia <= ALARM_THRESHOLD_CM);
    bool alarma_centro    = (distancia3 > 0.0 && distancia3 <= ALARM_THRESHOLD_CM);
    
    String alarma_estado_mqtt = "Normal";

    // Prioridad de los tonos (Centro > Izquierda)
    if (alarma_centro) {
        tone(PIN_BUZZER, FREC_CENTRO, TONE_DURATION);
        alarma_activa = true;
        alarma_estado_mqtt = "PELIGRO_CENTRO";
    } else if (alarma_izquierda) {
        tone(PIN_BUZZER, FREC_IZQUIERDA, TONE_DURATION);
        alarma_activa = true;
        alarma_estado_mqtt = "PELIGRO_IZQUIERDA";
    } else {
        noTone(PIN_BUZZER);
    }
    
    // 5. Lógica de Detección de Horizontalidad
    bool is_horizontal = (accel_z >= HORIZONTAL_UMBRAL_MIN && accel_z <= HORIZONTAL_UMBRAL_MAX);
    String estado_orientacion_mqtt = "Normal";
    if (is_horizontal) {
      estado_orientacion_mqtt = "HORIZONTAL";
    }


    // 6. Publicación de datos por MQTT (JSON)
    char jsonBuffer[256];
    // Se publica dist_izq y dist_cen (dist3)
    snprintf(jsonBuffer, sizeof(jsonBuffer), 
      "{\"dist_izq\": %.2f, \"dist_cen\": %.2f, \"accelZ\": %.2f, \"alarma\": \"%s\", \"orientacion\": \"%s\"}",
      distancia,
      distancia3,  
      accel_z,
      alarma_estado_mqtt.c_str(),
      estado_orientacion_mqtt.c_str()
    );

    Serial.print("Publicando JSON: ");
    Serial.println(jsonBuffer);
    client.publish("sentinel/datos", jsonBuffer);


    // 7. Impresión de los resultados en Serial
    Serial.println("--- RESULTADOS ---");
    Serial.print("Sensor Izquierda (D1/D2): "); Serial.print(distancia, 2); Serial.println(" cm");
    Serial.print("Sensor Centro (D5/D6): "); Serial.print(distancia3, 2); Serial.println(" cm");
    Serial.println("--- Acelerómetro GY-61 (Eje Z) ---");
    Serial.print("Eje Z (g): ");
    Serial.print(accel_z, 2); 
    Serial.print(" g | Estado: ");
    
    if (is_horizontal) {
        Serial.println("**Baston Caido**");
    } else {
        Serial.println("Normal (Inclinado o Invertido)");
    }
    
    if (alarma_activa) {
      Serial.println("** ¡ALARMA ULTRASÓNICA ACTIVADA! **");
    } else {
      Serial.println("Alarma Ultrasónica: Desactivada.");
    }
    Serial.println("------------------");

    delay(2000); // Espera de 2 segundos para sincronizar con la publicación MQTT
}