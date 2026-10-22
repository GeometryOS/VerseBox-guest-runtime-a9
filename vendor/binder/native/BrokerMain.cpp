#include "Broker.h"
#include <stdio.h>
int main(int argc, char** argv) {
    if (argc != 2) { fprintf(stderr, "Usage: binder-broker SOCKET_PATH\n"); return 2; }
    return a10binder::Broker().run(argv[1]);
}
