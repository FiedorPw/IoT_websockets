#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>     // Biblioteka zawierająca funkcje do obsługi socketów
#include <arpa/inet.h>      // Biblioteka dla operacji internetowych
#include <unistd.h>         // Dla funkcji close() i sleep()
#include <time.h>            // for time()
#include <sys/time.h>       // for gettimeofday()


#define CLIENT_PORT 1305    // Port klienta
#define BUFFER_SIZE 1024    // Rozmiar bufora
#define PING_MESSAGE_LEN 13 // Długość wiadomości PING do serwera

#define MAX_SERVERS 10

#define MAX_IP_LENGTH 16
#define UP 1
#define DOWN 0

// headery
#define HELLO 'h'
// Ping
#define PING 'i'
#define PONG 'o'
// request
#define REQUEST 'q'
#define RESPONSE 's'

// tymczasowe

#define REQUEST_INTERVAL 0.27  // 270ms
#define MAX_REQUEST_ATTEMPTS 3


// Zainicjuj generator liczb losowych (zrób to tylko raz w programie)
static int seeded = 0;

struct ServerInfo {
    int id;
    char ip[MAX_IP_LENGTH];
    int port;
    int status;  // UP or DOWN
    int failed_requests;  // Added: count failed requests
    time_t last_request_time;  // Added: track last request time
    long last_request_time_usec;
};

struct PingInfo {
    struct timeval start_time;
    int waiting_for_pong;
} ping_state = {.waiting_for_pong = 0};

// Global array of servers
struct ServerInfo servers[MAX_SERVERS];
int server_count = 0;

// Function prototypes
// char* generate_random_string(int length);
char* add_header(char* message, char header);
char* process_header(char* message);  // Added this line
void server_hello_handler(char* message, const char* ip, int port);
char* generate_random_string(int length);
char* add_header(char* message, char header);
void server_hello_handler(char* message, const char* ip, int port);
void print_servers();
void client_listen(int server_socket, struct sockaddr_in sender_addr, socklen_t client_len);
void init_random_generator_seed();
void send_pings(int client_socket);
int get_random_active_server();

void send_keep_alive_check(int client_socket) {
    struct timeval now;
    gettimeofday(&now, NULL);
    double current_time_ms = now.tv_sec + (now.tv_usec / 1000000.0);

    for(int i = 0; i < server_count; i++) {
        // Only check servers that are UP
        if(servers[i].status == UP) {
            double time_since_last = current_time_ms -
                                   (servers[i].last_request_time +
                                    (servers[i].last_request_time_usec / 1000000.0));

            if(time_since_last >= REQUEST_INTERVAL) {
                // Prepare server address
                struct sockaddr_in server_addr;
                memset(&server_addr, 0, sizeof(server_addr));
                server_addr.sin_family = AF_INET;
                server_addr.sin_port = htons(servers[i].port);
                inet_pton(AF_INET, servers[i].ip, &server_addr.sin_addr);

                // Send single character request
                char request = REQUEST;
                printf("\033[34mSending raw message: [%c]\033[0m\n", request);
                sendto(client_socket,
                      &request,
                      1,
                      0,
                      (struct sockaddr*)&server_addr,
                      sizeof(server_addr));

                printf("\033[33mSent REQUEST to server %d (attempt %d)\033[0m\n",
                       servers[i].id, servers[i].failed_requests + 1);

                // Update last request time
                servers[i].last_request_time = now.tv_sec;
                servers[i].last_request_time_usec = now.tv_usec;

                // Increment failed attempts
                servers[i].failed_requests++;

                // Check if max attempts reached
                if(servers[i].failed_requests >= MAX_REQUEST_ATTEMPTS) {
                    printf("\033[31mServer %d not responding, marking as DOWN\033[0m\n",
                           servers[i].id);
                    servers[i].status = DOWN;
                    servers[i].failed_requests = 0;  // Reset counter
                    print_servers();  // Show updated server list
                }
            }
        }
    }
}

void handle_pong_response(const char* message, struct timeval* current_time) {
    struct timeval end_time = *current_time;
    if (ping_state.waiting_for_pong) {
        // Calculate RTT in milliseconds
        double rtt = (end_time.tv_sec - ping_state.start_time.tv_sec) * 1000.0 +
                    (end_time.tv_usec - ping_state.start_time.tv_usec) / 1000.0;

        // Get current timestamp
        time_t now = time(NULL);
        char* timestamp = ctime(&now);
        timestamp[strlen(timestamp)-1] = '\0';  // Remove newline

        printf("\033[36mPONG received: %s, RTT: %.3f ms, Time: %s\033[0m\n",
               message,
               rtt,
               timestamp);

        ping_state.waiting_for_pong = 0;
    }
}

int get_random_active_server() {
    if (server_count == 0) return -1;

    // Count active servers
    int active_servers = 0;
    for(int i = 0; i < server_count; i++) {
        if(servers[i].status == UP) {
            active_servers++;
        }
    }

    if (active_servers == 0) return -1;

    // Select random active server
    int target = rand() % active_servers;
    int current = 0;

    // Find the selected server
    for(int i = 0; i < server_count; i++) {
        if(servers[i].status == UP) {
            if(current == target) {
                return i;
            }
            current++;
        }
    }

    return -1;
}

void send_pings(int client_socket) {
    int server_index = get_random_active_server();

    if(server_index == -1) {
        printf("No active servers to ping\n");
        return;
    }

    // Create ping message

    char* message = generate_random_string(PING_MESSAGE_LEN);
    char* message_with_header = add_header(message, PING);

    printf("\033[34mSending raw message: [%c]%s\033[0m\n",
            PING, message);

    // Store start time
    gettimeofday(&ping_state.start_time, NULL);
    ping_state.waiting_for_pong = 1;

    // Prepare server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(servers[server_index].port);
    inet_pton(AF_INET, servers[server_index].ip, &server_addr.sin_addr);

    // Send ping
    printf("\033[34mSending PING to server %d (IP: %s, Port: %d): %s\033[0m\n",
           servers[server_index].id,
           servers[server_index].ip,
           servers[server_index].port,
           message_with_header);

    sendto(client_socket,
           message_with_header,
           strlen(message_with_header),
           0,
           (struct sockaddr*)&server_addr,
           sizeof(server_addr));

    free(message);
    free(message_with_header);
}

void server_hello_handler(char* message, const char* ip, int port) {
    // Parse server ID from message
    int server_id;
    if (sscanf(message, "%d", &server_id) != 1) {
        printf("Failed to parse server ID\n");
        return;
    }

    // Check if server already exists
    for(int i = 0; i < server_count; i++) {
        if(servers[i].id == server_id) {
            // Update existing server
            strcpy(servers[i].ip, ip);
            servers[i].port = port;
            servers[i].status = UP;
            printf("Updated server %d\n", server_id);
            return;
        }
    }

    // Add new server if there's space
    if(server_count < MAX_SERVERS) {
        servers[server_count].id = server_id;
        strcpy(servers[server_count].ip, ip);
        servers[server_count].port = port;
        servers[server_count].status = UP;
        servers[server_count].failed_requests = 0;
        servers[server_count].last_request_time = time(NULL);
        servers[server_count].last_request_time_usec = 0;  //;
        printf("Added new server %d at IP: %s Port: %d\n",
               server_id, ip, port);
        server_count++;
    } else {
        printf("Server list full!\n");
    }
}

void print_servers() {
    printf("\nKnown servers:\n");
    for(int i = 0; i < server_count; i++) {
        printf("Server ID: %d, IP: %s, Port: %d, Status: %s\n",
               servers[i].id,
               servers[i].ip,
               servers[i].port,
               servers[i].status ? "UP" : "DOWN");
    }
    printf("\n");
}

void client_listen(int server_socket, struct sockaddr_in sender_addr, socklen_t client_len) {
    char buffer[BUFFER_SIZE];
    socklen_t sender_len = sizeof(sender_addr);
    struct timeval recv_time;

    int recv_len = recvfrom(server_socket,
                           buffer,
                           BUFFER_SIZE,
                           0,
                           (struct sockaddr*)&sender_addr,
                           &client_len);

    if (recv_len > 0) {
        // Get receive time immediately
        gettimeofday(&recv_time, NULL);

        buffer[recv_len] = '\0';

        char sender_ip[MAX_IP_LENGTH];
        inet_ntop(AF_INET, &(sender_addr.sin_addr), sender_ip, MAX_IP_LENGTH);
        int sender_port = ntohs(sender_addr.sin_port);

        char header = buffer[0];
        char* message = malloc(recv_len);
        strcpy(message, buffer + 1);

        printf("\033[35mReceived raw message: [%c]%s\033[0m\n",
                       header, buffer + 1);

        switch(header) {
            case HELLO:
                printf("\033[32mReceived HELLO message\033[0m\n");
                server_hello_handler(message, sender_ip, sender_port);
                // Reset failed requests and ensure status is UP
                for(int i = 0; i < server_count; i++) {
                    if(strcmp(servers[i].ip, sender_ip) == 0 &&
                       servers[i].port == sender_port) {
                        servers[i].failed_requests = 0;
                        servers[i].status = UP;
                        servers[i].last_request_time = time(NULL);
                        servers[i].last_request_time_usec = 0;
                        printf("\033[32mServer %d reactivated\033[0m\n", servers[i].id);
                        break;
                    }
                }
                print_servers();
                break;
            case PING:
                printf("\033[32mReceived PING message: %s\033[0m\n", message);
                break;
            case PONG:
                handle_pong_response(message, &recv_time);
                // Update server status
                for(int i = 0; i < server_count; i++) {
                    if(strcmp(servers[i].ip, sender_ip) == 0 &&
                       servers[i].port == sender_port) {
                        servers[i].status = UP;
                        break;
                    }
                }
                break;
            case REQUEST:
                printf("\033[32mReceived REQUEST message: %s\033[0m\n", message);
                break;
            case RESPONSE:
                printf("\033[32mReceived RESPONSE from %s:%d\033[0m\n",
                       sender_ip, sender_port);
                // Find and update server status
                for(int i = 0; i < server_count; i++) {
                    if(strcmp(servers[i].ip, sender_ip) == 0 &&
                       servers[i].port == sender_port) {
                        servers[i].failed_requests = 0;  // Reset failed attempts
                        servers[i].status = UP;
                        break;
                    }
                }
                break;
            default:
                printf("\033[31mUnknown header: %c\033[0m\n", header);
        }

        free(message);
    }
}



char* add_header(char* message, char header) {
    // Check if message is NULL
    if (message == NULL) {
        return NULL;
    }

    // Calculate length of new string (original + header + null terminator)
    size_t msg_len = strlen(message);
    size_t new_len = msg_len + 2;  // +1 for header, +1 for null terminator

    // Allocate memory for new string
    char* new_message = (char*)malloc(new_len);
    if (new_message == NULL) {
        return NULL;
    }

    // Add header and copy message
    new_message[0] = header;           // Add header
    strcpy(new_message + 1, message);  // Copy original message after header

    return new_message;
}

void init_random_generator_seed() {
    if (!seeded) {
        // srand(time(NULL)) initializes random number generator
        // time(NULL) returns current time in seconds since 1970
        // Using it as seed ensures different random sequences each program run
        srand(time(NULL));
        seeded = 1;
    }
}

char* generate_random_string(int length) {
    // Alokacja pamięci dla ciągu znaków (+1 dla terminatora null)
    char* random_string = (char*)malloc(length + 1);

    // Znaki, które mogą być użyte w losowym ciągu
    const char charset[] = "0123456789"
                         "abcdefghijklmnopqrstuvwxyz";

    // Pobierz długość zestawu znaków
    int charset_length = strlen(charset);


    // Wygeneruj losowy ciąg znaków
    for (int i = 0; i < length; i++) {
        int key = rand() % charset_length;
        random_string[i] = charset[key];
    }

    // Dodaj terminator null
    random_string[length] = '\0';

    return random_string;
}

void ping_server(int client_socket, struct sockaddr_in server_addr, socklen_t server_len, char *buffer){

    struct timeval start,end;
    double rtt;
    // Wskaźnik (pointer) używany jest tutaj, aby pokazać, że zmienna 'message'
    // jest wskaźnikiem na ciąg znaków (char*) umieszczony w pamięci,
    // który będzie zawierał wiadomość do wysłania.
    char *message = generate_random_string(PING_MESSAGE_LEN);  // Wskaźnik na stałą tekstową w pamięci programu

    // Jeśli alokacja pamięci się nie powiodła, to message == NULL
    if (message == NULL) {
            printf("Failed to generate random string\n");
            return;
    }

    //dodaj nagłówek
    message = add_header(message,PING);

    // printowanie na niebiesko przez \033[34m
    printf("\033[34mWysyłanie: %s \033[0m\n", message);

    // gettimeofday() zwraca czas w mikrosekundach, pobranie czasu startu
    gettimeofday(&start, NULL);
    // sendto() wysyła dane przez UDP
    // // Deskryptor socketu
    sendto(client_socket,
           message,
           strlen(message),                 // Długość wiadomości
           0,                              // Flagi
           (struct sockaddr*)&server_addr,  // Adres docelowy
           sizeof(server_addr));            // Długość adresu

    // recvfrom() odbiera odpowiedź
    int recv_len = recvfrom(client_socket,              // Deskryptor socketu
                           buffer,                      // Bufor
                           BUFFER_SIZE,                 // Rozmiar bufora
                           0,                          // Flagi
                           (struct sockaddr*)&server_addr,  // Adres nadawcy
                           &server_len);                    // Długość adresu
    if (recv_len > 0) {
            buffer[recv_len] = '\0';    // Zakończenie stringu nullem

            gettimeofday(&end,NULL);
            rtt = (double)(end.tv_usec - start.tv_usec) / 1000; // konwersja na ms

            // Pobierz aktualny znacznik czasu
            time_t current_time;
            time(&current_time);
            char *timestamp = ctime(&current_time);
            timestamp[strlen(timestamp)-1] = '\0';

            printf("Otrzymano odpowiedź: %s, RTT: %.3f ms, Czas: %s\n",
                   buffer + 1,  // Skip header character
                   rtt,
                   timestamp);
        }


    free(message); // zwalnianie pamięci

    sleep(1);    // Pauza 1 sekunda

}

int get_random_ping_interval() {
    // Returns milliseconds between 1500 and 2550
    return 1500 + (rand() % 1051);  // 1051 is 2550-1500+1
}

int main() {
    // aż do while(1) jest to inicjalizacja rzeczy potrzebnych do komunikacji
    printf("Klient\n");
    init_random_generator_seed();

    int client_socket;
    struct sockaddr_in server_addr, client_addr;    // Struktury adresów
    char buffer[BUFFER_SIZE];                       // Bufor na dane
    socklen_t server_len = sizeof(server_addr);     // Długość struktury adresu serwera

    // socket() tworzy nowy socket UDP
    client_socket = socket(AF_INET,     // IPv4
                          SOCK_DGRAM,   // UDP
                          0);           // Protokół domyślny

    // Konfiguracja adresu klienta
    memset(&client_addr, 0, sizeof(client_addr));           // Wyzerowanie struktury
    client_addr.sin_family = AF_INET;                       // IPv4
    client_addr.sin_addr.s_addr = INADDR_ANY;               // Dowolny interfejs
    client_addr.sin_port = htons(CLIENT_PORT);              // Port klienta

    // bind() przypisuje adres do socketu klienta
    if (bind(client_socket,
            (struct sockaddr*)&client_addr,
            sizeof(client_addr)) < 0) {
        perror("Błąd bind");
        exit(1);
    }


    // Variables for select()
    fd_set readfds;
    struct timeval tv;
    struct timeval last_ping_time;
        gettimeofday(&last_ping_time, NULL);
        int current_ping_interval = get_random_ping_interval();

        while (1) {
            FD_ZERO(&readfds);
            FD_SET(client_socket, &readfds);

            tv.tv_sec = 0;
            tv.tv_usec = 100000;  // 100ms

            int activity = select(client_socket + 1, &readfds, NULL, NULL, &tv);

            if (activity < 0) {
                printf("Select error");
                break;
            }

            // Check if it's time to send pings
            struct timeval current_time;
            gettimeofday(&current_time, NULL);

            // Calculate time difference in milliseconds
            long time_diff = (current_time.tv_sec - last_ping_time.tv_sec) * 1000 +
                            (current_time.tv_usec - last_ping_time.tv_usec) / 1000;

            if (time_diff >= current_ping_interval) {
                send_pings(client_socket);
                last_ping_time = current_time;
                // Get new random interval for next ping
                current_ping_interval = get_random_ping_interval();
            }

            // Send request checks
            send_keep_alive_check(client_socket);

            // If there's data to read
            if (FD_ISSET(client_socket, &readfds)) {
                client_listen(client_socket, server_addr, server_len);
            }
        }

        close(client_socket);
        return 0;
    }
