#include "server.h"

int main() {
    Server s("127.0.0.1", "8989");

    s.run();
}
