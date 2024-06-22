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

#define DATA_PERIOD_MS 50
#define NO_OF_ITERS 3
#define ITER_DELAY_MS 1000
#define RX_TASK_PRIO 8   // Receiving task priority
#define TX_TASK_PRIO 9   // Sending task priority
#define CTRL_TSK_PRIO 10 // Control task priority
#define TX_GPIO_NUM 21
#define RX_GPIO_NUM 22
#define EXAMPLE_TAG "CAN ECU"

#define ID_MASTER_START_CMD 0x0A0
#define ID_ADAPTER_TESTER 0x03E
#define ID_SLAVE_DATA 0x0B1
#define ID_SLAVE_TESTER_RESP 0x07E

typedef enum
{
    TX_SEND_TESTER_RESP,
    TX_SEND_DATA,
} tx_task_action_t;

typedef enum
{
    RX_RECEIVE_TESTER,
    RX_RECEIVE_START_CMD,
    RX_RECEIVE_DATA
} rx_task_action_t;

static const twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(TX_GPIO_NUM, RX_GPIO_NUM, TWAI_MODE_NORMAL);
static const twai_timing_config_t t_config = TWAI_TIMING_CONFIG_25KBITS();
static const twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

/* static const twai_message_t ping_resp = {.identifier = ID_TESTER_PING_RESP, .data_length_code = , .data = {0, 0, 0, 0, 0, 0, 0, 0}};
static const twai_message_t stop_resp = {.identifier = ID_SLAVE_STOP_RESP, .data_length_code = 0, .data = {0, 0, 0, 0, 0, 0, 0, 0}}; */

// Data bytes of data message will be initialized in the transmit task
static twai_message_t data_message = {.identifier = ID_SLAVE_DATA, .data_length_code = 4, .data = {0, 0, 0, 0, 0, 0, 0, 0}};

static QueueHandle_t tx_task_queue;
static QueueHandle_t rx_task_queue;
static SemaphoreHandle_t ctrl_task_sem;
static SemaphoreHandle_t stop_data_sem;
static SemaphoreHandle_t done_sem;

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

void send(char msg[], char id, int isExtd)
{
    twai_message_t message;
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
        // printf("%c \n", tmp_msg);
        // printf("%d \n", x);
        message.data[i] = x;
    }
    twai_transmit(&message, portMAX_DELAY);
}

static void twai_receive_task(void *arg)
{
    while (1)
    {
        rx_task_action_t action;
        xQueueReceive(rx_task_queue, &action, portMAX_DELAY);
        if (action == RX_RECEIVE_TESTER)
        {
            // Listen for pings from master
            twai_message_t rx_msg;
            while (1)
            {
                ESP_LOGI(EXAMPLE_TAG, "Started Listen");
                twai_receive(&rx_msg, portMAX_DELAY);
                int tmp = rx_msg.identifier;
                ESP_LOGI(EXAMPLE_TAG, "identifier %d" PRIu32, tmp);
                // twai_receive(&rx_msg, portMAX_DELAY);
                // if (rx_msg.identifier == ID_ADAPTER_TESTER)
                // {
                ESP_LOGI(EXAMPLE_TAG, "Transmitted data value-- %" PRIu32, tmp);
                receive(rx_msg);
                xSemaphoreGive(ctrl_task_sem);
                break;
                //  }
            }
        }
        else if (action == RX_RECEIVE_START_CMD)
        {
            // Listen for start command from master
            twai_message_t rx_msg;
            while (1)
            {
                // twai_receive(&rx_msg, portMAX_DELAY);
                if (twai_receive(&rx_msg, portMAX_DELAY) == ESP_OK)
                {
                    printf("Message received\n");
                }
                // if (rx_msg.identifier == ID_MASTER_START_CMD)
                //{
                receive(rx_msg);
                xSemaphoreGive(ctrl_task_sem);
                break;
                //}
            }
        }
    }
    vTaskDelete(NULL);
}

static void twai_transmit_task(void *arg)
{
    while (1)
    {
        tx_task_action_t action;
        xQueueReceive(tx_task_queue, &action, portMAX_DELAY);

        if (action == TX_SEND_TESTER_RESP)
        {
            // Transmit ping response to master
            // twai_transmit(&ping_resp, portMAX_DELAY);
            send("7E", "0xBBBB", 1);
            ESP_LOGI(EXAMPLE_TAG, "Transmitted ecu response");
            xSemaphoreGive(ctrl_task_sem);
        }
        else if (action == TX_SEND_DATA)
        {
            // Transmit data messages until stop command is received
            ESP_LOGI(EXAMPLE_TAG, "Start transmitting data");
            while (1)
            {

                send("B1", "0xBBBB", 1);
                // twai_transmit(&data_message, portMAX_DELAY);
                ESP_LOGI(EXAMPLE_TAG, "Transmitted data value " PRIu32, "0x3E");
                vTaskDelay(pdMS_TO_TICKS(DATA_PERIOD_MS));
                if (xSemaphoreTake(stop_data_sem, 0) == pdTRUE)
                {
                    break;
                }
            }
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

        // Listen of pings from master
        rx_action = RX_RECEIVE_TESTER;
        xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY);
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);

        // Send ping response
        tx_action = TX_SEND_TESTER_RESP;
        xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);

        // Listen for start command
        rx_action = RX_RECEIVE_START_CMD;
        xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY);
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);

        // Start sending data messages and listen for stop command
        tx_action = TX_SEND_DATA;
        /* rx_action = RX_RECEIVE_STOP_CMD; */
        xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
        // xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY);
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY);

        // Send stop response
        /* tx_action = TX_SEND_STOP_RESP;
        xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
        xSemaphoreTake(ctrl_task_sem, portMAX_DELAY); */

        // Wait for bus to become free
        twai_status_info_t status_info;
        twai_get_status_info(&status_info);
        while (status_info.msgs_to_tx > 0)
        {
            vTaskDelay(pdMS_TO_TICKS(100));
            twai_get_status_info(&status_info);
        }

        // ESP_ERROR_CHECK(twai_stop());
        // ESP_LOGI(EXAMPLE_TAG, "Driver stopped");
        vTaskDelay(pdMS_TO_TICKS(ITER_DELAY_MS));
    }

    // Stop TX and RX tasks
    /* tx_action = TX_TASK_EXIT;
    rx_action = RX_TASK_EXIT;
    xQueueSend(tx_task_queue, &tx_action, portMAX_DELAY);
    xQueueSend(rx_task_queue, &rx_action, portMAX_DELAY);

    // Delete Control task
    xSemaphoreGive(done_sem); */
    vTaskDelete(NULL);
}

void app_main(void)
{
    // Add short delay to allow master it to initialize first
    for (int i = 3; i > 0; i--)
    {
        printf("ECU starting in %d\n", i);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Create semaphores and tasks
    tx_task_queue = xQueueCreate(1, sizeof(tx_task_action_t));
    rx_task_queue = xQueueCreate(1, sizeof(rx_task_action_t));
    ctrl_task_sem = xSemaphoreCreateBinary();
    stop_data_sem = xSemaphoreCreateBinary();
    ;
    done_sem = xSemaphoreCreateBinary();
    ;
    xTaskCreatePinnedToCore(twai_receive_task, "TWAI_rx", 4096, NULL, RX_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(twai_transmit_task, "TWAI_tx", 4096, NULL, TX_TASK_PRIO, NULL, tskNO_AFFINITY);
    xTaskCreatePinnedToCore(twai_control_task, "TWAI_ctrl", 4096, NULL, CTRL_TSK_PRIO, NULL, tskNO_AFFINITY);

    // Install TWAI driver, trigger tasks to start
    ESP_ERROR_CHECK(twai_driver_install(&g_config, &t_config, &f_config));
    ESP_LOGI(EXAMPLE_TAG, "Driver installed");

    xSemaphoreGive(ctrl_task_sem);           // Start Control task
    xSemaphoreTake(done_sem, portMAX_DELAY); // Wait for tasks to complete

    // Uninstall TWAI driver
    ESP_ERROR_CHECK(twai_driver_uninstall());
    ESP_LOGI(EXAMPLE_TAG, "Driver uninstalled");

    // Cleanup
    vSemaphoreDelete(ctrl_task_sem);
    vSemaphoreDelete(stop_data_sem);
    vSemaphoreDelete(done_sem);
    vQueueDelete(tx_task_queue);
    vQueueDelete(rx_task_queue);
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
        if (twai_receive(&message, portMAX_DELAY) == ESP_OK)
        {
            printf("Message received\n");
        }
        else
        {
            printf("Failed to receive message\n");
            return;
        }

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
        if (!(message.rtr))
        {
            for (int i = 0; i < message.data_length_code; i++)
            {
                printf("Data byte %d = %c\n", i, (char)message.data[i]);
            }
        }
    }
    else
    {
        printf("Failed to start driver\n");
        return;
    }
} */