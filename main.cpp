#include <stdio.h>
#include <unistd.h>
#include <syslog.h>

#include "tserver.h"


int main(int argc, char* argv[]) {

	tserver_init("localhost", "3490");
	while (1) {
		sleep(1);
		printf("logging..\n");
	}

}
