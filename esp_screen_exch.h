#ifndef ESP_SCREEN_EXCH_H
#define ESP_SCREEN_EXCH_H

#include <cstdint>

struct RPI_ESP_SCREEN
{
	uint16_t cnt;		// Счетчик
	int16_t  t1;		// Температура от ESP Balcon
	uint16_t crc;
};

struct ESP_SCREEN_RPI
{
	uint16_t cnt;		// Счетчик
	int16_t  rssi;		// Уровень сигнала WiFi
	uint16_t button1;	// Кнопка 1
	uint16_t crc;
};


#endif // ESP_SCREEN_EXCH_H
