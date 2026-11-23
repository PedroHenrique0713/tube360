#pragma once
#include <memory>
#include <string>

namespace cast {
inline constexpr const char* connection = "urn:x-cast:com.google.cast.tp.connection";
inline constexpr const char* heartbeat = "urn:x-cast:com.google.cast.tp.heartbeat";
inline constexpr const char* receiver = "urn:x-cast:com.google.cast.receiver";
inline constexpr const char* media = "urn:x-cast:com.google.cast.media";
inline constexpr const char* app = "CC1AD845";
struct Message { std::string source, destination, space, json; };
// Um único worker possui o canal TLS. read preserva mensagens e cabeçalhos fragmentados.
class Connection {
    struct Impl;
    std::unique_ptr<Impl> impl;
public:
    Connection();
    ~Connection();
    bool open(const std::string& host, int port);
    void close();
    bool send(const std::string& source, const std::string& destination,
              const std::string& space, const std::string& json);
    int read(Message&, int timeoutMs); // 1 mensagem, 0 aguardando, -1 desconectado/protocolo inválido
};
}
