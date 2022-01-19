#pragma once
#include<Windows.h>
#include<stdio.h>
#include"ThreadPoolLib.h"
#include"c:\Users\ashokh\source\repos\Dll_LinkedList\Dll_LinkedList\Dll_LinkedList.h"

#define MAXTHREADS 100 //Max number of threads (in addition to the the Ideal number of threads) that can be created
#define WORKERTHREADCREATIONDELAY 100 //Number of milliseconds to wait before creating new worker threads (to prevent thread explosion)
#define WORKERTHREADIDLETIMEOUT -60000000LL //Number of 100nanoseconds to wait before terminating an idle worker thread
#define AMOUNTOFWORK 100 //number of milliseconds of client work
#define MAXPENDINGWORKITEMS 500 //Max number of pending work items in queue, post which client is asked to stop sending more work items
#define WORK_NOTCOMPLETE 0 //Work Item Not Complete Status
#define WORK_COMPLETE 1 //Work Item Complete Status

//Typedefs for importing functions from Dll_LinkedList.dll
typedef PLINK(*MYPROC)();
typedef BOOL(*MYPROC1)(PLINK, PLINK);
typedef PLINK(*MYPROC2)(PLINK);
typedef BOOL(*MYPROC3)(PLINK);

//WorkItem Structure
struct _WORKITEM {
	CALLBACK_INSTANCE pCallback; //Client supplied callback function
	PVOID pvParam; //Client supplied pointer to parameters to the callback function
	DWORD iPri; //Client supplied Priority of the Work Item
	DWORD iCompletionStatus; //Internal Completion Status of the Work Item
	LINK list_entry; //Internal Linked List entry member
};

//Thread Pool Structure
struct _TP {
	PTPQ pTPQ_low; //Low Pri queue  (Circular Linked List)
	PTPQ pTPQ_normal; //Normal Pri queue (Circular Linked List)
	PTPQ pTPQ_high; //High Pri queue (Circular Linked List)
	volatile int iIdealThreads; //Ideal Worker threads is NumofProcs
	volatile int iMaxThreads; //Max Worker threads is obtained from the MAXTHREADS macro(can be modified)
	volatile int iCRWThreads; //Current Running Worker Threads is 0
	volatile int iCWWThreads; //Current Waiting Worker Threads is Ideal Threads
	volatile int iNumWorkItemsAdded_low; //Number of Work Items Added to the Low Priority queue
	volatile int iNumWorkItemsPending_low; //Number of Work Items Pending in the Low Priority queue
	volatile int iNumWorkItemsHandled_low; //Number of Work Items Handled in the Low Priority queue
	volatile int iNumWorkItemsAdded_normal; //Number of Work Items Added to the Normal Priority queue
	volatile int iNumWorkItemsPending_normal; //Number of Work Items Pending in the Normal Priority queue
	volatile int iNumWorkItemsHandled_normal; //Number of Work Items Handled in the Normal Priority queue
	volatile int iNumWorkItemsAdded_high; //Number of Work Items Added to the High Priority queue
	volatile int iNumWorkItemsPending_high; //Number of Work Items Pending in the High Priority queue
	volatile int iNumWorkItemsHandled_high; //Number of Work Items Handled in the High Priority queue
};

//SRWlocks to sync access to the 3 Pri queues
SRWLOCK gSRWLock_TPQlow; 
SRWLOCK gSRWLock_TPQnormal;
SRWLOCK gSRWLock_TPQhigh;

HANDLE g_hControlThreadEvent; //ControlThread Notification Event
HANDLE g_hWIAvailableEvent; //Worker Thread notification Event 
HANDLE g_hKillWorkerThreadTimer; //Handle to Worker Thread idle timeout timer 
HANDLE g_hDeleteTPEvent; //Delete Thread Pool Event
LARGE_INTEGER liKillWorkerThreadTime; //Worker Thread idle timeout timer

DWORD WINAPI WorkerThreadProc(LPVOID pvParam); //WorkerThread procedure declaration
DWORD WINAPI ControlThreadProc(LPVOID pvParam); //ControlThread procedure declaration