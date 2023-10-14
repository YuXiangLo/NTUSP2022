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
#include <sys/select.h>

const int lose[20] = {0, 14, 0, 10, 13, 0, 8, 11, 9, 12};
const char papa[][2] = {"G", "G", "H" , "H", "I", "I", "J", "J", "M", "M", "K" , "N" , "N", "L", "C"};

Status p;
int log_fd;

int openFifo(int num, int tag){
	char filename[50] = {};
	sprintf(filename, "player%d.fifo", num);
	mkfifo(filename, 0644);
	return open(filename, tag);
}

int openLog(int id){
	char filename[50] = {};
	sprintf(filename, "log_player%d.txt", id);
	return open(filename, O_WRONLY | O_APPEND | O_CREAT, 0644);
}

void writeLog(char *s, char *type){
	char log[100] = {};
	sprintf(log, "%s,%d pipe %s %c,%d %d,%d,%c,%d\n", s, getpid(), type, papa[atoi(s)][0], getppid(), p.real_player_id, p.HP, p.current_battle_id, p.battle_ended_flag);
	write(log_fd, log, strlen(log));
}

int main(int argc, char *argv[]) {
	if(atoi(argv[1]) <= 7){
		char atr[20] = {};
		FILE* stat = fopen("player_status.txt", "r");
		for(int i = 0; i <= atoi(argv[1]); i++)
			fscanf(stat, "%d %d %s %c %d", &p.HP, &p.ATK, atr, &p.current_battle_id, &p.battle_ended_flag);
		p.real_player_id = atoi(argv[1]);
			 if(!strcmp(atr, "GRASS")) p.attr = GRASS;
		else if(!strcmp(atr, "FIRE"))  p.attr = FIRE;
		else if(!strcmp(atr, "WATER")) p.attr = WATER;
		fclose(stat);
	} else{
		int fd = openFifo(atoi(argv[1]), O_RDONLY);
		read(fd, &p, sizeof(Status));
		close(fd);
	}
	log_fd = openLog(p.real_player_id);
	if(atoi(argv[1]) > 7){
		char log[128] = {};
		sprintf(log, "%s,%d fifo from %d %d,%d\n", argv[1], getpid(), p.real_player_id, p.real_player_id, p.HP);
		write(log_fd, log, strlen(log));
	}
	int originHP = p.HP;
	while(p.HP > 0){
		write(STDOUT_FILENO, &p, sizeof(Status));
		writeLog(argv[1], "to");
		read(STDIN_FILENO, &p, sizeof(Status));
		writeLog(argv[1], "from");
		if(p.battle_ended_flag == 1 && p.HP > 0){
			if(p.current_battle_id == 'A') return 0;
			p.HP += (originHP - p.HP) / 2; 
			p.battle_ended_flag = 0;
		}
	}
	if(atoi(argv[1]) <= 7 && p.current_battle_id != 'A'){
		p.battle_ended_flag = 0;
		p.HP = originHP;
		int fd = openFifo(lose[p.current_battle_id - 'A'], O_WRONLY);
	   	write(fd, &p, sizeof(Status));
		char buf[128] = {};
		sprintf(buf, "%s,%d fifo to %d %d,%d\n", argv[1], getpid(), lose[p.current_battle_id - 'A'], p.real_player_id, p.HP);	
		write(log_fd, buf, strlen(buf));
		close(fd);
	}
	close(log_fd);
	return 0;
}
