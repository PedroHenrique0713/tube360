#include "app.hpp"
#include "public_key.hpp"
#include <xsdk/xam.h>
#include <cstdio>
int main() {
    updates::AppUpdate app("",updates::publicKeyHex);
    std::string error;
    auto path=app.prepare(error);
    if(path.empty()){std::printf("Tube360 updater: %s\n",error.c_str());return 1;}
    xsdk_launch(path.c_str(),nullptr,0);
    // ENOENT/path errors retornam aqui. Uma falha depois de XAM encerrar o launcher é recuperada na próxima abertura.
    if(app.failed(error))xsdk_launch(app.executable().c_str(),nullptr,0);
    std::printf("Tube360 updater: launch failed (%s)\n",error.c_str());
    return 1;
}
