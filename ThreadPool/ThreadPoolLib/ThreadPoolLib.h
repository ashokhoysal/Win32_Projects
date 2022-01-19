#pragma once
#include<stdio.h>
#include"C:\Users\ashokh\source\repos\Dll_LinkedList\Dll_LinkedList\Dll_LinkedList.h"

#define WORKITEM_LOW 0 //Low Pri Work Item
#define WORKITEM_NORMAL 1 //Normal Pri Work Item
#define WORKITEM_HIGH 2 //High Pri Work Item
#define WORK_NOTCOMPLETE 0 //Work Item Not Complete Status
#define WORK_COMPLETE 1 //Work Item Complete Status

typedef LINK TPQ;
typedef PLINK PTPQ;
typedef PVOID(*CALLBACK_INSTANCE)(PVOID); //Client function callback prototype

//WorkItem structure typedefs
typedef struct _WORKITEM WORKITEM;
typedef struct _WORKITEM* PWORKITEM;

//Thread Pool Structure typedefs
typedef struct _TP TP;
typedef struct _TP* PTP;

//Thread Pool Statistics structure
struct _TPSTATS {
	int iCurrentRunningThreads; //Num Of Threads Running in the Thread Pool
	int iCurrentWaitingThreads; //Num of Threads Waiting in the Thread Pool
	int iNumWorkItemsAdded_low; //Num of Low Pri Work Items Added
	int iNumWorkItemsPending_low; //Num of Low Pri Work Items Pending
	int iNumWorkItemsHandled_low; //Num of Low Pri Work Items Handled
	int iNumWorkItemsAdded_normal; //Num of Normal Pri Work Items Added
	int iNumWorkItemsPending_normal; //Num of Normal Pri Work Items Pending
	int iNumWorkItemsHandled_normal; //Num of Normal Pri Work Items Handled
	int iNumWorkItemsAdded_high; //Num of High Pri Work Items Added
	int iNumWorkItemsPending_high; //Num of High Pri Work Items Pending
	int iNumWorkItemsHandled_high; //Num of High Pri Work Items Handled
};
typedef struct _TPSTATS TPSTATS;
typedef struct _TPSTATS* PTPSTATS;

//Thread Pool public function declarations
PTP CreateTP();
PWORKITEM CreateWorkItem(PTP, CALLBACK_INSTANCE, PVOID, DWORD);
BOOL CanInsertWork(PTP, PWORKITEM);
BOOL InsertWork(PTP, PWORKITEM);
BOOL TryInsertWork(PTP, PWORKITEM);
BOOL IsWorkComplete(PTP, PWORKITEM);
BOOL DeleteWorkItem(PTP, PWORKITEM);
BOOL GetTPStats(PTP, PTPSTATS);
BOOL DeleteTP(PTP);

