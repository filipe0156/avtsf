#include "hardware_func.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//***************************************************2*******************************************************************

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//****************************************************3********************************************************************

#include <stdio.h>
#include <string.h>
#include <sys/unistd.h>
#include <sys/stat.h>
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "driver/sdspi_host.h"
#include "sdmmc_cmd.h"
#include <driver/adc.h>
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_sleep.h"

#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"

#include "sys/time.h"
#include "sdkconfig.h"

static const char *TAG = "example";


int init_led(void){
	gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.pin_bit_mask = GPIO_OUTPUT_PIN_SEL; 
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pull_up_en = 0;
    io_conf.pull_down_en = 0;
    esp_err_t code = gpio_config(&io_conf);
    if(code == ESP_OK){
    	return 0;
    }
    return 1;
}

int init_button(void){
	gpio_config_t io_conf;
    io_conf.intr_type = GPIO_PIN_INTR_DISABLE;
    io_conf.pin_bit_mask = GPIO_INPUT_PIN_SEL;
    //set as input mode    
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    esp_err_t code = gpio_config(&io_conf);
    if(code == ESP_OK){
    	return 0;
    }
    return 1;
}

int init_button_awake(void){
	esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_AUTO); //!< Keep power domain enabled in deep sleep, if it is needed by one of the wakeup options. Otherwise power it down.
    gpio_pullup_en(BUTTON_PIN);      // use pullup on GPIO
    gpio_pulldown_dis(BUTTON_PIN);       // not use pulldown on GPIO

    esp_err_t code = esp_sleep_enable_ext0_wakeup(BUTTON_PIN, 0); // Wake if GPIO is low
    if(code == ESP_OK){
    	return 0;
    }
    return 1;
}

int init_sd(void){
	ESP_LOGI(TAG, "Initializing SD card");

    ESP_LOGI(TAG, "Using SDMMC peripheral");
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // To use 1-line SD mode, uncomment the following line:
    host.flags = SDMMC_HOST_FLAG_1BIT;

    // This initializes the slot without card detect (CD) and write protect (WP) signals.
    // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();

    // GPIOs 15, 2, 4, 12, 13 should have external 10k pull-ups.
    // Internal pull-ups are not sufficient. However, enabling internal pull-ups
    // does make a difference some boards, so we do that here.
    gpio_set_pull_mode(15, GPIO_PULLUP_ONLY);   // CMD, needed in 4- and 1- line modes
    gpio_set_pull_mode(2, GPIO_PULLUP_ONLY);    // D0, needed in 4- and 1-line modes
    gpio_set_pull_mode(4, GPIO_PULLUP_ONLY);    // D1, needed in 4-line mode only
    gpio_set_pull_mode(12, GPIO_PULLUP_ONLY);   // D2, needed in 4-line mode only
    gpio_set_pull_mode(13, GPIO_PULLUP_ONLY);   // D3, needed in 4- and 1-line modes


    // Options for mounting the filesystem.
    // If format_if_mount_failed is set to true, SD card will be partitioned and
    // formatted in case when mounting fails.
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    sdmmc_card_t* card;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. ""If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            ESP_LOGE(TAG, "Failed to initialize the card (%s). ""Make sure SD card lines have pull-up resistors in place.", esp_err_to_name(ret));
        }
        return 1;
    }

    // Card has been initialized, print its properties
    sdmmc_card_print_info(stdout, card);
    return 0;
}



int init_adc(void){
	//**************************** ADC INIT *********************************************
    
    esp_err_t code = adc1_config_width(ADC_WIDTH_BIT_10);
    if(code != ESP_OK){
    	return 1;
    }
    code = adc1_config_channel_atten(ADC1_CHANNEL_6,ADC_ATTEN_DB_11);   //gpio34 3.9V full scale
    if(code != ESP_OK){
    	return 1;
    }

    //**************************** ADC TEST**********************************************
    #define NO_OF_SAMPLES   64
    uint32_t adc_reading = 0;
    for (int i = 0; i < NO_OF_SAMPLES; i++) {
        adc_reading += adc1_get_raw(ADC1_CHANNEL_6);
    }
    adc_reading /= NO_OF_SAMPLES;
    ESP_LOGI(TAG, "Valor adc: %d", adc_reading);
    return 0;
}

uint32_t adc_read(void){
	uint32_t read = 0;
	for(int i = 0;i<3;i++){
		read += adc1_get_raw(ADC1_CHANNEL_6);
	}
	read = read/3;
	return read;
}

void end_sd(void){
	esp_vfs_fat_sdmmc_unmount();
    ESP_LOGI(TAG, "Card unmounted");
}


FILE *sd_open_new_file_for_write(void){
	int i = 0;
    FILE* f;
    for (; i < MAX_LOG_FILES; i++)
    {
        memset(logFileName, 0, strlen(logFileName)); // Clear logFileName string
        // Set logFileName to "adclogXX.csv":
        sprintf(logFileName, "/sdcard/%s%d.%s", LOG_FILE_PREFIX, i, LOG_FILE_SUFFIX);
        f = fopen(logFileName, "r");
        if (f == NULL) // If a file doesn't exist
        {
            fclose(f);
            break; // Break out of this loop. We found our index
        }
        else // Otherwise:
        {
            fclose(f);
            printf(logFileName);
            printf(" exists\n"); // Print a debug statement
        }
    }
    printf("File name: ");
    printf(logFileName); // Debug print the file name
    printf("\n");
    ESP_LOGI(TAG, "Opening file");
    f = fopen(logFileName, "w+");
    if (f == NULL) 
    {
        ESP_LOGE(TAG, "Failed to open file for writing");
    }
    return f;
}


FILE *sd_open_existing_file_for_read(int file_number){
	memset(logFileName, 0, strlen(logFileName)); // Clear logFileName string
    // Set logFileName to "adclogXX.csv":
    sprintf(logFileName, "/sdcard/%s%d.%s", LOG_FILE_PREFIX, file_number-1, LOG_FILE_SUFFIX);
    
    FILE *fp;
    fp = fopen(logFileName, "r");
    if (fp == NULL){
        printf("Could not open file %s",logFileName);
    }
    return fp;
}

int sd_number_of_log_files(void){
	int i = 0;
    FILE* f;
    for (; i < MAX_LOG_FILES; i++)
    {
        memset(logFileName, 0, strlen(logFileName)); // Clear logFileName string
        // Set logFileName to "adclogXX.csv":
        sprintf(logFileName, "/sdcard/%s%d.%s", LOG_FILE_PREFIX, i, LOG_FILE_SUFFIX);
        f = fopen(logFileName, "r");
        if (f == NULL) // If a file doesn't exist
        {
            fclose(f);
            break; // Break out of this loop. We found our index
        }
        else // Otherwise:
        {
            fclose(f);
            printf(logFileName);
            printf(" exists\n"); // Print a debug statement
        }
    }
    return i;
}

int sd_delete_all_log_files(void){
	int i = 0;
	int files_removed = 0;
    for (; i < MAX_LOG_FILES; i++)
    {
        memset(logFileName, 0, strlen(logFileName)); // Clear logFileName string
        // Set logFileName to "adclogXX.csv":
        sprintf(logFileName, "/sdcard/%s%d.%s", LOG_FILE_PREFIX, i, LOG_FILE_SUFFIX);
        if(remove(logFileName) == 0){
        	files_removed++;
        }
    }
    return files_removed;
}

void blink_task(void *pvParameter){
    init_button();
    while(1){
        gpio_set_level(LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_PERIOD_MS));
        gpio_set_level(LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_PERIOD_MS));

    }

}

void blink_slow_task(void *pvParameter){
	init_button();
	int pairing_time_counter = 0;
    while(1){
        gpio_set_level(LED_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(LED_BLINK_PERIOD_MS));
        gpio_set_level(LED_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(1000 - LED_BLINK_PERIOD_MS));
        pairing_time_counter++;
        if(pairing_time_counter > MAX_PAIRING_TIME_SEC){
            printf("deep sleep\n");
            esp_deep_sleep_start();
        }
    }

}

long seconds_from_epoch(void){
	struct timeval now;
	gettimeofday(&now, NULL);
	long s = (long)now.tv_sec;
	return s;
}

long micro_seconds_from_epoch(void){
	struct timeval now;
	gettimeofday(&now, NULL);
	long s = (long)now.tv_usec;
	return s;
}

void set_epoch_time(long sec, long usec){
	struct timeval now;
	now.tv_sec = (time_t)sec;
	now.tv_usec = (suseconds_t)usec;
	settimeofday(&now, NULL);
}