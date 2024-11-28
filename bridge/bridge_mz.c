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
#include <poll.h>

#define BUFFER_SIZE 65536

volatile int EndFlag = 0;  // 終了フラグ

// シグナルハンドラ
void EndSignal(int sig) {
    EndFlag = 1;
    printf("Signal received, preparing to exit...\n");
    fflush(stdout);
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
    fflush(stdout);
    return sock;
}

int main() {
    char *interface1 = "enp0s31f6";  // 物理NIC
    char *interface2 = "VNIC-mizuno";  // VNIC
    int sock1, sock2;
    char buffer[BUFFER_SIZE];
    ssize_t num_bytes;
    struct pollfd fds[2];

    // シグナルハンドラの登録
    signal(SIGINT, EndSignal);
    signal(SIGTERM, EndSignal);

    // 各インターフェースにRAWソケットを作成
    sock1 = create_raw_socket(interface1);
    sock2 = create_raw_socket(interface2);

    printf("Bridge started. Press Ctrl+C to stop.\n");
    fflush(stdout);

    // poll構造体の設定
    fds[0].fd = sock1;
    fds[0].events = POLLIN;
    fds[1].fd = sock2;
    fds[1].events = POLLIN;

    while (!EndFlag) {
        // ソケットのイベントを待機
        int ret = poll(fds, 2, 100);  // 100msのタイムアウトを設定
        if (ret < 0) {
            perror("poll failed");
            break;
        } else if (ret == 0) {
            continue;  // タイムアウトの場合はループの先頭に戻る
        }

        // NIC1からデータを受信
        if (fds[0].revents & POLLIN) {
            num_bytes = recv(sock1, buffer, BUFFER_SIZE, 0);
            if (num_bytes < 0) {
                perror("Failed to receive packet from NIC1");
                break;
            } else if (num_bytes > 0) {
                printf("Packet received on %s, size: %zd bytes\n", interface1, num_bytes);
                fflush(stdout);

                // NIC2にデータを転送
                if (send(sock2, buffer, num_bytes, 0) < 0) {
                    perror("Failed to send packet to NIC2");
                    break;
                } else {
                    printf("Packet forwarded to %s\n", interface2);
                    fflush(stdout);
                }
            }
        }

        // NIC2からデータを受信
        if (fds[1].revents & POLLIN) {
            num_bytes = recv(sock2, buffer, BUFFER_SIZE, 0);
            if (num_bytes < 0) {
                perror("Failed to receive packet from NIC2");
                break;
            } else if (num_bytes > 0) {
                printf("Packet received on %s, size: %zd bytes\n", interface2, num_bytes);
                fflush(stdout);

                // NIC1にデータを転送
                if (send(sock1, buffer, num_bytes, 0) < 0) {
                    perror("Failed to send packet to NIC1");
                    break;
                } else {
                    printf("Packet forwarded to %s\n", interface1);
                    fflush(stdout);
                }
            }
        }
    }

    // ソケットのクローズ
    close(sock1);
    close(sock2);

    printf("Bridge stopped.\n");
    fflush(stdout);
    return 0;
}
