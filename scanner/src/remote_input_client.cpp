#include <WinSock2.h>
#include <Windows.h>
#include <WS2tcpip.h>

#include <cstdint>
#include <iostream>
#include <string>

#include "coop_protocol.h"

namespace {

constexpr DWORD kSendIntervalMs = 16;

coop::InputPacket read_local_input(std::uint32_t sequence) {
    coop::InputPacket packet{};
    packet.sequence = sequence;
    packet.timestamp_ms = GetTickCount();

    if ((GetAsyncKeyState('W') & 0x8000) != 0) {
        packet.move_forward += 1.0F;
        packet.buttons |= 1;
    }
    if ((GetAsyncKeyState('S') & 0x8000) != 0) {
        packet.move_forward -= 1.0F;
        packet.buttons |= 2;
    }
    if ((GetAsyncKeyState('A') & 0x8000) != 0) {
        packet.move_right -= 1.0F;
        packet.buttons |= 4;
    }
    if ((GetAsyncKeyState('D') & 0x8000) != 0) {
        packet.move_right += 1.0F;
        packet.buttons |= 8;
    }

    return packet;
}

}  // namespace

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";

    WSADATA wsa_data{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        std::cerr << "socket failed\n";
        WSACleanup();
        return 2;
    }

    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(coop::kHostInputPort);
    if (InetPtonA(AF_INET, host, &destination.sin_addr) != 1) {
        std::cerr << "Invalid IPv4 address: " << host << '\n';
        closesocket(sock);
        WSACleanup();
        return 3;
    }

    std::cout
        << "Sending InputPacket to " << host << ':' << coop::kHostInputPort
        << '\n'
        << "Hold WASD to transmit movement, press End to quit.\n";

    std::uint32_t sequence = 1;
    while ((GetAsyncKeyState(VK_END) & 0x8000) == 0) {
        const coop::InputPacket packet = read_local_input(sequence++);
        const int sent = sendto(
            sock,
            reinterpret_cast<const char*>(&packet),
            sizeof(packet),
            0,
            reinterpret_cast<const sockaddr*>(&destination),
            sizeof(destination)
        );
        if (sent != sizeof(packet)) {
            std::cerr << "sendto failed: " << WSAGetLastError() << '\n';
            break;
        }
        Sleep(kSendIntervalMs);
    }

    closesocket(sock);
    WSACleanup();
    return 0;
}
