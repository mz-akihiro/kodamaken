#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <net/ethernet.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>

#define BUFFER_SIZE 65536
#define HELLO_TEXT "hello"
#define HELLO_TEXT_LEN 5

volatile int EndFlag = 0;  // 終了フラグ

// シグナルハンドラ
void EndSignal(int sig) {
    EndFlag = 1;
    printf("Signal received, preparing to exit...\n");
}

// RAWソケットの作成とバインド
int create_raw_socket(char *interface) {
    int sock;
    struct ifreq ifr;
    struct sockaddr_ll sll;

    // RAWソケットの作成
    if ((sock = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
        perror("Socket creation failed");
        exit(1);
    }

    // インターフェースの取得
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interface, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("Interface index retrieval failed");
        close(sock);
        exit(1);
    }

    // ソケットにインターフェースをバインド
    memset(&sll, 0, sizeof(sll));
    sll.sll_family = AF_PACKET;
    sll.sll_ifindex = ifr.ifr_ifindex;
    sll.sll_protocol = htons(ETH_P_ALL);

    if (bind(sock, (struct sockaddr *)&sll, sizeof(sll)) < 0) {
        perror("Binding socket to interface failed");
        close(sock);
        exit(1);
    }

    printf("Socket bound to interface %s\n", interface);
    return sock;
}

// TCPオプションに "hello" を埋め込む関数
int add_hello_tcp_option(unsigned char *packet, ssize_t packet_size) {
    struct iphdr *ip_header = (struct iphdr *)packet;
    if (ip_header->protocol != IPPROTO_TCP) {
        // TCPパケットでない場合はスキップ
        return -1;
    }

    // IPヘッダ長とTCPヘッダ長を計算
    int ip_header_len = ip_header->ihl * 4;
    struct tcphdr *tcp_header = (struct tcphdr *)(packet + ip_header_len);
    int tcp_header_len = tcp_header->doff * 4;
    
    // TCPオプション領域の最大長を確認（TCPヘッダ全体で最大60バイト）
    int option_space = 60 - tcp_header_len;
    if (option_space < HELLO_TEXT_LEN + 2) {
        fprintf(stderr, "Not enough space in TCP header to add hello option.\n");
        return -1;
    }

    // TCPオプションに "hello" を追加
    unsigned char *option_ptr = packet + ip_header_len + tcp_header_len;
    option_ptr[0] = 0x01;            // カスタムオプションコード
    option_ptr[1] = HELLO_TEXT_LEN + 2; // オプション全体の長さ
    memcpy(option_ptr + 2, HELLO_TEXT, HELLO_TEXT_LEN);

    // TCPヘッダの長さフィールドを更新
    tcp_header->doff += (HELLO_TEXT_LEN + 2) / 4;

    // IPおよびTCPチェックサムを再計算する必要がありますが、ここでは省略
    return 0;
}

int main() {
    char *interface1 = "eth0";  // 物理NIC 1
    char *interface2 = "eth1";  // 物理NIC 2
    int sock1, sock2;
    char buffer[BUFFER_SIZE];
    ssize_t num_bytes;

    // シグナルハンドラの登録
    signal(SIGINT, EndSignal);
    signal(SIGTERM, EndSignal);

    // 各インターフェースにRAWソケットを作成
    sock1 = create_raw_socket(interface1);
    sock2 = create_raw_socket(interface2);

    printf("Bridge started. Press Ctrl+C to stop.\n");

    while (!EndFlag) {
        // NIC1からデータを受信
        num_bytes = recv(sock1, buffer, BUFFER_SIZE, 0);
        if (num_bytes < 0) {
            perror("Failed to receive packet from NIC1");
            break;
        } else if (num_bytes > 0) {
            printf("Packet received on %s, size: %zd bytes\n", interface1, num_bytes);

            // TCPオプションに "hello" を追加
            if (add_hello_tcp_option((unsigned char *)buffer, num_bytes) == 0) {
                printf("Added 'hello' option to TCP packet\n");
            }

            // NIC2にデータを転送
            if (send(sock2, buffer, num_bytes, 0) < 0) {
                perror("Failed to send packet to NIC2");
                break;
            } else {
                printf("Packet forwarded to %s\n", interface2);
            }
        }

        // NIC2からデータを受信
        num_bytes = recv(sock2, buffer, BUFFER_SIZE, 0);
        if (num_bytes < 0) {
            perror("Failed to receive packet from NIC2");
            break;
        } else if (num_bytes > 0) {
            printf("Packet received on %s, size: %zd bytes\n", interface2, num_bytes);

            // TCPオプションに "hello" を追加
            if (add_hello_tcp_option((unsigned char *)buffer, num_bytes) == 0) {
                printf("Added 'hello' option to TCP packet\n");
            }

            // NIC1にデータを転送
            if (send(sock1, buffer, num_bytes, 0) < 0) {
                perror("Failed to send packet to NIC1");
                break;
            } else {
                printf("Packet forwarded to %s\n", interface1);
            }
        }
    }

    // ソケットのクローズ
    close(sock1);
    close(sock2);

    printf("Bridge stopped.\n");
    return 0;
}
