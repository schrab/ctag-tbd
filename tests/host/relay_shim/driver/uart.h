#pragma once
#include <cstddef>
#include <cstdint>
using uart_port_t = int;
constexpr int UART_NUM_0 = 0, UART_DATA_8_BITS = 8, UART_PARITY_DISABLE = 0;
constexpr int UART_STOP_BITS_1 = 1, UART_HW_FLOWCTRL_DISABLE = 0, UART_SCLK_DEFAULT = 0;
constexpr int UART_PIN_NO_CHANGE = -1, ESP_OK = 0;
#define ESP_ERROR_CHECK(expression) ((void)(expression))
struct uart_config_t {
    int baud_rate, data_bits, parity, stop_bits, flow_ctrl, rx_flow_ctrl_thresh, source_clk;
};
int MockRelayWrite(const char *bytes, size_t size);
inline int uart_write_bytes(int, const char *bytes, size_t size) { return MockRelayWrite(bytes, size); }
inline int uart_read_bytes(int, uint8_t *, size_t, int) { return 0; }
inline int uart_get_buffered_data_len(int, size_t *size) { *size = 0; return ESP_OK; }
inline int uart_set_baudrate(int, int) { return ESP_OK; }
inline int uart_set_pin(int, int, int, int, int) { return ESP_OK; }
inline int uart_param_config(int, const uart_config_t *) { return ESP_OK; }
inline int uart_driver_install(int, int, int, int, void *, int) { return ESP_OK; }
