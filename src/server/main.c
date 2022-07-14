#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>

#include <eyeq/server.h>

void print_usage(void) {
    printf("Usage: eyeq-server [--endpoint <address>] [--config <store configuration>]\n");
}


static char *configuration_file = "";
static store_list_t stores = { 0 };
static stream_list_t streams = { 0 };

void save_store_list(void) {
    if (strlen(configuration_file) > 0) {
        fprintf(stderr, "Saving store list!\n");
        save_store_list_to_file(configuration_file, &stores);
    }
}

int main(int argc, char *argv[]) {

    static struct option long_options[] = {
        { "endpoint", required_argument, 0, 'l' },
        { "config", required_argument, 0, 'c' },
        { 0, 0, 0, 0 }
    };

    char *listen_endpoint = "tcp://*:13450";

    int opt = 0;
    int long_index = 0;

    while ((opt = getopt_long_only(argc, argv, "", long_options, &long_index )) != -1) {
        switch (opt) {
            case 'l':
                listen_endpoint = optarg;
                break;
            case 'c':
                configuration_file = optarg;
                break;
            default:
                print_usage(); 
                exit(EXIT_FAILURE);
        }
    }

    printf("Eyeq Server v0.2\n");
    printf("Listening on: %s\n", listen_endpoint);

    // Load configuration file
    if (strlen(configuration_file) > 0) {
        load_store_list_from_file(configuration_file, &stores);
    }

    atexit(save_store_list);

    eyeq_server(listen_endpoint, &stores, &streams);

    return 0;
}
