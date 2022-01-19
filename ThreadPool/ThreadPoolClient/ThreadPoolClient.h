#pragma once
#include<Windows.h>
#include<stdio.h>
#include"ThreadPoolLib.h"

#define AMOUNTOFWORK 1000 //Amount of client work
#define NUMOFWORKITEMS 1000 //number of workitems to submit to threadpool

//Function declarations
PVOID MyWork(PVOID); //Work Function
DWORD WINAPI SubmitHighWorkProc(LPVOID);
DWORD WINAPI SubmitNormalWorkProc(LPVOID);
DWORD WINAPI SubmitLowWorkProc(LPVOID);

//Typedefs for importing various functions from ThreadPoolLib.dll
typedef PTP(*MYPROC)();
typedef PWORKITEM(*MYPROC1)(PTP, CALLBACK_INSTANCE, PVOID, DWORD);
typedef PTP(*MYPROC2)(PTP, PWORKITEM);
typedef BOOL(*MYPROC3)(PTP, PTPSTATS);
typedef BOOL(*MYPROC4)(PTP);

//Declaration of the ThreadPoolLib function pointers
MYPROC _CreateTP;
MYPROC1 _CreateWorkItem;
MYPROC2 _CanInsertWork;
MYPROC2 _InsertWork;
MYPROC2 _TryInsertWork;
MYPROC2 _IsWorkComplete;
MYPROC2 _DeleteWorkItem;
MYPROC3 _GetTPStats;
MYPROC4 _DeleteTP;
