#include "status.h"
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <ctype.h>
#include <stdbool.h>

const char  left[][3] = {"B", "D", "F" , "G", "I", "K", "0", "2", "4", "6", "M" , "N" , "8", "11"};
const char right[][3] = {"C", "E", "14", "H", "J", "L", "1", "3", "5", "7", "10", "13", "9", "12"};
const char  papa[][2] = {"0", "A", "A" , "B", "B", "C", "D", "D", "E", "E", "F" , "F" , "K", "L" };
const int 	ATTR[]    = { 0 ,  1 ,  2  ,  2 ,  0 ,  0 ,  0 ,  1 ,  2 ,  1 ,  1  ,  1  ,  0 ,  2  }; 

int idx;
int cpid[2];
int in[2][2], out[2][2];
Status Player[2];
char pid[100];
int p[2];
int logFd;

void writeLog(char *s, int who, bool P, char *type){
	char log[128] = {};
	const char* ss = (who == 1) ? right[s[0] - 'A'] : (who == 2) ? papa[who] : left[s[0] - 'A'];
	if(P) ss = papa[s[0] - 'A'];
	int opid = (P) ? getppid() : cpid[who];
	sprintf(log, "%s,%d pipe %s %s,%d %d,%d,%c,%d\n",
		s, getpid(), type, ss, opid, Player[who].real_player_id, Player[who].HP, Player[who].current_battle_id, Player[who].battle_ended_flag);
	write(logFd, log, strlen(log));
}

void closePipe(){
	close(out[0][0]);
	close(out[0][1]);
	close(in[0][0]);
	close(in[0][1]);
	close(out[1][0]);
	close(out[1][1]);
	close(in[1][0]);
	close(in[1][1]);
}

int openBattle(char c){
	char name[50];
	sprintf(name, "log_battle%c.txt", c);
	return open(name, O_WRONLY | O_CREAT | O_APPEND, 0644);
}

void fight(int *remain){
	if(Player[0].HP < Player[1].HP || (Player[0].HP == Player[1].HP && Player[0].real_player_id < Player[1].real_player_id)){
		Player[1].HP -= Player[0].ATK * ((Player[0].attr == ATTR[idx]) + 1);
		if(Player[1].HP <= 0){
			*remain = 0;
			Player[0].battle_ended_flag = Player[1].battle_ended_flag = 1;
			return;
		}
		Player[0].HP -= Player[1].ATK * ((Player[1].attr == ATTR[idx]) + 1);
		if(Player[0].HP <= 0){
			*remain = 1;
			Player[0].battle_ended_flag = Player[1].battle_ended_flag = 1;
			return;
		}
	} else{
		Player[0].HP -= Player[1].ATK * ((Player[1].attr == ATTR[idx]) + 1);
		if(Player[0].HP <= 0){
			*remain = 1;
			Player[0].battle_ended_flag = Player[1].battle_ended_flag = 1;
			return;
		}
		Player[1].HP -= Player[0].ATK * ((Player[0].attr == ATTR[idx]) + 1);
		if(Player[1].HP <= 0){
			*remain = 0;
			Player[0].battle_ended_flag = Player[1].battle_ended_flag = 1;
			return;
		}
	}
}

int main(int argc, char *argv[]){
	idx = argv[1][0] - 'A';
	pipe(in[0]);
	pipe(out[0]);
	pipe(in[1]);
	pipe(out[1]);
	sprintf(pid, "%d", getpid());
	if((cpid[0] = fork()) == 0){
		dup2(out[0][1], STDOUT_FILENO);
		dup2(in[0][0], STDIN_FILENO);
		closePipe();
		if(isupper(left[idx][0])) 	execlp("./battle", "battle", left[idx], pid, NULL);
		else 						execlp("./player", "player", left[idx], pid, NULL);	
	} else if((cpid[1] = fork()) == 0){
		dup2(out[1][1], STDOUT_FILENO);
		dup2(in[1][0], STDIN_FILENO);
		closePipe();
		if(isupper(right[idx][0])) 	execlp("./battle", "battle", right[idx], pid, NULL);
		else 						execlp("./player", "player", right[idx], pid, NULL);	
	} else{
		close(in[0][0]);
		close(in[1][0]);
		close(out[0][1]);
		close(out[1][1]);
		logFd = openBattle(argv[1][0]);
		int remain = -1; 
		while(!Player[0].battle_ended_flag){
			read(out[0][0], Player, sizeof(Status));
			writeLog(argv[1], 0, 0, "from");
			read(out[1][0], Player + 1, sizeof(Status));
			writeLog(argv[1], 1, 0, "from");
			Player[0].current_battle_id = Player[1].current_battle_id = argv[1][0];
			fight(&remain);
			write(in[0][1], Player, sizeof(Status));
			writeLog(argv[1], 0, 0, "to");
			write(in[1][1], Player + 1, sizeof(Status));
			writeLog(argv[1], 1, 0, "to");
		}
		wait(NULL);
		if(argv[1][0] == 'A'){
			printf("Champion is P%d\n", Player[remain].real_player_id);
			wait(NULL);
			exit(0);
		}
		if(remain) 	close(in[0][1]), close(out[0][0]);
		else 		close(in[1][1]), close(out[1][0]);
		while(remain != -1){
			read(out[remain][0], Player + remain, sizeof(Status));
			writeLog(argv[1], remain, 0, "from");
			write(STDOUT_FILENO, Player + remain, sizeof(Status));
			writeLog(argv[1], remain, 1, "to");
			read(STDIN_FILENO, Player + remain, sizeof(Status));
			writeLog(argv[1], remain, 1, "from");
			write(in[remain][1], Player + remain, sizeof(Status));
			writeLog(argv[1], remain, 0, "to");
			if( Player[remain].HP <= 0 || 
				Player[remain].battle_ended_flag == 1 && 
				Player[remain].current_battle_id == 'A')
					remain = -1;
		}
		wait(NULL);
		close(logFd);
	}
    return 0;
}

