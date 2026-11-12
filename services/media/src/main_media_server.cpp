#include <cstdlib>
#include <string>

#include "MediaServer.h"

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
    server->Wait();
    return 0;
}
