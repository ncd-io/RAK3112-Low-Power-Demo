#include <Arduino.h>
#include <SX126x-Arduino.h>
#include <SPI.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>

// LoRa definitions
#define RF_FREQUENCY 915600000  // Hz
#define TX_OUTPUT_POWER 22      // dBm
#define LORA_BANDWIDTH 0        // [0: 125 kHz, 1: 250 kHz, 2: 500 kHz, 3: Reserved]
#define LORA_SPREADING_FACTOR 7 // [SF7..SF12]
#define LORA_CODINGRATE 1       // [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8]
#define LORA_PREAMBLE_LENGTH 8  // Same for Tx and Rx
#define LORA_FIX_LENGTH_PAYLOAD_ON false
#define LORA_IQ_INVERSION_ON false
#define TX_TIMEOUT_VALUE 5000

// LoRa pin configuration(Connections between ESP32 and SX1262 inside the RAK3112 module)
#define LORA_RESET_PIN 8
#define LORA_DIO_1_PIN 47
#define LORA_BUSY_PIN 48
#define LORA_NSS_PIN 7
#define LORA_SCLK_PIN 5
#define LORA_MISO_PIN 3
#define LORA_MOSI_PIN 6
#define LORA_TXEN_PIN -1
#define LORA_RXEN_PIN -1

// Sleep duration: 5 seconds
#define SLEEP_SECONDS 5
#define uS_TO_S_FACTOR 1000000

// LoRa callback events
static RadioEvents_t RadioEvents;

// Message to transmit
const char* message = "HELLO WORLD";

// Flag to indicate we should go to sleep (set by callbacks)
volatile bool shouldSleep = false;

// Function declarations
void OnTxDone(void);
void OnTxTimeout(void);
void goToSleep(void);

void setup()
{
	// Release RTC GPIO hold from previous sleep cycle (if any)
	rtc_gpio_hold_dis((gpio_num_t)LORA_NSS_PIN);

	// Initialize serial for debugging
	Serial1.begin(115200, SERIAL_8N1, PIN_SERIAL1_RX, PIN_SERIAL1_TX);
	Serial1.println("\nRAK3112 Deep Sleep Demo");
	Serial1.printf("Frequency: %.1f MHz\n", (double)(RF_FREQUENCY/1000000.0));

	// Define the HW configuration between MCU and SX126x
	hw_config hwConfig;
	hwConfig.CHIP_TYPE = SX1262_CHIP;
	hwConfig.PIN_LORA_RESET = LORA_RESET_PIN;
	hwConfig.PIN_LORA_NSS = LORA_NSS_PIN;
	hwConfig.PIN_LORA_SCLK = LORA_SCLK_PIN;
	hwConfig.PIN_LORA_MISO = LORA_MISO_PIN;
	hwConfig.PIN_LORA_DIO_1 = LORA_DIO_1_PIN;
	hwConfig.PIN_LORA_BUSY = LORA_BUSY_PIN;
	hwConfig.PIN_LORA_MOSI = LORA_MOSI_PIN;
	hwConfig.RADIO_TXEN = LORA_TXEN_PIN;
	hwConfig.RADIO_RXEN = LORA_RXEN_PIN;
	hwConfig.USE_DIO2_ANT_SWITCH = true;
	hwConfig.USE_DIO3_TCXO = true;
	hwConfig.USE_DIO3_ANT_SWITCH = false;

	if (lora_hardware_init(hwConfig) != 0) {
		Serial1.println("Error in hardware init");
		return;
	}

	// Initialize the callbacks
	RadioEvents.TxDone = OnTxDone;
	RadioEvents.TxTimeout = OnTxTimeout;
	Radio.Init(&RadioEvents);

	// Configure LoRa radio
	Radio.Standby();
	Radio.SetChannel(RF_FREQUENCY);
	Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, 0, LORA_BANDWIDTH,
					  LORA_SPREADING_FACTOR, LORA_CODINGRATE,
					  LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
					  true, 0, 0, LORA_IQ_INVERSION_ON, TX_TIMEOUT_VALUE);
	
	// Transmit message on wake
	Serial1.printf("Transmitting: %s\n", message);
	Radio.Send((uint8_t*)message, strlen(message));
	Serial1.println("Transmission started, waiting for callback...");
}

void loop()
{
	// Wait for transmission to complete, then sleep
	if (shouldSleep) {
		goToSleep();
	}
}

/**
 * Put device into deep sleep mode
 */
void goToSleep(void)
{
	Serial1.println("Going to sleep...");
	
	// Put LoRa module to sleep
	Radio.Standby();
	Radio.Sleep();
	
	// Flush and disable Serial
	Serial1.flush();
	Serial1.end();
	
	// Disable SPI
	SPI.end();
	
	// Hold NSS pin HIGH during deep sleep
	pinMode(LORA_NSS_PIN, OUTPUT);
	digitalWrite(LORA_NSS_PIN, HIGH);
	rtc_gpio_hold_en((gpio_num_t)LORA_NSS_PIN);
	
	// Configure timer wakeup for 5 seconds
	esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * uS_TO_S_FACTOR);
	
	// Enter deep sleep
	esp_deep_sleep_start();
}

// LoRa transmit success callback
void OnTxDone(void)
{
	Serial1.println("Transmit finished");
	shouldSleep = true;
}

// LoRa transmit timeout callback
void OnTxTimeout(void)
{
	Serial1.println("Transmit timeout");
	shouldSleep = true;
}