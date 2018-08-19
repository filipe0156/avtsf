#ifndef __HARDWARE_FUNC_H
#define __HARDWARE_FUNC_H

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define SAMPLING_PERIOD_MS 0.2  //adc sampling period
#define TWO_CLICKS_INTERVAL_MS 500
#define ONE_CLICK_INTERVAL_MS 2000
#define LED_BLINK_PERIOD_MS 50
#define BUTTON_STOP_REC_TIME_MS 1000
#define MAX_LOG_TIME_SEC 60
#define END_PERIOD_MS 2000
#define NUM_SAMPLES_TILL_STOP BUTTON_STOP_REC_TIME_MS/SAMPLING_PERIOD_MS
#define MAX_NUM_SAMPLES (MAX_LOG_TIME_SEC*1000)/SAMPLING_PERIOD_MS
#define GPIO_DEEP_SLEEP_IDLE_SEC     60
#define NO_BLOCKS_TO_SAVE 100
#define INITIAL_DELAY_MS 2000  //record delay
#define MAX_PAIRING_TIME_SEC  120

#define LOG_FILE_PREFIX "adclog" // Name of the log file.
#define MAX_LOG_FILES 99 // Number of log files that can be made
#define LOG_FILE_SUFFIX "csv" // Suffix of the log file
char logFileName[13]; // Char string to store the log file name

#define BUTTON_PIN    0
#define GPIO_INPUT_PIN_SEL  (1ULL<<BUTTON_PIN)
#define LED_PIN      19
#define GPIO_OUTPUT_PIN_SEL  (1ULL<<LED_PIN)

#define TIMER_DIVIDER         16  //  Hardware timer clock divider
#define TIMER_SCALE           (TIMER_BASE_CLK / TIMER_DIVIDER)  // convert counter value to seconds
#define TIMER_INTERVAL1_SEC   (SAMPLING_PERIOD_MS/1000)   // sample test interval for the second timer
#define TEST_WITHOUT_RELOAD   0        // testing will be done without auto reload
#define TEST_WITH_RELOAD      1        // testing will be done with auto reload


int init_led(void);
int init_button(void);
int init_button_awake(void);
int init_sd(void);  //mount card
void end_sd(void);  //unmount card
int init_adc(void);
uint32_t adc_read(void);

FILE *sd_open_new_file_for_write(void); //create a new file and returns it's file pointer
FILE *sd_open_existing_file_for_read(int file_number); //open file number 'file_number' and return it's file pointer
int sd_number_of_log_files(void);  //return the number of log files in the card
int sd_delete_all_log_files(void); //delete all log files, return number of deleted files

void blink_task(void *pvParameter);
void blink_slow_task(void *pvParameter);

long seconds_from_epoch(void);
long micro_seconds_from_epoch(void);

void set_epoch_time(long sec, long usec);

extern TaskHandle_t blinkHandle;


#endif