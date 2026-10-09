#include "control.h"
#include "camera.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define LINE_BUF_SIZE 256

static int listen_fd = -1;
static int client_fd = -1;
static char line_buf[LINE_BUF_SIZE];
static int line_pos = 0;

static void set_nonblock(int fd) {
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
}

static void send_response(const char *resp) {
    if (client_fd < 0) return;
    write(client_fd, resp, strlen(resp));
}

static void dispatch_command(const char *line) {
    int rc = 0;

    if (strncmp(line, "pan_tilt ", 9) == 0) {
        int pd, ps, td, ts;
        if (sscanf(line + 9, "%d %d %d %d", &pd, &ps, &td, &ts) == 4) {
            pthread_mutex_lock(&camera_mutex);
            if (ps == 0 && ts == 0) {
                rc = camera_stop();
            } else {
                rc = camera_pan_tilt((uint8_t)pd, (uint8_t)ps,
                                     (uint8_t)td, (uint8_t)ts);
            }
            pthread_mutex_unlock(&camera_mutex);
        } else {
            send_response("error:bad pan_tilt args\n");
            return;
        }
    } else if (strcmp(line, "stop") == 0) {
        pthread_mutex_lock(&camera_mutex);
        rc = camera_stop();
        pthread_mutex_unlock(&camera_mutex);
    } else if (strcmp(line, "center") == 0) {
        pthread_mutex_lock(&camera_mutex);
        rc = camera_center();
        pthread_mutex_unlock(&camera_mutex);
    } else if (strncmp(line, "zoom ", 5) == 0) {
        int val;
        if (sscanf(line + 5, "%d", &val) == 1) {
            if (val < ZOOM_MIN || val > ZOOM_MAX) {
                send_response("error:zoom out of range (100-400)\n");
                return;
            }
            pthread_mutex_lock(&camera_mutex);
            rc = camera_zoom((uint16_t)val);
            pthread_mutex_unlock(&camera_mutex);
        } else {
            send_response("error:bad zoom value\n");
            return;
        }
    } else if (strcmp(line, "status") == 0) {
        pthread_mutex_lock(&camera_mutex);
        camera_status_t st = camera_get_status();
        pthread_mutex_unlock(&camera_mutex);
        char resp[64];
        snprintf(resp, sizeof(resp), "status:%d %u\n", st.connected, st.zoom);
        send_response(resp);
        return;
    } else {
        send_response("error:unknown command\n");
        return;
    }

    send_response(rc == 0 ? "ok\n" : "error:camera not connected\n");
}

int control_init(uint16_t port) {
    listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("[control] socket");
        return -1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = htonl(INADDR_LOOPBACK),
    };

    if (bind(listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("[control] bind");
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }

    if (listen(listen_fd, 1) < 0) {
        perror("[control] listen");
        close(listen_fd);
        listen_fd = -1;
        return -1;
    }

    set_nonblock(listen_fd);

    printf("[control] TCP control socket listening on port %u\n", port);
    return 0;
}

void control_service(void) {
    if (listen_fd < 0) return;

    /* Accept new connection (non-blocking) */
    if (client_fd < 0) {
        client_fd = accept(listen_fd, NULL, NULL);
        if (client_fd >= 0) {
            set_nonblock(client_fd);
            line_pos = 0;
        }
    }

    if (client_fd < 0) return;

    /* Read available data into line buffer */
    char buf[128];
    ssize_t n = read(client_fd, buf, sizeof(buf));

    if (n == 0) {
        /* Client disconnected */
        close(client_fd);
        client_fd = -1;
        line_pos = 0;
        return;
    }

    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return;
        /* Read error */
        close(client_fd);
        client_fd = -1;
        line_pos = 0;
        return;
    }

    for (ssize_t i = 0; i < n; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            if (line_pos > 0) {
                line_buf[line_pos] = '\0';
                dispatch_command(line_buf);
                line_pos = 0;
            }
        } else if (line_pos < LINE_BUF_SIZE - 1) {
            line_buf[line_pos++] = buf[i];
        }
    }
}

void control_destroy(void) {
    if (client_fd >= 0) {
        close(client_fd);
        client_fd = -1;
    }
    if (listen_fd >= 0) {
        close(listen_fd);
        listen_fd = -1;
    }
}
