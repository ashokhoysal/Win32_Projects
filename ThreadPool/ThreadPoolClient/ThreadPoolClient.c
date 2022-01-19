/*
Author:ashokh@microsoft.com
Last Modified Date: 24th Oct,2019
ThreadPoolCLient.C - Sample Thread Pool Client app to demo the vaious Thread Pool Lib functions
Compiled using "cl ThreadPoolCLient.c /Zi"
*/

#pragma once
#include"ThreadPoolClient.h"

int main()
{
	//Loading ThreadPoolLib.dll explicitly and getting the relevant function pointers
	HMODULE hThreadPoolLib = LoadLibraryExW(L"ThreadPoolLib.dll", NULL, 0);
	if (hThreadPoolLib == NULL)
	{
		printf("Unable to load ThreadPoolLib.dll:%d", GetLastError());
		return FALSE;
	}
	_CreateTP = (MYPROC)GetProcAddress(hThreadPoolLib, "CreateTP");
	_CreateWorkItem = (MYPROC1)GetProcAddress(hThreadPoolLib, "CreateWorkItem");
	_CanInsertWork = (MYPROC2)GetProcAddress(hThreadPoolLib, "CanInsertWork");
	_InsertWork = (MYPROC2)GetProcAddress(hThreadPoolLib, "InsertWork");
	_TryInsertWork = (MYPROC2)GetProcAddress(hThreadPoolLib, "TryInsertWork");
	_IsWorkComplete = (MYPROC2)GetProcAddress(hThreadPoolLib, "IsWorkComplete");
	_DeleteWorkItem = (MYPROC2)GetProcAddress(hThreadPoolLib, "DeleteWorkItem");
	_GetTPStats = (MYPROC3)GetProcAddress(hThreadPoolLib, "GetTPStats");
	_DeleteTP = (MYPROC4)GetProcAddress(hThreadPoolLib, "DeleteTP");

	if (!(_CreateTP && _CreateWorkItem && _CanInsertWork && _InsertWork && _TryInsertWork && _IsWorkComplete && _DeleteWorkItem && _GetTPStats && _DeleteTP))
	{
		printf("Unable to GetProcAddress:%d", GetLastError());
		FreeLibrary(hThreadPoolLib);
		return FALSE;
	}

	printf("Creating TP\n");
	PTP pMyTPQ = _CreateTP();
	if (pMyTPQ == NULL)
	{
		printf("TP Creation failed\n");
		return 1;
	}
	printf("Successfully Created TP\n");
	HANDLE hThread[3];
	hThread[0] = CreateThread(NULL, 0, SubmitHighWorkProc, (LPVOID)pMyTPQ, 0, 0);
	hThread[1] = CreateThread(NULL, 0, SubmitNormalWorkProc, (LPVOID)pMyTPQ, 0, 0);
	hThread[2] = CreateThread(NULL, 0, SubmitLowWorkProc, (LPVOID)pMyTPQ, 0, 0);

	WaitForMultipleObjects(3, hThread, TRUE, INFINITE);
	printf("\nHit Enter to get TP Stats\n");
	getchar();
	PTPSTATS pMyTPStats = (PTPSTATS)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TPSTATS));
	if (_GetTPStats(pMyTPQ, pMyTPStats))
	{
		printf("\n************Thread Pool Stats*************\n");
		printf("Current No. of Running Threads:%d\n", pMyTPStats->iCurrentRunningThreads);
		printf("Current No. of Waiting Threads:%d\n", pMyTPStats->iCurrentWaitingThreads);
		printf("No. of HighPri Work Items Added:%d\n", pMyTPStats->iNumWorkItemsAdded_high);
		printf("No. of NormalPri Work Items Added:%d\n", pMyTPStats->iNumWorkItemsAdded_normal);
		printf("No. of LowPri Work Items Added:%d\n", pMyTPStats->iNumWorkItemsAdded_low);
		printf("No. of HighPri Work Items Handled:%d\n", pMyTPStats->iNumWorkItemsHandled_high);
		printf("No. of NormalPri Work Items Handled:%d\n", pMyTPStats->iNumWorkItemsHandled_normal);
		printf("No. of LowPri Work Items Handled:%d\n", pMyTPStats->iNumWorkItemsHandled_low);
		printf("No. of HighPri Work Items Pending:%d\n", pMyTPStats->iNumWorkItemsPending_high);
		printf("No. of NormalPri Work Items Pending:%d\n", pMyTPStats->iNumWorkItemsPending_normal);
		printf("No. of LowPri Work Items Pending:%d\n", pMyTPStats->iNumWorkItemsPending_low);
	}

	if (!_DeleteTP(pMyTPQ))
	{
		printf("Unable to delete TP\n");
		return 1;
	}
	printf("\nSuccessfully deleted TP\n");
	getchar();
	FreeLibrary(hThreadPoolLib);
	return 0;
}

PVOID MyWork(PVOID pvParam)
{
	printf("Doing work from worker thread %d\n", GetThreadId(GetCurrentThread()));
	int count = AMOUNTOFWORK;
	while (count > 0)
	{
		count--;
	}
	printf("Done work from worker thread %d\n", GetThreadId(GetCurrentThread()));
	return 0;
}

DWORD WINAPI SubmitHighWorkProc(LPVOID pTP)
{
	PWORKITEM pWork[NUMOFWORKITEMS];
	for (int i = 0; i < NUMOFWORKITEMS; i++)
	{
		pWork[i] = _CreateWorkItem(pTP, MyWork, NULL, WORKITEM_HIGH);
	TA_H:if (_TryInsertWork(pTP, pWork[i]))
	{
		printf("Inserted high pri Work Item\n");
	}
		 else
	{
		printf("WARNING:Cant insert Work %d to high pri queue, waiting for some time\n", i);
		Sleep(1000);
		goto TA_H;
	}
	}
	Sleep(5000);

	for (int i = 0; i < NUMOFWORKITEMS; i++)
	{
	ISWORKDONE:if (_IsWorkComplete(pTP, pWork[i]))
	{
		printf("High Pri Work %d is complete\n", i);
	}
			   else
	{
		printf("WARNING:High Pri Work %d is not yet complete,checking again\n", i);
		Sleep(1000);
		goto ISWORKDONE;
	}
	}
	for (int i = 0; i < NUMOFWORKITEMS; i++)
	{
		if (_DeleteWorkItem(pTP, pWork[i]))
		{
			printf("High pri Work %d deleted\n", i);
		}
		else
		{
			printf("High pri Work %d not deleted\n", i);
		}
	}
	return 0;
}

DWORD WINAPI SubmitLowWorkProc(LPVOID pTP)
{
	PWORKITEM pWork[NUMOFWORKITEMS];
	for (int i = 0; i < NUMOFWORKITEMS; i++)
	{
		pWork[i] = _CreateWorkItem(pTP, MyWork, NULL, WORKITEM_LOW);
	TA_H:if (_CanInsertWork(pTP, pWork[i]))
	{
		printf("Inserting work %d to low pri queue\n", i);
		_InsertWork(pTP, pWork[i]);
	}
		 else
	{
		printf("WARNING:Cant insert Work %d to low pri queue, waiting for some time\n", i);
		Sleep(1000);
		goto TA_H;
	}
	}
	Sleep(5000);

	for (int i = 0; i < NUMOFWORKITEMS; i++)
	{
	ISWORKDONE:if (_IsWorkComplete(pTP, pWork[i]))
	{
		printf("Low Pri Work %d is complete\n", i);
	}
			   else
	{
		printf("WARNING:Low Pri Work %d is not yet complete,checking again\n", i);
		Sleep(1000);
		goto ISWORKDONE;
	}
	}
	for (int i = 0; i < NUMOFWORKITEMS; i++)
	{
		if (_DeleteWorkItem(pTP, pWork[i]))
		{
			printf("Low pri Work %d deleted\n", i);
		}
		else
		{
			printf("Low pri Work %d not deleted\n", i);
		}
	}
	return 0;
}

DWORD WINAPI SubmitNormalWorkProc(LPVOID pTP)
{
	PWORKITEM pWork[NUMOFWORKITEMS];
	for (int i = 0; i < NUMOFWORKITEMS; i++)
	{
		pWork[i] = _CreateWorkItem(pTP, MyWork, NULL, WORKITEM_NORMAL);
	TA_H:if (_CanInsertWork(pTP, pWork[i]))
	{
		//printf("Yes Can insert high pri Work Item now\n");
		printf("Inserting work %d to normal pri queue\n", i);
		_InsertWork(pTP, pWork[i]);
	}
		 else
	{
		printf("WARNING:Cant insert Work %d to normal pri queue, waiting for some time\n", i);
		Sleep(1000);
		goto TA_H;
	}
	}
	Sleep(5000);

	for (int i = 0; i < NUMOFWORKITEMS; i++)
	{
	ISWORKDONE:if (_IsWorkComplete(pTP, pWork[i]))
	{
		printf("Normal Pri Work %d is complete\n", i);
	}
			   else
	{
		printf("WARNING:Normal Pri Work %d is not yet complete,checking again\n", i);
		Sleep(1000);
		goto ISWORKDONE;
	}
	}
	for (int i = 0; i < NUMOFWORKITEMS; i++)
	{
		if (_DeleteWorkItem(pTP, pWork[i]))
		{
			printf("Normal pri Work %d deleted\n", i);
		}
		else
		{
			printf("Normal pri Work %d not deleted\n", i);
		}
	}
	return 0;
}