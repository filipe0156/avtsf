/*
 * Copyright (C) 2016 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at
 * contact@bluekitchen-gmbh.com
 *
 
 Joao
 */

#define __BTSTACK_FILE__ "main.c"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "soc/timer_group_struct.h"
#include "driver/periph_ctrl.h"
#include "driver/timer.h"
#include "esp_types.h"
//**********************************************NEW***********************************************************************************
#include "hardware_func.h"
#include "upload_wifi.h"
//**********************************************END NEW***********************************************************************************

//**********************************************NEW***********************************************************************************

TaskHandle_t blinkHandle = NULL;




//*********************************** ADC INTERRUPT ********************************

int test_counter = 0;
xQueueHandle adc_queue;

void IRAM_ATTR timer_group0_isr(void *para)
{
    int timer_idx = (int) para;

    /* Retrieve the interrupt status and the counter value
       from the timer that reported the interrupt */
    uint32_t intr_status = TIMERG0.int_st_timers.val;
    TIMERG0.hw_timer[timer_idx].update = 1;


    if ((intr_status & BIT(timer_idx)) && timer_idx == TIMER_1) {
        TIMERG0.int_clr_timers.t1 = 1;
    }
    /* After the alarm has been triggered
      we need enable it again, so it is triggered the next time */
    TIMERG0.hw_timer[timer_idx].config.alarm_en = TIMER_ALARM_EN;

    /* Now just send the event data back to the main program task */
    uint32_t adc_reading = adc_read();
    xQueueSendFromISR(adc_queue, &adc_reading, NULL);
    test_counter++;
}

static void example_tg0_timer_init(int timer_idx, 
    bool auto_reload, double timer_interval_sec)
{
    /* Select and initialize basic parameters of the timer */
    timer_config_t config;
    config.divider = TIMER_DIVIDER;
    config.counter_dir = TIMER_COUNT_UP;
    config.counter_en = TIMER_PAUSE;
    config.alarm_en = TIMER_ALARM_EN;
    config.intr_type = TIMER_INTR_LEVEL;
    config.auto_reload = auto_reload;
    timer_init(TIMER_GROUP_0, timer_idx, &config);

    /* Timer's counter will initially start from value below.
       Also, if auto_reload is set, this value will be automatically reload on alarm */
    timer_set_counter_value(TIMER_GROUP_0, timer_idx, 0x00000000ULL);

    /* Configure the alarm value and the interrupt on alarm. */
    timer_set_alarm_value(TIMER_GROUP_0, timer_idx, timer_interval_sec * TIMER_SCALE);
    timer_enable_intr(TIMER_GROUP_0, timer_idx);
    timer_isr_register(TIMER_GROUP_0, timer_idx, timer_group0_isr, 
        (void *) timer_idx, ESP_INTR_FLAG_IRAM, NULL);

    timer_start(TIMER_GROUP_0, timer_idx);
}

//*********************************MAIN *****************************************************

// main
int app_main()
{
    if(init_led()){
        printf("led error \n");
    }
    if(init_button()){
        printf("button error \n");
    }
    if(init_button_awake()){
        printf("led error \n");
    }

    //************************* START-UP LOGIC *******************************


    int button_current = 0, button_previous = 0;
    int64_t rise_time = 0, fall_time = 0;
    int amostragem = 0, upload = 0;

    while(1){
        button_current = gpio_get_level(BUTTON_PIN);
        if (button_current == 0 && button_previous == 1){   //button press
            vTaskDelay(pdMS_TO_TICKS(20)); //debounce
            if(esp_timer_get_time() - rise_time < TWO_CLICKS_INTERVAL_MS*1000){
                 printf("******************amostragem\n");
                amostragem = 1;
                break;
            }
            rise_time = esp_timer_get_time();
        }
        if (button_current == 1 && button_previous == 0){  //button release
            vTaskDelay(pdMS_TO_TICKS(20)); //debounce
            fall_time = esp_timer_get_time();
            if(fall_time - rise_time > ONE_CLICK_INTERVAL_MS*1000){
                printf("********************upload\n");
                upload = 1;
                break;
            }
        }
        button_previous = button_current;
        if( esp_timer_get_time() - rise_time > GPIO_DEEP_SLEEP_IDLE_SEC*1000*1000) //idle timeout
        {
            printf("deep sleep\n");
            esp_deep_sleep_start();
        }
    }


    if(amostragem == 1 )
    {
        init_adc();
        init_sd();
        FILE* f = sd_open_new_file_for_write();
        if (f == NULL) 
        {
            printf("Failed to open file for writing\n");
        }else{
            TaskHandle_t xHandle;
            xTaskCreate(blink_task, "blink_task", 2048, NULL, 2, &xHandle); // start blinking
            fprintf(f, "%ld,%ld\n",seconds_from_epoch(), micro_seconds_from_epoch());

            uint32_t adc_local = 0;
            int cycle_counter = 0;
            uint64_t saved_samples = 0;

            test_counter = 0;
            adc_queue = xQueueCreate(100, sizeof(uint32_t));
            example_tg0_timer_init(TIMER_1, TEST_WITH_RELOAD,   TIMER_INTERVAL1_SEC);
            
            while(1)
            {
               
                xQueueReceive(adc_queue, &adc_local, portMAX_DELAY);
                fprintf(f,"%d\n", adc_local);
                saved_samples++;
                //printf("logged %d,%d\n",time_local,adc_local);

                
                if(gpio_get_level(BUTTON_PIN) == 0)
                {
                    cycle_counter++;
                } else {
                    cycle_counter--;
                }
                if(cycle_counter < 0)
                {
                    cycle_counter = 0;
                }
                if(cycle_counter > BUTTON_STOP_REC_TIME_MS*5)
                {
                    break;
                }
                if(saved_samples > MAX_NUM_SAMPLES)
                {
                    break;
                }
                //vTaskDelay(pdMS_TO_TICKS(SAMPLING_PERIOD_MS));
            }

            vTaskDelete(xHandle); // stop blinking
            gpio_set_level(LED_PIN, 0); //make sure led is off
            
            fclose(f);
            end_sd();
            printf("test_counter: %d\n",test_counter);
            vTaskDelay(pdMS_TO_TICKS(END_PERIOD_MS));
            printf("deep sleep\n");
            esp_deep_sleep_start();

        }

    }
    else if (upload == 1)
    {
        printf("bora fazer o upload\n");

        xTaskCreate(blink_slow_task, "blink_slow_task", 2048, NULL, 2, &blinkHandle); // start blinking
		//xTaskCreate(upload_wifi, "upload_wifi", 2048*4, NULL, 2, NULL); // start blinking
		upload_wifi(NULL);

    }
    else
    {
        vTaskDelay(pdMS_TO_TICKS(END_PERIOD_MS));
        printf("********************invalida\n");
    }
    return 0;
}