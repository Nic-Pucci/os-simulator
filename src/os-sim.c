/* Nic Pucci
 * OS-SIM IMPLEMENTATION
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include <stdarg.h>
#include "List.h"

#define NUM_PRIORITY_LEVELS 3
#define MAX_MESSAGE_LENGTH 40
#define MAX_INPUT_LENGTH ( 2 + MAX_MESSAGE_LENGTH )
#define INIT_PROCESS_ID 0
#define NUM_SEMAPHORES 5
#define MAX_CPU_BURSTS 5

typedef struct message
{
	int senderProcessID;
	int recipientProcessID;
	char messageStr [ MAX_MESSAGE_LENGTH ];
} MESSAGE;

enum STATE 
{
	SEND_BLOCKED = 0 ,
	RECEIVE_BLOCKED = 1 ,
	SEM_BLOCKED = 2 ,
	READY = 3,
	RUNNING = 4
};

enum PRIORITY_DIRECTION 
{
	PROMOTING = 0 ,
	DEMOTING = 1
};

typedef struct pcb 
{
	int processID;
	int priorityLevel;
	enum PRIORITY_DIRECTION priorityDirection;
	int numCPUBurstsInPriorityLevel;
	enum STATE processState;
	MESSAGE *readMessage;
} PCB;


PCB INIT_PROCESS = 
{ 
	.processID = INIT_PROCESS_ID , 
	.priorityLevel = NUM_PRIORITY_LEVELS ,
	.priorityDirection = DEMOTING ,
	.numCPUBurstsInPriorityLevel = 0 ,
	.processState = RUNNING ,
	.readMessage = NULL
};

const int SUCCESS_OP = 1;
const int FAILURE_OP = 0;

/* USER COMMANDS */
const char COMMAND_DELIMITER [] = ", ";
const char *CREATE_COMMAND = "C";
const char *FORK_COMMAND = "F";
const char *KILL_COMMAND = "K";
const char *EXIT_COMMAND = "E";
const char *QUANTUM_COMMAND = "Q";
const char *SEND_COMMAND = "S";
const char *RECEIVE_COMMAND = "R";
const char *REPLY_COMMAND = "Y";
const char *NEW_SEMAPHORE_COMMAND = "N";
const char *SEMAPHORE_P_COMMAND = "P";
const char *SEMAPHORE_V_COMMAND = "V";
const char *PROCESS_INFO_COMMAND = "I";
const char *TOTAL_INFO_COMMAND = "T";

int nextAvailProcessID = INIT_PROCESS_ID + 1;

const char DEFAULT_TEXT_COLOR [] = "\033[0m"; // default color by system
const char OS_TEXT_COLOR [] = "\033[0;36m"; // cyan
const char ERROR_TEXT_COLOR [] = "\033[0;31m"; // red
const char SUCCESS_TEXT_COLOR [] = "\033[0;32m"; // green

LIST *readyPriorityQueues [ NUM_PRIORITY_LEVELS ];
LIST *sendBlockedQueue;
LIST *receiveBlockedQueue;
LIST *messagesQueue;

enum SEMAPHORE_STATUS 
{
	CREATED = 0 ,
	NOT_CREATED = 1
};

typedef struct semaphore 
{
	int semID;
	enum SEMAPHORE_STATUS semStatus;
	int semValue;
	LIST *blockedPCBs;
} SEMAPHORE;

SEMAPHORE semaphores [ NUM_SEMAPHORES ];

PCB *runningProcess = &INIT_PROCESS;


void ChangeTextColorToDefault () 
{
	printf ( DEFAULT_TEXT_COLOR );
}

void ChangeTextColorToOS () 
{
	printf ( OS_TEXT_COLOR );
}

void ChangeTextColorToError () 
{
	printf ( ERROR_TEXT_COLOR );
}

void ChangeTextColorToSuccess () 
{
	printf ( SUCCESS_TEXT_COLOR );
}
 

void PrintMessage ( const MESSAGE *message ) 
{
	if ( !message ) 
	{
		return;
	}

	printf ( 
		"Message: %s (SenderID = %d, RecipientID = %d\n)" , 
		message -> messageStr ,
		message -> senderProcessID ,
		message -> recipientProcessID
	);
}

int NumSystemProcessesTotal () 
{
	int numReady = 0;
	for ( int i = 0 ; i < NUM_PRIORITY_LEVELS ; i++ ) 
	{
		LIST *readyQueue = readyPriorityQueues [ i ];
		numReady += ListCount ( readyQueue );
	}

	int numSemBlocked = 0;
	for ( int i = 0 ; i < NUM_SEMAPHORES ; i++ ) 
	{
		SEMAPHORE *semaphore = &semaphores [ i ];
		numSemBlocked += ListCount ( semaphore -> blockedPCBs );
	}

	int numSendBlocked = ListCount ( sendBlockedQueue );
	int numReceiveBlocked = ListCount ( receiveBlockedQueue );
	
	int initProcessCount = 1;
	int runningProcessCount = 0;
	if ( runningProcess -> processID != INIT_PROCESS_ID ) 
	{
		runningProcessCount = 1;
	}

	int numProcesses = initProcessCount + 
		runningProcessCount + 
		numReady + 
		numSemBlocked + 
		numSendBlocked + 
		numReceiveBlocked;

	return numProcesses;
}

void PrintPCB ( const PCB *pcb ) 
{
	if ( !pcb ) 
	{
		return;
	}

	char* processStateStr;
	switch ( pcb -> processState ) 
	{
		case SEND_BLOCKED :
			processStateStr = "SEND-BLOCKED";
			break;

		case RECEIVE_BLOCKED :
			processStateStr = "RECEIVE-BLOCKED";
			break;

		case SEM_BLOCKED :
			processStateStr = "SEMAPHORE-BLOCKED";
			break;

		case READY :
			processStateStr = "READY";
			break;

		case RUNNING :
			processStateStr = "RUNNING";
			break;

		default: 
			processStateStr = "ERROR";
	}

	char* priorityDirStr;
	switch ( pcb -> priorityDirection ) 
	{
		case PROMOTING :
			priorityDirStr = "PROMOTION";
			break;

		case DEMOTING :
			priorityDirStr = "DEMOTION";
			break;

		default: 
			priorityDirStr = "ERROR";
	}

	if ( pcb -> processID == INIT_PROCESS_ID )
	{
		printf ( 
			"INIT PROCESS (ID = %d) (%s) (TOTAL CPU-Bursts = %d)\n\n" ,
			pcb -> processID , 
			processStateStr ,
			pcb -> numCPUBurstsInPriorityLevel
		);
	}
	else 
	{
		printf ( 
			"PROCESS (ID = %d) (%s) (PRIORITY = %d) (%d CPU-Bursts until %s)\n\n" ,
			pcb -> processID , 
			processStateStr ,
			pcb -> priorityLevel ,
			MAX_CPU_BURSTS - pcb -> numCPUBurstsInPriorityLevel ,
			priorityDirStr
		);
	}
}

void PrintCurrentRunningProcess () 
{
	if ( !runningProcess ) 
	{
		return;
	}

	ChangeTextColorToOS ();
	printf ( "OS: now running - " );
	PrintPCB ( runningProcess );
	ChangeTextColorToDefault ();
}

void PrintInputPrompt () 
{
	printf ( "Prompt: Please input an OS Command (separate params with space or comma)\n" );
	printf ( "> " );
	fflush ( stdout );
}

void InitAllLists () 
{
	for ( int i = 0 ; i < NUM_PRIORITY_LEVELS ; i++ ) 
	{
		readyPriorityQueues [ i ] = ListCreate ();
	}

	for ( int i = 0 ; i < NUM_SEMAPHORES ; i++ ) 
	{
		semaphores [ i ].semID = i;
		semaphores [ i ].semValue = 0;
		semaphores [ i ].semStatus = NOT_CREATED;
		semaphores [ i ].blockedPCBs = ListCreate ();
	}

	receiveBlockedQueue = ListCreate ();
	sendBlockedQueue = ListCreate ();
	messagesQueue = ListCreate ();
}

void FreeMessage ( MESSAGE *message ) 
{
	if ( !message ) {
		return;
	}

	free ( message );
}

void FreePCB ( PCB *pcb ) 
{
	if ( !pcb ) {
		return;
	}

	FreeMessage ( pcb -> readMessage );
	free ( pcb );
}

void FreeAllLists () {
	for ( int i = 0 ; i < NUM_PRIORITY_LEVELS ; i++ ) 
	{
		ListFree ( readyPriorityQueues [ i ] , ( void *) &FreePCB );
	}

	for ( int i = 0 ; i < NUM_SEMAPHORES ; i++ ) 
	{
		SEMAPHORE *semaphore = &semaphores [ i ];
		ListFree ( semaphore -> blockedPCBs , ( void *) &FreePCB );
	}

	ListFree ( receiveBlockedQueue , ( void *) &FreePCB );
	ListFree ( sendBlockedQueue , ( void *) &FreePCB );
	ListFree ( messagesQueue , ( void *) &FreeMessage );
}

int ValidPriorityLevel ( int priorityLevel ) 
{
	return priorityLevel > -1 && priorityLevel < NUM_PRIORITY_LEVELS;
}

void AddToReadyQueue ( PCB *pcb ) 
{
	if ( !pcb || pcb -> processID == INIT_PROCESS_ID ) {
		return;
	}

	int priorityLevel = pcb -> priorityLevel;
	if ( !ValidPriorityLevel ( priorityLevel ) ) {
		return;
	}

	pcb -> processState = READY;
	LIST* readyQueue = readyPriorityQueues [ priorityLevel ];
	ListPrepend ( readyQueue , pcb );
}

void UpdateProcessPriorityLevel ( PCB *process ) 
{
	if ( !process ) 
	{
		return;
	}

	process -> numCPUBurstsInPriorityLevel += 1;
	if ( process -> numCPUBurstsInPriorityLevel < MAX_CPU_BURSTS ) 
	{
		return;
	}

	if ( process -> priorityLevel == NUM_PRIORITY_LEVELS - 1 ) 
	{
		process -> priorityDirection = PROMOTING;
	}
	else if ( process -> priorityLevel == 0 ) 
	{
		process -> priorityDirection = DEMOTING;
	}

	int prevPriorityLevel = process -> priorityLevel;
	if ( process -> priorityDirection == PROMOTING ) 
	{
		process -> priorityLevel -= 1;
		printf ( 
			"OS: PROCESS ( ID = %d) PROMOTED PRIORITY LEVEL (%d -> %d)\n\n" ,
			process -> processID ,
			prevPriorityLevel ,
			process -> priorityLevel
		);
	}
	else if ( process -> priorityDirection == DEMOTING ) 
	{
		process -> priorityLevel += 1;
		printf ( 
			"OS: PROCESS ( ID = %d) DEMOTED PRIORITY LEVEL (%d -> %d)\n\n" ,
			process -> processID ,
			prevPriorityLevel ,
			process -> priorityLevel
		);
	}

	process -> numCPUBurstsInPriorityLevel = 0;
}

void RunNextProcess () 
{
	int runningProcIsAlive = runningProcess != NULL;

	if ( runningProcIsAlive && runningProcess -> processID == INIT_PROCESS_ID ) 
	{
		INIT_PROCESS.processState = READY;
		INIT_PROCESS.numCPUBurstsInPriorityLevel += 1;
	}
	else if ( runningProcIsAlive )
	{
		UpdateProcessPriorityLevel ( runningProcess );
		AddToReadyQueue ( runningProcess );
	}

	runningProcess = NULL;
	for ( int i = 0 ; i < NUM_PRIORITY_LEVELS ; i++ ) {
		LIST *readyQueue = readyPriorityQueues [ i ];
		if ( ListCount ( readyQueue ) > 0 ) {
			runningProcess = ListTrim ( readyQueue );
			break;
		}
	}

	if ( !runningProcess ) {
		runningProcess = &INIT_PROCESS;
		INIT_PROCESS.processState = RUNNING;
	}

	runningProcess -> processState = RUNNING;

	PrintCurrentRunningProcess ();

	MESSAGE *receivedMessage = runningProcess -> readMessage;
	if ( receivedMessage ) 
	{
		ChangeTextColorToSuccess ();
		printf ( 
			"SUCCESS: Received message \"%s\" (SenderID = %d, recipientProcessID = %d)\n\n",
			receivedMessage -> messageStr,
			receivedMessage -> senderProcessID,
			receivedMessage -> recipientProcessID
		);
		ChangeTextColorToDefault ();

		FreeMessage ( receivedMessage );
		runningProcess -> readMessage = NULL;
	}
}

int EqualStr ( const char* str1 , const char* str2 ) 
{
	return strcmp ( str1 , str2 ) == 0;
}

int ParamToInt ( const char *param ) 
{
	if ( !param ) {
		return -1;
	}

	errno = 0;
	char *tailPtr = NULL;
	int paramInt = strtol ( param , &tailPtr , 0 );

	int notValidInt = tailPtr == param;
	if ( paramInt == 0 && notValidInt ) {
		return -1;
	}

	if ( errno == EINVAL ) {
		return -1;
	}

	int valIsOutOfRange = paramInt == LONG_MAX || paramInt == LONG_MIN;
	if ( errno == ERANGE && valIsOutOfRange ) {
		return -1;
	}

   return paramInt;
}

PCB *NewProcess ( int priorityLevel ) 
{
	if ( !ValidPriorityLevel ( priorityLevel ) ) {
		int lowestPriorityNum = NUM_PRIORITY_LEVELS - 1;

		ChangeTextColorToError ();
		printf ( "ERROR: Priority Level number can only be between 0 (Highest) and %d (Lowest)\n\n" , lowestPriorityNum );
		ChangeTextColorToDefault ();

		return NULL;
	}

	PCB *newProcess = ( PCB *) malloc ( sizeof ( PCB ) );
	newProcess -> processID = nextAvailProcessID;
	newProcess -> priorityLevel = priorityLevel;
	newProcess -> numCPUBurstsInPriorityLevel = 0;
	newProcess -> readMessage = NULL;

	if ( newProcess -> priorityLevel == NUM_PRIORITY_LEVELS - 1 ) 
	{
		newProcess -> priorityDirection = PROMOTING;
	}
	else 
	{
		newProcess -> priorityDirection = DEMOTING;
	}

	nextAvailProcessID++;
	return newProcess;
}

void CreateProcess ( int priorityLevel ) {
	PCB *newProcess = NewProcess ( priorityLevel );
	if ( !newProcess ) 
	{
		return;
	}

	AddToReadyQueue ( newProcess );

	ChangeTextColorToSuccess ();
	printf ( "SUCCESS: CREATED " );
	PrintPCB ( newProcess );
	ChangeTextColorToDefault ();

	if ( runningProcess -> processID == INIT_PROCESS_ID ) {
		RunNextProcess ();
	}
}

void QuantumExpired () 
{
	RunNextProcess ();
}

int EqualsProcessID ( void *pcb , void *processID ) 
{
	if ( !pcb || !processID )
	{
		return 0;
	}

	return ( ( PCB *) pcb ) -> processID == *( ( int *) processID );
}

PCB *RemoveProcessFromQueue ( int processID , LIST *queue ) 
{
	if ( !queue ) {
		return NULL;
	} 

	ListFirst ( queue );
	PCB *foundProcess = ( PCB *) ListSearch ( queue , &EqualsProcessID , &processID );
	if ( !foundProcess ) {
		return NULL;
	}

	ListRemove ( queue );
	return foundProcess;
}

PCB *FindProcessFromQueue ( int processID , LIST *queue ) 
{
	if ( !queue ) {
		return NULL;
	} 

	ListFirst ( queue );
	PCB *foundProcess = ( PCB *) ListSearch ( queue , &EqualsProcessID , &processID );
	if ( !foundProcess ) {
		return NULL;
	}

	return foundProcess;
}

PCB *FindAndRemoveProcessFromAllQueues ( int processID ) 
{
	PCB *foundProcess = RemoveProcessFromQueue ( processID , sendBlockedQueue );
	
	if ( !foundProcess ) 
	{
		foundProcess = RemoveProcessFromQueue ( processID , receiveBlockedQueue );
	}

	for ( int i = 0 ; i < NUM_PRIORITY_LEVELS && !foundProcess ; i++ ) 
	{
		LIST *readyQueue = readyPriorityQueues [ i ];
		foundProcess = RemoveProcessFromQueue ( processID , readyQueue );
	}

	for ( int i = 0 ; i < NUM_SEMAPHORES && !foundProcess ; i++ ) 
	{
		SEMAPHORE *semaphore = &semaphores [ i ];
		LIST *blockedQueue = semaphore -> blockedPCBs;
		foundProcess = RemoveProcessFromQueue ( processID , blockedQueue );
	}

	return foundProcess;
}

PCB *FindProcessFromAllQueues ( int processID ) 
{
	PCB *foundProcess = FindProcessFromQueue ( processID , sendBlockedQueue );
	
	if ( !foundProcess ) 
	{
		foundProcess = FindProcessFromQueue ( processID , receiveBlockedQueue );
	}

	for ( int i = 0 ; i < NUM_PRIORITY_LEVELS && !foundProcess ; i++ ) 
	{
		LIST *readyQueue = readyPriorityQueues [ i ];
		foundProcess = FindProcessFromQueue ( processID , readyQueue );
	}

	for ( int i = 0 ; i < NUM_SEMAPHORES && !foundProcess ; i++ ) 
	{
		SEMAPHORE *semaphore = &semaphores [ i ];
		LIST *blockedQueue = semaphore -> blockedPCBs;
		foundProcess = FindProcessFromQueue ( processID , blockedQueue );
	}

	return foundProcess;
}

int ProcessExistsInQueue ( int processID , LIST *queue ) 
{
	if ( !queue ) {
		return 0;
	} 

	ListFirst ( queue );
	PCB *foundProcess = ( PCB *) ListSearch ( queue , &EqualsProcessID , &processID );
	if ( !foundProcess ) {
		return 0;
	}

	return 1;
}

int ProcessExists ( int processID ) 
{
	int foundProcess = processID == INIT_PROCESS_ID;

	if ( !foundProcess ) 
	{
		foundProcess = processID == runningProcess -> processID;
	}

	if ( !foundProcess ) 
	{
		foundProcess = ProcessExistsInQueue ( processID , sendBlockedQueue );
	}

	if ( !foundProcess ) 
	{
		foundProcess = ProcessExistsInQueue ( processID , receiveBlockedQueue );
	}

	for ( int i = 0 ; i < NUM_PRIORITY_LEVELS && !foundProcess ; i++ ) 
	{
		LIST *readyQueue = readyPriorityQueues [ i ];
		foundProcess = ProcessExistsInQueue ( processID , readyQueue );
	}

	for ( int i = 0 ; i < NUM_SEMAPHORES && !foundProcess ; i++ ) 
	{
		SEMAPHORE *semaphore = &semaphores [ i ];
		LIST *blockedPCBs = semaphore -> blockedPCBs;
		foundProcess = ProcessExistsInQueue ( processID , blockedPCBs );
	}

	return foundProcess;
}

void EndProcess ( int processID , char *commandAction ) 
{
	int numSystemProcessesTotal = NumSystemProcessesTotal ();
	int onlyInitProcessesInSystem = numSystemProcessesTotal == 1;
	if ( processID == INIT_PROCESS_ID && !onlyInitProcessesInSystem ) {
		ChangeTextColorToError ();
		printf ( 
			"ERROR: Not %s INIT PROCESS (ID = %d), there are still %d other processes in the system\n\n" ,
			commandAction ,
			INIT_PROCESS.processID ,
			numSystemProcessesTotal - 1
		);
		ChangeTextColorToDefault ();

		return;
	}

	if ( processID == INIT_PROCESS_ID && onlyInitProcessesInSystem ) {
		ChangeTextColorToSuccess ();
		printf ( 
			"SUCCESS: %s INIT PROCESS (ID = %d)\n\n" , 
			commandAction , 
			runningProcess -> processID 
		);
		ChangeTextColorToDefault ();

		runningProcess = NULL;
		return;
	}

	if ( processID == runningProcess -> processID ) {
		ChangeTextColorToSuccess ();
		printf ( 
			"SUCCESS: %s PROCESS (ID = %d) (State = RUNNING)\n\n" , 
			commandAction , 
			runningProcess -> processID 
		);
		ChangeTextColorToDefault ();

		FreePCB ( runningProcess );
		runningProcess = NULL;
		RunNextProcess ();
		return;
	}

	PCB *foundProcess = FindAndRemoveProcessFromAllQueues ( processID );
	if ( !foundProcess ) {
		ChangeTextColorToError ();
		printf ( "ERROR: No Process with ID = %d exists\n\n" , processID );
		ChangeTextColorToDefault ();

		return;
	}

	char *processStateText = "";
	if ( foundProcess -> processState == RUNNING ) 
	{
		processStateText = "RUNNING";
	}
	else if ( foundProcess -> processState == READY )
	{
		processStateText = "READY";
	}

	ChangeTextColorToSuccess ();
	printf ( 
		"SUCCESS: %s PROCESS (ID = %d) (State = %s)\n\n" ,
		commandAction , 
		foundProcess -> processID , 
		processStateText
	);
	ChangeTextColorToDefault ();

	FreePCB ( foundProcess );
}

void SendBlockRunningProcess () 
{
	if ( runningProcess -> processID == INIT_PROCESS_ID ) 
	{
		return;
	} 

	runningProcess -> processState = SEND_BLOCKED;
	ListAppend ( sendBlockedQueue , ( void *) runningProcess );

	ChangeTextColorToOS ();
	printf ( "OS: Process (ID = %d) is SEND-BLOCKED\n\n" , runningProcess -> processID );
	ChangeTextColorToDefault ();

	runningProcess = NULL;
	RunNextProcess ();
}

int UnblockSendBlockedProcess ( MESSAGE *replyMessage ) 
{
	if ( !replyMessage ) 
	{
		return 0;
	}

	if ( replyMessage -> recipientProcessID == INIT_PROCESS_ID ) 
	{
		return 0;
	}

	PCB *unblockedProcess = RemoveProcessFromQueue ( replyMessage -> recipientProcessID , sendBlockedQueue );
	if ( !unblockedProcess ) 
	{
		return 0;
	}

	unblockedProcess -> readMessage = replyMessage;

	ChangeTextColorToOS ();
	printf ( "OS: Process (ID = %d) is SEND-UNBLOCKED\n\n" , unblockedProcess -> processID );
	ChangeTextColorToDefault ();

	AddToReadyQueue ( unblockedProcess );
	return 1;
}

void AddToMessagesQueue ( MESSAGE *message ) 
{
	if ( !message ) 
	{
		return;
	}

	ListAppend ( messagesQueue , ( void *) message );
}

MESSAGE *CreateMessage ( int senderProcessID , int recipientProcessID , const char *messageStr ) 
{
	if ( !messageStr || strlen ( messageStr ) == 0 ) 
	{
		messageStr = "<Blank Message>\0";
	}

	MESSAGE *message = ( MESSAGE *) malloc ( sizeof ( MESSAGE ) );

	message -> senderProcessID = senderProcessID;
	message -> recipientProcessID = recipientProcessID;
	strcpy ( message -> messageStr , messageStr );

	return message;
}

int EqualsMessageRecipientID (  void *message , void *processID ) 
{
	if ( !message || !processID ) 
	{
		return 0;
	}

	return ( ( MESSAGE *) message ) -> recipientProcessID == *( ( int *) processID );
}

MESSAGE *FindMessage ( int processID ) 
{
	ListFirst ( messagesQueue );
	MESSAGE *foundMessage = ( MESSAGE *) ListSearch ( messagesQueue , &EqualsMessageRecipientID , &processID );
	if ( !foundMessage ) {
		return 0;
	}

	ListRemove ( messagesQueue );

	return foundMessage;
}

void ReceiveBlockRunningProcess () 
{
	if ( runningProcess -> processID == INIT_PROCESS_ID ) 
	{
		return;
	}

	runningProcess -> processState = RECEIVE_BLOCKED;
	ListAppend ( receiveBlockedQueue , ( void *) runningProcess );

	ChangeTextColorToOS ();
	printf ( "OS: Running Process (ID = %d) is RECEIVE-BLOCKED\n\n" , runningProcess -> processID );
	ChangeTextColorToDefault ();

	runningProcess = NULL;
	RunNextProcess ();
}

int UnblockReceiveBlockedProcess ( MESSAGE *sentMessage ) 
{
	if ( !sentMessage ) 
	{
		return 0;
	}

	if ( sentMessage -> recipientProcessID == INIT_PROCESS_ID ) 
	{
		return 0;
	}

	PCB *unblockedProcess = RemoveProcessFromQueue ( sentMessage -> recipientProcessID , receiveBlockedQueue );
	if ( !unblockedProcess ) 
	{
		return 0;
	}

	unblockedProcess -> readMessage = sentMessage;

	ChangeTextColorToOS ();
	printf ( "OS: Process (ID = %d) is RECEIVE-UNBLOCKED\n\n" , unblockedProcess -> processID );
	ChangeTextColorToDefault ();

	AddToReadyQueue ( unblockedProcess );
	return 1;
}

void ReceiveMessage () 
{
	MESSAGE *receivedMessage = FindMessage ( runningProcess -> processID );
	if ( receivedMessage ) 
	{
		runningProcess -> readMessage = receivedMessage;
		ChangeTextColorToSuccess ();
		printf ( 
			"SUCCESS: Received message (SenderID = %d, recipientProcessID = %d) - \"%s\"\n\n",
			receivedMessage -> senderProcessID,
			receivedMessage -> recipientProcessID,
			receivedMessage -> messageStr
		);
		ChangeTextColorToDefault ();

		return;
	}

	if ( runningProcess -> processID == INIT_PROCESS_ID ) 
	{
		ChangeTextColorToSuccess ();
		printf ( "SUCCESS: No messages sent to INIT PROCESS (ID = %d)\n\n" , INIT_PROCESS_ID );
		ChangeTextColorToDefault ();

		return;
	}
	else 
	{
		ChangeTextColorToSuccess ();
		printf ( "SUCCESS: No messages sent to PROCESS (ID = %d)\n\n" , runningProcess -> processID );	
		ChangeTextColorToDefault ();
	}

	if ( runningProcess -> processID != INIT_PROCESS_ID ) 
	{
		ReceiveBlockRunningProcess ();
	}
}

void SendMessage ( int recipientProcessID , const char *messageStr ) 
{
	if ( recipientProcessID == runningProcess -> processID ) 
	{
		ChangeTextColorToError ();
		printf ( "ERROR: Process (ID = %d) cannot send message to self\n\n" , recipientProcessID );
		ChangeTextColorToDefault ();

		return;
	}

	int recipientProcessExists = ProcessExists ( recipientProcessID );
	if ( !recipientProcessExists ) 
	{
		ChangeTextColorToError ();
		printf ("ERROR: Recipient Process (ID = %d) does not exist in system\n\n" , recipientProcessID );
		ChangeTextColorToDefault ();

		return;
	}

	MESSAGE *message = CreateMessage ( runningProcess -> processID , recipientProcessID , messageStr );
	ChangeTextColorToSuccess ();
	printf ( 
		"SUCCESS: Process (ID = %d) Sent Message \"%s\" to Process (ID = %d)\n\n" ,
		message -> senderProcessID ,
		message -> messageStr ,
		message -> recipientProcessID
	);
	ChangeTextColorToDefault ();

	int unblockedAProcess = UnblockReceiveBlockedProcess ( message );
	if ( !unblockedAProcess ) 
	{
		AddToMessagesQueue ( message );
	}

	if ( runningProcess -> processID != INIT_PROCESS_ID ) 
	{
		SendBlockRunningProcess ();
	}
	else if ( runningProcess -> processID == INIT_PROCESS_ID ) 
	{
		RunNextProcess ();
	}
}

void ReplyMessage ( int recipientProcessID , char *messageStr ) 
{
	if ( recipientProcessID == runningProcess -> processID ) 
	{
		ChangeTextColorToError ();
		printf ( "ERROR: Process (ID = %d) cannot send message to self\n\n" , recipientProcessID );
		ChangeTextColorToDefault ();

		return;
	}

	int sendBlockedRecipientProcessExists = ProcessExistsInQueue ( recipientProcessID , sendBlockedQueue );
	if ( !sendBlockedRecipientProcessExists ) 
	{
		ChangeTextColorToError ();
		printf ("ERROR: No SEND-BLOCKED Recipient Process with ID = %d\n\n" , recipientProcessID );
		ChangeTextColorToDefault ();

		return;
	}

	MESSAGE *repliedMessage = CreateMessage ( runningProcess -> processID , recipientProcessID , messageStr );
	
	ChangeTextColorToSuccess ();
	printf ( 
		"SUCCESS: Process (ID = %d) Sent a Reply Message \"%s\" to Process (ID = %d)\n\n" ,
		repliedMessage -> senderProcessID ,
		repliedMessage -> messageStr ,
		repliedMessage -> recipientProcessID
	);
	ChangeTextColorToDefault ();

	int unblockedAProcess = UnblockSendBlockedProcess ( repliedMessage );
	if ( !unblockedAProcess ) 
	{
		AddToMessagesQueue ( repliedMessage );
	}

	if ( runningProcess -> processID == INIT_PROCESS_ID ) 
	{
		RunNextProcess ();
	}
}

int ValidSemID ( int semaphoreID ) 
{
	if ( semaphoreID < 0 || semaphoreID >= NUM_SEMAPHORES ) 
	{
		ChangeTextColorToError ();
		printf ( "ERROR: Invalid Semaphore ID (VALID IDs = 0-%d)\n\n" , NUM_SEMAPHORES - 1 );
		ChangeTextColorToDefault ();

		return 0;
	}

	return 1;
}

int CreatedSem ( int semaphoreID ) 
{
	if ( !ValidSemID ( semaphoreID ) ) 
	{
		return 0;
	}

	SEMAPHORE *semaphore = &semaphores [ semaphoreID ];
	if ( semaphore -> semStatus == NOT_CREATED ) 
	{
		return 0;
	}

	return 1;
}

void SemaphoreV ( int semaphoreID ) 
{
	if ( !CreatedSem ( semaphoreID ) ) 
	{
		ChangeTextColorToError ();
		printf ( "ERROR: Semaphore (ID = %d) has not been created\n\n" , semaphoreID );
		ChangeTextColorToDefault ();

		return;
	}

	SEMAPHORE *semaphore = &semaphores [ semaphoreID ];
	semaphore -> semValue += 1;

	ChangeTextColorToSuccess ();
	printf ( 
		"SUCCESS: V operation on Semaphore (ID = %d) (%d -> %d)\n\n" , 
		semaphoreID ,
		semaphore -> semValue - 1,
		semaphore -> semValue
	);
	ChangeTextColorToDefault ();

	if ( semaphore -> semValue <= 0 && ListCount ( semaphore -> blockedPCBs ) > 0 ) 
	{
		PCB *blockedProcess = ListTrim ( semaphore -> blockedPCBs );
		AddToReadyQueue ( blockedProcess );

		ChangeTextColorToOS ();
		printf ( "OS: PROCESS (ID = %d) SEM-UNBLOCKED\n\n" , blockedProcess -> processID );
		ChangeTextColorToDefault ();
	}

	if ( runningProcess -> processID == INIT_PROCESS_ID ) 
	{
		RunNextProcess ();
	}
}

void SemaphoreP ( int semaphoreID ) 
{
	if ( !CreatedSem ( semaphoreID ) ) 
	{
		ChangeTextColorToError ();
		printf ( "ERROR: Semaphore (ID = %d) has not been created\n\n" , semaphoreID );
		ChangeTextColorToDefault ();

		return;
	}

	SEMAPHORE *semaphore = &semaphores [ semaphoreID ];
	semaphore -> semValue -= 1;

	ChangeTextColorToSuccess ();
	printf ( 
		"SUCCESS: P operation on Semaphore (ID = %d) (%d -> %d)\n\n" , 
		semaphoreID ,
		semaphore -> semValue + 1,
		semaphore -> semValue
	);
	ChangeTextColorToDefault ();

	if ( runningProcess -> processID != INIT_PROCESS_ID && semaphore -> semValue < 0 ) 
	{
		ChangeTextColorToOS ();
		printf ( "OS: PROCESS (ID = %d) has been SEM-BLOCKED\n\n" , runningProcess -> processID );
		ChangeTextColorToDefault ();

		ListPrepend ( semaphore -> blockedPCBs , runningProcess );
		runningProcess -> processState = SEM_BLOCKED;
		
		runningProcess = NULL;
		RunNextProcess ();
	}
}

void NewSemaphore ( int semaphoreID , int initSemValue ) 
{
	if ( !ValidSemID ( semaphoreID ) ) 
	{
		return;
	}

	if ( initSemValue < 0 ) 
	{
		ChangeTextColorToError ();
		printf ( "ERROR: Invalid Semaphore Value (%d < 0)\n\n" , initSemValue );
		ChangeTextColorToDefault ();

		return;
	}

	if ( CreatedSem ( semaphoreID ) ) 
	{
		ChangeTextColorToError ();
		printf ( "ERROR: Semaphore (ID = %d) has already been created\n\n" , semaphoreID );
		ChangeTextColorToDefault ();

		return;
	}

	SEMAPHORE *semaphore = &semaphores [ semaphoreID ];
	semaphore -> semStatus = CREATED;
	semaphore -> semValue = initSemValue;
	semaphore -> blockedPCBs = ListCreate ();

	ChangeTextColorToSuccess ();
	printf ( "SUCCESS: Semaphore (ID = %d) (value = %d) CREATED\n\n" , semaphoreID , initSemValue );
	ChangeTextColorToDefault ();
}

void StrToUpper ( char *str ) 
{
	if ( !str ) 
	{
		return;
	}

	for ( int i = 0 ; i < strlen ( str ) ; i++ ) 
	{
		str [ i ] = toupper ( str [ i ] );
	}
}

void ProcInfo ( int processID ) 
{
	PCB *foundProcess = NULL;

	if ( processID == INIT_PROCESS_ID ) 
	{
		foundProcess = &INIT_PROCESS;
	}

	if ( processID == runningProcess -> processID ) 
	{
		foundProcess = runningProcess;
	}

	if ( !foundProcess ) 
	{
		foundProcess = FindProcessFromAllQueues ( processID );
	}
	
	if ( !foundProcess ) 
	{
		ChangeTextColorToError ();
		printf ( "ERROR: PROCESS (ID = %d) does not exist in system\n\n" , processID );
		ChangeTextColorToDefault ();

		return;
	}

	char* processStateStr;
	switch ( foundProcess -> processState ) 
	{
		case SEND_BLOCKED :
			processStateStr = "SEND-BLOCKED";
			break;

		case RECEIVE_BLOCKED :
			processStateStr = "RECEIVE-BLOCKED";
			break;

		case SEM_BLOCKED :
			processStateStr = "SEMAPHORE-BLOCKED";
			break;

		case READY :
			processStateStr = "READY";
			break;

		case RUNNING :
			processStateStr = "RUNNING";
			break;

		default: 
			processStateStr = "ERROR";
	}

	if ( foundProcess -> processID == INIT_PROCESS_ID )
	{
		ChangeTextColorToSuccess ();
		printf ( 
			"SUCCESS: INIT PROCESS (ID = %d) (STATE = %s) (PRIORITY LEVEL = %d)\n\n" ,
			foundProcess -> processID , 
			processStateStr ,
			foundProcess -> priorityLevel 
		);
		ChangeTextColorToDefault ();
	}
	else 
	{
		ChangeTextColorToSuccess ();
		printf ( 
			"SUCCESS: PROCESS (ID = %d) (STATE = %s) (PRIORITY LEVEL = %d)\n\n" ,
			foundProcess -> processID , 
			processStateStr ,
			foundProcess -> priorityLevel 
		);
		ChangeTextColorToDefault ();
	}
}

void ForkProcess () 
{
	if ( runningProcess -> processID == INIT_PROCESS_ID ) 
	{
		ChangeTextColorToError ();
		printf ( "ERROR: Cannot FORK INIT PROCESS (ID = %d)\n\n" , runningProcess -> processID );
		ChangeTextColorToDefault ();

		return;
	}

	int priorityLevel = runningProcess -> priorityLevel;
	PCB *newProcess = NewProcess ( priorityLevel );
	if ( !newProcess ) 
	{
		return;
	}

	AddToReadyQueue ( newProcess );

	ChangeTextColorToSuccess ();
	printf ( "SUCCESS: FORKED RUNNING " );
	PrintPCB ( runningProcess );
	printf ( " |\n" );
	printf ( " --> " );
	PrintPCB ( newProcess );
	ChangeTextColorToDefault ();
}

int SearchPrintMessage ( void *item , void *messageListNumbering ) 
{
	MESSAGE *message = ( MESSAGE *) item;

	int *numbering = ( int *) messageListNumbering;
	printf ( "\t%d. " , *numbering );

	if ( message ) 
	{
		PrintMessage ( message );
	}
	else 
	{
		ChangeTextColorToError ();
		printf ( "DEV-ERROR: Found NULL MESSAGE\n\n" );	
		ChangeTextColorToDefault ();	
	}

	*numbering += 1;
	return 0;
}

int SearchPrintPCB ( void *item , void *pcbListNumbering ) 
{
	PCB *pcb = ( PCB *) item;

	int *numbering = ( int *) pcbListNumbering;
	printf ( "\t%d. " , *numbering );

	if ( pcb ) 
	{
		PrintPCB ( pcb );
	}
	else 
	{
		ChangeTextColorToError ();
		printf ( "DEV-ERROR: Found NULL PCB\n\n" );	
		ChangeTextColorToDefault ();	
	}

	*numbering += 1;
	return 0;
}

void PrintSemaphore ( const SEMAPHORE *semaphore ) 
{
	if ( !semaphore ) 
	{
		return;
	}

	printf ( "\tSemaphore (ID = %d), Status: " , semaphore -> semID );
	if ( semaphore -> semStatus == NOT_CREATED ) 
	{
		printf ( "NOT CREATED\n" );
		return;
	}

	printf ( 
		"CREATED - Value = %d\n" , 
		semaphore -> semValue
	);

	if ( ListCount ( semaphore -> blockedPCBs ) == 0 ) 
	{
		printf ( "\tNo processes SEM-BLOCKED\n\n" );
	}
	else
	{
		printf ( 
			"\tProcesses SEM-BLOCKED: %d\n" , 
			ListCount ( semaphore -> blockedPCBs ) 
		);

		int pcbListNumbering = 1;
		ListFirst ( semaphore -> blockedPCBs );
		ListSearch ( semaphore -> blockedPCBs , &SearchPrintPCB , &pcbListNumbering );
	}
}

void DisplayTotalSystemInfo () 
{
	printf ( "\n-------------- TOTAL SYSTEM INFO --------------\n" );

	ChangeTextColorToOS ();
	printf ( "OS: Number of Processes in System = %d\n\n" , NumSystemProcessesTotal () );
	printf ( "OS: Currently Running Process\n\t" );
	ChangeTextColorToDefault ();

	PrintPCB ( runningProcess );

	for ( int i = 0 ; i < NUM_PRIORITY_LEVELS ; i++ ) 
	{
		ChangeTextColorToOS ();
		printf ( "OS: Ready Queue (Priority Level = %d) - Process List\n" , i );
		ChangeTextColorToDefault ();

		LIST *readyQueue = readyPriorityQueues [ i ];

		if ( ListCount ( readyQueue ) == 0 ) 
		{
			printf ( "\tEMPTY - No Processes In Queue\n\n" );
		}
		else 
		{
			printf ( 
				"\tCOUNT - %d (LAST TO RUN -> 1 ... %d -> NEXT TO RUN\n\n" ,
				ListCount ( readyQueue ) ,
				ListCount ( readyQueue )
			);

			int pcbListNumbering = 1;
			ListFirst ( readyQueue );
			ListSearch ( readyQueue , &SearchPrintPCB , &pcbListNumbering );			
		}
	}

	ChangeTextColorToOS ();
	printf ( "OS: SEND-BLOCKED Queue - Process List\n" );
	ChangeTextColorToDefault ();

	if ( ListCount ( sendBlockedQueue ) == 0 ) 
	{
		printf ( "\tEMPTY - No Processes SEND-BLOCKED\n\n" );
	}
	else 
	{
		printf ( 
			"\tCOUNT - %d\n\n" , 
			ListCount ( sendBlockedQueue ) 
		);

		int pcbListNumbering = 1;
		ListFirst ( sendBlockedQueue );
		ListSearch ( sendBlockedQueue , &SearchPrintPCB , &pcbListNumbering );
	}

	ChangeTextColorToOS ();
	printf ( "OS: RECEIVE-BLOCKED Queue - Process List\n" );
	ChangeTextColorToDefault ();

	if ( ListCount ( receiveBlockedQueue ) == 0 ) 
	{
		printf ( "\tEMPTY - No Processes RECEIVE-BLOCKED\n\n" );
	}
	else 
	{
		printf ( 
			"\tCOUNT - %d\n\n" , 
			ListCount ( receiveBlockedQueue ) 
		);

		int pcbListNumbering = 1;
		ListFirst ( receiveBlockedQueue );
		ListSearch ( receiveBlockedQueue , &SearchPrintPCB , &pcbListNumbering );
	}

	ChangeTextColorToOS ();
	printf ( "OS: MESSAGES Queue - Messages List\n" );
	ChangeTextColorToDefault ();

	if ( ListCount ( messagesQueue ) == 0 ) 
	{
		printf ( "\tEMPTY - No Messages Waiting\n\n" );
	}
	else
	{
		printf ( 
			"\tCOUNT - %d\n\n" , 
			ListCount ( messagesQueue ) 
		);

		int messagesListNumbering = 1;
		ListFirst ( messagesQueue );
		ListSearch ( messagesQueue , &SearchPrintMessage , &messagesListNumbering );
	}

	ChangeTextColorToOS ();
	printf ( "OS: SEMAPHORES List\n" );
	ChangeTextColorToDefault ();

	for ( int i = 0 ; i < NUM_SEMAPHORES ; i++ ) 
	{
		SEMAPHORE *semaphore = &semaphores [ i ];
		PrintSemaphore ( semaphore );
	}

	printf ( "------------- END Of SYSTEM INFO -------------\n\n" );
}

int main () 
{
	InitAllLists ();

	RunNextProcess ();

	PrintInputPrompt ();

	char inputBuffer [ MAX_INPUT_LENGTH ];
	int inputLength = 0;
	while ( runningProcess && ( inputLength = read ( STDIN_FILENO , inputBuffer , MAX_INPUT_LENGTH ) ) > 0 )
	{
		inputBuffer [ inputLength - 1 ] = '\0'; // replace new line char from end of user input with null-terminate char
		if ( inputLength == 1 ) {
			PrintInputPrompt ();
			continue;
		}

		char *remainderStr = inputBuffer;

		char *command = strtok_r ( remainderStr , COMMAND_DELIMITER , &remainderStr );
		StrToUpper ( command );
		char *param1 = strtok_r ( remainderStr , COMMAND_DELIMITER , &remainderStr );
		char *param2 = remainderStr;

		if ( EqualStr ( command , CREATE_COMMAND ) )
		{
			int priorityLevel = ParamToInt ( param1 );
			CreateProcess ( priorityLevel );
		}
		else if ( EqualStr ( command , FORK_COMMAND ) )
		{
			ForkProcess ();
		}
		else if ( EqualStr ( command , KILL_COMMAND ) )
		{
			int processID = ParamToInt ( param1 );
			EndProcess ( processID , "KILLING" );
		}
		else if ( EqualStr ( command , EXIT_COMMAND ) )
		{
			int processID = runningProcess -> processID;
			EndProcess ( processID , "EXITING" );
		}	
		else if ( EqualStr ( command , QUANTUM_COMMAND ) )
		{
			QuantumExpired ();
		}
		else if ( EqualStr ( command , SEND_COMMAND ) )
		{
			int recipientProcessID = ParamToInt ( param1 );
			char *messageStr = param2;
			SendMessage ( recipientProcessID , messageStr );
		}			
		else if ( EqualStr ( command , RECEIVE_COMMAND ) )
		{
			ReceiveMessage ();
		}
		else if ( EqualStr ( command , REPLY_COMMAND ) )
		{
			int recipientProcessID = ParamToInt ( param1 );
			char *messageStr = param2;
			ReplyMessage ( recipientProcessID , messageStr );
		}
		else if ( EqualStr ( command , NEW_SEMAPHORE_COMMAND ) )
		{
			int semaphoreID = ParamToInt ( param1 );
			int initSemValue = ParamToInt ( param2 );
			NewSemaphore ( semaphoreID , initSemValue );
		}	
		else if ( EqualStr ( command , SEMAPHORE_P_COMMAND ) )
		{
			int semaphoreID = ParamToInt ( param1 );
			SemaphoreP ( semaphoreID ); 
		}
		else if ( EqualStr ( command , SEMAPHORE_V_COMMAND ) )
		{
			int semaphoreID = ParamToInt ( param1 );
			SemaphoreV ( semaphoreID ); 
		}
		else if ( EqualStr ( command , PROCESS_INFO_COMMAND ) )
		{
			int processID = ParamToInt ( param1 );
			ProcInfo ( processID );
		}			
		else if ( EqualStr ( command , TOTAL_INFO_COMMAND ) )
		{
			DisplayTotalSystemInfo ();
		}
		else 
		{
			ChangeTextColorToError ();
			printf ( "ERROR: \"%s\" is not a recognized command\n\n" , command );
			ChangeTextColorToDefault ();
		}

		if ( runningProcess ) 
		{
			PrintInputPrompt ();
		}
	}

	ChangeTextColorToOS ();
	printf ( "OS: System Shutting Down ... Goodbye\n\n");
	ChangeTextColorToDefault ();

	FreeAllLists ();
	exit ( 0 );
}
