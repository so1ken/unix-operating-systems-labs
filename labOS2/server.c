#define _POSIX_C_SOURCE 200809L

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#define DEFAULT_PORT 5555
#define LISTEN_BACKLOG 16
#define RECEIVE_BUFFER_SIZE 4096

static volatile sig_atomic_t was_sighup = 0;
static volatile sig_atomic_t stop_requested = 0;

static void signal_handler(int signal_number) {
    if (signal_number == SIGHUP) {
        was_sighup = 1;
    } else {
        stop_requested = 1;
    }
}

static int parse_port(const char* text, unsigned short* port) {
    char* end = NULL;
    errno = 0;
    const unsigned long value = strtoul(text, &end, 10);

    if (errno != 0 || end == text || *end != '\0' || value == 0 || value > 65535) {
        return -1;
    }

    *port = (unsigned short)value;
    return 0;
}

static int set_descriptor_flags(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        return -1;
    }

    flags = fcntl(fd, F_GETFD, 0);
    if (flags == -1 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == -1) {
        return -1;
    }

    return 0;
}

static int create_server_socket(unsigned short port) {
    const int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("socket");
        return -1;
    }

    int reuse_address = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR,
                   &reuse_address, sizeof(reuse_address)) == -1) {
        perror("setsockopt(SO_REUSEADDR)");
        close(server_fd);
        return -1;
    }

    if (set_descriptor_flags(server_fd) == -1) {
        perror("fcntl(server_fd)");
        close(server_fd);
        return -1;
    }

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) == -1) {
        perror("bind");
        close(server_fd);
        return -1;
    }

    if (listen(server_fd, LISTEN_BACKLOG) == -1) {
        perror("listen");
        close(server_fd);
        return -1;
    }

    if (server_fd >= FD_SETSIZE) {
        fprintf(stderr, "Слишком большой дескриптор серверного сокета для fd_set\n");
        close(server_fd);
        return -1;
    }

    return server_fd;
}

static int install_signal_handling(sigset_t* wait_mask) {
    sigset_t blocked_mask;
    sigset_t original_mask;

    sigemptyset(&blocked_mask);
    sigaddset(&blocked_mask, SIGHUP);
    sigaddset(&blocked_mask, SIGINT);
    sigaddset(&blocked_mask, SIGTERM);

    // Сначала блокируем сигналы, чтобы между проверкой флага и pselect()
    // не возникло окна, в котором сигнал может быть потерян.
    if (sigprocmask(SIG_BLOCK, &blocked_mask, &original_mask) == -1) {
        perror("sigprocmask");
        return -1;
    }

    *wait_mask = original_mask;
    sigdelset(wait_mask, SIGHUP);
    sigdelset(wait_mask, SIGINT);
    sigdelset(wait_mask, SIGTERM);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = signal_handler;
    action.sa_mask = blocked_mask;
    action.sa_flags = 0;

    if (sigaction(SIGHUP, &action, NULL) == -1 ||
        sigaction(SIGINT, &action, NULL) == -1 ||
        sigaction(SIGTERM, &action, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    return 0;
}

static void handle_new_connection(int server_fd, int* client_fd) {
    struct sockaddr_in peer_address;
    socklen_t peer_length = sizeof(peer_address);
    const int new_fd = accept(server_fd, (struct sockaddr*)&peer_address, &peer_length);

    if (new_fd == -1) {
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            perror("accept");
        }
        return;
    }

    char address_text[INET_ADDRSTRLEN] = "unknown";
    (void)inet_ntop(AF_INET, &peer_address.sin_addr,
                    address_text, sizeof(address_text));

    printf("Новое подключение: %s:%u\n",
           address_text, (unsigned int)ntohs(peer_address.sin_port));

    if (new_fd >= FD_SETSIZE) {
        printf("Соединение закрыто: дескриптор не помещается в fd_set\n");
        close(new_fd);
        return;
    }

    if (*client_fd != -1) {
        printf("Соединение отклонено: активный клиент уже существует\n");
        close(new_fd);
        return;
    }

    if (set_descriptor_flags(new_fd) == -1) {
        perror("fcntl(client_fd)");
        close(new_fd);
        return;
    }

    *client_fd = new_fd;
    printf("Соединение оставлено активным\n");
}

static void receive_client_data(int* client_fd) {
    char buffer[RECEIVE_BUFFER_SIZE];
    const ssize_t received = recv(*client_fd, buffer, sizeof(buffer), 0);

    if (received > 0) {
        printf("Получено данных от клиента: %zd байт\n", received);
        return;
    }

    if (received == 0) {
        printf("Клиент закрыл соединение\n");
        close(*client_fd);
        *client_fd = -1;
        return;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
        return;
    }

    perror("recv");
    close(*client_fd);
    *client_fd = -1;
}

int main(int argc, char** argv) {
    unsigned short port = DEFAULT_PORT;
    if (argc > 2 || (argc == 2 && parse_port(argv[1], &port) == -1)) {
        fprintf(stderr, "Использование: %s [порт 1..65535]\n", argv[0]);
        return EXIT_FAILURE;
    }

    setvbuf(stdout, NULL, _IOLBF, 0);

    sigset_t wait_mask;
    if (install_signal_handling(&wait_mask) == -1) {
        return EXIT_FAILURE;
    }

    const int server_fd = create_server_socket(port);
    if (server_fd == -1) {
        return EXIT_FAILURE;
    }

    int client_fd = -1;
    printf("Сервер слушает порт %u; для уведомления отправьте SIGHUP\n",
           (unsigned int)port);

    while (!stop_requested) {
        fd_set read_fds;
        FD_ZERO(&read_fds);
        FD_SET(server_fd, &read_fds);

        int max_fd = server_fd;
        if (client_fd != -1) {
            FD_SET(client_fd, &read_fds);
            if (client_fd > max_fd) {
                max_fd = client_fd;
            }
        }

        // pselect() атомарно устанавливает wait_mask на время ожидания.
        const int ready = pselect(max_fd + 1, &read_fds, NULL, NULL, NULL, &wait_mask);

        if (ready == -1 && errno != EINTR) {
            perror("pselect");
            break;
        }

        if (was_sighup) {
            was_sighup = 0;
            printf("Получен сигнал SIGHUP; сервер продолжает работу\n");
        }

        if (stop_requested) {
            break;
        }

        if (ready == -1) {
            continue;
        }

        if (FD_ISSET(server_fd, &read_fds)) {
            handle_new_connection(server_fd, &client_fd);
        }

        if (client_fd != -1 && FD_ISSET(client_fd, &read_fds)) {
            receive_client_data(&client_fd);
        }
    }

    if (client_fd != -1) {
        close(client_fd);
    }
    close(server_fd);
    printf("Сервер завершает работу\n");
    return EXIT_SUCCESS;
}
