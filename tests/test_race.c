#include "internal.h"
#include "test_harness.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#else
#  include <arpa/inet.h>
#  include <netinet/in.h>
#  include <sys/socket.h>
#  include <unistd.h>
#endif

static int cf_test_listen_ephemeral(uint16_t *out_port)
{
#ifdef _WIN32
    static int wsa;
    if (!wsa) {
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        wsa = 1;
    }
#endif
    int sock = (int)socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
        return -1;

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    sa.sin_port = 0;

    if (bind(sock, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }

    socklen_t len = sizeof(sa);
    if (getsockname(sock, (struct sockaddr *)&sa, &len) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }
    if (listen(sock, 1) < 0) {
#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        return -1;
    }

    *out_port = ntohs(sa.sin_port);
    return sock;
}

int test_race_run(void)
{
    uint16_t port = 0;
    int listener = cf_test_listen_ephemeral(&port);
    if (listener < 0 || port == 0)
        return 0;

    char addr_a[64] = "127.0.0.1";
    char addr_b[64] = "127.0.0.1";
    char *addrs[] = { addr_a, addr_b };

    cf_config *cfg = cf_config_create();
    ASSERT(cfg != NULL);
    cf_config_set_connect_timeout_ms(cfg, 2000);

    cf_race_result *race = NULL;
    ASSERT(cf_tcp_race(addrs, 2, port, cfg, &race) == 0);
    ASSERT(race != NULL);
    int winner_fd = cf_race_winner_fd(race);
    ASSERT(winner_fd >= 0);
    ASSERT(strcmp(cf_race_winner_addr(race), "127.0.0.1") == 0);
    cf_race_sync_finish(race);
    ASSERT(cf_race_rank_count(race) >= 1);
#ifdef _WIN32
    closesocket((SOCKET)winner_fd);
#else
    close(winner_fd);
#endif
    cf_race_result_free(race);

    /* Refused port must fail the race. */
    char *refused[] = { "127.0.0.1" };
    race = NULL;
    ASSERT(cf_tcp_race(refused, 1, 1, cfg, &race) < 0);

    cf_config_destroy(cfg);
#ifdef _WIN32
    closesocket((SOCKET)listener);
#else
    close(listener);
#endif
    return 0;
}
