/*
 * linkctl — CLI client for linkctl-daemon
 *
 * Connects to the daemon's TCP control socket and sends camera commands.
 * Usage: linkctl <command> [args]
 *
 * Zero external dependencies — pure POSIX.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <signal.h>
#include <ctype.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* --- Constants --- */

#define DEFAULT_HOST "127.0.0.1"
#define DEFAULT_PORT 9001

#define DIR_POSITIVE 1
#define DIR_NEGATIVE 255
#define DIR_STOP     0

#define PAN_MAX_SPEED  30
#define TILT_MAX_SPEED 20
#define ZOOM_MIN       100
#define ZOOM_MAX       400

#define JOG_DEFAULT_PAN_SPEED  15
#define JOG_DEFAULT_TILT_SPEED 10
#define JOG_SPEED_STEP         3
#define JOG_MIN_SPEED          3
#define JOG_STOP_TIMEOUT_US    400000  /* 400ms — matches typical macOS key repeat delay */

/* --- State --- */

static volatile bool done = false;
static struct termios orig_termios;
static bool termios_saved = false;
static int orig_stdin_flags = -1;

/* --- Terminal helpers --- */

static void restore_terminal(void) {
    if (termios_saved)
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
    if (orig_stdin_flags >= 0)
        fcntl(STDIN_FILENO, F_SETFL, orig_stdin_flags);
}

static void enter_raw_mode(void) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    termios_saved = true;

    orig_stdin_flags = fcntl(STDIN_FILENO, F_GETFL);
    fcntl(STDIN_FILENO, F_SETFL, orig_stdin_flags | O_NONBLOCK);

    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

static void handle_signal(int sig) {
    (void)sig;
    done = true;
}

/* --- TCP helpers --- */

static int tcp_connect(const char *host, uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
    };
    inet_pton(AF_INET, host, &addr.sin_addr);

    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Send a command and read the response line. Returns response in buf. */
static int send_cmd(int sock, const char *cmd, char *resp, size_t resp_size) {
    if (write(sock, cmd, strlen(cmd)) < 0) return -1;

    ssize_t n = read(sock, resp, resp_size - 1);
    if (n <= 0) return -1;
    resp[n] = '\0';

    /* Strip trailing newline */
    while (n > 0 && (resp[n-1] == '\n' || resp[n-1] == '\r'))
        resp[--n] = '\0';

    return 0;
}

/* --- Usage --- */

static void print_usage(void) {
    printf("Usage: linkctl <command> [args]\n\n");
    printf("Commands:\n");
    printf("  help              Show this help\n");
    printf("  status            Show camera connection and zoom level\n");
    printf("  center            Center gimbal and reset zoom to 1.0x\n");
    printf("  zoom <1.0-4.0>    Set zoom level\n");
    printf("  zoom              Show current zoom level\n");
    printf("  stop              Stop all gimbal movement\n");
    printf("  jog               Interactive WASD pan/tilt mode\n");
    printf("\nThe linkctl-daemon must be running for commands to work.\n");
}

/* --- One-shot commands --- */

static int cmd_status(int sock) {
    char resp[128];
    if (send_cmd(sock, "status\n", resp, sizeof(resp)) < 0) return 1;

    int connected;
    unsigned zoom;
    if (sscanf(resp, "status:%d %u", &connected, &zoom) == 2) {
        printf("Camera: %s\n", connected ? "connected" : "not connected");
        if (connected && zoom > 0)
            printf("Zoom:   %.1fx (%u)\n", zoom / 100.0, zoom);
    } else {
        fprintf(stderr, "%s\n", resp);
        return 1;
    }
    return 0;
}

static int cmd_center(int sock) {
    char resp[128];
    /* Center + stop + zoom reset */
    if (send_cmd(sock, "center\n", resp, sizeof(resp)) < 0) return 1;
    if (strncmp(resp, "ok", 2) != 0) { fprintf(stderr, "%s\n", resp); return 1; }

    if (send_cmd(sock, "stop\n", resp, sizeof(resp)) < 0) return 1;

    char cmd[32];
    snprintf(cmd, sizeof(cmd), "zoom %d\n", ZOOM_MIN);
    if (send_cmd(sock, cmd, resp, sizeof(resp)) < 0) return 1;
    if (strncmp(resp, "ok", 2) != 0) { fprintf(stderr, "%s\n", resp); return 1; }

    printf("Centered, zoom reset to 1.0x\n");
    return 0;
}

static int cmd_zoom_set(int sock, float value) {
    uint16_t raw = (uint16_t)(value * 100.0f + 0.5f);
    if (raw < ZOOM_MIN) raw = ZOOM_MIN;
    if (raw > ZOOM_MAX) raw = ZOOM_MAX;

    char cmd[32], resp[128];
    snprintf(cmd, sizeof(cmd), "zoom %u\n", raw);
    if (send_cmd(sock, cmd, resp, sizeof(resp)) < 0) return 1;
    if (strncmp(resp, "ok", 2) != 0) { fprintf(stderr, "%s\n", resp); return 1; }

    printf("Zoom set to %.1fx\n", value);
    return 0;
}

static int cmd_zoom_query(int sock) {
    char resp[128];
    if (send_cmd(sock, "status\n", resp, sizeof(resp)) < 0) return 1;

    int connected;
    unsigned zoom;
    if (sscanf(resp, "status:%d %u", &connected, &zoom) == 2 && connected) {
        printf("Zoom: %.1fx (%u)\n", zoom / 100.0, zoom);
    } else {
        printf("Camera not connected\n");
        return 1;
    }
    return 0;
}

static int cmd_stop(int sock) {
    char resp[128];
    if (send_cmd(sock, "stop\n", resp, sizeof(resp)) < 0) return 1;
    if (strncmp(resp, "ok", 2) != 0) { fprintf(stderr, "%s\n", resp); return 1; }
    printf("Stopped\n");
    return 0;
}

/* --- Jog mode --- */

static int cmd_jog(int sock) {
    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "Jog mode requires a terminal\n");
        return 1;
    }

    printf("\n=== Jog Mode ===\n");
    printf("  WASD  Pan/tilt      +/- Speed\n");
    printf("  Space Stop          c   Center\n");
    printf("  q/ESC Exit\n\n");

    uint8_t pan_speed = JOG_DEFAULT_PAN_SPEED;
    uint8_t tilt_speed = JOG_DEFAULT_TILT_SPEED;
    printf("Speed: pan=%u tilt=%u\n", pan_speed, tilt_speed);

    enter_raw_mode();

    bool moving = false;
    struct timeval last_input;
    gettimeofday(&last_input, NULL);

    struct pollfd fds[2] = {
        { .fd = STDIN_FILENO, .events = POLLIN },
        { .fd = sock,         .events = POLLIN },
    };

    while (!done) {
        poll(fds, 2, 50);

        /* Read keyboard input */
        if (fds[0].revents & POLLIN) {
            char c;
            if (read(STDIN_FILENO, &c, 1) == 1) {
                gettimeofday(&last_input, NULL);
                c = tolower((unsigned char)c);

                char cmd[128];
                cmd[0] = '\0';

                switch (c) {
                case 'w':
                    snprintf(cmd, sizeof(cmd), "pan_tilt %d %d %d %d\n",
                             DIR_STOP, 0, DIR_NEGATIVE, tilt_speed);
                    moving = true;
                    break;
                case 's':
                    snprintf(cmd, sizeof(cmd), "pan_tilt %d %d %d %d\n",
                             DIR_STOP, 0, DIR_POSITIVE, tilt_speed);
                    moving = true;
                    break;
                case 'a':
                    snprintf(cmd, sizeof(cmd), "pan_tilt %d %d %d %d\n",
                             DIR_POSITIVE, pan_speed, DIR_STOP, 0);
                    moving = true;
                    break;
                case 'd':
                    snprintf(cmd, sizeof(cmd), "pan_tilt %d %d %d %d\n",
                             DIR_NEGATIVE, pan_speed, DIR_STOP, 0);
                    moving = true;
                    break;
                case '+': case '=':
                    if (pan_speed + JOG_SPEED_STEP <= PAN_MAX_SPEED)
                        pan_speed += JOG_SPEED_STEP;
                    else
                        pan_speed = PAN_MAX_SPEED;
                    if (tilt_speed + JOG_SPEED_STEP <= TILT_MAX_SPEED)
                        tilt_speed += JOG_SPEED_STEP;
                    else
                        tilt_speed = TILT_MAX_SPEED;
                    printf("\rSpeed: pan=%u tilt=%u   \r", pan_speed, tilt_speed);
                    fflush(stdout);
                    break;
                case '-':
                    if (pan_speed > JOG_MIN_SPEED + JOG_SPEED_STEP)
                        pan_speed -= JOG_SPEED_STEP;
                    else
                        pan_speed = JOG_MIN_SPEED;
                    if (tilt_speed > JOG_MIN_SPEED + JOG_SPEED_STEP)
                        tilt_speed -= JOG_SPEED_STEP;
                    else
                        tilt_speed = JOG_MIN_SPEED;
                    printf("\rSpeed: pan=%u tilt=%u   \r", pan_speed, tilt_speed);
                    fflush(stdout);
                    break;
                case ' ':
                    snprintf(cmd, sizeof(cmd), "stop\n");
                    moving = false;
                    break;
                case 'c':
                    snprintf(cmd, sizeof(cmd), "center\n");
                    moving = false;
                    break;
                case 'q':
                case 0x1B:  /* ESC */
                case 0x03:  /* Ctrl-C */
                    write(sock, "stop\n", 5);
                    done = true;
                    break;
                }

                if (cmd[0])
                    write(sock, cmd, strlen(cmd));
            }
        }

        /* Drain incoming responses (don't need them during jog) */
        if (fds[1].revents & POLLIN) {
            char discard[256];
            read(sock, discard, sizeof(discard));
        }

        /* Auto-stop after no input */
        if (moving) {
            struct timeval now;
            gettimeofday(&now, NULL);
            long elapsed_us = (now.tv_sec - last_input.tv_sec) * 1000000L
                            + (now.tv_usec - last_input.tv_usec);
            if (elapsed_us > JOG_STOP_TIMEOUT_US) {
                write(sock, "stop\n", 5);
                moving = false;
            }
        }
    }

    restore_terminal();
    printf("\nExited jog mode.\n");
    return 0;
}

/* --- Main --- */

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage();
        return 1;
    }

    const char *subcmd = argv[1];

    if (strcmp(subcmd, "help") == 0 || strcmp(subcmd, "--help") == 0 ||
        strcmp(subcmd, "-h") == 0) {
        print_usage();
        return 0;
    }

    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    /* Connect to daemon */
    int sock = tcp_connect(DEFAULT_HOST, DEFAULT_PORT);
    if (sock < 0) {
        fprintf(stderr, "Error: daemon not active (is linkctl-daemon running?)\n");
        return 1;
    }

    int rc = 0;

    if (strcmp(subcmd, "status") == 0) {
        rc = cmd_status(sock);
    } else if (strcmp(subcmd, "center") == 0) {
        rc = cmd_center(sock);
    } else if (strcmp(subcmd, "zoom") == 0) {
        if (argc >= 3) {
            char *end;
            float val = strtof(argv[2], &end);
            if (*end != '\0' || val < 1.0f || val > 4.0f) {
                fprintf(stderr, "Error: zoom must be a number between 1.0 and 4.0\n");
                rc = 1;
            } else {
                rc = cmd_zoom_set(sock, val);
            }
        } else {
            rc = cmd_zoom_query(sock);
        }
    } else if (strcmp(subcmd, "stop") == 0) {
        rc = cmd_stop(sock);
    } else if (strcmp(subcmd, "jog") == 0) {
        rc = cmd_jog(sock);
    } else {
        fprintf(stderr, "Unknown command: %s\nRun 'linkctl help' for usage.\n", subcmd);
        rc = 1;
    }

    close(sock);
    return rc;
}
