#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <winsock2.h>
#include <windows.h>
#include <process.h>

#pragma comment(lib,"ws2_32.lib")

#define MAX_CLIENTS 10
#define BUFFER_SIZE 512

typedef struct {
    SOCKET sock;
    char name[50];
} Client;

Client* clients[MAX_CLIENTS];
CRITICAL_SECTION cs;

// Broadcast message to all clients except sender
void broadcast(char* message, SOCKET exclude_sock) {
    EnterCriticalSection(&cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i]) {
            if (clients[i]->sock != exclude_sock) {
                send(clients[i]->sock, message, strlen(message), 0);
            }
        }
    }
    LeaveCriticalSection(&cs);
}

// Send private message
void private_msg(char* message, char* target_name) {
    EnterCriticalSection(&cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] && strcmp(clients[i]->name, target_name) == 0) {
            send(clients[i]->sock, message, strlen(message), 0);
            break;
        }
    }
    LeaveCriticalSection(&cs);
}

// Thread to handle client
unsigned __stdcall handle_client(void* arg) {
    char buffer[BUFFER_SIZE];
    char msg[BUFFER_SIZE + 50];
    Client* cli = (Client*)arg;

    // Receive name
    int recv_len = recv(cli->sock, cli->name, 50, 0);
    cli->name[recv_len] = '\0';
    sprintf(msg, "%s joined the chat\n", cli->name);
    printf("%s", msg);
    broadcast(msg, cli->sock);

    while (1) {
        int len = recv(cli->sock, buffer, BUFFER_SIZE - 1, 0);
        if (len <= 0) break;
        buffer[len] = '\0';

        if (strncmp(buffer, "/msg ", 5) == 0) {
            char target[50], *message_text;
            message_text = strchr(buffer + 5, ' ');
            if (message_text) {
                *message_text = '\0';
                message_text++;
                strcpy(target, buffer + 5);
                sprintf(msg, "[Private from %s]: %s\n", cli->name, message_text);
                private_msg(msg, target);
            }
        } else {
            sprintf(msg, "[%s]: %s\n", cli->name, buffer);
            broadcast(msg, cli->sock);
        }
    }

    // Remove client
    closesocket(cli->sock);
    EnterCriticalSection(&cs);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clients[i] == cli) {
            clients[i] = NULL;
            break;
        }
    }
    LeaveCriticalSection(&cs);

    sprintf(msg, "%s left the chat\n", cli->name);
    printf("%s", msg);
    broadcast(msg, -1);
    free(cli);
    return 0;
}

int main() {
    WSADATA wsa;
    SOCKET server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    int addr_len = sizeof(client_addr);

    InitializeCriticalSection(&cs);

    if (WSAStartup(MAKEWORD(2,2), &wsa) != 0) {
        printf("Failed to initialize Winsock\n");
        return 1;
    }

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == INVALID_SOCKET) {
        printf("Could not create socket\n");
        return 1;
    }

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        printf("Bind failed\n");
        return 1;
    }

    listen(server_sock, MAX_CLIENTS);
    printf("Server listening on port 8080...\n");

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr*)&client_addr, &addr_len);
        if (client_sock == INVALID_SOCKET) continue;

        Client* cli = (Client*)malloc(sizeof(Client));
        cli->sock = client_sock;

        EnterCriticalSection(&cs);
        int added = 0;
        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (!clients[i]) {
                clients[i] = cli;
                added = 1;
                break;
            }
        }
        LeaveCriticalSection(&cs);

        if (!added) {
            char* msg = "Server full!\n";
            send(cli->sock, msg, strlen(msg), 0);
            closesocket(cli->sock);
            free(cli);
        } else {
            _beginthreadex(NULL, 0, handle_client, (void*)cli, 0, NULL);
        }
    }

    closesocket(server_sock);
    WSACleanup();
    DeleteCriticalSection(&cs);
    return 0;
}
