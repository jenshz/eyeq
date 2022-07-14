#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <strings.h>
#include <inttypes.h>

#include <slash/slash.h>
#include <eyeq/client.h>

#define EYEQ_LINE_SIZE    256
#define EYEQ_HISTORY_SIZE 2048
#define EYEQ_PROMPT_GOOD "\033[96meyeq \033[90m%\033[0m "
#define EYEQ_PROMPT_BAD  "\033[96meyeq \033[31m!\033[0m "

void usage(void)
{
	printf("usage: eyeq [command]\n");
	printf("\n");
	printf("Options:\n");
	printf(" -e ENDPOINT,\tUse ENDPOINT as EyeQ endpoint (default: tcp://localhost:13450)\n");
	printf(" -t TIMEOUT,\tUse TIMEOUT as query timeout in ms. (default 10000).\n");
}

int main(int argc, char **argv) {
	int p = 0;
	char *ex;
	char c;
	char *endpoint = "tcp://localhost:13450";
	uint32_t timeout = 10000;

	while ((c = getopt(argc, argv, "+he:t:")) != (char)-1) {
		switch (c) {
		case 'h':
			usage();
			exit(EXIT_SUCCESS);
		case 'e':
			endpoint = optarg;
			break;
		case 't':
			timeout = strtoul(optarg, NULL, 10);
			break;
		default:
			exit(EXIT_FAILURE);
		}
	}

	int remain = argc - optind;
	int index = optind;

	fprintf(stderr, "Using '%s' as EyeQ endpoint.\n", endpoint);

    void *eyeq_ctx = eyeq_create_context();
	eyeq_client_t *eyeq_client = eyeq_connect(endpoint, eyeq_ctx, -1);
	eyeq_client->timeout_ms = timeout;

	init_eyeq_slash_client(eyeq_client);

	static struct slash *slash;

	slash = slash_create(EYEQ_LINE_SIZE, EYEQ_HISTORY_SIZE);
	if (!slash) {
		fprintf(stderr, "Failed to init slash\n");
		exit(EXIT_FAILURE);
	}

	// Interactive or one-shot mode
	if (remain > 0) {
		ex = malloc(EYEQ_LINE_SIZE);
		if (!ex) {
			fprintf(stderr, "Failed to allocate command memory");
			exit(EXIT_FAILURE);
		}

		// Build command string
		for (int i = 0; i < remain; i++) {
			if (i > 0) {
				p = ex - strncat(ex, " ", EYEQ_LINE_SIZE - p);
			}
			p = ex - strncat(ex, argv[index + i], EYEQ_LINE_SIZE - p);
		}
		slash_execute(slash, ex);
		free(ex);
	} else {
		slash_loop(slash, EYEQ_PROMPT_GOOD, EYEQ_PROMPT_BAD);
	}

	slash_destroy(slash);

	return 0;
}

