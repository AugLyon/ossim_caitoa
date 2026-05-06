
#include "mem.h"
#include "cpu.h"
#include "loader.h"
#include <stdio.h>
#include <stdlib.h> // ADD.

//CHANGES: Main now take input for file path for testing (line 8 to 11 + 14 to 17 added)
int main(int argc, char **argv) {
	init_mem();
	char *path = (argc > 1) ? argv[1] : "input/p0";
	struct pcb_t * ld = load(path);
	struct pcb_t * proc = load(path);
	if(proc == NULL || ld == NULL) {
        printf("Error: Cannot load program\n");
        return -1;
    }
	unsigned int i;
	for (i = 0; i < proc->code->size; i++) {
		run(proc);
		run(ld);
	}
	dump();
	return 0;
}

