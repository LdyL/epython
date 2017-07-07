/*
 * Copyright (c) 2015, Nick Brown
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <sys/time.h>
#include <ctype.h>
#include <math.h>
#include <mpi.h>

#include "e-hal.h"
#include "e-loader.h"
#include "basictokens.h"
#include "memorymanager.h"
#include "byteassembler.h"
#include "interpreter.h"
#include "device-support.h"
#include "configuration.h"
#include "shared.h"
#include "misc.h"

#define SYMBOL_TABLE_EXTRA 2

struct timeval tval_before[TOTAL_CORES];
extern e_platform_t e_platform;
e_mem_t management_DRAM;
e_epiphany_t epiphany;
static short active[TOTAL_CORES];
int totalActive;
volatile unsigned int * pb;

static void initialiseCores(struct shared_basic*, int, struct interpreterconfiguration*);
static void loadBinaryInterpreterOntoCores(struct interpreterconfiguration*, char);
static void placeByteCode(struct shared_basic*, int, char*);
static void checkStatusFlagsOfCore(struct shared_basic*, struct interpreterconfiguration*, int, MPI_Request *, int *, char *);
static void deactivateCore(struct interpreterconfiguration*, int);
static void startApplicableCores(struct shared_basic*, struct interpreterconfiguration*);
static void timeval_subtract(struct timeval*, struct timeval*,  struct timeval*);
static void displayCoreMessage(int, struct core_ctrl*);
static void raiseError(int, struct core_ctrl*);
static void stringConcatenate(int, struct core_ctrl*);
static void inputCoreMessage(int, struct core_ctrl*);
static void remoteP2P_Send(int, struct shared_basic*);
static void remoteP2P_Recv(int, struct shared_basic*);
static void remoteP2P_SendRecv_Start(int, struct shared_basic*, MPI_Request *, char *);
static void remoteP2P_SendRecv_Finish(int, struct shared_basic*, char *);
static void performMathsOp(struct core_ctrl*);
static int getTypeOfInput(char*);
static int resolveRank(int);
static char* getEpiphanyExecutableFile(struct interpreterconfiguration*);
static int doesFileExist(char*);
static char * allocateChunkInSharedHeapMemory(size_t, struct core_ctrl *);
static void printbuf(char *, int);
static void printbinchar(char);

/**
 * Loads up the code onto the appropriate Epiphany cores, sets up the state (Python bytecode, symbol table, data area etc)
 * and then starts the cores running
 */
struct shared_basic * loadCodeOntoEpiphany(struct interpreterconfiguration* configuration) {
	struct shared_basic * basicCode;
	int i, result, codeOnCore=0;
	int cluster_num_node, my_node_id;
	e_set_host_verbosity(H_D0);
	result = e_init(NULL);
	if (result == E_ERR) fprintf(stderr, "Error on initialisation\n");
	result = e_reset_system();
	if (result == E_ERR) fprintf(stderr, "Error on system reset\n");
	result = e_open(&epiphany, 0, 0, e_platform.chip[0].rows, e_platform.chip[0].cols);
	if (result != E_OK) fprintf(stderr, "Error opening Epiphany\n");

	result = e_alloc(&management_DRAM, EXTERNAL_MEM_ABSOLUTE_START, SHARED_DATA_SIZE);
	if (result == E_ERR) fprintf(stderr, "Error allocating memory\n");

	basicCode=(void*) management_DRAM.base;
	basicCode->length=getMemoryFilledSize();

	if (configuration->forceCodeOnCore) {
		codeOnCore=1;
	} else if (configuration->forceCodeOnShared) {
		codeOnCore=0;
	} else {
		codeOnCore=basicCode->length <= CORE_CODE_MAX_SIZE;
		if (!codeOnCore) {
			printf("Warning: Your code size of %d bytes exceeds the %d byte limit for placement on cores so storing in shared memory\n", basicCode->length, CORE_CODE_MAX_SIZE);
		}
	}
	basicCode->symbol_size=getNumberEntriesInSymbolTable();
	basicCode->allInSharedMemory=configuration->forceDataOnShared;
	basicCode->codeOnCores=codeOnCore==1;
	basicCode->num_procs=configuration->coreProcs+configuration->hostProcs;
	basicCode->baseHostPid=configuration->coreProcs;

	//Assign Nunber of Nodes and Node ID to the current copy of ePyhton based on MPI size and rank
	MPI_Comm_size(MPI_COMM_WORLD, &cluster_num_node);
	MPI_Comm_rank(MPI_COMM_WORLD, &my_node_id);
	basicCode->num_nodes=cluster_num_node;
	basicCode->nodeId=my_node_id;

	initialiseCores(basicCode, codeOnCore, configuration);
	placeByteCode(basicCode, codeOnCore, configuration->intentActive);
	startApplicableCores(basicCode, configuration);

	pb=(unsigned int*) malloc(sizeof(unsigned int) * TOTAL_CORES);
	for (i=0;i<TOTAL_CORES;i++) {
		pb[i]=1;
	}

	return basicCode;
}

/**
 * Finalises the cores and shuts then down
 */
void finaliseCores(void) {
	if (e_close(&epiphany)) {
		fprintf(stderr, "\nERROR: Can't close connection to Epiphany device!\n\n");
		exit(1);
	}
	if (e_free(&management_DRAM)) {
		fprintf(stderr, "\nERROR: Can't release Epiphany DRAM!\n\n");
		exit(1);
	}

	e_finalize();
}

/**
 * The host acts as a monitor, responding to core communications for host actions (such as IO, some maths etc..)
 */
void monitorCores(struct shared_basic * basicState, struct interpreterconfiguration* configuration) {
	int i;
	int commStatus[TOTAL_CORES]={0};
	char Parallella_postbox[TOTAL_CORES*15];
	MPI_Request requests[TOTAL_CORES*2];

	while (totalActive > 0) {
		for (i=0;i<TOTAL_CORES;i++) {
			if (active[i]) {
				checkStatusFlagsOfCore(basicState, configuration, i, requests, commStatus, Parallella_postbox);
			}
		}
	}
}

/**
 * Checks whether the core has sent some command to the host and actions this command if so
 */
static void checkStatusFlagsOfCore(struct shared_basic * basicState, struct interpreterconfiguration* configuration, int coreId, MPI_Request *reqs, int * interParallellaCommInProgress, char * postbox) {
	char updateCoreWithComplete=0;
	if (basicState->core_ctrl[coreId].core_busy == 0) {
		if (basicState->core_ctrl[coreId].core_run == 0) {
			deactivateCore(configuration, coreId);
		} else if (basicState->core_ctrl[coreId].core_command == 1) {
			displayCoreMessage(coreId, &basicState->core_ctrl[coreId]);
			updateCoreWithComplete=1;
		} else if (basicState->core_ctrl[coreId].core_command == 2) {
			inputCoreMessage(coreId, &basicState->core_ctrl[coreId]);
			updateCoreWithComplete=1;
		} else if (basicState->core_ctrl[coreId].core_command == 3) {
			raiseError(coreId, &basicState->core_ctrl[coreId]);
			updateCoreWithComplete=1;
		} else if (basicState->core_ctrl[coreId].core_command == 4) {
			stringConcatenate(coreId, &basicState->core_ctrl[coreId]);
			updateCoreWithComplete=1;
		} else if (basicState->core_ctrl[coreId].core_command == 5) {
			remoteP2P_Send(coreId, basicState);
			updateCoreWithComplete=1;
		} else if (basicState->core_ctrl[coreId].core_command == 6) {
			remoteP2P_Recv(coreId, basicState);
			updateCoreWithComplete=1;
		} else if (basicState->core_ctrl[coreId].core_command == 7) {
			if (!interParallellaCommInProgress[coreId]) {
				//printf("[node %d]starting sendrecv\n", basicState->nodeId);
				remoteP2P_SendRecv_Start(coreId, basicState, reqs, postbox);
				//printf("[node %d]starting sendrecv done!\n", basicState->nodeId);
				interParallellaCommInProgress[coreId] = 1;
			} else {
				int flag;
				MPI_Testall(2, &reqs[coreId*2], &flag, MPI_STATUSES_IGNORE);
				//printf("[node %d]sendrecv tested with flag:%d\n", basicState->nodeId, flag);
				if (flag) {
					//printf("[node %d]finishing sendrecv\n", basicState->nodeId);
					remoteP2P_SendRecv_Finish(coreId, basicState, postbox);
					//printf("[node %d]sendrecv finished!\n", basicState->nodeId);
					interParallellaCommInProgress[coreId] = 0;
					updateCoreWithComplete=1;
				}
			}
		} else if (basicState->core_ctrl[coreId].core_command >= 1000) {
			performMathsOp(&basicState->core_ctrl[coreId]);
			updateCoreWithComplete=1;
		}
		if (updateCoreWithComplete) {
			basicState->core_ctrl[coreId].core_command=0;
			basicState->core_ctrl[coreId].core_busy=++pb[coreId];
		}
	}
}

/**
 * Called when a core informs the host it has finished, optionally displays timing information
 */
static void deactivateCore(struct interpreterconfiguration* configuration, int coreId) {
	if (configuration->displayTiming) {
		struct timeval tval_after, tval_result;
		gettimeofday(&tval_after, NULL);
		timeval_subtract(&tval_result, &tval_after, &tval_before[coreId]);
		printf("Core %d completed in %ld.%06ld seconds\n", coreId, (long int)tval_result.tv_sec, (long int)tval_result.tv_usec);
	}
	active[coreId]=0;
	totalActive--;
}

/**
 * Initialises the cores by setting up their data structures and loading the interpreter onto the cores
 */
static void initialiseCores(struct shared_basic * basicState, int codeOnCore, struct interpreterconfiguration* configuration) {
	unsigned int i, j;
	char allActive=1;
	for (i=0;i<TOTAL_CORES;i++) {
		for (j=0;j<15;j++) basicState->core_ctrl[i].data[j]=0;
		basicState->core_ctrl[i].core_run=0;
		basicState->core_ctrl[i].core_busy=0;
		basicState->core_ctrl[i].core_command=0;
		basicState->core_ctrl[i].symbol_table=(void*) CORE_DATA_START;
		basicState->core_ctrl[i].postbox_start=(void*) (CORE_DATA_START+(basicState->symbol_size*
				(sizeof(struct symbol_node)+SYMBOL_TABLE_EXTRA))+(codeOnCore?basicState->length:0));
		if (!configuration->forceDataOnShared) {
			// If on core then store after the symbol table and code
			basicState->core_ctrl[i].stack_start=basicState->core_ctrl[i].postbox_start+100;
			basicState->core_ctrl[i].heap_start=basicState->core_ctrl[i].stack_start+LOCAL_CORE_STACK_SIZE;
		} else {
			basicState->core_ctrl[i].stack_start=SHARED_DATA_AREA_START+(i*(SHARED_STACK_DATA_AREA_PER_CORE+SHARED_HEAP_DATA_AREA_PER_CORE))+(void*)management_DRAM.ephy_base;
			basicState->core_ctrl[i].heap_start=basicState->core_ctrl[i].stack_start+SHARED_STACK_DATA_AREA_PER_CORE;
		}
		basicState->core_ctrl[i].shared_stack_start=SHARED_DATA_AREA_START+(i*(SHARED_STACK_DATA_AREA_PER_CORE+SHARED_HEAP_DATA_AREA_PER_CORE))+(void*)management_DRAM.ephy_base;
		basicState->core_ctrl[i].shared_heap_start=basicState->core_ctrl[i].shared_stack_start+SHARED_STACK_DATA_AREA_PER_CORE;
		basicState->core_ctrl[i].host_shared_data_start=SHARED_DATA_AREA_START+SHARED_STACK_DATA_AREA_PER_CORE+(i*(SHARED_STACK_DATA_AREA_PER_CORE+SHARED_HEAP_DATA_AREA_PER_CORE))+(void*) management_DRAM.base;
		active[i]=0;
		if (!configuration->intentActive[i]) allActive=0;
	}
	loadBinaryInterpreterOntoCores(configuration, allActive);
}

/**
 * Physically loads the binary interpreter onto the cores
 */
static void loadBinaryInterpreterOntoCores(struct interpreterconfiguration* configuration, char allActive) {
	unsigned int i;
	int result;
	char* binaryName=getEpiphanyExecutableFile(configuration);
	if (allActive && e_platform.chip[0].num_cores == TOTAL_CORES) {
		result = e_load_group(binaryName, &epiphany, 0, 0, epiphany.rows, epiphany.cols, E_TRUE);
		if (result != E_OK) fprintf(stderr, "Error loading Epiphany program\n");
	} else {
		for (i=0;i<TOTAL_CORES;i++) {
			if (configuration->intentActive[i]) {
				int row=i/epiphany.cols;
				result = e_load(binaryName, &epiphany, row, i-(row*epiphany.cols), E_TRUE);
				if (result != E_OK) fprintf(stderr, "Error loading Epiphany program onto core %d\n", i);
			}
		}
	}
	free(binaryName);
}

static char* getEpiphanyExecutableFile(struct interpreterconfiguration* configuration) {
        char * fullFilename=(char*) malloc(strlen(EPIPHANY_BINARY_FILE) + 6);
	if (configuration->loadElf) {
		sprintf(fullFilename, "%s.elf", EPIPHANY_BINARY_FILE);
	} else if (configuration->loadSrec) {
		sprintf(fullFilename, "%s.srec", EPIPHANY_BINARY_FILE);
	} else {
		fprintf(stderr, "Neither ELF nore SREC file formats selected for device executable\n");
		exit(0);
	}
	if (doesFileExist(fullFilename)) return fullFilename;
	char * binLocation=(char*) malloc(strlen(fullFilename) + strlen(BIN_PATH) + 1);
	sprintf(binLocation, "%s%s", BIN_PATH, fullFilename);
	if (doesFileExist(binLocation)) return binLocation;
	fprintf(stderr, "Can not device binary '%s' in the local directory or binary (%s) directory\n",
			fullFilename, BIN_PATH);
	exit(0);
}

static int doesFileExist(char * filename) {
	FILE * file = fopen(filename, "r");
	if (file != NULL) {
		fclose(file);
		return 1;
	}
	return 0;
}

/**
 * Places the bytecode representation of the users Python code onto the cores
 */
static void placeByteCode(struct shared_basic * basicState, int codeOnCore, char * intentActive) {
	basicState->data=(void*) (SHARED_CODE_AREA_START+management_DRAM.base);
	basicState->esdata=(void*) (SHARED_CODE_AREA_START+(void*)management_DRAM.ephy_base);
	memcpy(basicState->data, getAssembledCode(), basicState->length);
	if (!codeOnCore) {
		basicState->edata=basicState->esdata;
	} else {
		// Place code after symbol table
		basicState->edata=(void*) CORE_DATA_START+(basicState->symbol_size*
				(sizeof(struct symbol_node)+SYMBOL_TABLE_EXTRA));
	}
}

/**
 * Starts up applicable cores and tracks which were initially active
 */
static void startApplicableCores(struct shared_basic * basicState, struct interpreterconfiguration* configuration) {
	unsigned int i;
	for (i=0;i<TOTAL_CORES;i++) {
		if (configuration->intentActive[i]) {
			if (configuration->displayTiming) gettimeofday(&tval_before[i], NULL);
			basicState->core_ctrl[i].core_run=1;
			basicState->core_ctrl[i].active=1;
			active[i]=1;
			totalActive++;
		} else {
			basicState->core_ctrl[i].active=0;
		}
	}
}

/**
 * Performs some maths operation
 */
static void performMathsOp(struct core_ctrl * core) {
	if (core->core_command-1000 == RANDOM_MATHS_OP) {
		core->data[0]=INT_TYPE;
		int r=rand();
		memcpy(&core->data[1], &r, sizeof(int));
	} else {
		float fvalue=0.0, r=0.0;
		int ivalue;
		if (core->data[0]==REAL_TYPE) {
            memcpy(&fvalue, &core->data[1], sizeof(float));
		} else if (core->data[0]==INT_TYPE) {
		    memcpy(&ivalue, &core->data[1], sizeof(int));
		    fvalue=(float) ivalue;
		}
		if (core->core_command-1000 == SQRT_MATHS_OP) r=sqrtf(fvalue);
		if (core->core_command-1000 == SIN_MATHS_OP) r=sinf(fvalue);
		if (core->core_command-1000 == COS_MATHS_OP) r=cosf(fvalue);
		if (core->core_command-1000 == TAN_MATHS_OP) r=tanf(fvalue);
		if (core->core_command-1000 == ASIN_MATHS_OP) r=asinf(fvalue);
		if (core->core_command-1000 == ACOS_MATHS_OP) r=acosf(fvalue);
		if (core->core_command-1000 == ATAN_MATHS_OP) r=atanf(fvalue);
		if (core->core_command-1000 == SINH_MATHS_OP) r=sinhf(fvalue);
		if (core->core_command-1000 == COSH_MATHS_OP) r=coshf(fvalue);
		if (core->core_command-1000 == TANH_MATHS_OP) r=tanhf(fvalue);
		if (core->core_command-1000 == FLOOR_MATHS_OP) r=floorf(fvalue);
		if (core->core_command-1000 == CEIL_MATHS_OP) r=ceilf(fvalue);
		if (core->core_command-1000 == LOG_MATHS_OP) r=logf(fvalue);
		if (core->core_command-1000 == LOG10_MATHS_OP) r=log10f(fvalue);
		core->data[0]=REAL_TYPE;
		memcpy(&core->data[1], &r, sizeof(float));
	}
}

/**
 * Concatenates two strings, or a string and integer/real together with necessary conversions
 */
static void __attribute__((optimize("O0"))) stringConcatenate(int coreId, struct core_ctrl * core) {
	char * newString, *str1, *str2;
	unsigned int relativeLocation;
	if (core->data[0]==STRING_TYPE && core->data[5]==STRING_TYPE) {
        memcpy(&relativeLocation, &core->data[1], sizeof(unsigned int));
		str1=core->host_shared_data_start+relativeLocation;

		memcpy(&relativeLocation, &core->data[6], sizeof(unsigned int));
		str2=core->host_shared_data_start+relativeLocation;

		int totalLen=strlen(str1)+strlen(str2)+1;
		newString=(char*) malloc(totalLen);
		sprintf(newString,"%s%s", str1, str2);
	} else if (core->data[0]==STRING_TYPE) {
		memcpy(&relativeLocation, &core->data[1], sizeof(unsigned int));
		str1=core->host_shared_data_start+relativeLocation;

		int totalLen=strlen(str1)+21;
		newString=(char*) malloc(totalLen);
		if (((core->data[5] >> 7) & 1) == 1) {
			int v;
			memcpy(&v, &core->data[6], sizeof(int));
			sprintf(newString,"%s0x%x", str1, v);
		} else if (core->data[5]==INT_TYPE) {
			int d;
			memcpy(&d, &core->data[6], sizeof(int));
			sprintf(newString,"%s%d", str1, d);
		} else if (core->data[5]==BOOLEAN_TYPE) {
			int d;
			memcpy(&d, &core->data[6], sizeof(int));
			sprintf(newString,"%s%s", str1, d > 0?"true":"false");
		} else if (core->data[5]==NONE_TYPE) {
			sprintf(newString, "%sNONE", str1);
		} else if (core->data[5]==REAL_TYPE) {
			float f;
			memcpy(&f, &core->data[6], sizeof(float));
			sprintf(newString,"%s%f", str1, f);
		}
	} else {
		memcpy(&relativeLocation, &core->data[6], sizeof(unsigned int));
		str2=core->host_shared_data_start+relativeLocation;

		int totalLen=strlen(str2)+21;
		newString=(char*) malloc(totalLen);
		if (((core->data[0] >> 7) & 1) == 1) {
			int v;
			memcpy(&v, &core->data[1], sizeof(int));
			sprintf(newString,"0x%x%s", v, str2);
		} else if (core->data[0]==INT_TYPE) {
			int d;
			memcpy(&d, &core->data[1], sizeof(int));
			sprintf(newString,"%d%s", d, str2);
		} else if (core->data[0]==BOOLEAN_TYPE) {
			int d;
			memcpy(&d, &core->data[1], sizeof(int));
			sprintf(newString,"%s%s", d > 0?"true":"false", str2);
		} else if (core->data[0]==NONE_TYPE) {
			sprintf(newString, "NONE%s", str2);
		} else if (core->data[0]==REAL_TYPE) {
			float f;
			memcpy(&f, &core->data[1], sizeof(float));
			sprintf(newString,"%f%s", f, str2);
		}
	}
	char * target=allocateChunkInSharedHeapMemory(strlen(newString) + 1, core);
	strcpy(target, newString);
	relativeLocation=target-core->host_shared_data_start;
	memcpy(&core->data[11], &relativeLocation, sizeof(unsigned int));
	free(newString);
}

static char * allocateChunkInSharedHeapMemory(size_t size, struct core_ctrl * core) {
    unsigned char chunkInUse;
    unsigned int chunkLength, splitChunkLength;
    char * heapPtr=core->host_shared_data_start;

    size_t headersize=sizeof(unsigned char) + sizeof(unsigned int);
    size_t lenStride=sizeof(unsigned int);
    while (1==1) {
        memcpy(&chunkLength, heapPtr, sizeof(unsigned int));
        memcpy(&chunkInUse, &heapPtr[lenStride], sizeof(unsigned char));
        if (!chunkInUse && chunkLength >= size) {
            char * splitChunk=(char*) (heapPtr + size + headersize);
            splitChunkLength=chunkLength - size - headersize;
            memcpy(splitChunk, &splitChunkLength, sizeof(unsigned int));
            memcpy(&splitChunk[lenStride], &chunkInUse, sizeof(unsigned char));
            chunkLength=size;
            memcpy(heapPtr, &chunkLength, sizeof(unsigned int));
            chunkInUse=1;
            memcpy(&heapPtr[lenStride], &chunkInUse, sizeof(unsigned char));
            return heapPtr + headersize;
        } else {
            heapPtr+=chunkLength + headersize;
            if (heapPtr  >= core->host_shared_data_start + SHARED_HEAP_DATA_AREA_PER_CORE) {
                break;
            }
        }
    }
    return NULL;
}

/**
 * The core has raised an error
 */
static void raiseError(int coreId, struct core_ctrl * core) {
	unsigned char errorCode;
    memcpy(&errorCode, &core->data[1], sizeof(unsigned char));
    char* errorMessage=translateErrorCodeToMessage(errorCode);
	if (errorMessage != NULL) {
        fprintf(stderr, "Error from core %d: %s\n", coreId, errorMessage);
        free(errorMessage);
	}
}

/**
 * Inputs a message from the user, with some optional displayed message.
 */
static void __attribute__((optimize("O0"))) inputCoreMessage(int coreId, struct core_ctrl * core) {
	char inputvalue[1000];
	unsigned int relativeLocation;
	if (core->data[0] == STRING_TYPE) {
		memcpy(&relativeLocation, &core->data[1], sizeof(unsigned int));
		char * message=core->host_shared_data_start+relativeLocation;
		printf("[device %d] %s", coreId, message);
	} else {
		printf("device %d> ", coreId);
	}
	errorCheck(scanf("%[^\n]", inputvalue), "Getting user input");
	int inputType=getTypeOfInput(inputvalue);
	// The following 2 lines cleans up the input so it is ready for the next input call
	int c;
	while ( (c = getchar()) != '\n' && c != EOF ) { }

	if (inputType==INT_TYPE) {
		core->data[0]=INT_TYPE;
		int iv=atoi(inputvalue);
		memcpy(&core->data[1], &iv, sizeof(int));
	} else if (inputType==REAL_TYPE) {
		core->data[0]=REAL_TYPE;
		float fv=atof(inputvalue);
		memcpy(&core->data[1], &fv, sizeof(float));
	} else {
		core->data[0]=STRING_TYPE;
		char * target=allocateChunkInSharedHeapMemory(strlen(inputvalue) + 1, core);
		strcpy(target, inputvalue);
		relativeLocation=target-core->host_shared_data_start;
		memcpy(&core->data[1], &relativeLocation, sizeof(unsigned int));
	}
}

/**
 * Determines the type of input from the user (is it an integer, real or string)
 */
static int getTypeOfInput(char * input) {
	unsigned int i;
	char allNumbers=1, hasDecimal=0;
	for (i=0;i<strlen(input);i++) {
		if (!isdigit(input[i])) {
			if (input[i] == '.') {
				hasDecimal=1;
			} else {
				allNumbers=0;
			}
		}
	}
	if (allNumbers && !hasDecimal) return INT_TYPE;
	if (allNumbers && hasDecimal) return REAL_TYPE;
	return STRING_TYPE;
}

/**
 * Displays a message from the core
 */
static void displayCoreMessage(int coreId, struct core_ctrl * core) {
	if (((core->data[0] >> 7) & 1) == 1) {
		int y;
        memcpy(&y, &(core->data[1]), sizeof(int));
        char t=core->data[0] & 0x1F;
        char dt=core->data[0] >> 5 & 0x3;
		printf("[device %d] 0x%x points to %s %s\n", coreId, y,
				t==0 ? "integer" : t==1 ? "floating point" : t==2 ? "boolean" : t==3 ? "none" : t==4 ? "function" : "unknown", dt==0 ? "scalar" : "array");
	} else if (core->data[0] == 0) {
		int y;
        memcpy(&y, &(core->data[1]), sizeof(int));
		printf("[device %d] %d\n", coreId, y);
	} else if (core->data[0] == 1) {
		float y;
        memcpy(&y, &(core->data[1]), sizeof(float));
		printf("[device %d] %f\n", coreId, y);
	} else if (core->data[0] == 3) {
		int y;
        memcpy(&y, &(core->data[1]), sizeof(int));
		printf("[device %d] %s\n", coreId, y> 0 ? "true" : "false");
	} else if (core->data[0] == 4) {
		printf("[device %d] NONE\n", coreId);
	} else if (core->data[0] == 2) {
		unsigned int relativeLocation;
		memcpy(&relativeLocation, &core->data[1], sizeof(unsigned int));
		char * message=core->host_shared_data_start+relativeLocation;
		printf("[device %d] %s\n", coreId, message);
	}
}

/**
 * Helper timeval subtraction for core timing information
 */
static void timeval_subtract(struct timeval *result, struct timeval *x,  struct timeval *y) {
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  result->tv_sec = x->tv_sec - y->tv_sec;
  result->tv_usec = x->tv_usec - y->tv_usec;
}

/**
 * Provisional remote point-to-point communication function: SEND
 */
static void __attribute__((optimize("O0"))) remoteP2P_Send(int sourceId, struct shared_basic * info) {
	int dest, sourceId_global;
	int val;
	int myid;
	int sendbuf[3];
	sourceId_global = TOTAL_CORES*info->nodeId + sourceId;

	MPI_Comm_rank(MPI_COMM_WORLD, &myid);

	//retrieve data from Epiphany
	memcpy(&dest, &(info->core_ctrl[sourceId].data[0]), sizeof(int));
	memcpy(&val, &(info->core_ctrl[sourceId].data[6]), sizeof(int));

	//send data to another Parallella
	memcpy(&sendbuf[0], &dest, sizeof(int));
	memcpy(&sendbuf[1], &val, sizeof(int));
	memcpy(&sendbuf[2], &sourceId_global, sizeof(int));
	printf("Sending message[value:%d] from local core%d to remote core%d via host node%d\n", val, sourceId_global, dest, myid);
	MPI_Send(sendbuf, 3, MPI_INT, resolveRank(dest), sourceId_global, MPI_COMM_WORLD);
}

/**
 * Provisional remote point-to-point communication function: RECV
 */
static void __attribute__((optimize("O0"))) remoteP2P_Recv(int destId, struct shared_basic * info) {
	int source;
	int var;
	int myid;
	int recvbuf[3];
	int destId_global = TOTAL_CORES*info->nodeId + destId;
	MPI_Status status;

	MPI_Comm_rank(MPI_COMM_WORLD, &myid);

	//receive data from other Parallellas
	memcpy(&source, &(info->core_ctrl[destId].data[0]), sizeof(int));
	MPI_Recv(recvbuf, 3, MPI_INT, resolveRank(source), source, MPI_COMM_WORLD, &status);
	memcpy(&var, &recvbuf[1], sizeof(int));
	printf("Receiving message[int:%d] from romote core%d to local core%d via host node%d\n",var, source, destId_global, myid);

	//hand the received data to Epiphany
	memcpy(&(info->core_ctrl[destId].data[6]), &var, sizeof(int));
}

/**
 * Provisional remote point-to-point communication function: SEND AND RECV
 */
static void __attribute__((optimize("O0"))) remoteP2P_SendRecv_Start(int callerId, struct shared_basic * info, MPI_Request *r_handles, char *recvbuf) {
	int target;
	int callerId_global = TOTAL_CORES*info->nodeId + callerId;

	char sendbuf[15];
	sendbuf[14] = info->core_ctrl[callerId].data[5];
	if (sendbuf[14]==REAL_TYPE) {
		float val_float;
		memcpy(&val_float, &(info->core_ctrl[callerId].data[6]), sizeof(float));
		printf("[node %d]data to be sent has a real value:%f(Length: %dbytes)\n",info->nodeId, val_float, sizeof(float));
		printbuf((char *)&val_float, sizeof(float));
		memcpy(&sendbuf[4], &val_float, sizeof(float));
	} else if (sendbuf[14]==INT_TYPE) {
		int val_int;
		memcpy(&val_int, &(info->core_ctrl[callerId].data[6]), sizeof(int));
		printf("[node %d]data to be sent has a integer value:%d(Length: %dbytes)\n",info->nodeId, val_int, sizeof(int));
		memcpy(&sendbuf[4], &val_int, sizeof(int));
	} else {
		printf("[node %d]unsupported sending data type\n",info->nodeId);
	}
	memcpy(&target, info->core_ctrl[callerId].data, sizeof(int));
	memcpy(&sendbuf[0], &target, sizeof(int));
	memcpy(&sendbuf[8], &callerId_global, sizeof(int));

	printf("[node %d]sendbuf:\n", info->nodeId);
	printbuf(sendbuf, 15);
	printf("[node %d]recvbuf before sending:\n", info->nodeId);
	printbuf(&recvbuf[callerId*15], 15);

	MPI_Isend(sendbuf, 15, MPI_CHAR, resolveRank(target), callerId_global, MPI_COMM_WORLD, &r_handles[callerId*2]);
	MPI_Irecv(&recvbuf[callerId*15], 15, MPI_CHAR, resolveRank(target), target, MPI_COMM_WORLD, &r_handles[callerId*2+1]);
}

static void __attribute__((optimize("O0"))) remoteP2P_SendRecv_Finish(int callerId, struct shared_basic * info, char *recvbuf) {
	info->core_ctrl[callerId].data[10]=recvbuf[callerId*15+14];
	printf("[node %d]recvbuf:\n", info->nodeId);
	printbuf(&recvbuf[callerId*15], 15);
	if (info->core_ctrl[callerId].data[10]==REAL_TYPE) {
		float val_float;
		memcpy(&val_float, &recvbuf[callerId*15+4], sizeof(float));
		printf("[node %d]data received has a real value:%f\n",info->nodeId, val_float);
		memcpy(&(info->core_ctrl[callerId].data[11]), &recvbuf[callerId*15+4], sizeof(float));
	} else if (info->core_ctrl[callerId].data[10]==INT_TYPE) {
		int val_int;
		memcpy(&val_int, &recvbuf[callerId*15+4], sizeof(int));
		printf("[node %d]data received has a integer value:%d\n",info->nodeId, val_int);
		memcpy(&(info->core_ctrl[callerId].data[11]), &recvbuf[callerId*15+4], sizeof(int));
	} else {
		printf("[node %d]unknown data type for received data\n",info->nodeId);
	}
}

/**
 * Return the rank of the node on which the core[given by id] is located
 */
static int resolveRank(int id) {
	int rank;
	rank = (id+1)/TOTAL_CORES;
	return rank;
}

/**
 * Debugging function: Display the bits int the memory
 */
static void printbuf(char *buf, int length) {
	int i;
	for (i=0; i<length; i++){
		printbinchar(buf[i]);
	}
	printf("\n");
}

static void printbinchar(char c) {
    for (int i = 7; i >= 0; --i) {
        putchar( (c & (1 << i)) ? '1' : '0' );
    }
    printf(" ");
}
