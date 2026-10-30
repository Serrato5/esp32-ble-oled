/*
  Laboratorio: Control inalámbrico por Bluetooth con ESP32 - Grupo 4 (pantalla OLED I2C)
  Electrónica Digital II - Universidad Surcolombiana

  El celular (iPhone, app nRF Toolbox -> UART) envía comandos por Bluetooth BLE
  y el ESP32 los muestra o los ejecuta en una pantalla OLED SSD1306 de 128x64.

  Conexiones OLED -> ESP32:  VCC -> 3V3 | GND -> GND | SDA -> GPIO21 | SCL -> GPIO22

  Comandos (se escriben en el celular):
    cualquier texto  -> lo escribe en la pantalla (texto libre)
    TXT:mensaje      -> igual, escribe "mensaje"
    BORRAR           -> borra la pantalla y vuelve a la esquina superior
    POS:x,y          -> mueve el cursor a la posición x,y en píxeles (x 0-127, y 0-63)
    TAM:n            -> tamaño de letra n = 1, 2 o 3
    AYUDA            -> devuelve la lista de comandos al celular
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

// ---------- Pantalla OLED ----------
#define ANCHO 128
#define ALTO  64
Adafruit_SSD1306 oled(ANCHO, ALTO, &Wire, -1);

// ---------- BLE: servicio UART de Nordic (lo reconocen nRF Toolbox y Bluefruit) ----------
#define UUID_SERVICIO "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define UUID_RX       "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"   // celular -> ESP32
#define UUID_TX       "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"   // ESP32 -> celular

BLECharacteristic *txChar;
bool conectado = false;
String comando = "";
volatile bool hayComando = false;

int posX = 0, posY = 0, tam = 1;   // estado del cursor y tamaño de letra
bool hayOled = false;              // false si la pantalla no responde (permite probar el BLE sin ella)

// Envía una respuesta al celular y al Monitor Serie
void responder(const String &msg) {
  Serial.println(msg);
  if (conectado) {
    txChar->setValue(msg.c_str());
    txChar->notify();
  }
}

// Eventos de conexión
class EventosServidor : public BLEServerCallbacks {
  void onConnect(BLEServer *s) override {
    conectado = true;
  }
  void onDisconnect(BLEServer *s) override {
    conectado = false;
    s->getAdvertising()->start();        // vuelve a ser visible para reconectar
  }
};

// Llega un comando desde el celular: solo se guarda; se procesa en loop()
class EventosRx : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override {
    comando = String(c->getValue().c_str());
    comando.trim();
    hayComando = true;
  }
};

// Escribe texto en la posición y tamaño actuales
void escribir(const String &texto) {
  if (hayOled) {
    oled.setTextSize(tam);
    oled.setCursor(posX, posY);
    oled.print(texto);
    oled.display();
  }
  posY += 8 * tam;                      // baja a la siguiente línea
  if (posY >= ALTO) posY = 0;
  responder("OK texto: " + texto);
}

// Interpreta cada comando
void procesar(const String &cmd) {
  String may = cmd;
  may.toUpperCase();

  if (may == "BORRAR") {
    if (hayOled) {
      oled.clearDisplay();
      oled.display();
    }
    posX = 0;
    posY = 0;
    responder("OK pantalla borrada");
  }
  else if (may.startsWith("TAM:")) {
    int t = cmd.substring(4).toInt();
    if (t >= 1 && t <= 3) {
      tam = t;
      responder("OK tamano " + String(tam));
    } else {
      responder("ERROR: usa TAM:1, TAM:2 o TAM:3");
    }
  }
  else if (may.startsWith("POS:")) {
    int coma = cmd.indexOf(',');
    if (coma > 4) {
      posX = constrain(cmd.substring(4, coma).toInt(), 0, ANCHO - 1);
      posY = constrain(cmd.substring(coma + 1).toInt(), 0, ALTO - 1);
      responder("OK posicion " + String(posX) + "," + String(posY));
    } else {
      responder("ERROR: usa POS:x,y  ej. POS:0,32");
    }
  }
  else if (may == "AYUDA") {
    responder("Comandos: texto | TXT:msg | BORRAR | POS:x,y | TAM:1-3");
  }
  else if (may.startsWith("TXT:")) {
    escribir(cmd.substring(4));
  }
  else if (cmd.length() > 0) {
    escribir(cmd);                        // texto libre
  }
}

void setup() {
  Serial.begin(115200);

  // OLED por I2C en los pines por defecto del ESP32
  Wire.begin(21, 22);
  hayOled = oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  if (!hayOled) {
    Serial.println("No se encontro la OLED (revisa cables y direccion 0x3C). El Bluetooth sigue funcionando.");
  } else {
    oled.clearDisplay();
    oled.setTextColor(SSD1306_WHITE, SSD1306_BLACK);   // con fondo: sobrescribe limpio
    oled.setTextSize(1);
    oled.setCursor(0, 0);
    oled.println("ESP32 + OLED + BLE");
    oled.println("Grupo 4");
    oled.println("Esperando conexion...");
    oled.display();
  }
  posY = 32;

  // Bluetooth BLE con el servicio UART
  BLEDevice::init("ESP32_Grupo4");
  BLEServer *servidor = BLEDevice::createServer();
  servidor->setCallbacks(new EventosServidor());
  BLEService *servicio = servidor->createService(UUID_SERVICIO);

  txChar = servicio->createCharacteristic(UUID_TX, BLECharacteristic::PROPERTY_NOTIFY);
  txChar->addDescriptor(new BLE2902());

  BLECharacteristic *rxChar = servicio->createCharacteristic(
      UUID_RX, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rxChar->setCallbacks(new EventosRx());

  servicio->start();
  BLEAdvertising *anuncio = BLEDevice::getAdvertising();
  anuncio->addServiceUUID(UUID_SERVICIO);
  anuncio->setScanResponse(true);
  BLEDevice::startAdvertising();
  Serial.println("Listo: busca 'ESP32_Grupo4' en el celular");
}

void loop() {
  if (hayComando) {
    hayComando = false;
    procesar(comando);
  }
  delay(10);
}
