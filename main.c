#include <stdio.h>
#include <sys/types.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <signal.h>

#ifndef PATH
#define PATH "badapple.txt"
#endif

//    DELIM should not be longer than SCANBUF_SIZE
#ifndef DELIM
#define DELIM "nekomark"
#endif

//    there is an extra "\r\n\r\n" before DELIM, this param cuts certain number of characters off from each frame
#ifndef TAIL_CUT
#define TAIL_CUT 4
#endif

//    there is an extra "\r\n" after DELIM, this param cuts certain number of characters off from each frame
#ifndef OVER_SEEK
#define OVER_SEEK 2
#endif

//    used by get_framesize, should not be smaller than strlen(DELIM)
#ifndef SCANBUF_SIZE
#define SCANBUF_SIZE 4096
#endif

#define EQUALS(x, y) (strcmp(x, y) == 0)

//    declare the function after main() to avoid warning
int get_framesize(int fd);
void draw(int sig);

int fd;
int framesize;
char* framebuf;
timer_t timer;

int main(int argc, char const *argv[])
{
	struct timespec delay = {
		.tv_sec = 0,
		.tv_nsec = 126084441,
	};

	for(int i = 1; i < argc; i++) {
		if(EQUALS(argv[i], "-d")) {
			delay.tv_nsec = strtol(argv[++i], NULL, 0);
			if (!delay.tv_nsec) {
				printf("error: delay should not be 0\n");
				exit(EXIT_FAILURE);
			}
		} else if(EQUALS(argv[i], "-h") || EQUALS(argv[i], "--help")) {
			printf(
			"badapple\n"
			"\n"
			"Usage:\tbadapple <-d delay in ns> <-h,--help>\n"
			"\n"
			"-h,--help\tShow this help page\n"
			"\n"
			"-d\t\tSet delay between frames, default: 126084441ns\n"
			"\n"
			"Author: kisekied <https://github.com/kisekied>\n"
			);
			exit(EXIT_FAILURE);
		}
	}

	struct sigaction sigact = {
		.sa_handler = draw,
		.sa_flags = 0,
	};
	sigemptyset(&sigact.sa_mask);
	sigaction(SIGUSR1, &sigact, NULL);

	fd = open(PATH, O_RDONLY);
	if(fd < 0) {
		perror(PATH);
		return EXIT_FAILURE;
	}

	framesize = get_framesize(fd);
	if(framesize < 0) {
		close(fd);
		return EXIT_FAILURE;
	}

//6 is the length of "\033[0;0H"
	framebuf = malloc(framesize + 6);
	memcpy(framebuf, "\033[0;0H", 6);

	struct sigevent sev = {
		.sigev_notify = SIGEV_SIGNAL,
		.sigev_signo = SIGUSR1,
	};
	timer_create(CLOCK_MONOTONIC, &sev, &timer);
	struct itimerspec its = {
		.it_interval = delay,
		.it_value = delay,
	};
	timer_settime(timer, 0, &its, NULL);

	while (1) {
		sleep(1);
	}
}

void draw(int sig)
{
	if (read(fd, framebuf + 6, framesize - TAIL_CUT) <= 0) {
		timer_delete(timer);
		printf("\n");
		free(framebuf);
		close(fd);
		exit(0);
	}
//write a frame to stdout
	write(STDOUT_FILENO, framebuf, framesize + 6 - TAIL_CUT);
	lseek(fd, strlen(DELIM) + TAIL_CUT + OVER_SEEK, SEEK_CUR);
	return;
}


/*
	This function is for getting the size of a frame, not including DELIM itself
*/
int get_framesize(int fd)
{
	char* scanbuf = malloc(SCANBUF_SIZE);
	off_t i;
	off_t last_lineend;
	off_t seek = 0;
	ssize_t bytes_get;
	size_t delim_size = strlen(DELIM);

	bytes_get = read(fd, scanbuf, SCANBUF_SIZE);
	if(bytes_get < 0) {
		perror(PATH);
		return -1;
	}

	while(bytes_get) {
		//    if no "\n" is found in scanbuf, we need last_lineend set instead of the value in last loop,
		//    set it to -1 to increase speed in case SCANBUF_SIZE = delim_size
		last_lineend = -1;

		//    if nekomark is at the beginning of the scanbuf, we also need to return the result
		if(strncmp(DELIM, scanbuf, delim_size) == 0) {
			printf("frame size: %u\n", seek - 1);
			free(scanbuf);
			lseek(fd, 0, SEEK_SET);    //    seek offset to the beginning before exiting function
			return seek;    //    seek bytes before first "nekomark"
		}

		for(i = 0; i < bytes_get; i++) {
			if(scanbuf[i] == '\n') {
				last_lineend = i;
				if(bytes_get - i - 1 >= delim_size) {    //    make sure we won't access memory that is not allocated
					if(strncmp(DELIM, scanbuf + i + 1, delim_size) == 0) {
						printf("frame size: %u\n", seek + i);
						free(scanbuf);
						lseek(fd, 0, SEEK_SET);    //    seek offset to the beginning before exiting function
						return seek + i + 1;    //    seek+i+1 bytes before first "nekomark"
					}
				}
			}
		}

		//    if the file isn't end with "/n" and we dont find any DELIM, the loop won't stop, so break if nothing to read
		if(bytes_get < SCANBUF_SIZE)
			break;

		if(bytes_get - last_lineend - 1 < delim_size) {
			seek = seek + 1 + last_lineend;
			//    seek to the character after last "\n" in scanbuf if there is no space for another strncmp after last "/n"
			lseek(fd, seek, SEEK_SET);
		} else {
			//    we need to use seek for return value, so if we didn't do lseek, we need to update it
			seek = seek + SCANBUF_SIZE;
		}

		bytes_get = read(fd, scanbuf, SCANBUF_SIZE);
		if(bytes_get < 0) {
			perror(PATH);
			return -1;
		}
	}

	fprintf(stderr, "error: no %s found in %s\n", DELIM, PATH);
	free(scanbuf);
	return -1;
}
