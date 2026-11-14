#include <cstdlib>
#include <string>

#include "MediaServer.h"
#include "Registry.h"
#include <iostream>

int main() {
    std::string address = "0.0.0.0:50062";
    if (const char* env = std::getenv("AVPLAYER_MEDIA_SERVER_ADDRESS")) {
        address = env;
    }

    std::string storageRoot = std::string(AVPLAYER_PROJECT_ROOT) + "/storage";
    if (const char* env = std::getenv("AVPLAYER_STORAGE_ROOT")) {
        storageRoot = env;
    }

    auto server = av::StartMediaServer(address, storageRoot);
    if (!server) {
        return 1;
    }
    std::string advertised;
    if (const char* hosts = std::getenv("AVPLAYER_ZK_HOSTS"); hosts && *hosts) {
        const char* value = std::getenv("AVPLAYER_MEDIA_ADVERTISE");
        if (!value || !*value) {
            std::cerr << "AVPLAYER_MEDIA_ADVERTISE=reachable-host:port is required with ZooKeeper" << std::endl;
            return 2;
        }
        advertised = value;
    }
    av::Registry registration(advertised);
    server->Wait();
    return 0;
}
