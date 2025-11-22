// udp_driver.c - UDP Socket Wrapper for MQTT-SN

#include <stdio.h>
#include <string.h>
#include <stdbool.h> //for debugging to see values
#include <lwip/udp.h>
#include <lwip/netif.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/sem.h"
#include "pico/mutex.h"

#include "udp_driver.h"
#include "network_errors.h"

// UDP State
static struct udp_pcb *udp_pcb = NULL;
static uint8_t *recv_buffer = NULL;
static size_t recv_buffer_size = 0;
static size_t recv_len = 0;
static bool data_received = false;

// Semaphore for signaling data arrival
static semaphore_t recv_sem;
static bool sem_initialized = false;

// Mutex for protecting shared state
static mutex_t recv_mutex;
static bool mutex_initialized = false;

// Callback for UDP receives
static void udp_recv_callback(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                               const ip_addr_t *addr, u16_t port) {
    if (p != NULL) {
        // printf("[UDP CALLBACK] Received %d bytes from port %s:%d\n", p->len, ip4addr_ntoa(addr),port);

        if (mutex_initialized){
            mutex_enter_blocking(&recv_mutex);
        }
        
        if (recv_buffer != NULL && !data_received) {
            // Copy data up to the available buffer size
            size_t copy_len = p->len < recv_buffer_size ? p->len : recv_buffer_size;
            memcpy(recv_buffer, p->payload, copy_len);
            // Update recv_len to the actual amount copied
            recv_len = copy_len;
            data_received = true;
            
            // printf("[UDP CALLBACK] Copied %d bytes to buffer\n", copy_len);

            // Signal that data is ready
            if (sem_initialized) {
                sem_release(&recv_sem);
            }

        } else {
            // Uncomment for debugging dropped packets:
            // if (data_received){
            //     printf("[UDP CALLBACK] Buffer already has data, dropping packet\n");
            // } else {
            //     printf("[UDP CALLBACK] No receive buffer set, dropping packet\n");
            // }
        }

        if (mutex_initialized) {
            mutex_exit(&recv_mutex);
        }

        pbuf_free(p);
    }
}


int wifi_udp_create(uint16_t local_port){
    // Initialize semaphore on first call
    if (!sem_initialized) {
        sem_init(&recv_sem, 0, 1);  // Binary semaphore, initial count 0
        sem_initialized = true;
        printf("[UDP] Semaphore initialized\n");
    }
    
    // Initialize mutex on first call
    if (!mutex_initialized) {
        mutex_init(&recv_mutex);
        mutex_initialized = true;
        printf("[UDP] Mutex initialized\n");
    }

    // Close existing PCB if open
    if (udp_pcb != NULL){
        printf("[INFO] Closing existing UDP sockets\n");
        udp_remove(udp_pcb);
        udp_pcb = NULL;
    }

    // Create new UDP PCB
    udp_pcb = udp_new();
    if(udp_pcb == NULL){
        printf("[ERROR] Failed to create UDP PCB.\n");
        return WIFI_ENOMEM; // memory allocation failed
    }

    // Bind to local port
    err_t err = udp_bind(udp_pcb, IP_ADDR_ANY, local_port);
    if (err != ERR_OK){
        printf("[ERROR] Failed to blind UDP PCB to port %d (Error: %d)\n", local_port, err);
        udp_remove(udp_pcb);
        udp_pcb = NULL;
        
        // Map lwip error to custom error code
        if (err == ERR_USE){
            printf("[INFO] UDP Port already in use\n");
            return WIFI_ESOCKET;
        } else if (err == ERR_MEM){
            return WIFI_ENOMEM;
        } else {
            return WIFI_ESOCKET;
        }
    }

    // Register receive callback
    udp_recv(udp_pcb, udp_recv_callback, NULL);

    printf("[INFO] UDP Socket created and bound to port %d\n", local_port);
    return WIFI_OK;                        
}

int wifi_udp_send(const char *dest_ip, uint16_t dest_port, const uint8_t *data, size_t len){
        if (udp_pcb == NULL){
            printf("[ERROR] UDP send failed: socket not created.\n");
            return WIFI_ESOCKET;
        }

        if (dest_ip == NULL || data == NULL || len == 0){
            printf("[UDP] Send Failed: Invalid Parameters\n");
            return WIFI_EINVAL;
        }

        if (dest_port == 0){
            printf("[UDP] Send Failed: Invalid Port (0)\n");
            return WIFI_EINVAL;
        }

        ip_addr_t dest_addr;
        if (!ip4addr_aton(dest_ip, &dest_addr)){
            printf("[UDP] Send Failed: Invalid IP Address '%s'\n", dest_ip);
            return WIFI_EINVAL;
        }

        // Alloate packet buffer
        struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
        if (p == NULL){
            printf("[UDP] Send Failed: Could not allocate pbuf (%zu bytes)\n", len);
            return WIFI_ENOMEM;
        }

        memcpy(p->payload, data, len);

        err_t err = udp_sendto(udp_pcb, p, &dest_addr, dest_port);
        pbuf_free(p);

        if (err != ERR_OK){
            printf("[UDP] Send Failed: udp_sendto error %d\n", err);

            switch (err){
                case ERR_RTE:
                    printf("[UDP] No Route to %s\n", dest_ip);
                    return WIFI_ENOROUTE;
                case ERR_MEM:
                case ERR_BUF:
                    return WIFI_ENOMEM;
                default:
                    return WIFI_ESOCKET;
            }
        }

        printf("[UDP] Sent %zu bytes to %s:%d\n", len, dest_ip, dest_port);
        return WIFI_OK;
}

int wifi_udp_receive(uint8_t *buffer, size_t max_len, uint32_t timeout_ms) {
    // Treat all failures here as not connected
    if (udp_pcb == NULL){
        printf("[UDP] Received failed: socket not created\n");
        return WIFI_ESOCKET;
    }

    if (buffer == NULL || max_len == 0){
        printf("[UDP] Received failed: Invalid Buffer\n");
        return WIFI_EINVAL;
    }

    // Lock mutex to set up receive buffer
    if (mutex_initialized) {
        mutex_enter_blocking(&recv_mutex);
    }

    // Set up receive buffer
    recv_buffer = buffer;
    recv_buffer_size = max_len;
    recv_len = 0;
    data_received = false;

    if (mutex_initialized) {
        mutex_exit(&recv_mutex);
    }

    int result;

    if (timeout_ms == 0) {
        // Non-blocking mode: Check if data is already available
        if (mutex_initialized) {
            mutex_enter_blocking(&recv_mutex);
        }

        if (data_received) {
            result = recv_len;
            data_received = false;
            printf("[UDP] Non-blocking receive: %d bytes\n", result);
            
        } else {
            result = 0;
        }

        recv_buffer = NULL;

        if (mutex_initialized) {
            mutex_exit(&recv_mutex);
        }
    } else {
        // Blocking mode with timeout
        // Wait on semaphore with timeout
        bool acquired = sem_acquire_timeout_ms(&recv_sem, timeout_ms);

        // Lock mutex to read result
        if (mutex_initialized) {
            mutex_enter_blocking(&recv_mutex);
        }

        if (acquired && data_received) {
            result = recv_len;
            data_received = false;
            printf("[UDP] Received %d bytes\n", result);
        } else {
            result = WIFI_ETIMEDOUT;
            // Timeout is normal, don't spam console
        }
        
        recv_buffer = NULL;
        
        if (mutex_initialized) {
            mutex_exit(&recv_mutex);
        }
    }

    return result;
}


void wifi_udp_close(void){
    if (udp_pcb != NULL){
        printf("[UDP] Closing socket...\n");
        
        // Lock mutex during cleanup
        if (mutex_initialized) {
            mutex_enter_blocking(&recv_mutex);
        }

        udp_remove(udp_pcb);
        udp_pcb = NULL;
        recv_buffer = NULL;
        data_received = false;

        if (mutex_initialized) {
            mutex_exit(&recv_mutex);
        }
    }
}

bool is_udp_open(void){
    return (udp_pcb != NULL);
}