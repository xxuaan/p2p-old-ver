#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"

#include "wifi_driver.h"
#include "network_errors.h"

static simple_wifi_t wifi_state = {0};

// Initialize WiFi with credentials
int wifi_init(const char *ssid, const char *password) {
    printf("\n=== Initializing WiFi ===\n");
    
    if (cyw43_arch_init_with_country(CYW43_COUNTRY_SINGAPORE)) {
        printf("[WARNING] WiFi hardware init failed\n");
        return -1;
    }
    
    cyw43_arch_enable_sta_mode();
    
    // Store credentials
    strncpy(wifi_state.ssid, ssid, sizeof(wifi_state.ssid) - 1);
    wifi_state.ssid[sizeof(wifi_state.ssid) - 1] = '\0';
    strncpy(wifi_state.password, password, sizeof(wifi_state.password) - 1);
    wifi_state.password[sizeof(wifi_state.password) - 1] = '\0';
    wifi_state.initialized = true;
    wifi_state.connected = false;
    wifi_state.last_check_time = get_absolute_time();
    wifi_state.last_reconnect_time = nil_time;
    wifi_state.reconnect_count = 0;
    wifi_state.disconnect_count = 0;
    
    printf("[INFO] WiFi initialized\n");
    printf("[INFO] SSID: %s\n", wifi_state.ssid);
    
    return WIFI_OK;
}

// Check if WiFi is connected
bool wifi_is_connected(void) {
    int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    bool currently_connected = (link_status == CYW43_LINK_UP);  // Checks if wifi connection is connected
    
    // Detect disconnection
    if (wifi_state.connected && !currently_connected) {
        printf("\n[WARNING]: WiFi disconnected!\n");
        printf("[DEBUG] Link status changed to: %s\n", wifi_get_status());
        wifi_state.connected = false;
        wifi_state.disconnect_count++;
    }
    
    return currently_connected;
}

// Get connection status as string
const char* wifi_get_status(void) {
    int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    
    switch (link_status) {
        case CYW43_LINK_DOWN: return "Disconnected";    // 0
        case CYW43_LINK_JOIN: return "Connecting...";   // 1
        case CYW43_LINK_NOIP: return "No IP";           // 2
        case CYW43_LINK_UP: return "Connected";         // 3
        case CYW43_LINK_FAIL: return "Failed";          // -1
        case CYW43_LINK_NONET: return "Network Not Found";  // -2
        case CYW43_LINK_BADAUTH: return "Bad Password";     // -3
        default: return "Unknown";
    }
}

int wifi_get_network_info(wifi_network_info_t *info) {
    if (info == NULL) {
        return WIFI_ENONETIF;
    }
    
    struct netif *netif = netif_default;
    
    if (netif == NULL) {
        printf("[ERROR] Network interface not available\n");
        return WIFI_ENONETIF;
    }
    
    // Fix: Use the correct way to get IP addresses
    ip4_addr_copy(info->ip, *netif_ip4_addr(netif));
    ip4_addr_copy(info->netmask, *netif_ip4_netmask(netif));
    ip4_addr_copy(info->gateway, *netif_ip4_gw(netif));
    
    return WIFI_OK;
}



// Connect to WiFi and get IP from DHCP pool (blocking)
int wifi_connect(void) {
    printf("[INFO] Connecting to: %s\n", wifi_state.ssid);
    // Debug: print masked password info to help diagnose "Bad Password" auth
    size_t pwdlen = strlen(wifi_state.password);
    if (pwdlen > 0) {
        char first = wifi_state.password[0];
        char last = wifi_state.password[pwdlen - 1];
        printf("[DEBUG] Password length=%zu, first='%c', last='%c'\n", pwdlen, first, last);
    } else {
        printf("[DEBUG] Password length=0\n");
    }

    int result = cyw43_arch_wifi_connect_timeout_ms(
        wifi_state.ssid, 
        wifi_state.password, 
        CYW43_AUTH_WPA2_AES_PSK, 
        CONNECTION_TIMEOUT_MS
    );
    
    // Sucessful Connection
    if (result == 0) {
        wifi_state.connected = true;
        
        // Get and display IP address
        wifi_print_network_info();
        
        return 0;
    } else {
        /* Extra debug information: print the raw cyw43 return and link status
         * This helps distinguish authentication failures from other errors
         * (e.g. wrong auth mode, AP rejecting association, or driver errors). */
        int link_status = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
        printf("[INFO] WiFi Connection failed: %d\n", result);
        printf("   Link status (numeric): %d\n", link_status);
        printf("   Status: %s\n", wifi_get_status());
        wifi_state.connected = false;
        return WIFI_ENOTCONN;
    }
}

// Auto-reconnect logic
void wifi_auto_reconnect(void) {
    absolute_time_t now = get_absolute_time();
    
    // Check connection status periodically
    if (absolute_time_diff_us(wifi_state.last_check_time, now) >= 
        RECONNECT_CHECK_INTERVAL_MS * 1000) {
        
        wifi_state.last_check_time = now;
        
        if (!wifi_is_connected()) {
            // Only attempt reconnect if enough time has passed
            if (is_nil_time(wifi_state.last_reconnect_time) ||
                absolute_time_diff_us(wifi_state.last_reconnect_time, now) >= 
                RECONNECT_ATTEMPT_INTERVAL_MS * 1000) {
                
                wifi_state.last_reconnect_time = now;
                wifi_state.reconnect_count++;
                
                printf("\n[INFO] Re-Connection Attempt #%lu\n", 
                       wifi_state.reconnect_count);
                
                wifi_connect();
            }
        }
    }
}

// Print connection statistics
void wifi_print_stats(void) {
    printf("\n╔════════════════════════════════════════╗\n");
    printf("║          WiFi Statistics               ║\n");
    printf("╚════════════════════════════════════════╝\n");
    printf("SSID: %s\n", wifi_state.ssid);
    printf("Status: %s\n", wifi_get_status());
    printf("Disconnections: %lu\n", wifi_state.disconnect_count);
    printf("Reconnect attempts: %lu\n", wifi_state.reconnect_count);
    
    if (wifi_state.connected) {
        wifi_network_info_t net_info;
        if (wifi_get_network_info(&net_info) == WIFI_OK) {
            printf("IP: %s\n", ip4addr_ntoa(&net_info.ip));
        }
    }
}

// WiFi Network Info Direct Print 
int wifi_print_network_info(void) {
    struct netif *netif = netif_default;
    
    if (netif == NULL) {
        printf("[ERROR] Network interface not available\n");
        return WIFI_ENONETIF;
    }
    
    printf("   IP Address: %s\n", ip4addr_ntoa(netif_ip4_addr(netif)));
    printf("   Netmask: %s\n", ip4addr_ntoa(netif_ip4_netmask(netif)));
    printf("   Gateway: %s\n", ip4addr_ntoa(netif_ip4_gw(netif)));
    
    return WIFI_OK;
}

int wifi_get_rssi(void) {
    // Treat all failures here as not connected
    if (!wifi_state.connected) {
        return WIFI_ENOTCONN;
    }

    int32_t rssi;
    // cyw43_wifi_get_rssi returns 0 on success. Treat failure as not connected.
    if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) == 0) {
        return rssi;
    }
    
    return WIFI_ENOTCONN;
}