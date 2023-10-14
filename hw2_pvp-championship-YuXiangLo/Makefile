all: battle player

battle: battle.c
	gcc battle.c -fsanitize=undefined -o battle
player: player.c
	gcc player.c -fsanitize=undefined -o player
clean:
	rm player battle log_* *.fifo
