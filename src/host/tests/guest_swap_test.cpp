// The redeploy's guest half: what the file server answers for each name,
// and what the swap script does with the window mirror's guest half - the
// in-use file renamed aside before the new one moves in, never replaced
// in place, and nothing of it at all when there is no mirror to send.

#include "lsw/host/services/guest_file_server.hpp"
#include "lsw/host/services/guest_swap_script.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <string_view>

using lsw::host::services::GuestFileServer;
using lsw::host::services::GuestSwapScript;

namespace {

int g_failures = 0;

void check(bool ok, const char* label) {
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", label);
    if (!ok) {
        ++g_failures;
    }
}

// The whole response to one GET, as curl in the guest would read it.
std::string fetch(unsigned short port, std::string_view path) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    std::string response;
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
        const std::string request =
            "GET " + std::string{path} + " HTTP/1.1\r\nHost: 10.0.2.2\r\n\r\n";
        (void)::send(fd, request.data(), request.size(), MSG_NOSIGNAL);
        char buf[4096];
        for (ssize_t n = 0; (n = ::recv(fd, buf, sizeof(buf), 0)) > 0;) {
            response.append(buf, static_cast<std::size_t>(n));
        }
    }
    ::close(fd);
    return response;
}

bool ends_with(const std::string& text, std::string_view tail) {
    return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
}

void serves_by_name() {
    check(GuestFileServer::requested_name("GET /remotewin-guest.exe HTTP/1.1\r\n") ==
              "remotewin-guest.exe",
          "the name is the request line's path without its slash");
    check(GuestFileServer::requested_name("GET /lsw-guest-agent.exe?x=1 HTTP/1.1\r\n") ==
              "lsw-guest-agent.exe",
          "a query is not part of the name");
    check(GuestFileServer::requested_name("garbage").empty(), "no request line, no name");

    GuestFileServer server{{{GuestSwapScript::kAgentFile, QByteArray("AGENT")},
                            {GuestSwapScript::kMirrorFile, QByteArray("MIRROR")}}};
    check(server.start(), "the server starts");
    const std::string agent = fetch(server.port(), "/lsw-guest-agent.exe");
    const std::string mirror = fetch(server.port(), "/remotewin-guest.exe");
    const std::string missing = fetch(server.port(), "/other.exe");
    check(agent.rfind("HTTP/1.1 200", 0) == 0 && ends_with(agent, "\r\n\r\nAGENT"),
          "the agent is served under its name");
    check(mirror.rfind("HTTP/1.1 200", 0) == 0 && ends_with(mirror, "\r\n\r\nMIRROR"),
          "the mirror's guest half is served under its name");
    check(missing.rfind("HTTP/1.1 404", 0) == 0, "any other name is not found");
    check(server.downloads_served() == 2, "only the two served count as downloads");
    server.stop();
}

void swaps_the_mirror_aside() {
    const std::string script = GuestSwapScript{40123, true}.text();
    check(script.find("__PORT__") == std::string::npos, "every port is filled in");
    check(script.find("http://10.0.2.2:40123/lsw-guest-agent.exe") != std::string::npos,
          "the agent is fetched from the server");
    check(script.find("http://10.0.2.2:40123/remotewin-guest.exe") != std::string::npos,
          "the mirror's guest half is fetched from the server");
    const auto aside = script.find(R"(move /Y "%RW%" "%DIR%\remotewin-guest.)");
    const auto in = script.find(R"(move /Y "%RWNEW%" "%RW%")");
    check(aside != std::string::npos && in != std::string::npos && aside < in,
          "the mirror in place is renamed aside before the new one moves in");
    const auto cleared = script.find(R"(del /Q "%DIR%\remotewin-guest.*.old.exe")");
    check(cleared != std::string::npos && cleared < aside,
          "earlier aside copies are deleted first, the ones still running failing quietly");
    check(script.find("[swap] mirror download failed, keeping the old one") != std::string::npos,
          "a failed mirror download leaves the old one");
    const auto stop = script.find("sc.exe stop lsw-agent");
    check(stop != std::string::npos && in < stop,
          "the mirror is swapped before the agent is stopped");
    check(script.find("sc.exe start lsw-agent") != std::string::npos,
          "the agent's service is started again");

    const std::string agent_only = GuestSwapScript{40123, false}.text();
    check(agent_only.find("remotewin") == std::string::npos,
          "with no mirror to send, the script leaves the guest's alone");
    check(agent_only.find("sc.exe start lsw-agent") != std::string::npos,
          "and still swaps the agent");
}

}  // namespace

int main() {
    serves_by_name();
    swaps_the_mirror_aside();
    return g_failures == 0 ? 0 : 1;
}
