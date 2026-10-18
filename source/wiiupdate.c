#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <gccore.h>
#include <network.h>
#include <poll.h>
#include <unistd.h>
#include <ogc/lwp_watchdog.h>
#include <mbedtls/ssl.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/platform_time.h>
#include "wiiupdate.h"
#include "wiiupdate_certs.h"

int wiiupdate_newest_tag(const char *json, char *out, size_t out_len);

#define API_HOST    "api.github.com"
#define TIMEOUT_MS  15000
#define MAX_BODY    (256 * 1024)
#define STACK_SIZE  (64 * 1024)

static volatile wiiupdate_state state = WIIUPDATE_IDLE;
static volatile int cancelled;
static volatile int noticed;
static volatile int running; /* thread alive, even after a cancel */
static char owner[64], repo[96], current[48], latest[48], url[200], message[200];
static lwp_t thread = LWP_THREAD_NULL;
static u8 stack[STACK_SIZE] ATTRIBUTE_ALIGN(32);

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    return (mbedtls_ms_time_t)ticks_to_millisecs(gettime());
}

/* ── Network ─────────────────────────────────────────────────────────────── */

/* The Wii has no exposed hardware RNG; gather timing jitter from the CPU
 * timebase while doing memory work, which varies with bus and cache state. */
static int wii_entropy(void *data, unsigned char *out, size_t len, size_t *olen)
{
    (void)data;
    static volatile u32 scratch[256];
    for (size_t i = 0; i < len; i++) {
        u32 acc = 0;
        for (int k = 0; k < 16; k++) {
            u64 t0 = gettime();
            for (int n = 0; n < (int)(t0 & 31) + 8; n++) scratch[(t0 + n * 37) & 255] += (u32)t0 ^ n;
            u64 t1 = gettime();
            acc = (acc << 3 | acc >> 29) ^ (u32)(t1 - t0) ^ (u32)t1;
        }
        out[i] = (unsigned char)(acc ^ acc >> 8 ^ acc >> 16 ^ acc >> 24);
    }
    *olen = len;
    return 0;
}

static int wait_fd(int fd, int events)
{
    /* Poll in short slices so a cancel is noticed quickly. */
    for (int waited = 0; waited < TIMEOUT_MS; waited += 250) {
        if (cancelled) return -1;
        struct pollsd p = { .socket = fd, .events = events, .revents = 0 };
        int ret = net_poll(&p, 1, 250);
        if (ret < 0) return -1;
        if (ret > 0) return 0;
    }
    return -1;
}

static int sock_send(void *ctx, const unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;
    if (wait_fd(fd, POLLOUT) < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = net_send(fd, buf, len > 16384 ? 16384 : len, 0);
    if (ret == -EAGAIN) return MBEDTLS_ERR_SSL_WANT_WRITE;
    return ret < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : ret;
}

static int sock_recv(void *ctx, unsigned char *buf, size_t len)
{
    int fd = (int)(intptr_t)ctx;
    if (wait_fd(fd, POLLIN) < 0) return MBEDTLS_ERR_SSL_TIMEOUT;
    int ret = net_recv(fd, buf, len, 0);
    if (ret == -EAGAIN) return MBEDTLS_ERR_SSL_WANT_READ;
    return ret < 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : ret;
}

static int net_up(void)
{
    if (net_gethostip()) return 0;
    for (int attempt = 0; attempt < 3 && !cancelled; attempt++) {
        char ip[16] = "", mask[16] = "", gw[16] = "";
        if (if_config(ip, mask, gw, true, 20) >= 0) return 0;
    }
    return -1;
}

static int tcp_connect(void)
{
    struct hostent *he = net_gethostbyname(API_HOST);
    if (!he || he->h_addrtype != AF_INET || !he->h_addr_list[0]) return -1;
    int fd = net_socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) return -1;
    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons(443);
    memcpy(&sa.sin_addr, he->h_addr_list[0], sizeof sa.sin_addr);
    if (net_connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        net_close(fd);
        return -1;
    }
    return fd;
}

static int dechunk(char *body, size_t len, size_t *out_len)
{
    size_t r = 0, w = 0;
    while (r < len) {
        char *line_end = strstr(body + r, "\r\n");
        if (!line_end) break;
        size_t size = strtoul(body + r, NULL, 16);
        r = line_end - body + 2;
        if (size == 0) break;
        if (r + size > len) size = len - r; /* truncated read: keep what we have */
        memmove(body + w, body + r, size);
        w += size;
        r += size + 2;
    }
    body[w] = 0;
    *out_len = w;
    return 0;
}

/* GET /repos/<owner>/<repo>/releases over HTTPS into a malloc'd buffer. */
static char *fetch_releases(void)
{
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context drbg;
    mbedtls_x509_crt cas;
    mbedtls_ssl_config conf;
    mbedtls_ssl_context ssl;
    char *buf = NULL;
    int fd = -1, ok = 0;

    mbedtls_entropy_init(&entropy);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_x509_crt_init(&cas);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ssl_init(&ssl);
    mbedtls_entropy_add_source(&entropy, wii_entropy, NULL, 32, MBEDTLS_ENTROPY_SOURCE_STRONG);

    char personal[64];
    snprintf(personal, sizeof personal, "libwiiupdate %llu %lld", (unsigned long long)gettime(), (long long)time(NULL));
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy, (const unsigned char *)personal, strlen(personal)))
        goto out;
    if (mbedtls_x509_crt_parse(&cas, (const unsigned char *)WIIUPDATE_CA_PEM, sizeof WIIUPDATE_CA_PEM) < 0) goto out;
    if (mbedtls_ssl_config_defaults(&conf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM,
                                    MBEDTLS_SSL_PRESET_DEFAULT))
        goto out;
    mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    mbedtls_ssl_conf_ca_chain(&conf, &cas, NULL);
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);
    if (mbedtls_ssl_setup(&ssl, &conf) || mbedtls_ssl_set_hostname(&ssl, API_HOST)) goto out;

    if (cancelled || (fd = tcp_connect()) < 0) goto out;
    mbedtls_ssl_set_bio(&ssl, (void *)(intptr_t)fd, sock_send, sock_recv, NULL);

    int ret;
    while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
        if (cancelled || (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)) goto out;
    }

    char req[384];
    int req_len = snprintf(req, sizeof req,
                           "GET /repos/%s/%s/releases?per_page=10 HTTP/1.1\r\n"
                           "Host: " API_HOST "\r\n"
                           "User-Agent: libwiiupdate/1.0 (%s)\r\n"
                           "Accept: application/vnd.github+json\r\n"
                           "Accept-Encoding: identity\r\n"
                           "Connection: close\r\n\r\n",
                           owner, repo, repo);
    for (int sent = 0; sent < req_len;) {
        ret = mbedtls_ssl_write(&ssl, (const unsigned char *)req + sent, req_len - sent);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret <= 0 || cancelled) goto out;
        sent += ret;
    }

    buf = malloc(MAX_BODY + 1);
    if (!buf) goto out;
    size_t len = 0;
    while (len < MAX_BODY && !cancelled) {
        ret = mbedtls_ssl_read(&ssl, (unsigned char *)buf + len, MAX_BODY - len);
        if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) continue;
        if (ret <= 0) break; /* close_notify, EOF or error: parse what arrived */
        len += ret;
    }
    buf[len] = 0;

    if (strncmp(buf, "HTTP/1.", 7) || strncmp(buf + 9, "200", 3)) goto out;
    char *body = strstr(buf, "\r\n\r\n");
    if (!body) goto out;
    *body = 0;
    int chunked = strcasestr(buf, "\ntransfer-encoding: chunked") != NULL;
    body += 4;
    size_t body_len = len - (body - buf);
    memmove(buf, body, body_len + 1);
    if (chunked) dechunk(buf, body_len, &body_len);
    ok = 1;

out:
    if (fd >= 0) {
        mbedtls_ssl_close_notify(&ssl);
        net_close(fd);
    }
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_x509_crt_free(&cas);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    if (!ok) {
        free(buf);
        buf = NULL;
    }
    return buf;
}

/* ── Thread and public API ───────────────────────────────────────────────── */

static void *check_thread(void *arg)
{
    (void)arg;
    wiiupdate_state result = WIIUPDATE_FAILED;
    char tag[sizeof latest];

    if (net_up() == 0 && !cancelled) {
        char *json = fetch_releases();
        if (json) {
            if (wiiupdate_newest_tag(json, tag, sizeof tag) == 0 && WIIUPDATE_CompareVersions(tag, current) > 0) {
                snprintf(latest, sizeof latest, "%s", tag);
                snprintf(message, sizeof message, "Version %s is available (you have %s%s).", latest,
                         isdigit((unsigned char)current[0]) ? "v" : "", current);
                result = WIIUPDATE_AVAILABLE;
            } else {
                result = WIIUPDATE_UP_TO_DATE;
            }
            free(json);
        }
    }
    state = cancelled ? WIIUPDATE_CANCELLED : result;
    running = 0;
    return NULL;
}

int WIIUPDATE_Start(const char *o, const char *r, const char *version)
{
    if (running) return -1;
    snprintf(owner, sizeof owner, "%s", o);
    snprintf(repo, sizeof repo, "%s", r);
    snprintf(current, sizeof current, "%s", version ? version : "0");
    snprintf(url, sizeof url, "github.com/%s/%s/releases", owner, repo);
    latest[0] = message[0] = 0;
    cancelled = noticed = 0;
    state = WIIUPDATE_CHECKING;
    running = 1;
    if (LWP_CreateThread(&thread, check_thread, NULL, stack, STACK_SIZE, 40) < 0) {
        running = 0;
        state = WIIUPDATE_FAILED;
        return -1;
    }
    return 0;
}

wiiupdate_state WIIUPDATE_State(void) { return state; }

wiiupdate_state WIIUPDATE_Wait(int timeout_ms)
{
    u64 start = gettime();
    while (state == WIIUPDATE_CHECKING) {
        if (timeout_ms >= 0 && ticks_to_millisecs(diff_ticks(start, gettime())) >= (u64)timeout_ms) break;
        usleep(10000);
    }
    return state;
}

void WIIUPDATE_Cancel(void)
{
    cancelled = 1;
    if (state == WIIUPDATE_CHECKING || state == WIIUPDATE_AVAILABLE) state = WIIUPDATE_CANCELLED;
}

int WIIUPDATE_TakeNotice(void)
{
    if (state != WIIUPDATE_AVAILABLE || noticed) return 0;
    noticed = 1;
    return 1;
}

const char *WIIUPDATE_Latest(void) { return latest; }
const char *WIIUPDATE_Current(void) { return current; }
const char *WIIUPDATE_Url(void) { return url; }
const char *WIIUPDATE_Message(void) { return message; }
