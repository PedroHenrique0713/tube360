// Teste de viabilidade do YouTube nativo: o console baixa trechos MPEG-1 do notebook por TCP e mede quantos quadros por
// segundo a CPU decodifica com o pl_mpeg, em cada resolução, com e sem o áudio MP2. Sai pelo log (e pelo log de rede).
// server.txt ao lado do .xex: "<IP do notebook> <porta>".

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "xsdk/log.h"

#define PL_MPEG_IMPLEMENTATION
#include "pl_mpeg.h"

enum { MAX_STREAM = 16 << 20, RECV_CHUNK = 64 << 10 };

static double now_s(void)
{
	struct timespec t;
	(void)clock_gettime(CLOCK_MONOTONIC, &t);
	return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

static bool read_server(char *ip, size_t ip_size, int *port)
{
	static const char *const paths[] = { "game:\\server.txt", "server.txt" };
	for (size_t i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
		FILE *f = fopen(paths[i], "r");
		if (!f)
			continue;
		char line[128] = { 0 };
		const bool ok = fgets(line, sizeof(line), f) != NULL;
		fclose(f);
		if (ok && sscanf(line, "%63s %d", ip, port) == 2) {
			ip[ip_size - 1] = 0;
			return true;
		}
	}
	return false;
}

// Corpo inteiro de GET path; NULL se falhar. Mede o tempo da rede.
static uint8_t *http_get(const char *ip, int port, const char *path, size_t *size, double *seconds)
{
	const int fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		xsdk_log_write("FAIL: socket: %d", errno);
		return NULL;
	}
	struct sockaddr_in a = { 0 };
	a.sin_family = AF_INET;
	a.sin_port = htons((uint16_t)port);
	if (inet_pton(AF_INET, ip, &a.sin_addr) != 1) {
		xsdk_log_write("FAIL: IP invalido %s", ip);
		close(fd);
		return NULL;
	}
	if (connect(fd, (struct sockaddr *)&a, sizeof(a)) != 0) {
		xsdk_log_write("FAIL: connect %s:%d: %d", ip, port, errno);
		close(fd);
		return NULL;
	}
	char req[256];
	const int n = snprintf(req, sizeof(req), "GET %s HTTP/1.0\r\nHost: %s\r\n\r\n", path, ip);
	if (send(fd, req, (size_t)n, 0) != n) {
		xsdk_log_write("FAIL: send: %d", errno);
		close(fd);
		return NULL;
	}
	uint8_t *buf = malloc(MAX_STREAM);
	if (!buf) {
		xsdk_log_write("FAIL: malloc %d", MAX_STREAM);
		close(fd);
		return NULL;
	}
	size_t total = 0;
	const double t0 = now_s();
	for (;;) {
		const size_t room = MAX_STREAM - total;
		if (room == 0)
			break;
		const ssize_t got = recv(fd, buf + total, room < RECV_CHUNK ? room : RECV_CHUNK, 0);
		if (got < 0) {
			xsdk_log_write("FAIL: recv: %d", errno);
			break;
		}
		if (got == 0)
			break;
		total += (size_t)got;
	}
	*seconds = now_s() - t0;
	close(fd);
	uint8_t *body = NULL;
	for (size_t i = 0; i + 3 < total; i++) {
		if (memcmp(buf + i, "\r\n\r\n", 4) == 0) {
			body = buf + i + 4;
			break;
		}
	}
	if (!body) {
		xsdk_log_write("FAIL: resposta sem cabecalho (%u bytes)", (unsigned)total);
		free(buf);
		return NULL;
	}
	*size = total - (size_t)(body - buf);
	memmove(buf, body, *size);
	return buf;
}

static void bench(const char *ip, int port, const char *name)
{
	char path[64];
	snprintf(path, sizeof(path), "/%s.mpg", name);
	size_t size = 0;
	double net_s = 0;
	uint8_t *data = http_get(ip, port, path, &size, &net_s);
	if (!data)
		return;
	xsdk_log_write("rede %s: %u bytes em %.2f s = %.2f MB/s", name, (unsigned)size, net_s, size / 1e6 / net_s);

	// Só vídeo.
	plm_t *plm = plm_create_with_memory(data, size, 0);
	if (!plm) {
		xsdk_log_write("FAIL: plm_create %s", name);
		free(data);
		return;
	}
	plm_set_audio_enabled(plm, 0);
	const int w = plm_get_width(plm), h = plm_get_height(plm);
	int frames = 0;
	double worst = 0;
	const double t0 = now_s();
	for (;;) {
		const double f0 = now_s();
		plm_frame_t *frame = plm_decode_video(plm);
		if (!frame)
			break;
		const double d = now_s() - f0;
		if (d > worst)
			worst = d;
		frames++;
	}
	const double video_s = now_s() - t0;
	plm_destroy(plm);
	xsdk_log_write("video %s %dx%d: %d quadros em %.2f s = %.1f fps (pior quadro %.1f ms)", name, w, h, frames,
		       video_s, frames / video_s, worst * 1000);

	// Só áudio.
	plm = plm_create_with_memory(data, size, 0);
	if (plm) {
		plm_set_video_enabled(plm, 0);
		plm_set_audio_enabled(plm, 1);
		int samples = 0;
		const double a0 = now_s();
		plm_samples_t *s;
		while ((s = plm_decode_audio(plm)) != NULL)
			samples += (int)s->count;
		const double audio_s = now_s() - a0;
		const double media_s = samples / 44100.0;
		xsdk_log_write("audio %s: %.1f s de audio em %.2f s (%.0f%% de um nucleo)", name, media_s, audio_s,
			       media_s > 0 ? 100.0 * audio_s / media_s : 0);
		plm_destroy(plm);
	}
	free(data);
	xsdk_log_write("PASS: %s", name);
}

int main(void)
{
	char ip[64] = { 0 };
	int port = 0;
	if (!read_server(ip, sizeof(ip), &port)) {
		xsdk_log_write("FAIL: server.txt ausente");
		return 1;
	}
	xsdk_log_write("spike: servidor %s:%d", ip, port);
	bench(ip, port, "360");
	bench(ip, port, "480");
	bench(ip, port, "720");
	xsdk_log_write("spike: fim");
	return 0;
}
