#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <rfb/rfbclient.h>
#include <rfb/rfb.h>

union pixel_rgba {
	struct {
		uint8_t a;
		uint8_t b;
		uint8_t g;
		uint8_t r;
	};
	uint32_t rgba;
};

static rfbBool resize(rfbClient* vnc_client) {
	void* buffer = calloc(vnc_client->width * vnc_client->height, sizeof(union pixel_rgba));
	if(!buffer) {
		fprintf(stderr, "Failed to allocate client framebuffer, out of memory\n");
		return FALSE;
	}
	vnc_client->frameBuffer = buffer;
	return TRUE;
}

#ifndef max
#define max(a, b) ((a) > (b) ? (a) : (b))
#endif
#ifndef min
#define min(a, b) ((a) < (b) ? (a) : (b))
#endif

static void fbcopy(union pixel_rgba* dst, unsigned int dst_width, unsigned int dst_height,
					union pixel_rgba* src, unsigned int src_width, unsigned int src_height) {
	unsigned int height = min(dst_height, src_height);
	unsigned int width = min(dst_width, src_width);
	printf("Updating %ux%u pixels\n", width, height);
	while(height--) {
		union pixel_rgba* dst_line_base = dst + (dst_width * height);
		union pixel_rgba* src_line_base = src + (src_width * height);
		memcpy(dst_line_base, src_line_base, width * sizeof(union pixel_rgba));
	}
}

static void update(rfbClient* vnc_client, int x, int y, int w, int h) {
	
}

static void usage(char* binary) {
	fprintf(stderr, "Usage: %s [-w <width>] [-h <height>] [-r <update rate>] [-l <listen port>] <vnc host[:port]>\n", binary);
	exit(1);
}

inline long long int get_timespec_diff(struct timespec* a, struct timespec* b) {
	return (a->tv_sec - b->tv_sec) * 1000000000LL + (a->tv_nsec - b->tv_nsec);
}

int main(int argc, char** argv) {
	int err, opt, tmp;
	unsigned int width = 1920, height = 1080;
	rfbScreenInfoPtr vnc_server;
	rfbClient* vnc_client;
	char* binary = argv[0];
	unsigned short port = 5900;
	unsigned short listen_port = 5901;
	char* host = "127.0.0.1";
	char* port_delim;
	bool do_exit = false;
	struct timespec before, after;
	long long time_delta;
	int screen_update_rate = 60;

	while((opt = getopt(argc, argv, "w:h:r:l:?")) != -1) {
		switch(opt) {
			case 'w':
				tmp = atoi(optarg);
				if(tmp < 0) {
					err = EINVAL;
					fprintf(stderr, "Invalid width '%s'\n", optarg);
					goto fail;
				}
				width = tmp;
				break;
			case 'h':
				tmp = atoi(optarg);
				if(tmp < 0) {
					err = EINVAL;
					fprintf(stderr, "Invalid height '%s'\n", optarg);
					goto fail;
				}
				height = tmp;
				break;
			case 'r':
				tmp = atoi(optarg);
				if(tmp <= 0) {
					err = EINVAL;
					fprintf(stderr, "Invalid rate '%s'\n", optarg);
					goto fail;
				}
				screen_update_rate = tmp;
				break;
			case 'l':
				tmp = atoi(optarg);
				if(tmp < 0 || tmp > 65535) {
					err = EINVAL;
					fprintf(stderr, "Invalid listen port '%s'\n", optarg);
					goto fail;
				}
				listen_port = tmp;
				break;
			default:
				usage(binary);
		}
	}

	argv += optind;
	argc -= optind;
	optind = 0;

	if(argc < 1) {
		usage(binary);
	}

	host = argv[0];
	if((port_delim = strchr(host, ':'))) {
		*port_delim = '\0';
		tmp = atoi(port_delim + 1);
		if(tmp < 0 || port > 65535) {
			err = EINVAL;
			fprintf(stderr, "Invalid port '%d'\n", tmp); 
			goto fail;
		}
		port = tmp;
	}

	vnc_client = rfbGetClient(8, 3, 4);
	if(!vnc_client) {
		err = ENOMEM;
		goto fail;
	}

	vnc_client->width = width;
	vnc_client->height = height;
	vnc_client->MallocFrameBuffer = resize;
	vnc_client->GotFrameBufferUpdate = update;
	vnc_client->canHandleNewFBSize = TRUE;
	if(!resize(vnc_client)) {
		err = ENOMEM;
		fprintf(stderr, "Failed to set initial framebuffer\n");
		goto fail_client;
	}

//	vnc_client->serverHost = strdup(host);
//	vnc_client->serverPort = port;

	printf("Connecting to %s:%d\n", host, port);
	if(!ConnectToRFBServer(vnc_client, host, port)) {
		err = 1;
		fprintf(stderr, "Failed to initialize VNC client\n");
		goto fail_client;
	}

	vnc_server = rfbGetScreen(NULL, NULL, width, height, 8, 3, 4);
	if(!vnc_server) {
		err = ENOMEM;
		fprintf(stderr, "Failed to initialize VNC server\n");
		goto fail_server;
	}

	vnc_server->autoPort = FALSE;
	vnc_server->port = vnc_server->ipv6port = listen_port;
	vnc_server->frameBuffer = calloc(width * height, sizeof(union pixel_rgba));
	if(!vnc_server->frameBuffer) {
		err = ENOMEM;
		fprintf(stderr, "Failed to allocate server framebuffer, out of memory\n");
		goto fail_server;
	}

	rfbInitServer(vnc_server);
	rfbRunEventLoop(vnc_server, -1, TRUE);

	while(!do_exit) {
		clock_gettime(CLOCK_MONOTONIC, &before);

		fbcopy((union pixel_rgba*)vnc_server->frameBuffer, width, height,
			   (union pixel_rgba*)vnc_client->frameBuffer, vnc_client->width, vnc_client->height);
		printf("Update\n");
		rfbMarkRectAsModified(vnc_server, 0, 0, width, height);

		clock_gettime(CLOCK_MONOTONIC, &after);
		time_delta = get_timespec_diff(&after, &before);
		time_delta = 1000000000UL / screen_update_rate - time_delta;
		if(time_delta > 0) {
			usleep(time_delta / 1000UL);
		}
	}

fail_server_fb:
	free(vnc_server->frameBuffer);
fail_server:
	rfbScreenCleanup(vnc_server);
fail_client:
	rfbClientCleanup(vnc_client);
fail:
	return err;
}
