#include "main.h"
#include "stm32f30x.h"
#include "symbols.h"
#include "stdlib.h"
#include "string.h"
#include "time.h"
#include <stdio.h>

#ifdef __GNUC__
/* With GCC/RAISONANCE, small printf (option LD Linker->Libraries->Small printf
 set to 'Yes') calls __io_putchar() */
#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)
#else
#define PUTCHAR_PROTOTYPE int fputc(int ch, FILE *f)
#endif /* __GNUC__ */

#define LCD_SET_RESET_LINE()      GPIO_WriteBit(GPIOD, GPIO_Pin_10, Bit_RESET)
#define LCD_RESET_RESET_LINE()    GPIO_WriteBit(GPIOD, GPIO_Pin_10, Bit_SET)

#define LCD_READ_DATA()           GPIO_WriteBit(GPIOD, GPIO_Pin_11, Bit_SET)
#define LCD_WRITE_DATA()          GPIO_WriteBit(GPIOD, GPIO_Pin_11, Bit_RESET)

#define LCD_SEND_DATA()           GPIO_WriteBit(GPIOD, GPIO_Pin_12, Bit_SET)
#define LCD_SEND_COMMAND()        GPIO_WriteBit(GPIOD, GPIO_Pin_12, Bit_RESET)

#define LCD_SET_STROBE_LINE()     GPIO_WriteBit(GPIOD, GPIO_Pin_13, Bit_SET)
#define LCD_RESET_STROBE_LINE()   GPIO_WriteBit(GPIOD, GPIO_Pin_13, Bit_RESET)

#define LCD_CHOOSE_CRYSTAL_0()		GPIO_WriteBit(GPIOD, GPIO_Pin_8, Bit_SET); GPIO_WriteBit(GPIOD, GPIO_Pin_9, Bit_RESET)

#define LCD_CHOOSE_CRYSTAL_1()		GPIO_WriteBit(GPIOD, GPIO_Pin_8, Bit_RESET); GPIO_WriteBit(GPIOD, GPIO_Pin_9, Bit_SET)

static __IO uint32_t TimingDelay;

void DelayUs(__IO uint32_t nTime);
void DelayMs(__IO uint32_t nTime);
void TimingDelay_Decrement(void);

void enableClocks(void);
void setUpGPIO(void);
void setUpUsart(void);
void setUpRTC(void);

void waitForLCDReady( uint8_t);
void writeByteToLCD( uint8_t, uint8_t, uint8_t);
void initLCD(void);
void drawString( uint8_t, uint8_t, const volatile char*);
void drawWeatherImage( uint8_t, uint8_t, uint8_t);
void updateLCD(void);
void clearLCD(void);

void initWizFi220(char*[], uint8_t);
void callWeather(const char*, char*);

uint8_t convertIconNameToId( uint8_t);

int parseAndSetDateTime(const char*);
void parseAndSetWeather(int, const char*);

GPIO_InitTypeDef GPIO_InitStructure;
uint16_t dataLines[8] = { GPIO_Pin_0, GPIO_Pin_1, GPIO_Pin_2, GPIO_Pin_3,
		GPIO_Pin_4, GPIO_Pin_5, GPIO_Pin_6, GPIO_Pin_7 };

volatile uint8_t displayArray[8][128] = { 0x00 };

const uint8_t _wizFiCommandsNumber = 8;
const char *_wizFiCommands[_wizFiCommandsNumber] = { "AT\r", "ATE0\r",// Отключаем эхо команд
		"AT+WD\r",		          // Отключаемся от сетей
		"AT+NDHCP=1\r",     		// Включаем DHCP
		"AT+WPAPSK=AndroidAP,qwertyuiop\r", // Ключ WPA2
		"AT+WA=AndroidAP\r",    // Имя Wi-Fi точки
		"AT+NCLOSEALL\r", "AT+NCTCP=144.76.102.166,80\r", //IP-адрес openweathermap.org
		};

const uint8_t wizFiRecallWeatherCommandsNumber = 2;
const char *wizFiRecallWeatherCommands[wizFiRecallWeatherCommandsNumber] = {
		"AT+NCLOSEALL\r", "AT+NCTCP=144.76.102.166,80\r", //IP-адрес openweathermap.org
		};

const char *weatherRequest =
		"\x1BS0GET /data/2.5/forecast/daily?q=Moscow&units=metric&cnt=7 HTTP/1.1\n"
				"Host: openweathermap.org\n"
				"Connection: keep-alive\n"
				"\n\x1B"; // Обязательно пустая строка!

char weatherResponse[4096] = { '\0' };

const char *wizFiOK = "[OK]";
const char *wizFiError = "[ERROR]";

volatile char timeRTC[6];
uint8_t newTime = 0;

const uint8_t GMTOffset = 4;
const char *daysOfWeek[7] = { "Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun" };

int main(void) {

	if (SysTick_Config(SystemCoreClock / 100000)) {
		while (1)
			;
	}

	enableClocks();
	setUpGPIO();
	setUpUsart();
	DelayUs(10000);
	setUpRTC();
	initLCD();

	drawString(0, 56, "Please wait...");
	updateLCD();

	initWizFi220(_wizFiCommands, _wizFiCommandsNumber);
	callWeather(weatherRequest, weatherResponse);
	printf(weatherResponse);
	clearLCD();
	updateLCD();

	int dayId;
	dayId = parseAndSetDateTime(weatherResponse);
	parseAndSetWeather(dayId, weatherResponse);
	RTC_TimeTypeDef RTC_CurrentTime;
	RTC_GetTime(RTC_Format_BIN, &RTC_CurrentTime);
	sprintf(timeRTC, "%0.2d:%0.2d", RTC_CurrentTime.RTC_Hours,
			RTC_CurrentTime.RTC_Minutes);
	drawString(44, 0, timeRTC);
	updateLCD();
	uint8_t minutesCounter = 0;
	while (1) {
		if (newTime == 1) {
			newTime = 0;
			drawString(44, 0, timeRTC);
			updateLCD();
			minutesCounter++;
		}
		// Каждые 30 минут запрашиваем новую темепературу
		if (minutesCounter > 30) {
			minutesCounter = 0;
			//Вновь устанавливаем соединение
			initWizFi220(wizFiRecallWeatherCommands,
					wizFiRecallWeatherCommandsNumber);
			// Запрашиваем погоду и парсим ответ
			clearLCD();
			callWeather(weatherRequest, weatherResponse);
			dayId = parseAndSetDateTime(weatherResponse);
			parseAndSetWeather(dayId, weatherResponse);
			drawString(44, 0, timeRTC);
			updateLCD();
		}
	}
}

void enableClocks(void) {
	RCC_AHBPeriphClockCmd(
			RCC_AHBPeriph_GPIOD | RCC_AHBPeriph_GPIOE | RCC_AHBPeriph_GPIOA, ENABLE);

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR, ENABLE);
}

void setUpGPIO(void) {
	//Все используемые пины порта D - на выход
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0 | GPIO_Pin_1 | GPIO_Pin_2
			| GPIO_Pin_3 | GPIO_Pin_4 | GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7
			| GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10 | GPIO_Pin_11 | GPIO_Pin_12
			| GPIO_Pin_13;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	//Светодиоды на STM32F3DISCOVERY
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_9 | GPIO_Pin_10;
	GPIO_Init(GPIOE, &GPIO_InitStructure);

	/* USART1 */
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_7);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF_7);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9 | GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	/* USART2 */
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource2, GPIO_AF_7);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource3, GPIO_AF_7);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2 | GPIO_Pin_3;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
}

void setUpUsart(void) {
	USART_InitTypeDef USART_InitStructure;

	USART_DeInit(USART1);
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_HardwareFlowControl =
			USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
	USART_Init(USART1, &USART_InitStructure);

	// Отключаем проверку переполнения приемного буфера
	USART_OverrunDetectionConfig(USART1, USART_OVRDetection_Disable);

	/* USART enable */
	USART_Cmd(USART1, ENABLE);

	USART_DeInit(USART2);
	USART_Init(USART2, &USART_InitStructure);
	USART_Cmd(USART2, ENABLE);
}

void setUpRTC() {
	RTC_InitTypeDef RTC_InitStructure;
	RTC_TimeTypeDef RTC_TimeStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	EXTI_InitTypeDef EXTI_InitStructure;

	/* Allow access to RTC */
	PWR_BackupAccessCmd(ENABLE);

	/* LSI used as RTC source clock */
	/* The RTC Clock may varies due to LSI frequency dispersion. */
	/* Enable the LSI OSC */
	RCC_LSICmd(ENABLE);

	/* Wait till LSI is ready */
	while (RCC_GetFlagStatus(RCC_FLAG_LSIRDY) == RESET) {
	}

	/* Select the RTC Clock Source */
	RCC_RTCCLKConfig(RCC_RTCCLKSource_LSI);

	/* Enable the RTC Clock */
	RCC_RTCCLKCmd(ENABLE);

	/* Wait for RTC APB registers synchronisation */
	RTC_WaitForSynchro();

	/* Calendar Configuration */
	RTC_InitStructure.RTC_AsynchPrediv = 88;
	RTC_InitStructure.RTC_SynchPrediv = 470; /* (42KHz / 89) - 1 = 470 */
	RTC_InitStructure.RTC_HourFormat = RTC_HourFormat_24;
	RTC_Init(&RTC_InitStructure);

	/* Ставим время */
	RTC_TimeStructure.RTC_H12 = RTC_HourFormat_24;
	RTC_TimeStructure.RTC_Hours = 00;
	RTC_TimeStructure.RTC_Minutes = 00;
	RTC_TimeStructure.RTC_Seconds = 00;

	RTC_SetTime(RTC_Format_BIN, &RTC_TimeStructure);

	/* EXTI configuration *******************************************************/
	EXTI_ClearITPendingBit(EXTI_Line20);
	EXTI_InitStructure.EXTI_Line = EXTI_Line20;
	EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	/* Enable the RTC Wakeup Interrupt */
	NVIC_InitStructure.NVIC_IRQChannel = RTC_WKUP_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	/* Configure the RTC WakeUp Clock source: CK_SPRE (1Hz) */
	RTC_WakeUpClockConfig(RTC_WakeUpClock_CK_SPRE_16bits);
	RTC_SetWakeUpCounter(0x0);

	RTC_ClearITPendingBit(RTC_IT_WUT);
	/* Enable the RTC Wakeup Interrupt */
	RTC_ITConfig(RTC_IT_WUT, ENABLE);

	/* Enable Wakeup Counter */
	RTC_WakeUpCmd(ENABLE);

}

void waitForLCDReady(uint8_t crystalId) {

	// Пин PD7 -- на вход для чтения статуса кристалла
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7 | GPIO_Pin_5 | GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	LCD_READ_DATA();
	LCD_SEND_COMMAND();
	if (crystalId == 0) {
		LCD_CHOOSE_CRYSTAL_0()
		;
	} else {
		LCD_CHOOSE_CRYSTAL_1()
		;
	}
	DelayUs(1);
	LCD_SET_STROBE_LINE();
	DelayUs(1);

	// Ждем готовности
	while (GPIO_ReadInputDataBit(GPIOD, GPIO_Pin_7) == Bit_SET) {
		;
	}

	// Вновь меняем направление работы пина
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_7 | GPIO_Pin_5 | GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	LCD_RESET_STROBE_LINE();

}

void writeByteToLCD(uint8_t crystalId, uint8_t byteType, uint8_t byte) {
	static uint8_t mask = 1;
	waitForLCDReady(crystalId);
	LCD_WRITE_DATA();
	if (byteType == 0) {
		//Будем писать команду
		LCD_SEND_COMMAND();
	} else {
		//Или данные
		LCD_SEND_DATA();
	}

	// Выставляем байт
	static uint8_t i;
	for (i = 0; i < 8; ++i) {
		GPIO_WriteBit(GPIOD, dataLines[i],
				((byte & (mask << i)) != 0) ? Bit_SET : Bit_RESET);
	}

	DelayUs(1);
	LCD_SET_STROBE_LINE();
	DelayUs(1);
	LCD_RESET_STROBE_LINE();
	DelayUs(1);
}

void initLCD(void) {
	int crystalId = 0;
	LCD_RESET_STROBE_LINE();
	LCD_SET_RESET_LINE();
	DelayMs(1);
	LCD_RESET_RESET_LINE();
	DelayMs(1);
	for (crystalId = 0; crystalId < 2; ++crystalId) {
		writeByteToLCD(crystalId, 0, 0xC0);
		writeByteToLCD(crystalId, 0, 0x3F);
	}
}

void drawString(uint8_t x, uint8_t y, const volatile char *message) {
	uint8_t firstPageNumber = y / 8;
	uint8_t secondPageNumber = firstPageNumber + 1;
	uint8_t offset = y % 8;

	uint8_t LCDXValue = 0;

	uint8_t i = 0;
	uint8_t charInMessage = 0;
	while (message[i] != '\0') {
		for (charInMessage = 0; charInMessage < 8; charInMessage++) {
			LCDXValue = i * 8 + charInMessage + x;
			if ((firstPageNumber < 9) && (LCDXValue < 127)) {
				displayArray[firstPageNumber][LCDXValue] =
						asciiTable[message[i]][charInMessage] << offset;
			}
			if ((secondPageNumber < 9) && (offset != 0) && (LCDXValue < 127)) {
				displayArray[secondPageNumber][LCDXValue] =
						asciiTable[message[i]][charInMessage] >> (8 - offset);
			}
		}
		i++;
	}
}

void drawWeatherImage(uint8_t x, uint8_t y, uint8_t imageId) {
	uint8_t offset = y % 8;

	uint8_t rowPartId = 0;
	uint8_t colPartId = 0;

	uint8_t LCDXValue = 0;
	uint8_t byteInPage = 0;

	uint8_t firstPageNumber = 0;
	uint8_t secondPageNumber = 0;

	for (rowPartId = 0; rowPartId < 2; rowPartId++) {
		firstPageNumber = y / 8 + rowPartId;
		secondPageNumber = firstPageNumber + 1;
		for (colPartId = 0; colPartId < 2; colPartId++) {
			for (byteInPage = 0; byteInPage < 8; byteInPage++) {
				LCDXValue = colPartId * 8 + x + byteInPage;
				if ((firstPageNumber < 9) && (LCDXValue < 127)) {
					displayArray[firstPageNumber][LCDXValue] |=
							weatherImages[imageId][rowPartId * 2 + colPartId][byteInPage]
									<< offset;
				}
				if ((secondPageNumber < 9) && (offset != 0) && (LCDXValue < 127)) {
					displayArray[secondPageNumber][LCDXValue] =
							weatherImages[imageId][rowPartId * 2 + colPartId][byteInPage]
									>> (8 - offset);
				}
			}
		}
	}
}

void updateLCD(void) {
	uint8_t page = 0;
	uint8_t offset = 0;
	uint8_t crystalId = 0;
	uint8_t pixel = 0;
	for (page = 0; page < 8; ++page) {
		offset = 0;
		for (crystalId = 0; crystalId < 2; ++crystalId) {
			writeByteToLCD(crystalId, 0, page | 0xB8);
			writeByteToLCD(crystalId, 0, 0x40);

			for (pixel = offset; pixel < 64 + offset; ++pixel) {
				writeByteToLCD(crystalId, 1, displayArray[page][pixel]);
			}
			offset = 64;
		}
	}
}

void clearLCD(void) {
	uint16_t i = 0;
	uint16_t j = 0;
	for (i = 0; i < 8; ++i) {
		for (j = 0; j < 128; ++j) {
			displayArray[i][j] = 0;
		}
	}
}

void initWizFi220(char* wizFiCommands[], uint8_t wizFiCommandsNumber) {
	uint8_t commandId = 0;
	const uint8_t responseSize = 255;
	uint8_t response[responseSize] = { '\0' };
	uint16_t responseSymbolId = 0;
	uint32_t temp = 0;
	uint8_t useTimeout = 0;
	uint8_t repeatCommand = 0;
	uint8_t i;
	for (commandId = 0; commandId < wizFiCommandsNumber; commandId++) {

		responseSymbolId = 0;

		for (i = 0; i < responseSize; ++i) {
			response[i] = '\0';
		}

		while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) {
		}
		USART_SendData(USART2, '\r');
		while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) {
		}
		USART_SendData(USART2, '>');
		while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) {
		}

		//Финт ушами
		//USART_Cmd(USART1, DISABLE);
		//USART_Cmd(USART1, ENABLE);

		printf(wizFiCommands[commandId]);

		TimingDelay = 3000000;
		useTimeout = 1;
		repeatCommand = 0;

		do {
			// Ждем приема байта
			while ((USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == RESET)
					&& (TimingDelay != 0)) {
			}

			if ((TimingDelay == 0) && (useTimeout == 1)) {
				// Повтор команды
				commandId--;
				repeatCommand = 1;
			} else {

				temp = USART_ReceiveData(USART1);
				response[responseSymbolId] = (uint8_t) temp;
				USART_SendData(USART2, response[responseSymbolId]);
				responseSymbolId++;
			}
		} while ((response[responseSymbolId - 1] != ']') && (repeatCommand == 0));

		//Ответ получен
		if (repeatCommand == 0) {
			// Но там ошибка
			if (strstr(response, wizFiOK) == NULL) {
				commandId--;
			}
		}

	}
}

void callWeather(const char* request, char* response) {
	printf(request);
	printf("E");
	// Ожидаем, что за 4 секунды придет вся нужная информация
	const uint32_t timeout = 500000;
	uint32_t charCounter = 0;
	TimingDelay = timeout;
	while ((TimingDelay != 0) && (charCounter < 4096)) {
		if (USART_GetFlagStatus(USART1, USART_FLAG_RXNE) == SET) {
			*(response + charCounter) = USART_ReceiveData(USART1);
			charCounter++;
		}
	}
}

int parseAndSetDateTime(const char* weatherResponse) {
	RTC_TimeTypeDef RTC_TimeStructure;
	const char *dateToken = "Date:";

	char *tmp;
	tmp = strstr(weatherResponse, dateToken);
	char dateTime[32];
	int i = 0;
	while (tmp[i + 6] != '\n') {
		dateTime[i] = tmp[i + 6];
		++i;
	}
	char* time;
	// Получим строку с временем после четырех отсечений
	strtok(dateTime, " ");
	for (i = 0; i < 4; ++i) {
		time = strtok('\0', " ");
	}

	RTC_TimeStructure.RTC_H12 = RTC_HourFormat_24;

	RTC_TimeStructure.RTC_Hours = (atoi(strtok(time, ":")) + GMTOffset) % 24;
	RTC_TimeStructure.RTC_Minutes = atoi(strtok('\0', ":"));
	RTC_TimeStructure.RTC_Seconds = atoi(strtok('\0', ":"));

	RTC_SetTime(RTC_Format_BIN, &RTC_TimeStructure);

	// Берем дату и выводим на экран
	while (tmp[i + 6] != '\n') {
		dateTime[i] = tmp[i + 6];
		++i;
	}
	char onlyDate[20] = { '\0' };
	strncpy(onlyDate, dateTime, 16);
	drawString(0, 8, onlyDate);

	// Найдем название дня недели
	strtok(onlyDate, " ");
	int a = 0;
	for (i = 0; i < 7; ++i) {
		if (strstr(onlyDate, daysOfWeek[i]) != NULL) {
			a = i;
			break;
		}
	}
	return a;
}

void parseAndSetWeather(int dayId, const char* weatherResponse) {
	const char *dailyWeatherToken = "\"day\":";
	const char *dailyWeatherIconToken = "\"icon\":\"";
	char temp[32] = "";
	char displayWeather[17] = { '\0' };

	int a = 0;
	int i = 0;
	char *tmp = { '\0' };
	int temperatures[4] = { 0 };
	tmp = strstr(weatherResponse, dailyWeatherToken);
	for (a = 0; a < 4; ++a) {
		for (i = 0; i < 2; i++) {
			temp[i] = tmp[i + 6];
		}
		tmp++;
		i = 0;
		tmp = strstr(tmp, dailyWeatherToken);
		temperatures[a] = atoi(temp);
	}
	sprintf(displayWeather, "%3.0d %3.0d %3.0d %3.0d", temperatures[0],
			temperatures[1], temperatures[2], temperatures[3]);
	drawString(0, 32, displayWeather);

	char displayDaysOfWeek[17] = { '\0' };
	for (a = 0; a < 4; ++a) {
		strcat(displayDaysOfWeek, daysOfWeek[(dayId + a) % 7]);
		strcat(displayDaysOfWeek, " ");
	}
	drawString(0, 24, displayDaysOfWeek);

	// Аналогично поступаем для иконок с погодой
	const uint8_t iconXOffset = 8;
	const uint8_t iconXStep = 32;
	uint8_t iconNames[4] = { 0 };

	tmp = strstr(weatherResponse, dailyWeatherIconToken);
	for (a = 0; a < 4; ++a) {
		for (i = 0; i < 2; i++) {
			temp[i] = tmp[i + 8];
		}
		tmp++;
		i = 0;
		tmp = strstr(tmp, dailyWeatherIconToken);
		drawWeatherImage(iconXOffset + iconXStep * a, 40,
				convertIconNameToId(atoi(temp)));
		//iconNames[a] =
	}

	//char displayIcons[17] = {'\0'};
	//sprintf(displayIcons, "%3.0d %3.0d %3.0d %3.0d", iconNames[0], iconNames[1], iconNames[2], iconNames[3]);
	//drawString(0, 40, displayIcons);
}

uint8_t convertIconNameToId(uint8_t name) {
	uint8_t iconId = 0;
	switch (name) {
	case 1:
		iconId = 0;
		break;

	case 2:
		iconId = 1;
		break;

	case 3:
		iconId = 2;
		break;

	case 4:
		iconId = 3;
		break;

	case 9:
		iconId = 4;
		break;

	case 10:
		iconId = 5;
		break;

	case 11:
		iconId = 6;
		break;

	case 13:
		iconId = 7;
		break;

	case 50:
		iconId = 8;
		break;
	}
	return iconId;
}

void DelayMs(__IO uint32_t nTime) {
	TimingDelay = nTime * 100;
	while(TimingDelay != 0);
}

void DelayUs(__IO uint32_t nTime) {
	TimingDelay = nTime;
	while(TimingDelay != 0);
}

void TimingDelay_Decrement(void) {
	if (TimingDelay != 0x00) {
		TimingDelay--;
	}
}

PUTCHAR_PROTOTYPE {
	USART_SendData(USART1, (uint8_t) ch);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
	}
	USART_SendData(USART2, (uint8_t) ch);
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) {
	}

	return ch;
}
