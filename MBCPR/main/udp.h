#ifndef UDP_H
#define UDP_H

#include "lwip/sockets.h"

// Wi-Fi 초기화 (SSID/PASS 지정)
void udp_wifi_init(const char *ssid, const char *pass);

// UDP 소켓 초기화
int udp_socket_init(void);

// 서버에 내 IP를 START 메시지로 전송
int udp_send_start_ip(int sock, const struct sockaddr_in *server_addr);

// 서버 연결 확인 ACK 전송 ("OK")
int udp_send_connection_ack(int sock, const struct sockaddr_in *dest_addr);

// 센서 데이터 전송 (DATA,p,t,c)
int udp_send_sensor_data(int sock, const struct sockaddr_in *server_addr);

#endif /* UDP_H */
