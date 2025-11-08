// Добавить таймер реконнекта. Если реконнекта нет дольше 5 мин, то перезапустить устройство
// Опрос датчика температуры не даёт ветке 0.1 с работать с нужной частотой. Может попробовать через прерывание от таймера опрашивать?

#include <AsyncUDP.h>
#include <WiFi.h>

#include "esp_screen_exch.h"
#include "CRC_Software_calculation.h"

const int led_connect_rpi_pin = 15;          // Светодиод состояния связи с RPI
const int button1_pin = 21;                  // Кнопка 1

//const char *ssid_router = "T&J";
//const char *password_router = "58635863";
const char *ssid_router = "Z2";
const char *password_router = "123fptry";

// Буферы обмена по UDP
ESP_SCREEN_RPI esp_rpi;
RPI_ESP_SCREEN rpi_esp;

AsyncUDP udp;
const uint16_t my_port = 50100;
const uint16_t rpi_port = 50001;
IPAddress rpi_ip(10,42,0,107);   // Не используется, т.к. посылки от ESP на RPI широковещательные

void parsePacket(AsyncUDPPacket packet)
{
    bool debug_printf = false;

    const uint8_t *buf = (uint8_t*)packet.data();  // Адрес начала данных в памяти
    const size_t size = packet.length();
    
    if(buf != nullptr && size > 0 && debug_printf)
    {
      Serial.print(String("IN (") + size + String("): "));
      for (size_t i=0; i<size; i++)
      {
        Serial.print(buf[i], HEX);
        Serial.print(" ");
      }
      Serial.println("");
    }

    // Проверяем длину буфера на соответствие
		if (size != sizeof(rpi_esp))
		{
			Serial.println(String("Error: UDP packet size (") + size + String(") does not match our structure (") + sizeof(rpi_esp) + String(")"));
			return;
		}

    // Проверяем контрольную сумму
    uint16_t crc_buf = *(uint16_t*)(&buf[size-2]);  // Последнее 16-ти битное слово буфера содержит CRC16
    
    // Алгоритм CRC16_CCIT_ZERO
    uint16_t crc_calc = CRC16_Calculate_software((uint16_t*)buf, size/2 - 1, 0x0000, 0x1021, false, false, 0x0000);
    
    if (debug_printf)
    {
      Serial.print("CRC_READ = 0x"); Serial.println(crc_buf, HEX);
      Serial.print("CRC_CALC = 0x"); Serial.println(crc_calc, HEX);
    }

    if (crc_buf != crc_calc)
    {
      Serial.print("Error: UDP packet wrong CRC 0x");
      Serial.println(crc_buf, HEX);
			return;
    }

    // Все проверки пройдены, можно скопировать входящий буфер
		memcpy(&rpi_esp, buf, static_cast<uint64_t>(size));

    Serial.println(String("cnt=") + rpi_esp.cnt);
    Serial.println(String("t=") + rpi_esp.t1/100.0);
    Serial.print("crc=0x"); 
    Serial.println(rpi_esp.crc, HEX);
}

void connect_to_router()
{
  digitalWrite(led_connect_rpi_pin, LOW);     // Связи с RPI точно нет

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_router, password_router);
  Serial.println(String("Connecting to ") + ssid_router + String(" network"));

  while (WiFi.status() != WL_CONNECTED)
  {
    static int cnt = 0;
    cnt++;
    if(cnt >= 49) 
    { 
      Serial.print("\n");
      cnt = 0;
    }
    delay(500);
    Serial.print(".");
  }

  Serial.print("\nConnected, IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println(String("WiFi RSSI: ") + WiFi.RSSI() + String(" dB"));
}

void setup() {
  // put your setup code here, to run once:

  memset (&rpi_esp, 0, sizeof(rpi_esp));
	memset (&esp_rpi, 0, sizeof(esp_rpi));

  Serial.begin(115200);
  
  pinMode(led_connect_rpi_pin, OUTPUT);
  pinMode(button1_pin, INPUT_PULLUP);     // Кнопка 1 

  delay(2000);

  connect_to_router();

  if (udp.listen(my_port))
  {
    udp.onPacket(parsePacket);
  }
  /*
  if (udp.connect(rpi_ip, rpi_port))
  {
    Serial.println("UDP: connection created");
  }
  else
  {    
    Serial.println("UDP: connection not created");
    delay(5000);
    esp_restart();
  }
  */
}

void loop() 
{
  // put your main code here, to run repeatedly:

  unsigned long t_current = millis();

  static unsigned long t_work_01_sec = t_current;
  static unsigned long t_work_1_sec = t_current;
  static unsigned long t_work_3_sec = t_current;
  static unsigned long t_rpi_cnt = t_current;
  
  if (t_current - t_work_01_sec >= 100)   // Период работы главного цикла 0.1 сек
  {
    t_work_01_sec = t_current;
    
    int b1 = !digitalRead(button1_pin);     // Кнопка
    
    //Serial.println(String("b1=") + b1);   // Выводим состояние кнопки
    esp_rpi.button1 = b1;
  }

  if (t_current - t_work_1_sec >= 1000)   // Период работы главного цикла 1 сек
  {
    t_work_1_sec = t_current;

    static int cnt = 0;
    cnt++;
    Serial.println(cnt);

    Serial.println(String("t1=") + rpi_esp.t1/100.0);
    
    // Проверка состояния соединения с роутером по WiFi
    if (WiFi.status() != WL_CONNECTED)
    {
      connect_to_router();
    }

    long rssi = WiFi.RSSI();

    // Заполнение и отправка исходящего буфера
    esp_rpi.cnt = cnt;
    esp_rpi.rssi = static_cast<int16_t>(rssi);
    
    // Алгоритм CRC16_CCIT_ZERO
	  esp_rpi.crc = CRC16_Calculate_software((uint16_t*) &esp_rpi, sizeof(esp_rpi) / 2 - 1, 0x0000, 0x1021, false, false, 0x0000);
    //Serial.print("CRC = 0x");
    //Serial.println(esp_rpi.crc, HEX);

    udp.broadcastTo((uint8_t *)&esp_rpi, sizeof(esp_rpi), rpi_port);

    //Serial.println(String("WiFi RSSI: ") + rssi);
  
  } // 1 сек

  if (t_current - t_work_3_sec >= 3000)   // Период работы 3 сек
  {
    t_work_3_sec = t_current;
    
    // Проверка связи с RPI и включение светодиода
    // Если хоть раз изменился счётчик rpi_esp.cnt, значит связь есть
    static uint16_t rpi_cnt = rpi_esp.cnt;
    if (rpi_esp.cnt != rpi_cnt)
    {
      digitalWrite(led_connect_rpi_pin, HIGH);
      rpi_cnt = rpi_esp.cnt;
    }
    else
    {
      digitalWrite(led_connect_rpi_pin, LOW);
    }
  } // 3 сек

}


