#include "driver/gpio.h"
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "driver/twai.h"
#include <string.h>

typedef enum
{
    TX_SEND_TESTER,
    TX_SEND_CMT,
    TX_SEND_DATA
} tx_task_action_t;

typedef enum
{
    RX_RECEIVE_TESTER_RESP,
    RX_RECEIVE_DATA,
    RX_RECEIVE_STOP_RESP,
} rx_task_action_t;

#define PING_PERIOD_MS 250
#define NO_OF_DATA_MSGS 50
#define NO_OF_ITERS 3
#define ITER_DELAY_MS 1000
#define RX_TASK_PRIO 8
#define TX_TASK_PRIO 9
#define CTRL_TSK_PRIO 10
#define TX_GPIO_NUM 21
#define RX_GPIO_NUM 22
#define EXAMPLE_TAG "CAN Adapter"

#define ID_ADAPTER_TESTER_MSG 0x03E
#define ID_SLAVE_DATA 0x0B1
#define ID_SLAVE_TESTER_RESP 0x07E
#define ID_MASTER_START_CMD 0x0A0

static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_25KBITS();
static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();
static const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);

/* static const twai_message_t ping_message = {.identifier = ID_MASTER_PING, .data_length_code = 0, .ss = 1, .data = {0, 0, 0, 0, 0, 0, 0, 0}};
static const twai_message_t start_message = {.identifier = ID_MASTER_START_CMD, .data_length_code = 2, .data = {0, 0, 0, 0, 0, 0, 0, 0}};
static const twai_message_t stop_message = {.identifier = ID_MASTER_STOP_CMD, .data_length_code = 0, .data = {0, 0, 0, 0, 0, 0, 0, 0}}; */

static QueueHandle_t tx_task_queue;
static QueueHandle_t rx_task_queue;
static SemaphoreHandle_t stop_ping_sem;
static SemaphoreHandle_t ctrl_task_sem;
static SemaphoreHandle_t done_sem;

void send(char msg[], char id, int isExtd)
{
    /* twai_message_t message;
    // char msg[] = "3E";
    if (isExtd == 1)
        message.extd = 1;
    else
        message.extd = 0;
    message.data_length_code = strlen(msg);
    // char buff[80];
    // sprintf(buff, "0x%02x", 62);
    // printf(buff);
    char tmp_msg = "";
    int x = 0;
    for (int i = 0; i < strlen(msg); i++)
    {
        tmp_msg = msg[i];
        x = (int)(tmp_msg);
        printf("%c \n", tmp_msg);
        printf("%d \n", x);
        message.data[i] = x;
    }

    if (twai_transmit(&message, portMAX_DELAY) == ESP_OK)
    {
        printf("Message queued for transmission\n");
    }
    else
    {
        printf("Failed to queue message for transmission\n");
    }
    uint32_t alerts_to_enable = TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF;
    if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK)
    {
        printf("Alerts reconfigured\n");
    }
    else
    {
        printf("Failed to reconfigure alerts");
    }

    // Block indefinitely until an alert occurs
    uint32_t alerts_triggered;
    twai_read_alerts(&alerts_triggered, portMAX_DELAY); */
}

void receive(twai_message_t message)
{
    // Process received message
    if (message.extd)
    {
        printf("Message is in Extended Format\n");
    }
    else
    {
        printf("Message is in Standard Format\n");
    }
    printf("ID is %d\n", message.identifier);
    char RecieveMsg[] = "";
    if (!(message.rtr))
    {
        for (int i = 0; i < message.data_length_code; i++)
        {
            RecieveMsg[i] = (char)message.data[i];
            // printf("Data byte %d = %c\n", i, (char)message.data[i]);
        }
        printf("Present : %s ", RecieveMsg);
    }
}

static void twai_receive_task(void *arg)
{
    while (1)
    {
        rx_task_action_t action;
        xQueueReceive(rx_task_queue, &action, portMAX_DELAY);

        if (action == RX_RECEIVE_TESTER_RESP)
        {
            // Listen for ping response from slave
            while (1)
            {
                twai_message_t rx_msg;
                twai_receive(&rx_msg, portMAX_DELAY);
                // printf("Tester Response Recieved");
                // if (rx_msg.identifier == ID_SLAVE_TESTER_RESP)
                //{
                receive(rx_msg);

                // xSemaphoreGive(stop_ping_sem);
                // xSemaphoreGive(ctrl_task_sem);
                // break;
                // }
            }
        }
        else if (action == RX_RECEIVE_DATA)
        {
            // Receive data messages from slave
            uint32_t data_msgs_rec = 0;
            while (data_msgs_rec < NO_OF_DATA_MSGS)
            {
                twai_message_t rx_msg;
                // twai_receive(&rx_msg, portMAX_DELAY);
                if (twai_receive(&rx_msg, portMAX_DELAY) == ESP_OK)
                {
                    printf("Message received\n");
                }

                // if (rx_msg.identifier == ID_SLAVE_TESTER_RESP)
                //{
                receive(rx_msg);
                /* uint32_t data = 0;
                for (int i = 0; i < rx_msg.data_length_code; i++)
                {
                    data |= (rx_msg.data[i] << (i * 8));
                }
                ESP_LOGI(EXAMPLE_TAG, "Received data value %" PRIu32, data);
                data_msgs_rec++; */
                //}
            }
            xSemaphoreGive(ctrl_task_sem);
        }
    }
    vTaskDelete(NULL);
}

static void twai_transmit_task(void *arg)
{
    while (1)
    {
        ESP_LOGI(EXAMPLE_TAG, "Transmiting ");
        tx_task_action_t action;
        xQueueReceive(tx_task_queue, &action, portMAX_DELAY);

        if (action == TX_SEND_TESTER)
        {
            // Repeatedly transmit pings
            ESP_LOGI(EXAMPLE_TAG, "Transmitting Tester");
            while (xSemaphoreTake(stop_ping_sem, 0) != pdTRUE)
            {
                // send("3E", "0xAAAA", 1);
                twai_message_t message;
                char msg[] = "3E";
                message.identifier = "3E";
                message.extd = 1;
                message.data_length_code = strlen(msg);
                // char buff[80];
                // sprintf(buff, "0x%02x", 62);
                // printf(buff);
                char tmp_msg = "";
                int x = 0;
                for (int i = 0; i < strlen(msg); i++)
                {
                    tmp_msg = msg[i];
                    x = (int)(tmp_msg);
                    printf("%c \n", tmp_msg);
                    printf("%d \n", x);
                    message.data[i] = x;
                }

                if (twai_transmit(&message, portMAX_DELAY) == ESP_OK)
                {
                    printf("Message queued for transmission\n");
                }
                else
                {
                    printf("Failed to queue message for transmission\n");
                }

                // twai_transmit(&ping_message, portMAX_DELAY);
                vTaskDelay(pdMS_TO_TICKS(PING_PERIOD_MS));
            }
        }
        else if (action == TX_SEND_CMT)
        {
            // Transmit start command to slave
            send("A0", 0xAAAA, 1);
            // twai_transmit(&start_message, portMAX_DELAY);
            ESP_LOGI(EXAMPLE_TAG, "Transmitted start command");
        }
    }
    vTaskDelete(NULL);
}

static void twai_control_task(void *arg)
{
    xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);
    tx_task_action_t tx_action;
    rx_task_action_t rx_action;

    for (int iter = 0; iter < NO_OF_ITERS; iter++)
    {
        ESP_ERROR_CHECK(twai_start());
        ESP_LOGI(EXAMPLE_TAG, "Driver started");

        // Start transmitting pings, and listen for ping response

        tx_action = TX_SEND_TESTER;
        rx_action = RX_RECEIVE_TESTER_RESP;
        xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
        xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY);

        // Send Start command to slave, and receive data messages
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);
        tx_action = TX_SEND_DATA;
        rx_action = RX_RECEIVE_DATA;
        xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
        xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY);

        // Send Stop command to slave when enough data messages have been received. Wait for stop response
        /* xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);
        tx_action = TX_SEND_STOP_CMD;
        rx_action = RX_RECEIVE_STOP_RESP;
        xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
        xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY); */

        // xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);
        /* ESP_ERROR_CHECK(twai_stop());
        ESP_LOGI(EXAMPLE_TAG, "Driver stopped"); */
        vTaskDelay(pdMS_TO_TICKS(ITER_DELAY_MS));
    }
    // Stop TX and RX tasks
    /* tx_action = TX_TASK_EXIT;
    rx_action = RX_TASK_EXIT;
    xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
    xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY); */

    // Delete Control task
    // xSemaphoreGive(done_sem);
    vTaskDelete(NULL);
}

void app_main(void)
{
    // Create tasks, queues, and semaphores
    rx_task_queue = xQueueCreate(1, sizeof(rx_task_action_t));
    tx_task_queue = xQueueCreate(1, sizeof(tx_task_action_t));
    ctrl_task_sem = xSemaphoreCreateBinary();
    stop_ping_sem = xSemaphoreCreateBinary();
    done_sem = xSemaphoreCreateBinary();

    // Install TWAI driver
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(EXAMPLE_TAG, "Driver installed");

    xTaskCreatePinnedToCore(twai_receive_task, "TWAI_rx", 4096, NULL, RX_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(twai_transmit_task, "TWAI_tx", 4096, NULL, TX_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(twai_control_task, "TWAI_ctrl", 4096, NULL, CTRL_TSK_PRIO, NULL, tskNO_AFFINITY);

    xSemaphoreGive(ctrl_task_sem);           // Start control task
    xSemaphoreTake(done_sem, portMAX_DELAY); // Wait for completion

    // Uninstall TWAI driver
    ESP_ERROR_CHECK(twai_driver_uninstall());
    ESP_LOGI(EXAMPLE_TAG, "Driver uninstalled");

    // Cleanup
    /* vQueueDelete(rx_task_queue);
    vQueueDelete(tx_task_queue);
    vSemaphoreDelete(ctrl_task_sem);
    vSemaphoreDelete(stop_ping_sem);
    vSemaphoreDelete(done_sem); */
}

/* void app_main()
{
    // Initialize configuration structures using macro initializers
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(21, 22, TWAI_MODE_NORMAL);
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_25KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    // Install TWAI driver
    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK)
    {
        printf("Driver installed\n");
    }
    else
    {
        printf("Failed to install driver\n");
        return;
    }

    // Start TWAI driver
    if (twai_start() == ESP_OK)
    {
        printf("Driver started\n");
        twai_message_t message;
        // int msg[] = {6, 2};
        char msg[] = "3E";
        message.identifier = 0xAAAA;
        message.extd = 1;
        message.data_length_code = strlen(msg);
        char buff[80];
        sprintf(buff, "0x%02x", 62);
        printf(buff);
        char tmp_msg = "";
        int x = 0;
        for (int i = 0; i < strlen(msg); i++)
        {
            tmp_msg = msg[i];
            x = (int)(tmp_msg);
            // sscanf(tmp_msg, "%d", &x);

            printf("%c \n", tmp_msg);
            printf("%d \n", x);
            message.data[i] = x;
        }

        // Queue message for transmission
        if (twai_transmit(&message, portMAX_DELAY) == ESP_OK)
        {
            printf("Message queued for transmission\n");
        }
        else
        {
            printf("Failed to queue message for transmission\n");
        }
        uint32_t alerts_to_enable = TWAI_ALERT_ERR_PASS | TWAI_ALERT_BUS_OFF;
        if (twai_reconfigure_alerts(alerts_to_enable, NULL) == ESP_OK)
        {
            printf("Alerts reconfigured\n");
        }
        else
        {
            printf("Failed to reconfigure alerts");
        }

        // Block indefinitely until an alert occurs
        uint32_t alerts_triggered;
        twai_read_alerts(&alerts_triggered, portMAX_DELAY);
    }
    else
    {
        printf("Failed to start driver\n");
        return;
    }
} */