
#include "hardware_func.h"
#include "esp_sleep.h"
#include "upload_wifi.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
 
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <sys/fcntl.h>
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "nvs_flash.h"

#define TCP_BUF_SIZE 1024

#define AP_SSID "ESP_32"
#define AP_PASSPHARSE "12345678"
#define AP_SSID_HIDDEN 0
#define AP_MAX_CONNECTIONS 4
#define AP_AUTHMODE WIFI_AUTH_WPA2_PSK // the passpharese should be atleast 8 chars long
#define AP_BEACON_INTERVAL 100 // in milli seconds
#define LISTENQ 2
#define MESSAGE "Closing channel!!"
static EventGroupHandle_t wifi_event_group;
const int CLIENT_CONNECTED_BIT = BIT0;
const int CLIENT_DISCONNECTED_BIT = BIT1;
const int AP_STARTED_BIT = BIT2;

void stop_blinking(void){
	if(blinkHandle != NULL){
		vTaskDelete(blinkHandle); // stop blinking
	}
}

void led_on(void){
	gpio_set_level(LED_PIN, 1); //make sure led is on
}

void led_off(void){
	gpio_set_level(LED_PIN, 0); //make sure led is on
}


int act_on_command(char * buffer){
	char local_buffer[TCP_BUF_SIZE];
	sprintf(local_buffer,"%s", buffer);
	size_t ls = strlen(local_buffer);


	if(ls == 1){
		switch(local_buffer[0]){
			case '0':
			printf("qntos arquivos?\n");
			init_sd();
			sprintf(buffer, "N%02d\n", sd_number_of_log_files());
			end_sd();
			return 0;
			break;

			case '1':
			printf("formate o cartao\n");
			init_sd();
			int deleted_files = sd_delete_all_log_files();
			printf("%d logs deletados \n",deleted_files);
			end_sd();
			sprintf(buffer, "EFOK\n", seconds_from_epoch(), micro_seconds_from_epoch());
			return 0;
			break;

			case '2':
			printf("deep sleep\n");
			vTaskDelay(pdMS_TO_TICKS(10));
			esp_deep_sleep_start();
			return 0; // LOL g.g
			break;

			case '3':
			printf("qual o horario armazenado?\n");
			sprintf(buffer, "T%ld,%ld\n", seconds_from_epoch(), micro_seconds_from_epoch());
			return 0;
			break;


			case '4':
			printf("qual a leitura do adc?\n");
			init_adc();
			sprintf(buffer, "A%d\n",adc_read());
			return 0;
			break;

			case '5':
			printf("fechar o canal tcp\n");
			sprintf(buffer, "CTOK\n");
			return 1;
			break;

			default:
			break;
		}
	}



	sprintf(buffer,"%s", "resposta padrao\n");
	return 0;
}






static const char *TAG="ap_mode_tcp_server";
static esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_AP_START:
		printf("Event:ESP32 is started in AP mode\n");
        xEventGroupSetBits(wifi_event_group, AP_STARTED_BIT);
        break;
		
	case SYSTEM_EVENT_AP_STACONNECTED:
		xEventGroupSetBits(wifi_event_group, CLIENT_CONNECTED_BIT);
		stop_blinking();
		led_on();
		break;

	case SYSTEM_EVENT_AP_STADISCONNECTED:
		xEventGroupSetBits(wifi_event_group, CLIENT_DISCONNECTED_BIT);
		break;		
    default:
        break;
    }
    return ESP_OK;
}

static void start_dhcp_server(){
    
    	// initialize the tcp stack
	    tcpip_adapter_init();
        // stop DHCP server
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_stop(TCPIP_ADAPTER_IF_AP));
        // assign a static IP to the network interface
        tcpip_adapter_ip_info_t info;
        memset(&info, 0, sizeof(info));
        IP4_ADDR(&info.ip, 192, 168, 1, 1);
        IP4_ADDR(&info.gw, 192, 168, 1, 1);//ESP acts as router, so gw addr will be its own addr
        IP4_ADDR(&info.netmask, 255, 255, 255, 0);
        ESP_ERROR_CHECK(tcpip_adapter_set_ip_info(TCPIP_ADAPTER_IF_AP, &info));
        // start the DHCP server   
        ESP_ERROR_CHECK(tcpip_adapter_dhcps_start(TCPIP_ADAPTER_IF_AP));
        printf("DHCP server started \n");
}
static void initialise_wifi_in_ap(void)
{
	nvs_flash_init();
    esp_log_level_set("wifi", ESP_LOG_NONE); // disable wifi driver logging
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );

    // configure the wifi connection and start the interface
	wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .password = AP_PASSPHARSE,
			.ssid_len = 0,
			.channel = 0,
			.authmode = AP_AUTHMODE,
			.ssid_hidden = AP_SSID_HIDDEN,
			.max_connection = AP_MAX_CONNECTIONS,
			.beacon_interval = AP_BEACON_INTERVAL,			
        },
    };
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK( esp_wifi_start() );
    printf("ESP WiFi started in AP mode \n");
}

void tcp_server(void *pvParam){
    ESP_LOGI(TAG,"tcp_server task started \n");
    struct sockaddr_in tcpServerAddr;
    tcpServerAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    tcpServerAddr.sin_family = AF_INET;
    tcpServerAddr.sin_port = htons( 3000 );
    int s, r;
    static char recv_buf[TCP_BUF_SIZE];
    static struct sockaddr_in remote_addr;
    static unsigned int socklen;
    socklen = sizeof(remote_addr);
    int cs;//client socket
    xEventGroupWaitBits(wifi_event_group,AP_STARTED_BIT,false,true,portMAX_DELAY);
    while(1){
        s = socket(AF_INET, SOCK_STREAM, 0);
        if(s < 0) {
            ESP_LOGE(TAG, "... Failed to allocate socket.\n");
            vTaskDelay(1000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... allocated socket\n");
         if(bind(s, (struct sockaddr *)&tcpServerAddr, sizeof(tcpServerAddr)) != 0) {
            ESP_LOGE(TAG, "... socket bind failed errno=%d \n", errno);
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        ESP_LOGI(TAG, "... socket bind done \n");
        if(listen (s, LISTENQ) != 0) {
            ESP_LOGE(TAG, "... socket listen failed errno=%d \n", errno);
            close(s);
            vTaskDelay(4000 / portTICK_PERIOD_MS);
            continue;
        }
        while(1){
            cs=accept(s,(struct sockaddr *)&remote_addr, &socklen);
            ESP_LOGI(TAG,"New connection request,Request data:");
            //set O_NONBLOCK so that recv will return, otherwise we need to impliment message end 
            //detection logic. If know the client message format you should instead impliment logic
            //detect the end of message
            //fcntl(cs,F_SETFL,O_NONBLOCK);
            do {
                bzero(recv_buf, sizeof(recv_buf));
                r = read(cs, recv_buf, sizeof(recv_buf)-1);     // r = recv(cs, recv_buf, sizeof(recv_buf)-1,0);
				recv_buf[r] = NULL;
				printf("incoming bytes: %s\n", recv_buf);
				if(act_on_command(recv_buf)){
					write(cs , recv_buf , strlen(recv_buf));
					break;
				}
				write(cs , recv_buf , strlen(recv_buf));
                //for(int i = 0; i < r; i++) {
                    //putchar(recv_buf[i]);
                //}
            } while(r > 0);
            
            ESP_LOGI(TAG, "... done reading from socket. Last read return=%d errno=%d\r\n", r, errno);
            
            ESP_LOGI(TAG, "... socket send success");
            close(cs);
        }
        ESP_LOGI(TAG, "... server will be opened in 5 seconds");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
    ESP_LOGI(TAG, "...tcp_client task closed\n");
}

// print the list of connected stations
void printStationList() 
{
	printf(" Connected stations:\n");
	printf("--------------------------------------------------\n");
	
	wifi_sta_list_t wifi_sta_list;
	tcpip_adapter_sta_list_t adapter_sta_list;
   
	memset(&wifi_sta_list, 0, sizeof(wifi_sta_list));
	memset(&adapter_sta_list, 0, sizeof(adapter_sta_list));
   
	ESP_ERROR_CHECK(esp_wifi_ap_get_sta_list(&wifi_sta_list));	
	ESP_ERROR_CHECK(tcpip_adapter_get_sta_list(&wifi_sta_list, &adapter_sta_list));
	
	for(int i = 0; i < adapter_sta_list.num; i++) {
		
		tcpip_adapter_sta_info_t station = adapter_sta_list.sta[i];
         printf("%d - mac: %.2x:%.2x:%.2x:%.2x:%.2x:%.2x - IP: %s\n", i + 1,
				station.mac[0], station.mac[1], station.mac[2],
				station.mac[3], station.mac[4], station.mac[5],
				ip4addr_ntoa(&(station.ip)));
	}
	
	printf("\n");
}

void print_sta_info(void *pvParam){
    printf("print_sta_info task started \n");
    while(1) {	
		EventBits_t staBits = xEventGroupWaitBits(wifi_event_group, CLIENT_CONNECTED_BIT | CLIENT_CONNECTED_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
		if((staBits & CLIENT_CONNECTED_BIT) != 0) printf("New station connected\n\n");
        else printf("A station disconnected\n\n");
        printStationList();
	}
}



void upload_wifi(void *ignore){
	printf("entrei no upload\n");
	ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_event_group = xEventGroupCreate();
	start_dhcp_server();
    initialise_wifi_in_ap();
    xTaskCreate(&tcp_server,"tcp_server",4096,NULL,5,NULL);
    xTaskCreate(&print_sta_info,"print_sta_info",4096,NULL,5,NULL);
}



