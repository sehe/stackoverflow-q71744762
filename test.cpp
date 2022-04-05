#include "server.h"

int main() {
    Server s("127.0.0.1", "8989");

    std::thread yolo([&s] {
        using namespace std::literals;
        int i = 1;

        do {
            std::this_thread::sleep_for(5s);
        } while (s.deliver("HEARTBEAT DEMO " + std::to_string(i++)));
    });

    s.run();

    yolo.join();
}
