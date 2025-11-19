#include <AsyncUDP.h>
#include <WiFi.h>
#include <SPI.h>        // Для экранов с SPI
#include <Wire.h>       // Для экранов с I2C

// ***** Monochrome OLEDs based on SSD1306 drivers **********
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128 // OLED display width, in pixels
#define SCREEN_HEIGHT 64 // OLED display height, in pixels
#define OLED_RESET     -1 // Reset pin # (or -1 if sharing Arduino reset pin)
#define SCREEN_ADDRESS 0x3C ///< See datasheet for Address; 0x3D for 128x64, 0x3C for 128x32
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
bool SSD1306 = false;

// ***** 4-Digit LED Display TM1637 *********
#include <TM1637Display.h>
#define CLK 22
#define DIO 23
TM1637Display disp1637(CLK, DIO);
bool TM1637 = false;

// **** OLED 2.42 126x64 Ver.4.3 SSD1309
#include <U8g2lib.h>
U8G2_SSD1309_128X64_NONAME0_F_4W_HW_SPI u8g2(U8G2_R0, /* cs=*/ 5, /* dc=*/ 16, /* reset=*/ 17);
bool SSD1309 = true;

#include "esp_timer.h"
#include "esp_screen_exch.h"
#include "CRC_Software_calculation.h"

hw_timer_t *hw_timer = NULL;                 // Аппаратный таймер
esp_timer_handle_t timer = NULL;             // Главный программный таймер

const int led_connect_rpi_pin = 02;          // Светодиод состояния связи с RPI
const int button1_pin = 19;                  // Кнопка 1   (В режиме INPUT, PULLUP, подтягивающий резистор внутри, поэтому соединение напрямую)
const int pir_sensor_pin = 34;               // GPIO 34 - Сенсор движения
const int time_to_show_display = 60;         // Время подсветки диспленя

int button1_prev = 0;                        // Состояние кнопки с предыдущего такта
int cnt_display_off = time_to_show_display;  // Счетчик для подсветки дисплея

bool rpi_connected = false;                  // Признак наличия связи с RPI

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

    //Serial.println(String("cnt=") + rpi_esp.cnt);
    //Serial.println(String("t=") + rpi_esp.t1/100.0);
    //Serial.print("crc=0x"); 
    //Serial.println(rpi_esp.crc, HEX);
}

void connect_to_router()
{
  digitalWrite(led_connect_rpi_pin, LOW);                   // Связи с RPI точно нет
 
  // ******************* SSD1306 *************************************
  if (SSD1306)
  {
    display.clearDisplay();                   // Clear display buffer
	  display.setTextSize(0);
    display.setCursor(0,0);
  }
  // ******************* TM1637 **************************************
  if (TM1637)
  {
    disp1637.clear();                          // Clear display
  }
  // *****************************************************************

  if (esp_timer_is_active(timer)) esp_timer_stop(timer);    // Остановим главный таймер

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid_router, password_router);
  Serial.println(String("Connecting to ") + ssid_router + String(" network"));
  
  if (SSD1306)
  {
    display.print(String("Connecting to ") + ssid_router + String(" network"));
    display.display(); 
  }
  if (SSD1309)
  {
    u8g2.clearBuffer();					// clear the internal memory
    u8g2.setFont(u8g2_font_7x13_t_cyrillic);	// choose a suitable font
    u8g2.setCursor(3,20);
    char c[128];
    sprintf(c, "Соединение с %s ", ssid_router);
    u8g2.print(c);
    u8g2.sendBuffer();					// transfer internal memory to the display
  }  

  int cnt1309_x = 0;
  int cnt1309_y = 0;
  int start1309_y = 32;
  int lines1309_y = 5;

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
    
    if (SSD1306)
    {
      static int cnt_display_SSD1306 = 0;
      cnt_display_SSD1306++;
      if (cnt_display_SSD1306 > 140)
      {
        display.clearDisplay();                   // Clear display buffer
        display.setCursor(0,0);
        cnt_display_SSD1306 = 0;
      }
      display.print(".");
      display.display();
    }
    if (TM1637)
    {
      static int pos = 0;
      disp1637.clear();
      disp1637.showNumberDec(0, false, 1, pos);
      pos++;
      if (pos > 3) pos = 0;
    }
    if (SSD1309)
    {
      u8g2.setFont(u8g2_font_10x20_t_cyrillic);	// choose a suitable font      
      u8g2.setCursor(0+cnt1309_x*5, start1309_y+cnt1309_y*7);
      u8g2.print(".");
      u8g2.sendBuffer();

      cnt1309_x++;
      if (cnt1309_x >= 24) 
      {
        cnt1309_x = 0;
        cnt1309_y++;
      }
      if (cnt1309_y >= lines1309_y) 
      {
        u8g2.clearBuffer();
        cnt1309_y = 0; 
        cnt1309_x = 0;
        start1309_y = 10;
        lines1309_y = 8;
      }
    }  
  }

  Serial.print("\nConnected, IP address: ");
  Serial.println(WiFi.localIP());
  Serial.println(String("WiFi RSSI: ") + WiFi.RSSI() + String(" dB"));

  // Соединение установлено, запускаем главный таймер
  esp_err_t res = esp_timer_start_periodic(timer, 10000);    // 10 000 мкс = 10 мс
  if (res != ESP_OK) Serial.println(String("ERROR start timer: ") + esp_err_to_name(res));
}

// Главный таймер, 100 Гц (10 мс)x
void on_main_timer(void *arg)
{
  static uint32_t cntTimerTick = 0;
  cntTimerTick++;

  if (cntTimerTick % 10 == 0)           // 0.1 cек
  {
    int button1 = !digitalRead(button1_pin);               // Кнопка
    //Serial.println(String("button1=") + button1);        // Выводим текущее мгновенное состояние кнопки

    int pir = digitalRead(pir_sensor_pin);
    //Serial.println(String("pir=") + pir);                  // Выводим текущее мгновенное состояние датчика

    if (button1_prev == 0 && button1 == 1)                 // Произошло нажатие
    {
      Serial.println(String("button1=") + button1);
      esp_rpi.button1 = 1;
      cnt_display_off = time_to_show_display;
      u8g2.setPowerSave(0);                                // Включаем дисплей
    }
    button1_prev = button1;

    if (pir == 1) 
    {
      cnt_display_off = time_to_show_display;
      u8g2.setPowerSave(0);                                // Включаем дисплей
    }

  }

  if (cntTimerTick % 100 == 0)           // 1 cек
  {
      static int cnt = 0;
      cnt++;

      Serial.println(cnt);
      Serial.println(String("t1=") + rpi_esp.t1/100.0); 
	 
      // Заполнение и отправка исходящего буфера
      esp_rpi.cnt = cnt;
   
      // Алгоритм CRC16_CCIT_ZERO
	    esp_rpi.crc = CRC16_Calculate_software((uint16_t*) &esp_rpi, sizeof(esp_rpi) / 2 - 1, 0x0000, 0x1021, false, false, 0x0000);
      //Serial.print("CRC = 0x");
      //Serial.println(esp_rpi.crc, HEX);

      udp.broadcastTo((uint8_t *)&esp_rpi, sizeof(esp_rpi), rpi_port);

      //Serial.println(String("esp_rpi.button1 = ") + esp_rpi.button1);
      //Serial.println(String("WiFi RSSI: ") + rssi);
  
      esp_rpi.button1 = 0; // Обнуляем кнопку после отправки, чтобы поймать следующее нажатие

      // Контроль подсветки дисплея
      cnt_display_off--;
      if (cnt_display_off == 0) u8g2.setPowerSave(1);        // Выключаем дисплей
  }

  if (cntTimerTick % 300 == 0)             // 3 сек
  {
    // Проверка связи с RPI и включение светодиода
    // Если хоть раз изменился счётчик rpi_esp.cnt, значит связь есть
    static uint16_t rpi_cnt = rpi_esp.cnt;
    if (rpi_esp.cnt != rpi_cnt)
    {
      digitalWrite(led_connect_rpi_pin, HIGH);
      rpi_connected = true;
      rpi_cnt = rpi_esp.cnt;
    }
    else
    {
      digitalWrite(led_connect_rpi_pin, LOW);
      rpi_connected = false;
    }
  } // 3 сек

}

void IRAM_ATTR on_hw_timer()  // Прерывание аппаратного таймера, 100 Гц (0.01 сек)
{
  static uint32_t cntTimerTick = 0;
  cntTimerTick++;

  if (cntTimerTick % 100 == 0)           // 1 cек
  {
      static int cnt = 0;
      cnt++;
      //Serial.println(String("HW TIMER ") + cnt);   
      //digitalWrite(led_connect_int_pin, !digitalRead(led_connect_int_pin)); 
  }
}

void setup() {
  // put your setup code here, to run once:

  Serial.begin(115200);

  // ******************* SSD1306 *************************************
  // Display SSD1306_SWITCHCAPVCC = generate display voltage from 3.3V internally
  if (SSD1306)
  {
    if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) 
    {
      Serial.println(F("SSD1306 allocation failed"));
      for(;;); // Don't proceed, loop forever
    }
    display.clearDisplay();                   // Clear display buffer
	  display.setTextColor(SSD1306_WHITE);      // Draw white text
	  display.setTextSize(1);
    display.setCursor(5,30);
    display.print("Starting...");
    display.display();
  }

  // ******************* TM1637 **************************************
  if (TM1637)
  {
    disp1637.clear();
    disp1637.setBrightness(7);
  }
  
  // ******************* SSD1309 *************************************
  if (SSD1309)
  {
    u8g2.begin();
    u8g2.enableUTF8Print();
    u8g2.setContrast(0);    
    u8g2.clearBuffer();					// clear the internal memory
    u8g2.setFont(u8g2_font_10x20_t_cyrillic);	// choose a suitable font
    u8g2.setCursor(20,40);
    u8g2.print("Запуск...");
    u8g2.sendBuffer();					// transfer internal memory to the display
  }

  memset (&rpi_esp, 0, sizeof(rpi_esp));
  memset (&esp_rpi, 0, sizeof(esp_rpi));  
  
  pinMode(led_connect_rpi_pin, OUTPUT);
  pinMode(button1_pin, INPUT_PULLUP);     // Кнопка 1 
  pinMode(pir_sensor_pin, INPUT);

  delay(2000);

  // Инициализация таймера на периодичность 0.1 с
  hw_timer = timerBegin(1000000);                       // Делитель таймера, работает с частотой 1 МГц (1/1000000 c)
  timerAttachInterrupt(hw_timer, &on_hw_timer);         // Привязываем таймер к функции-прерыванию
  timerAlarm(hw_timer, 10000, true, 0);                 // Срабатывать прерыванию при достижении 10000 отсчетов (0.01сек), true - таймер сбросится для периодичности, 0 - неограниченное кол-во раз

  esp_timer_create_args_t timer_config =
  {
    .callback = &on_main_timer,
    .name = "100Hz Timer"
  };
  esp_timer_create(&timer_config, &timer);              // А запустим его после установки соединения из функции connect_to_router()

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

  for (int i=0; i<3; i++)
  {
    digitalWrite(led_connect_rpi_pin, HIGH);   
    delay(100);
    digitalWrite(led_connect_rpi_pin, LOW);     
    delay(100);
  }

}

// *****************************************************************************
//    Асинхронный цикл
// *****************************************************************************
void loop() 
{
  // put your main code here, to run repeatedly:

  unsigned long t_current = millis();
  static unsigned long t_work_1_sec = t_current;  
  if (t_current - t_work_1_sec >= 1000)   // Период работы асинхронного цикла 1 сек
  {
    t_work_1_sec = t_current;
    // ....
  } // 1 сек

  // Проверка состояния соединения с роутером по WiFi
  if (WiFi.status() != WL_CONNECTED)
  {
    connect_to_router();
  }

  long rssi = WiFi.RSSI();
  esp_rpi.rssi = static_cast<int16_t>(rssi);


  // ******************* SSD1306 *************************************
  if (SSD1306)
  {
    display.clearDisplay();                   // Clear display buffer
	  display.setTextColor(SSD1306_WHITE);      // Draw white text
	
    if (rpi_connected) display.setTextSize(9);
    else 
    {
      display.setTextSize(0);
      display.setCursor(0,56);
      display.print("RPI NOT CONNECTED");
      display.setTextSize(7);
    }

    int t = rpi_esp.t1/100.0;
    int t_abs = abs(t);

    bool oneDigit = false;
    int  oneDigitOffset = 0;
    if (t>-10 && t<10) 
    {
      oneDigit = true;
      oneDigitOffset = 30;
    }
    if (t<0) display.fillRect(0 + oneDigitOffset, 28, 14, 8, SSD1306_INVERSE);   // Знак "-"

    display.setCursor(20 + oneDigitOffset,0);
    display.print(t_abs);
    display.display();
  }
  
  // ******************* TM1637 *************************************
  if (TM1637)
  {
     int t = rpi_esp.t1/100.0;
     disp1637.showNumberDec(t);
     delay(200);
  }

  // ********************* SSD1309 ************************************
  
  if (SSD1309)
  {
    float t1 = rpi_esp.t1/100.0;
    float t2 = 0;

    // Добавочные коэффициенты для перемещения статических надписей
    static double addX = 0.0;
    static double addY = 0.0;
    static double offsetX = 0.05;
    static double offsetY = 0.01;

    static double addGradX = 0.0;
    static double addGradY = 0.0;
    static double offsetGradX = 0.04;
    static double offsetGradY = 0.02;

    u8g2.clearBuffer();					// clear the internal memory
    //u8g2.setFontMode(1);

    if (rpi_connected)
    {
      if (rpi_esp.t1 == -273*100)   // Признак того, что у RPI нет связи с ESP балкон
      {
        u8g2.setFont(u8g2_font_8x13_t_cyrillic);
        u8g2.setCursor(15,30);
        u8g2.print("нет данных"); 
        u8g2.setCursor(15,46);
        u8g2.print("от ESP балкон"); 
      }
      else
      {
        u8g2.setFont(u8g2_font_9x15_t_cyrillic);	// choose a suitable font
        u8g2.setCursor(15+addX, 15+addY);
        u8g2.print("За балконом");

        u8g2.setFont(u8g2_font_logisoso30_tf);
        //u8g2.setFont(u8g2_font_helvR24_tn);
        
        char c[32];
        sprintf(c, "%.1f", t1);
        if(t1 <= -10.0)                u8g2.setCursor(18+addGradX, 58+addGradY);
        if(t1 > -10.0 && t1 < 0.0)     u8g2.setCursor(26+addGradX, 58+addGradY);
        if(t1 >= 0.0  && t1 < 10.0)    u8g2.setCursor(38+addGradX, 58+addGradY);
        if(t1 >= 10.0)                 u8g2.setCursor(30+addGradX, 58+addGradY);
        u8g2.print(c);
      } 
    }
    else
    {
      u8g2.setFont(u8g2_font_8x13_t_cyrillic);
      u8g2.setCursor(25,30);
      u8g2.print("нет обмена"); 
      u8g2.setCursor(25,46);
      u8g2.print("c RPI"); 
    }

    u8g2.sendBuffer();					// transfer internal memory to the display

    // Движение статических надписей
    addX = addX + offsetX;
    addY = addY + offsetY;
    if (addX < -4 || addX > 4) 
    { 
      offsetX = offsetX * -1;
      addX = addX + offsetX;
    }
    if (addY <= -1 || addY >= 1) 
    {
      offsetY = offsetY * -1;
      addY = addY + offsetY;
    }

    addGradX = addGradX + offsetGradX;
    addGradY = addGradY + offsetGradY;
    if (addGradX < -5 || addGradX > 5) 
    { 
      offsetGradX = offsetGradX * -1;
      addGradX = addGradX + offsetGradX;
    }
    if (addGradY <= -2 || addGradY >= 2) 
    {
      offsetGradY = offsetGradY * -1;
      addGradY = addGradY + offsetGradY;
    }

    delay(500);  
  }

  Serial.println("loop");
}





