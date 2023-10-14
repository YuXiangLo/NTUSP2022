#include <unistd.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdbool.h>

#define ERR_EXIT(a) do { perror(a); exit(1); } while(0)

#define OBJ_NUM 3

#define FOOD_INDEX 0
#define CONCERT_INDEX 1
#define ELECS_INDEX 2
#define RECORD_PATH "./bookingRecord"
#define READ_ID 4
#define READ_OPR 5
#define END_READ 6

static char* obj_names[OBJ_NUM] = {"Food", "Concert", "Electronics"};

typedef struct {
    char hostname[512];  // server's hostname
    unsigned short port;  // port to listen
    int listen_fd;  // fd to wait for a new connection
} server;

typedef struct {
    char host[512];  // client's host
    int conn_fd;  // fd to talk with client
    char buf[512];  // data sent by/to client
    size_t buf_len;  // bytes used by buf
    int id;
	int state;
    int wait_for_write;  // used by handle_read to know if the header is read or not.
} request;

server svr;  // server
request* requestP = NULL;  // point to a list of requests
int maxfd;  // size of open file descriptor table, size of request list

fd_set readfds;

const char* accept_read_header = "ACCEPT_FROM_READ";
const char* accept_write_header = "ACCEPT_FROM_WRITE";
const char IAC_IP[3] = "\xff\xf4";

static void init_server(unsigned short port);
// initailize a server, exit for error

static void init_request(request* reqP);
// initailize a request instance

static void free_request(request* reqP);
// free resources used by a request instance

void reqClose(int conn_fd){
	memset(requestP[conn_fd].buf, 0, sizeof(requestP[conn_fd].buf));
	FD_CLR(conn_fd, &readfds);
	close(requestP[conn_fd].conn_fd);
	free_request(&requestP[conn_fd]);
}

typedef struct {
    int id;          // 902001-902020
    int bookingState[OBJ_NUM]; // array of booking number of each object (0 or positive numbers)
}record;

int lock(int fd, int cmd, int type, off_t offset, int whence, off_t len) {

    struct flock lock;
    lock.l_len = len;
    lock.l_type = type;
    lock.l_whence = whence;
    lock.l_start = offset;

    return (fcntl(fd, cmd, &lock));
}

bool test(int fd, int type, off_t off_set, int whence, off_t len) {

    struct flock lock;
    lock.l_type = type;
    lock.l_start = off_set;
    lock.l_whence = whence;
    lock.l_len = len;

    fcntl(fd, F_GETLK, &lock);

    if (lock.l_type == F_UNLCK) return false;
    return true;
}

bool get3Num(int opr[3], char *s){
	opr[0] = opr[1] = opr[2] = 0;
	int len = strlen(s);
	if(s[len - 1] == '-') return false;
	int chk = 0;
	for(int i = 0, j = 0; i < len; i++){
		if(s[i] == '-'){
			if((i - 1 >= 0 && s[i - 1] != ' ') || s[i + 1] == ' ') return false;
			chk = 1;
		} else if(s[i] == ' '){
			if(chk) opr[j] *= -1;
			j++, chk = 0;
		} else if(s[i] <= '9' && s[i] >= '0'){
			opr[j] *= 10;
			opr[j] += s[i] - '0';
		} else
			return false;
	}
	if(chk) opr[2] = -opr[2];
	return true;
}

int handle_read(request* reqP) {
    /*  Return value:
     *      1: read successfully
     *      0: read EOF (client down)
     *     -1: read failed
     */
    int r;
    char buf[512] = {};

    // Read in request from client
    r = read(reqP->conn_fd, buf, sizeof(buf));
    if (r < 0) return -1;
    if (r == 0) return 0;
    char* p1 = strstr(buf, "\015\012");
    int newline_len = 2;
    if (p1 == NULL) {
       p1 = strstr(buf, "\012");
        if (p1 == NULL) {
            if (!strncmp(buf, IAC_IP, 2)) {
                // Client presses ctrl+C, regard as disconnection
                fprintf(stderr, "Client presses ctrl+C....\n");
                return 0;
            }
            ERR_EXIT("this really should not happen...");
        }
    }
    size_t len = p1 - buf + 1;
    memmove(reqP->buf, buf, len);
    reqP->buf[len - 1] = '\0';
    reqP->buf_len = len-1;
    return 1;
}

int main(int argc, char** argv) {

    // Parse args.
    if (argc != 2) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        exit(1);
    }

    struct sockaddr_in cliaddr;  // used by accept()
    int clilen;

    int conn_fd;  // fd for a new connection with client
    int file_fd;  // fd for file that we open for reading
    char buf[512];
    int buf_len;

#ifdef READ_SERVER
	int readFd = open(RECORD_PATH, O_RDONLY);
#elif WRITE_SERVER
	int writeFd = open(RECORD_PATH, O_RDWR);
#endif
	
    // Initialize server
    init_server((unsigned short) atoi(argv[1]));

    // Loop for handling connections
    fprintf(stderr, "\nstarting on %.80s, port %d, fd %d, maxconn %d...\n", svr.hostname, svr.port, svr.listen_fd, maxfd);

	struct timeval tv;
	tv.tv_sec = 0;
	tv.tv_usec = 1000;

	FD_ZERO(&readfds);
	FD_SET(svr.listen_fd, &readfds);
	maxfd = svr.listen_fd;
	bool vis[20] = {};
	int readCnt[20] = {};


    while (1) {
        
		// TODO: Add IO multiplexing
		fd_set fds = readfds;
		int selectRet = select(maxfd + 1, &fds, NULL, NULL, &tv);
		if(selectRet == -1) 		ERR_EXIT("select");
		else if(selectRet == 0) 	continue;
        
		for(int i = 0; i <= maxfd; i++)
			if(FD_ISSET(i, &fds)){
				if(i == svr.listen_fd){
					// Check new connection
					clilen = sizeof(cliaddr);
					conn_fd = accept(svr.listen_fd, (struct sockaddr*)&cliaddr, (socklen_t*)&clilen);
					if (conn_fd < 0) {
						if (errno == EINTR || errno == EAGAIN) continue;  // try again
						if (errno == ENFILE) {
							(void) fprintf(stderr, "out of file descriptor table ... (maxconn %d)\n", maxfd);
							continue;
						}
						ERR_EXIT("accept");
					}
					FD_SET(conn_fd, &readfds);
					if(conn_fd > maxfd) maxfd = conn_fd;

					requestP[conn_fd].conn_fd = conn_fd;
					requestP[conn_fd].state = READ_ID;
					strcpy(requestP[conn_fd].host, inet_ntoa(cliaddr.sin_addr));
					fprintf(stderr, "getting a new request... fd %d from %s\n", conn_fd, requestP[conn_fd].host);

					sprintf(buf, "Please enter your id (to check your booking state):\n");
					write(requestP[conn_fd].conn_fd, buf, strlen(buf));
				} else{
					// TODO: handle requests from clients
					conn_fd = i;
					int ret = handle_read(&requestP[conn_fd]); // parse data from client to requestP[conn_fd].buf
					if(ret == -1) 	ERR_EXIT("handle read");
					if(ret == 0){
#ifdef READ_SERVER
						if(requestP[conn_fd].state == READ_OPR && --readCnt[requestP[conn_fd].id] == 0)
							lock(readFd, F_SETLK, F_UNLCK, requestP[conn_fd].id * sizeof(record), SEEK_SET, sizeof(record));
#elif WRITE_SERVER	
						lock(writeFd, F_SETLK, F_UNLCK, requestP[conn_fd].id * sizeof(record), SEEK_SET, sizeof(record));
						vis[requestP[conn_fd].id] = false;
#endif
						reqClose(conn_fd);
						continue;
					}
					fprintf(stderr, "fd %d success read %s\n", conn_fd,  requestP[conn_fd].buf);
					int clientID = requestP[conn_fd].id;
					if(requestP[conn_fd].state == READ_ID){
						if(strlen(requestP[conn_fd].buf) != 6){
							sprintf(buf, "[Error] Operation failed. Please try again.\n");
							write(requestP[conn_fd].conn_fd, buf, strlen(buf));
							reqClose(conn_fd);
							continue;
						}
						clientID = atoi(requestP[conn_fd].buf) - 902001;
						if(clientID >= 20 || clientID < 0){
							sprintf(buf, "[Error] Operation failed. Please try again.\n");
							write(requestP[conn_fd].conn_fd, buf, strlen(buf));
							reqClose(conn_fd);
							continue;
						}
						requestP[conn_fd].id = clientID;
						requestP[conn_fd].state = READ_OPR;
#ifdef READ_SERVER
						if(test(readFd, F_RDLCK, clientID * sizeof(record), SEEK_SET, sizeof(record))){
							sprintf(buf,"Locked.\n");
							write(requestP[conn_fd].conn_fd, buf, strlen(buf));
							reqClose(conn_fd);
							continue;
						}

						lock(readFd, F_SETLK, F_RDLCK, clientID * sizeof(record), SEEK_SET, sizeof(record));
						readCnt[clientID]++;
						
						record curData;
						lseek(readFd, clientID * sizeof(record), SEEK_SET);
						read(readFd, &curData, sizeof(record));
						sprintf(buf, 
								"Food: %d booked\nConcert: %d booked\nElectronics: %d booked\n\n(Type Exit to leave...)\n", 
								curData.bookingState[0], curData.bookingState[1], curData.bookingState[2]);
						write(requestP[conn_fd].conn_fd, buf, strlen(buf));
#elif defined WRITE_SERVER
						if( vis[clientID] || test(writeFd, F_WRLCK, clientID * sizeof(record), SEEK_SET, sizeof(record))){
							sprintf(buf,"Locked.\n");
							write(requestP[conn_fd].conn_fd, buf, strlen(buf));
							reqClose(conn_fd);
							continue;
						}
						lock(writeFd, F_SETLK, F_WRLCK, clientID * sizeof(record), SEEK_SET, sizeof(record));
						vis[clientID] = true;
						record curData;
						lseek(writeFd, clientID * sizeof(record), SEEK_SET);
						read(writeFd, &curData, sizeof(record));
						sprintf(buf, 
	"Food: %d booked\nConcert: %d booked\nElectronics: %d booked\n\nPlease input your booking command. (Food, Concert, Electronics. Positive/negative value increases/decreases the booking amount.):\n", 
								curData.bookingState[0], curData.bookingState[1], curData.bookingState[2]);
						write(requestP[conn_fd].conn_fd, buf, strlen(buf));
#endif
					} else if(requestP[conn_fd].state == READ_OPR){
#ifdef READ_SERVER      
						if(!strcmp(requestP[conn_fd].buf, "Exit"))
							requestP[conn_fd].state = END_READ;
#elif defined WRITE_SERVER
						int opr[3] = {};
						if(!get3Num(opr, requestP[conn_fd].buf)){
							sprintf(buf, "[Error] Operation failed. Please try again.\n");
							write(requestP[conn_fd].conn_fd, buf, strlen(buf));
							reqClose(conn_fd);
							lock(writeFd, F_SETLK, F_UNLCK, clientID * sizeof(record), SEEK_SET, sizeof(record));
							vis[clientID] = false;
							continue;
						}
						fprintf(stderr, "opr %d %d %d\n", opr[0], opr[1], opr[2]);
						record curData;
						lseek(writeFd, clientID * sizeof(record), SEEK_SET);
						read(writeFd, &curData, sizeof(record));
						curData.bookingState[0] += opr[0];
						curData.bookingState[1] += opr[1];
						curData.bookingState[2] += opr[2];
						if(curData.bookingState[0] + curData.bookingState[1] + curData.bookingState[2] > 15){
							sprintf(buf, "[Error] Sorry, but you cannot book more than 15 items in total.\n");
							write(requestP[conn_fd].conn_fd, buf, strlen(buf));
						} else if(curData.bookingState[0] < 0 || curData.bookingState[1] < 0 || curData.bookingState[2] < 0){
							sprintf(buf, "[Error] Sorry, but you cannot book less than 0 items.\n");
							write(requestP[conn_fd].conn_fd, buf, strlen(buf));
						} else{
							lseek(writeFd, clientID * sizeof(record), SEEK_SET);
							write(writeFd, &curData, sizeof(record));
							sprintf(buf, 
	"Bookings for user %d are updated, the new booking state is:\nFood: %d booked\nConcert: %d booked\nElectronics: %d booked\n",
							clientID + 902001, curData.bookingState[0], curData.bookingState[1], curData.bookingState[2]);
							write(requestP[conn_fd].conn_fd, buf, strlen(buf));
						}
						requestP[conn_fd].state = END_READ;
#endif
					}
					if(requestP[conn_fd].state == END_READ){
						reqClose(conn_fd);
#ifdef READ_SERVER
					if(--readCnt[clientID] == 0)
						lock(readFd, F_SETLK, F_UNLCK, clientID * sizeof(record), SEEK_SET, sizeof(record));
#elif WRITE_SERVER	
					lock(writeFd, F_SETLK, F_UNLCK, clientID * sizeof(record), SEEK_SET, sizeof(record));
					vis[clientID] = false;
#endif
					}
				}
			}
    }
    free(requestP);
    return 0;
}

// ======================================================================================================
// You don't need to know how the following codes are working
#include <fcntl.h>

static void init_request(request* reqP) {
    reqP->conn_fd = -1;
    reqP->buf_len = 0;
    reqP->id = 0;
}

static void free_request(request* reqP) {
    /*if (reqP->filename != NULL) {
        free(reqP->filename);
        reqP->filename = NULL;
    }*/
    init_request(reqP);
}

static void init_server(unsigned short port) {
    struct sockaddr_in servaddr;
    int tmp;

    gethostname(svr.hostname, sizeof(svr.hostname));
    svr.port = port;

    svr.listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (svr.listen_fd < 0) ERR_EXIT("socket");

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    tmp = 1;
    if (setsockopt(svr.listen_fd, SOL_SOCKET, SO_REUSEADDR, (void*)&tmp, sizeof(tmp)) < 0) {
        ERR_EXIT("setsockopt");
    }
    if (bind(svr.listen_fd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0) {
        ERR_EXIT("bind");
    }
    if (listen(svr.listen_fd, 1024) < 0) {
        ERR_EXIT("listen");
    }

    // Get file descripter table size and initialize request table
    maxfd = getdtablesize();
    requestP = (request*) malloc(sizeof(request) * maxfd);
    if (requestP == NULL) {
        ERR_EXIT("out of memory allocating all requests");
    }
    for (int i = 0; i < maxfd; i++) {
        init_request(&requestP[i]);
    }
    requestP[svr.listen_fd].conn_fd = svr.listen_fd;
    strcpy(requestP[svr.listen_fd].host, svr.hostname);

    return;
}
