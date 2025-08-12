#include <ncurses.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>

#define WIDTH 80
#define HEIGHT 30
#define OFFSETX 10
#define OFFSETY 5
#define BUFFER_SIZE 128
#define PADDLE_SPEED 3

typedef struct {
    int x, y;
    int dx, dy;
} Ball;

typedef struct {
    int x;
    int width;
} Paddle;

Ball ball;
Paddle paddleA, paddleB;
int game_running = 1;
int player_role; // 0 for server, 1 for client
int sockfd;
int connfd;
int scoreA = 0, scoreB = 0;
int server_port;
struct sockaddr_in server_addr, client_addr;

void init();
void end_game();
void draw(WINDOW *win);
void *move_ball(void *args);
void update_paddle(int ch);
void reset_ball();
void *send_game_state();   //
void *receive_game_state();    //

void setup_server();
void setup_client(char *server_ip);

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s server <server_port> | %s client <server_ip> <client_port> \n", argv[0], argv[0]);
        return 1;
    }
    ball = (Ball){WIDTH / 2, HEIGHT / 2, 1, 1};
    paddleA = (Paddle){WIDTH / 2 - 3, 10};
    paddleB = (Paddle){WIDTH / 2 - 3, 10};
    
    if (strcmp(argv[1], "server") == 0) {
        player_role = 0;
        server_port = atoi(argv[2]);
        setup_server();
    } else if (strcmp(argv[1], "client") == 0 && argc == 3) {
        player_role = 1;
        setup_client(argv[2]);
    } else {
        printf("Invalid usage!\n");
        return 1;
    }
    
    init();
    pthread_t ball_thread, recv_thread,send_thread;
    // server controls ball
    if (player_role == 0) {
        pthread_create(&ball_thread, NULL, move_ball, NULL);
    }

    if (pthread_create(&send_thread, NULL, send_game_state, NULL) != 0) {
        perror("pthread_create for send_thread");
        exit(EXIT_FAILURE);
    }
    if (pthread_create(&recv_thread, NULL, receive_game_state, NULL) != 0) {
        perror("pthread_create for recv_thread");
        exit(EXIT_FAILURE);
    }
    
    while (game_running) {
        int ch = getch();
        if (ch == 'q') {
            game_running = 0;
            break;
        }
        update_paddle(ch); 
        draw(stdscr);
    }
    // join threads
    pthread_join(recv_thread, NULL);
    pthread_join(send_thread, NULL);
    if (player_role == 0) {
        pthread_join(ball_thread, NULL);
    }
    end_game();
    close(sockfd);
    return 0;
}

void setup_server() {
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("Error creating server socket");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, '\0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;   
    server_addr.sin_port = htons(server_port);

    if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    if (listen(sockfd, 1) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    socklen_t len = sizeof(client_addr);
    sockfd = accept(sockfd, (struct sockaddr *)&client_addr, &len);
    if (sockfd < 0) {
        perror("Accept failed");
        exit(EXIT_FAILURE);
    }
}

void setup_client(char *server_ip) {  
    sockfd = socket(AF_INET, SOCK_STREAM, 0);

    memset(&server_addr, '\0', sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8080);    // take 8080
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection to server failed");
        exit(1);
    }
}

void *move_ball(void *args) {
    while (game_running) {
        ball.x += ball.dx;
        ball.y += ball.dy;

        // Ball bounces off top
        if (ball.y <= 1) ball.dy = -ball.dy;

        // Ball bounces off left and right walls
        if (ball.x <= 2 || ball.x >= WIDTH - 2) ball.dx = -ball.dx;

        // Ball hits paddleA (server paddle at bottom)
        if (ball.y == HEIGHT - 5 && ball.x >= paddleA.x && ball.x < paddleA.x + paddleA.width) {
            ball.dy = -ball.dy;
        }

        // Ball hits paddleB (client paddle at top)
        if (ball.y == 2 && ball.x >= paddleB.x && ball.x < paddleB.x + paddleB.width) {
            ball.dy = -ball.dy;
        }

        if (ball.y >= HEIGHT - 2) {
            scoreB++;
            reset_ball();
            // ball.dy = -ball.dy;
        }

        // Ball goes past paddleB (client loses a point)
        if (ball.y <= 1) {
            scoreA++;
            reset_ball();
        }
        
        if (ball.y >= HEIGHT - 1) { // Ensure the ball passes the paddle
          scoreB++;  // Increase opponent's score
          reset_ball(); // Reset the ball for the next round
        }
        usleep(100000); 
    }
    return NULL;
}

void update_paddle(int ch) {
    if (ch == KEY_LEFT) {
        if (player_role == 0 && paddleA.x > 2) {
            // paddleA.x--;
            paddleA.x = paddleA.x - PADDLE_SPEED;
        }
        if (player_role == 1 && paddleB.x > 2) {
            // paddleB.x--;
            paddleB.x = paddleB.x - PADDLE_SPEED;
        }
    }
    if (ch == KEY_RIGHT) {
        if (player_role == 0 && paddleA.x < WIDTH - paddleA.width - 2) {
            // paddleA.x++;
            paddleA.x = paddleA.x + PADDLE_SPEED;
        }
        if (player_role == 1 && paddleB.x < WIDTH - paddleB.width - 2) {
            // paddleB.x++;
            paddleB.x = paddleB.x + PADDLE_SPEED;
        }
    }
   
}

void *send_game_state(void* args) {
    while (game_running) {
        char buffer[BUFFER_SIZE];
        snprintf(buffer, BUFFER_SIZE, "%d %d %d %d %d %d %d %d\n", ball.x, ball.y, paddleA.x, paddleB.x, ball.dx, ball.dy, scoreA, scoreB);
        send(sockfd, buffer, strlen(buffer), 0);
        usleep(8000); // Ensure synchronization delay
    }
    return NULL;
}


void *receive_game_state(void* args) {
    while (game_running) {
        char buffer[BUFFER_SIZE];
        int bytes_received = recv(sockfd, buffer, BUFFER_SIZE, 0);
        int z;
        if(player_role==1){ //client is receiving everything except it's paddle position
            sscanf(buffer, "%d %d %d %d %d %d %d %d", &ball.x, &ball.y, &paddleA.x, &z, &ball.dx, &ball.dy, &scoreA, &scoreB);
        }
        else{ //server is receiving only clien'ts paddle position
            sscanf(buffer, "%d %d %d %d %d %d %d %d", 
                &z, &z, &z, &paddleB.x, &z, &z, &z, &z);
        }
    }
    return NULL;
}

void draw(WINDOW *win) {
    erase();    // check required whether erase or clear

    attron(COLOR_PAIR(1));
    for (int i = OFFSETX; i <= OFFSETX + WIDTH; i++) {
        mvprintw(OFFSETY - 1, i, " ");
    }
    mvprintw(OFFSETY - 1, OFFSETX + 3, "PingPong: %d, %d", ball.x, ball.y);
    mvprintw(OFFSETY - 1, OFFSETX + WIDTH - 25, "A: %d, B: %d", scoreA, scoreB);

    for (int i = OFFSETY; i < OFFSETY + HEIGHT; i++) {
        mvprintw(i, OFFSETX, "  ");
        mvprintw(i, OFFSETX + WIDTH - 1, "  ");
    }
    for (int i = OFFSETX; i < OFFSETX + WIDTH; i++) {
        mvprintw(OFFSETY, i, " ");
        mvprintw(OFFSETY + HEIGHT - 1, i, " ");
    }
    attroff(COLOR_PAIR(1));

    // Draw the ball
    mvprintw(OFFSETY + ball.y, OFFSETX + ball.x, "o");

    // Draw paddleA (bottom)
    attron(COLOR_PAIR(2));
    for (int i = 0; i < paddleA.width; i++) {
        mvprintw(OFFSETY + HEIGHT - 4, OFFSETX + paddleA.x + i, " ");
    }

    // Draw paddleB (top)
    for (int i = 0; i < paddleB.width; i++) {
        mvprintw(OFFSETY + 1, OFFSETX + paddleB.x + i, " ");
    }
    attroff(COLOR_PAIR(2));

    refresh();
}

void reset_ball() {
    ball.x = WIDTH / 2;
    ball.y = HEIGHT / 2;
    ball.dx = 1;
    ball.dy = 1;
}

void init() {
    initscr();
    start_color();
    init_pair(1, COLOR_BLUE, COLOR_WHITE);
    init_pair(2, COLOR_YELLOW, COLOR_YELLOW);
    timeout(10);
    keypad(stdscr, TRUE);
    curs_set(FALSE);
    noecho();
}

void end_game() {
    endwin();
}


