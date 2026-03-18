/*
 * esp01_wifi.c
 *
 *  Created on: Mar 10, 2026
 *      Author: PC
 */

#include "esp01_wifi.h"

uint8_t uart_rxBuffer[1024];
uint8_t uart_txBuffer[1024];

char wifi_ip[16] = {0};
char wifi_mac[18] = {0};

volatile uint8_t data_received = 0;
volatile uint16_t uart_rx_len = 0;

// ========= debug ==========
void uart_printf(UART_HandleTypeDef *huart, const char *fmt, ...)
{
    char buffer[PRINTF_BUFFER_SIZE];

    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buffer, PRINTF_BUFFER_SIZE, fmt, args);
    va_end(args);

    if (len > 0)
    {
        if (len > PRINTF_BUFFER_SIZE)
            len = PRINTF_BUFFER_SIZE;

        HAL_UART_Transmit(huart, (uint8_t*)buffer, len, HAL_MAX_DELAY);
    }
}

// ========== core functions ==========
void wifi_send(const char* data){
	memset(uart_txBuffer, 0, 1024);
    int len = strlen(data);
    strncpy((char*)uart_txBuffer, data, len);
    data_received = 0;
    HAL_UART_Transmit_DMA(&ESP01_UART, uart_txBuffer, len);
}

void wifi_receive(void){
	//memset(uart_rxBuffer, 0, sizeof(uart_rxBuffer));
	HAL_UARTEx_ReceiveToIdle_DMA(&ESP01_UART, uart_rxBuffer, 1024);
}

void wifi_clear_rx(void){
    memset(uart_rxBuffer, 0, 1024);
}

void wifi_clear_tx(void){
    memset(uart_txBuffer, 0, 1024);
}

uint8_t wifi_waitForRespond(const char* res){
    while(data_received != 1);
    data_received = 0;

    if (strcmp((char*)uart_rxBuffer, res) == 0) return 1;
    else return 0;
}

uint8_t wifi_waitForRespond_finicky(const char* res){
    while(data_received != 1);
    data_received = 0;

    for(uint8_t i = 0; i < strlen(res); i++){
        if (uart_rxBuffer[i] != res[i]) return 0;
    }
    return 1;
}



// ========== commands functions ==========
void wifi_reset(void){
	wifi_send("AT+RST\r\n");
	uart_printf(&DEBUG_UART, "Sent: AT+RST\r\n");
}

void wifi_echoOff(void){
	wifi_send("ATE0\r\n");
	uart_printf(&DEBUG_UART, "Sent: ATE0\r\n");
	//while (!wifi_waitForRespond("\r\nOK\r\n"));
}
/*
 * 1: station
 * 2: ap
 * 3: station + ap
 */
void wifi_mode(uint8_t mode){
    char cmd[16];
    snprintf(cmd, sizeof(cmd), "AT+CWMODE=%d\r\n", mode);
    wifi_send(cmd);
    uart_printf(&DEBUG_UART, "Sent: %s", cmd);
    while (!wifi_waitForRespond("\r\nOK\r\n"));
}

void wifi_scannet(void){
	wifi_send("AT+CWLAP\r\n");
	uart_printf(&DEBUG_UART, "Sent: AT+CWLAP\r\n");
}

void wifi_connect(const char *ssid, const char *password){
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"\r\n", ssid, password);
	wifi_send(cmd);
	uart_printf(&DEBUG_UART, "Sent: %s", cmd);
	while (!wifi_waitForRespond("\r\nOK\r\n"));
}

void wifi_getIP(void){
	wifi_send("AT+CIFSR\r\n");
	uart_printf(&DEBUG_UART, "Sent: AT+CIFSR\r\n");
	while (!wifi_waitForRespond_finicky("+CIFSR"));
}



// ========== MQTT ==========
void wifi_connectTCP(const char* ip, uint16_t port){
    wifi_send("AT+CIPMUX=1\r\n");
    uart_printf(&DEBUG_UART, "Sent: AT+CIPMUX=1\r\n");
    while (!wifi_waitForRespond("\r\nOK\r\n"));

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CIPSTART=4,\"TCP\",\"%s\",%d\r\n", ip, port);
    wifi_send(cmd);
    uart_printf(&DEBUG_UART, "Sent: %s", cmd);
    while (!wifi_waitForRespond("4,CONNECT\r\n\r\nOK\r\n"));

    uint8_t mqtt_connect[] = {
        0x10, 0x12,                     // remaining length = 18
        0x00, 0x04, 'M','Q','T','T',
        0x04, 0x02,
        0x00, 0x3C,
        0x00, 0x06, 'c','l','i','e','n','t'
    };

    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=4,%d\r\n", sizeof(mqtt_connect));
    wifi_send(cmd);
    uart_printf(&DEBUG_UART, "Sent: %s", cmd);
    while (!wifi_waitForRespond("\r\nOK\r\n> "));

    HAL_UART_Transmit_DMA(&ESP01_UART, mqtt_connect, sizeof(mqtt_connect));
    uart_printf(&DEBUG_UART, "Sent: MQTT connect packet\r\n");
    //while (!wifi_waitForRespond("\r\nSEND OK\r\n\r\n+IPD,4,4: "));
}

void wifi_pingMQTT(void){
    uint8_t ping[2] = {0xC0, 0x00};
    wifi_send("AT+CIPSEND=4,2\r\n");
    uart_printf(&DEBUG_UART, "Sent: AT+CIPSEND=4,2\r\n");
    while(!wifi_waitForRespond("\r\nOK\r\n> "));
    HAL_UART_Transmit_DMA(&ESP01_UART, ping, 2);
    uart_printf(&DEBUG_UART, "Sent: MQTT ping packet\r\n");
    while(!wifi_waitForRespond_finicky("\r\nSEND OK"));
}

void wifi_publishMQTT(const char* topic, const char* payload){
    uint16_t topic_len = (uint16_t)strlen(topic);
    uint16_t payload_len = (uint16_t)strlen(payload);
    int remaining = 2 + topic_len + payload_len;
    int total_len = 2 + remaining;

    uint8_t packet[4 + topic_len + payload_len];
    int p = 0;

    packet[p++] = 0x30;
    packet[p++] = (uint8_t)remaining;
    packet[p++] = (uint8_t)((topic_len >> 8) & 0xFF);
    packet[p++] = (uint8_t)(topic_len & 0xFF);
    memcpy(&packet[p], topic, topic_len); p += topic_len;
    memcpy(&packet[p], payload, payload_len); p += payload_len;

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+CIPSEND=4,%d\r\n", total_len);
    wifi_send(cmd);
    uart_printf(&DEBUG_UART, "Sent: %s", cmd);

    while (!wifi_waitForRespond("\r\nOK\r\n> "));

    HAL_UART_Transmit_DMA(&ESP01_UART, packet, total_len);
    uart_printf(&DEBUG_UART, "Sent: MQTT publish packet\r\n");
    while(!wifi_waitForRespond_finicky("\r\nSEND OK"));
}

void wifi_subscribeMQTT(const char* topic, uint8_t qos){
	uint8_t topic_len = (uint8_t)strlen(topic);
	uint8_t total_len = 7 + topic_len;

	uint8_t packet[total_len];
	int p = 0;

	uint8_t remaining_len = 5 + topic_len;

	packet[p++] = 0x82;										//header
	packet[p++] = remaining_len;				    		//remaining len
	packet[p++] = 0x00; packet[p++] = 0x01;         		//id
	packet[p++] = ((topic_len >> 8) & 0xFF);				//topic len high byte
	packet[p++] = (topic_len & 0xFF);						//topic len low byte
	memcpy(&packet[p], topic, topic_len); p += topic_len;	//topic
	packet[p++] = qos;										//qos

	char cmd[64];
	snprintf(cmd, sizeof(cmd), "AT+CIPSEND=4,%d\r\n", total_len);
	wifi_send(cmd);
	uart_printf(&DEBUG_UART, "Sent: %s", cmd);

	while (!wifi_waitForRespond("\r\nOK\r\n> "));

	HAL_UART_Transmit_DMA(&ESP01_UART, packet, total_len);
	uart_printf(&DEBUG_UART, "Sent: MQTT subscribe packet\r\n");
}

int wifi_readMQTT_pub(char *topic_out, size_t topic_out_size, char *payload_out, size_t payload_out_size)
{
    if (payload_out == NULL || payload_out_size == 0) return 0;
    if (uart_rx_len == 0) return 0;

    char *buf = (char*)uart_rxBuffer;
    size_t buf_len = (size_t)uart_rx_len;
    if (buf_len == 0) return 0;

    /* static storage of last returned topic+payload to detect "new" data */
    static char last_topic[256] = {0};
    static size_t last_topic_len = 0;
    static char last_payload[1024] = {0};
    static size_t last_payload_len = 0;

    size_t p_off = 0;

    while (p_off < buf_len) {
        /* find "+IPD," starting from p_off */
        char *ipd = NULL;
        for (size_t i = p_off; i + 5 <= buf_len; ++i) {
            if (buf[i] == '+' && i + 4 < buf_len &&
                buf[i+1] == 'I' && buf[i+2] == 'P' && buf[i+3] == 'D' && buf[i+4] == ',') {
                ipd = &buf[i];
                p_off = i;
                break;
            }
        }
        if (!ipd) break;

        char *q = ipd + 5; /* after "+IPD," */
        /* skip mux id digits */
        while ((size_t)(q - buf) < buf_len && *q >= '0' && *q <= '9') q++;
        if ((size_t)(q - buf) >= buf_len) break; /* incomplete */

        if (*q != ',') { p_off = (size_t)(ipd - buf) + 1; continue; }
        q++; /* now at length */

        /* parse length */
        int len = 0;
        while ((size_t)(q - buf) < buf_len && *q >= '0' && *q <= '9') {
            len = len*10 + (*q - '0');
            q++;
        }
        if ((size_t)(q - buf) >= buf_len) break; /* incomplete */

        if (*q == ':') q++; /* data starts at q */
        else {
            /* try to find ':' within remaining bytes */
            char *col = memchr(q, ':', buf_len - (size_t)(q - buf));
            if (!col) break;
            q = col + 1;
        }

        /* ensure 'len' payload bytes are present */
        if ((size_t)(buf + buf_len - q) < (size_t)len) break; /* incomplete */

        uint8_t *data = (uint8_t*)q;
        int available = len;
        int idx = 0;

        /* Only handle MQTT PUBLISH (0x30..0x3F) */
        if (available > 0 && (data[idx] & 0xF0) == 0x30) {
            idx++; /* fixed header byte consumed */

            /* decode Remaining Length (var-length) */
            int rem_len = 0;
            int multiplier = 1;
            uint8_t encoded;
            do {
                if (idx >= available) { idx = -1; break; }
                encoded = data[idx++];
                rem_len += (encoded & 0x7F) * multiplier;
                multiplier *= 128;
            } while ((encoded & 0x80) && idx < available);

            if (idx < 0) { /* malformed/incomplete, skip this ipd */
                p_off = (size_t)(q - buf) + len;
                continue;
            }

            /* topic length (2 bytes big-endian) */
            if (idx + 2 > available) { p_off = (size_t)(q - buf) + len; continue; }
            int topic_len = (data[idx] << 8) | data[idx+1];
            idx += 2;

            if (idx + topic_len > available) { p_off = (size_t)(q - buf) + len; continue; }
            /* copy topic to local temporary */
            int tl = topic_len;
            if (tl > 255) tl = 255;
            char topic_local[256];
            if (topic_out && topic_out_size > 0) {
                /* copy into topic_local, then will copy to topic_out if accepted as new */
                memcpy(topic_local, &data[idx], tl);
                topic_local[tl] = '\0';
            } else {
                /* still read into local buffer to compare with last topic */
                memcpy(topic_local, &data[idx], tl > 255 ? 255 : tl);
                topic_local[tl > 255 ? 255 : tl] = '\0';
            }
            idx += topic_len;

            /* Determine QoS and skip packet identifier if needed */
            uint8_t qos = (data[0] >> 1) & 0x03; /* from first fixed header byte */
            if (qos > 0) {
                if (idx + 2 > available) { p_off = (size_t)(q - buf) + len; continue; }
                idx += 2;
            }

            /* compute payload length (remaining - 2 - topic_len - packet id if any) */
            int payload_len = rem_len - 2 - topic_len - (qos > 0 ? 2 : 0);
            if (payload_len < 0) payload_len = 0;
            if (payload_len > available - idx) payload_len = available - idx;
            if (payload_len > 1023) payload_len = 1023;

            char payload_local[1024];
            memcpy(payload_local, &data[idx], payload_len);
            payload_local[payload_len] = '\0';

            /* Compare with last returned topic+payload — if different, return this one */
            int is_new = 0;
            if (payload_len != (int)last_payload_len) {
                is_new = 1;
            } else {
                if (memcmp(payload_local, last_payload, payload_len) != 0) is_new = 1;
            }
            /* Also consider topic changes as new */
            if (!is_new) {
                size_t tlen_check = (tl < (int)sizeof(last_topic)) ? (size_t)tl : (sizeof(last_topic)-1);
                if (tlen_check != last_topic_len) is_new = 1;
                else if (memcmp(topic_local, last_topic, tlen_check) != 0) is_new = 1;
            }

            if (is_new) {
                /* copy into outputs (respect sizes) */
                if (topic_out && topic_out_size > 0) {
                    size_t copy_t = (size_t)tl < (topic_out_size - 1) ? (size_t)tl : (topic_out_size - 1);
                    memcpy(topic_out, topic_local, copy_t);
                    topic_out[copy_t] = '\0';
                }
                size_t copy_p = (size_t)payload_len < (payload_out_size - 1) ? (size_t)payload_len : (payload_out_size - 1);
                memcpy(payload_out, payload_local, copy_p);
                payload_out[copy_p] = '\0';

                /* save as last returned */
                last_payload_len = copy_p;
                memcpy(last_payload, payload_local, copy_p);
                last_payload[copy_p] = '\0';

                if (tl > 0) {
                    last_topic_len = (tl < (int)(sizeof(last_topic)-1)) ? (size_t)tl : (sizeof(last_topic)-1);
                    memcpy(last_topic, topic_local, last_topic_len);
                    last_topic[last_topic_len] = '\0';
                } else {
                    last_topic_len = 0;
                    last_topic[0] = '\0';
                }

                return 1; /* found new payload, returned */
            }
            /* else not new — continue scanning next +IPD in this chunk */
        } /* end PUBLISH check */

        /* advance past this +IPD block */
        p_off = (size_t)(q - buf) + len;
    } /* end while scan */

    return 0; /* no new publish found in this chunk */
}

// ========== DMA callbacks ==========
void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t Size)
{
    if (huart->Instance == USART2)
    {
    	if (Size >= sizeof(uart_rxBuffer)) Size = sizeof(uart_rxBuffer) - 1;
    	uart_rx_len = Size;
        uart_rxBuffer[Size] = '\0';
        data_received = 1;
        uart_printf(&DEBUG_UART, "Received: %s", uart_rxBuffer);
        wifi_receive();
    }
}
