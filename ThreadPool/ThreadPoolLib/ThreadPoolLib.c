/*
Author:ashokh@microsoft.com
Last Modified Date: 24th Oct,2019
ThreadPoolLib.C - Contains the definitions of the various Thread Pool APIs
Compiled using "cl ThreadPoolLib.c ThreadPoolLib.def /LD /Zi"
*/

#include"ThreadPoolLib_Private.h"
#include"ThreadPoolLib.h"
#include"ThreadPoolLib_Debug.h"

/*
This API creates the main Thread Pool structure and initializes its members
The API does not accept any arguements and returns pointer to TP upon success, else return NULL
*/
PTP CreateTP()
{
	//Get Default Process Heap Handle
	HANDLE hDefaultHeap = GetProcessHeap();
	if (hDefaultHeap == NULL) //if it fails return NULL
	{
		LOG_ERROR("Unable to get handle to Default Process Heap:%d", GetLastError());
		return NULL;
	}

	//Get the System Info details (we need the Number of Processors member)
	LPSYSTEM_INFO pSystemInfo = (LPSYSTEM_INFO)HeapAlloc(hDefaultHeap, HEAP_ZERO_MEMORY, sizeof(SYSTEM_INFO));
	if (pSystemInfo == NULL) //if it fails return NULL
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		LOG_ERROR("Unable to get SystemInfo:%d", GetLastError());
		return NULL;
	}
	GetSystemInfo(pSystemInfo);

	//Allocate memory for the TP structure from default process heap
	PTP pTP = (PTP)HeapAlloc(hDefaultHeap, HEAP_ZERO_MEMORY, sizeof(TP));
	if (pTP == NULL)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		LOG_ERROR("Unable to create TP structure:%d", GetLastError());
		return NULL;
	}

	//Loading DLL_Linkedlist.dll explicitly and getting the relevant functions
	HMODULE hDll_LinkedList = LoadLibraryExW(L"DLL_LinkedList.dll", NULL, 0);
	if (hDll_LinkedList == NULL)
	{
		LOG_ERROR("Unable to load DLL_LinkedList.dll:%d", GetLastError());
		return NULL;
	}
	MYPROC InitializeQueue = (MYPROC)GetProcAddress(hDll_LinkedList, "InitializeListHead");
	if (!InitializeQueue)
	{
		LOG_ERROR("Unable to GetProcAddress:%d", GetLastError());
		return NULL;
	}

	//Initialize the 3 Pri queues
	pTP->pTPQ_low = InitializeQueue();
	pTP->pTPQ_normal = InitializeQueue();
	pTP->pTPQ_high = InitializeQueue();
	if (!(pTP->pTPQ_low && pTP->pTPQ_normal && pTP->pTPQ_high))
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		LOG_ERROR("Unable to Initialize Pri Queue:%d", GetLastError());
		return NULL;
	}

	FreeLibrary(hDll_LinkedList);

	//Initialise the SRWLocks to synchronize access to each of the 3 Pri queues
	InitializeSRWLock(&gSRWLock_TPQlow);
	InitializeSRWLock(&gSRWLock_TPQnormal);
	InitializeSRWLock(&gSRWLock_TPQhigh);

	//Set initial TP parameters
	pTP->iIdealThreads = pSystemInfo->dwNumberOfProcessors; //Ideal Worker threads is NumofProcs
	pTP->iMaxThreads = MAXTHREADS; //Max Worker threads is obtained from the MAXTHREADS macro(can be modified)
	pTP->iCRWThreads = 0; //Current Running Worker Threads is 0
	pTP->iCWWThreads = pSystemInfo->dwNumberOfProcessors; //Current Waiting Worker Threads is Ideal Threads
	pTP->iNumWorkItemsAdded_low = 0; //Number of Work Items Added to the Low Priority queue
	pTP->iNumWorkItemsPending_low = 0;//Number of Work Items Pending in the Low Priority queue
	pTP->iNumWorkItemsHandled_low = 0;//Number of Work Items Handled in the Low Priority queue
	pTP->iNumWorkItemsAdded_normal = 0;//Number of Work Items Added to the Normal Priority queue
	pTP->iNumWorkItemsPending_normal = 0;//Number of Work Items Pending in the Normal Priority queue
	pTP->iNumWorkItemsHandled_normal = 0;//Number of Work Items Handled in the Normal Priority queue
	pTP->iNumWorkItemsAdded_high = 0;//Number of Work Items Added to the High Priority queue
	pTP->iNumWorkItemsPending_high = 0;//Number of Work Items Pending in the High Priority queue
	pTP->iNumWorkItemsHandled_high = 0;//Number of Work Items Handled in the High Priority queue

	//Free the SystemInfo structure as we are done with it
	if (HeapFree(hDefaultHeap, 0, pSystemInfo) == 0)
	{
		LOG_ERROR("Unable to free SystemInfo:%d", GetLastError());
	}

	/*Create Event to notify Control Thread when Current Waiting Worker Threads are 0
	This is a Auto Reset Event and initial state is not signalled
	Control Thread creates new Worker Threads upto macro MAXTHREADS*/
	g_hControlThreadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hControlThreadEvent == NULL) //if it fails return NULL
	{
		LOG_ERROR("Unable to Create Control Thread Event:%d", GetLastError());
		return NULL;
	}

	/*Create Event to notify the WorkerThreads about availability of Work Item
	The Event is a Auto Reset event, and the initial state is not signaled
	So the Worker Threads wait on this event to be signalled
	The event gets signalled when InsertWork notifies that there is a Work Item in the queue
	This wakes one of the worker threads which performs work
	*/
	g_hWIAvailableEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	if (g_hWIAvailableEvent == NULL) //if it fails return NULL
	{
		LOG_ERROR("Unable to Create Worker Thread Event:%d", GetLastError());
		return NULL;
	}

	//Create Waitable Timer for timeout defined by macro WORKERTHREADIDLETIMEOUT(modifiable), and kill the Worker Thread if timeout is reached
	liKillWorkerThreadTime.QuadPart = WORKERTHREADIDLETIMEOUT;
	g_hKillWorkerThreadTimer = CreateWaitableTimer(NULL, TRUE, NULL);
	if (g_hKillWorkerThreadTimer == NULL) //if it fails return NULL
	{
		LOG_ERROR("Unable to Create KillWorkerThreadTimer:%d", GetLastError());
		return NULL;
	}

	//Create Delete Thread Pool event
	g_hDeleteTPEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	//Create Control Thread which monitors number of available Worker Threads and creates new Worker Threads as required
	if (CreateThread(NULL, 0, ControlThreadProc, (LPVOID)pTP, 0, 0) == NULL)
	{
		LOG_ERROR("Unable to Create Control Thread:%d", GetLastError());
		return NULL;
	}

	//Create Worker Threads upto iIdealThreads, Worker Threads call WorkerThreadProc and wait on g_hWIAvailableEvent event
	for (int i = 1; i <= pTP->iIdealThreads; i++)
	{
		if (CreateThread(NULL, 0, WorkerThreadProc, (LPVOID)pTP, 0, 0) == NULL)
		{
			LOG_ERROR("Unable to Create Worker Thread:%d", GetLastError());
			return NULL;
		}
	}

	return pTP;
}

/*
This API is the Worker Thread Function. Accepts pointer to Thread Pool as arguement and returns 0 or 1 upon termination
The Worker Thread waits for Work Item to be available
Once available, it executes work based on priority
If no work is available for time governed by macro WORKERTHREADIDLETIMEOUT and there are ideal number of threads available, worker thread dies
*/
DWORD WINAPI WorkerThreadProc(LPVOID pTP)
{
	//Parameter Validation
	if (pTP == NULL)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		LOG_ERROR("Invalid Thread Pool:%d", GetLastError());
		InterlockedDecrement(&(((PTP)pTP)->iCWWThreads));
		return 1;
	}

	//Loading DLL_Linkedlist.dll explicitly and getting the relevant functions
	HMODULE hDll_LinkedList = LoadLibraryExW(L"DLL_LinkedList.dll", NULL, 0);
	if (hDll_LinkedList == NULL)
	{
		LOG_ERROR("Unable to load DLL_LinkedList.dll:%d", GetLastError());
		InterlockedDecrement(&(((PTP)pTP)->iCWWThreads));
		return 1;
	}
	MYPROC2 Dequeue = (MYPROC2)GetProcAddress(hDll_LinkedList, "RemoveTailList");
	if (!Dequeue)
	{
		LOG_ERROR("Unable to GetProcAddress:%d", GetLastError());
		goto WORKERTHREADCLEANUP;
	}

	DWORD iWorkerThreadId = GetThreadId(GetCurrentThread());
	if (iWorkerThreadId == 0)
		LOG_ERROR("Invalid WorkerThreadId:%d", GetLastError());
	LOG_INFO("Starting Worker Thread %d\n", iWorkerThreadId);

	HANDLE hWorkerThreadEvents[3] = { g_hDeleteTPEvent,g_hKillWorkerThreadTimer,g_hWIAvailableEvent };
	while (TRUE)
	{
		//If work items are available in queue, explicitly set g_hWIAvailableEvent event as there is a possibility of the event getting reset and work items are still available
		if ((((PTP)pTP)->iNumWorkItemsPending_high || ((PTP)pTP)->iNumWorkItemsPending_normal || ((PTP)pTP)->iNumWorkItemsPending_low))
		{
			LOG_INFO("Work Items are available\n");
			if (!(SetEvent(g_hWIAvailableEvent)))
			{
				LOG_ERROR("Unable to Set g_hWIAvailableEvent:%d", GetLastError());
				goto WORKERTHREADCLEANUP;
			}
		}
		LOG_INFO("Worker Thread %d waiting for Work Item\n", iWorkerThreadId);
		if (!(SetWaitableTimer(g_hKillWorkerThreadTimer, &liKillWorkerThreadTime, 0, NULL, NULL, 0)))//Start WORKERTHREADIDLETIMEOUT Timer
		{
			LOG_ERROR("Unable to Set Timer:%d", GetLastError());
			goto WORKERTHREADCLEANUP;
		}
		DWORD dw = WaitForMultipleObjects(3, hWorkerThreadEvents, FALSE, INFINITE); //Wait on Delete TP event or WORKERTHREADIDLETIMEOUT timer or work item to be available
		switch (dw)
		{
		case WAIT_FAILED: //Wait failed
			LOG_ERROR("Worker Thread Wait failed:%d", GetLastError());
			goto WORKERTHREADCLEANUP;

		case WAIT_OBJECT_0 + 0: //Delete TP
			LOG_INFO("Worker Thread %d terminating due to Thread Pool deletion\n", iWorkerThreadId);
			FreeLibrary(hDll_LinkedList);
			InterlockedDecrement(&(((PTP)pTP)->iCWWThreads));
			return 0;

		case WAIT_OBJECT_0 + 1: //WORKERTHREADIDLETIMEOUT timer fired
			LOG_INFO("Worker Thread %d idle timeout\n", iWorkerThreadId);
			//if worker threads is more than ideal threads terminate
			if ((((PTP)pTP)->iCWWThreads + ((PTP)pTP)->iCRWThreads) > ((PTP)pTP)->iIdealThreads)
			{
				LOG_INFO("Worker Thread %d terminating due to idle timeout\n", iWorkerThreadId);
				FreeLibrary(hDll_LinkedList);
				InterlockedDecrement(&(((PTP)pTP)->iCWWThreads));
				return 0;
			}
			//else remain alive
			else
			{
				LOG_INFO("Keeping min number of worker threads\n");
				break;
			}

		case WAIT_OBJECT_0 + 2: //Work Item Available
		{
			LOG_INFO("Worker Thread %d woken due to WI available\n", iWorkerThreadId);
			if (((PTP)pTP)->iNumWorkItemsPending_high > 0) //First handle the High Pri work item
			{
				InterlockedDecrement(&(((PTP)pTP)->iCWWThreads));
				InterlockedIncrement(&(((PTP)pTP)->iCRWThreads));
				while (((PTP)pTP)->iNumWorkItemsPending_high > 0)
				{
					InterlockedDecrement(&(((PTP)pTP)->iNumWorkItemsPending_high));
					AcquireSRWLockExclusive(&gSRWLock_TPQhigh);
					PLINK pTemp = Dequeue(((PTP)(pTP))->pTPQ_high); //Get work item from queue
					ReleaseSRWLockExclusive(&gSRWLock_TPQhigh);
					if (pTemp)
					{
						LOG_INFO("Worker Thread %d done with removing high pri work from list\n", iWorkerThreadId);
						PWORKITEM pWork = ADDR_BASE(pTemp, WORKITEM, list_entry);
						LOG_INFO("Worker Thread %d calling high pri Work callback function\n", iWorkerThreadId);
						pWork->pCallback(pWork->pvParam); //Call client callback function
						pWork->iCompletionStatus = WORK_COMPLETE; //update work item completion status
						InterlockedIncrement(&(((PTP)pTP)->iNumWorkItemsHandled_high));
					}
					else
					{
						InterlockedIncrement(&(((PTP)pTP)->iNumWorkItemsPending_high));
						LOG_INFO("Worker Thread %d Unable to remove high low pri work from list\n", iWorkerThreadId);
					}
				}
				InterlockedIncrement(&(((PTP)pTP)->iCWWThreads));
				InterlockedDecrement(&(((PTP)pTP)->iCRWThreads));
				break;
			}
			else if (((PTP)pTP)->iNumWorkItemsPending_normal > 0) //Next handle the normal pri work items
			{
				InterlockedDecrement(&(((PTP)pTP)->iCWWThreads));
				InterlockedIncrement(&(((PTP)pTP)->iCRWThreads));
				while ((((PTP)pTP)->iNumWorkItemsPending_high == 0) && (((PTP)pTP)->iNumWorkItemsPending_normal > 0))
				{
					InterlockedDecrement(&(((PTP)pTP)->iNumWorkItemsPending_normal));
					AcquireSRWLockExclusive(&gSRWLock_TPQnormal);
					PLINK pTemp = Dequeue(((PTP)(pTP))->pTPQ_normal); //Get work item from queue
					ReleaseSRWLockExclusive(&gSRWLock_TPQnormal);
					if (pTemp)
					{
						LOG_INFO("Worker Thread %d done with removing normal pri work from list\n", iWorkerThreadId);
						PWORKITEM pWork = ADDR_BASE(pTemp, WORKITEM, list_entry);
						LOG_INFO("Worker Thread %d calling normal pri work callback function\n", iWorkerThreadId);
						pWork->pCallback(pWork->pvParam); //Call client callback function
						pWork->iCompletionStatus = WORK_COMPLETE; //update work item completion status
						InterlockedIncrement(&(((PTP)pTP)->iNumWorkItemsHandled_normal));
					}
					else
					{
						InterlockedIncrement(&(((PTP)pTP)->iNumWorkItemsPending_normal));
						LOG_INFO("Worker Thread %d unable to remove normal pri work from list\n", iWorkerThreadId);
					}
				}
				InterlockedIncrement(&(((PTP)pTP)->iCWWThreads));
				InterlockedDecrement(&(((PTP)pTP)->iCRWThreads));
				break;
			}
			else if (((PTP)pTP)->iNumWorkItemsPending_low > 0) //lastly handle the low priority work items
			{
				InterlockedDecrement(&(((PTP)pTP)->iCWWThreads));
				InterlockedIncrement(&(((PTP)pTP)->iCRWThreads));
				while ((((PTP)pTP)->iNumWorkItemsPending_high == 0) && (((PTP)pTP)->iNumWorkItemsPending_normal == 0) && (((PTP)pTP)->iNumWorkItemsPending_low > 0))
				{
					InterlockedDecrement(&(((PTP)pTP)->iNumWorkItemsPending_low));
					AcquireSRWLockExclusive(&gSRWLock_TPQlow);
					PLINK pTemp = Dequeue(((PTP)(pTP))->pTPQ_low); //Get work item from queue
					ReleaseSRWLockExclusive(&gSRWLock_TPQlow);
					if (pTemp)
					{
						LOG_INFO("Worker Thread %d done with removing low pri work from list\n", iWorkerThreadId);
						PWORKITEM pWork = ADDR_BASE(pTemp, WORKITEM, list_entry);
						LOG_INFO("Worker Thread %d calling low pri work callback function\n", iWorkerThreadId);
						pWork->pCallback(pWork->pvParam); //Call client callback function
						pWork->iCompletionStatus = WORK_COMPLETE; //update work item completion status
						InterlockedIncrement(&(((PTP)pTP)->iNumWorkItemsHandled_low));
					}
					else
					{
						InterlockedIncrement(&(((PTP)pTP)->iNumWorkItemsPending_low));
						LOG_INFO("Worker Thread %d unable to remove low pri work from list\n", iWorkerThreadId);
					}
				}
				InterlockedIncrement(&(((PTP)pTP)->iCWWThreads));
				InterlockedDecrement(&(((PTP)pTP)->iCRWThreads));
				break;
			}
		}
		}
	}
WORKERTHREADCLEANUP:
	FreeLibrary(hDll_LinkedList);
	InterlockedDecrement(&(((PTP)pTP)->iCWWThreads));
	return 1;
}

/*
This API is the Control Thread Function. Accepts pointer to Thread Pool as arguement and returns 0 or 1 upon termination
When there are no Worker Threads available to handle pending work items, Control Thread does the following:
a.If num of Running Worker Threads is less than macro MAXTHREADS, creates additional worker threads after a delay of WORKERTHREADCREATIONDELAY
b.If num of Running Worker Threads is = MAXTHREADS, does not create any more worker threads
*/
DWORD WINAPI ControlThreadProc(LPVOID pTP)
{
	//Parameter Validation
	if (pTP == NULL)
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		LOG_ERROR("Invalid Thread Pool:%d", GetLastError());
		return 1;
	}
	HANDLE hControlThreadEvents[2] = { g_hDeleteTPEvent,g_hControlThreadEvent };
	LOG_INFO("Starting Control Thread \n");
	while (TRUE)
	{
		volatile int iSleepCounter = 0;
		LOG_INFO("Control Thread waiting for CWWT to become zero\n");
		DWORD dw = WaitForMultipleObjects(2, hControlThreadEvents, FALSE, INFINITE);
		switch (dw)
		{
		case WAIT_FAILED: //Wait failed
			LOG_ERROR("Worker Thread Wait failed:%d", GetLastError());
			return 1;

		case WAIT_OBJECT_0 + 0: //Delete TP
			LOG_INFO("Control Thread terminating due to thread pool deletion\n");
			return 0;

		case WAIT_OBJECT_0 + 1: //CWWT threads is zero
		CHECKCWT:if (((PTP)pTP)->iCWWThreads == 0) //Check if CWWT is 0
		{
			LOG_INFO("CWWT is zero\n");
			if (((PTP)pTP)->iCRWThreads < MAXTHREADS) //Check if CRWT is < MAXTHREADS, only then create more worker threads
			{
				LOG_INFO("CRWT is less than Max Threads\n");
				if (iSleepCounter == 0)
				{
					LOG_INFO("Delaying New Worker Thread creation\n");
					Sleep(WORKERTHREADCREATIONDELAY); //Delay
					iSleepCounter++;
					goto CHECKCWT;
				}
				else
				{
					LOG_INFO("Additional Worker Thread creation\n");
					if (CreateThread(NULL, 0, WorkerThreadProc, (LPVOID)pTP, 0, 0) == NULL) //Create Additional Worker Thread post delay
					{
						LOG_ERROR("Unable to Create Additional Worker Threads:%d", GetLastError());
						return 1;
					}
					InterlockedIncrement(&(((PTP)pTP)->iCWWThreads));
				}
			}
			else
			{
				LOG_INFO("CWWT is Zero and CRWT is MAX, Not creating any more threads!!\n");
			}
		}
		}
	}
}

/*
This API Create a Work Item structure to be used in the Thread Pool
Accepts 4 arguments:
a.Pointer to ThreadPool
b.Client Callback Function of type CALLBACK_INSTANCE
c.Void pointer to parameters to be passed to the callback function
d.Priority of the work (one of WORKITEM_HIGH, WORKITEM_NORMAL or WORKITEM_LOW)
Return pointer to workitem upon success, else returns NULL
*/
PWORKITEM CreateWorkItem(PTP pTP, CALLBACK_INSTANCE pCallback, PVOID pvParam, DWORD iPri)
{
	//Parameter Validation
	if ((!(pTP && pCallback && pvParam)) && ((iPri < 0) || (iPri > 2)))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		LOG_ERROR("Unable to Create Work Item:%d", GetLastError());
		return NULL;
	}
	//Get Default Process Heap Handle
	HANDLE hDefaultHeap = GetProcessHeap();
	if (hDefaultHeap == NULL)
	{
		LOG_ERROR("Unable to get handle to Default Process Heap:%d", GetLastError());
		return NULL;
	}
	//Allocate WorkItem structure
	PWORKITEM pWorkItem = (PWORKITEM)HeapAlloc(hDefaultHeap, HEAP_ZERO_MEMORY, sizeof(WORKITEM));
	if (pWorkItem == NULL)
	{
		SetLastError(ERROR_NOT_ENOUGH_MEMORY);
		LOG_ERROR("Unable to create WorkItem structure:%d", GetLastError());
		return NULL;
	}
	pWorkItem->pCallback = pCallback;
	pWorkItem->pvParam = pvParam;
	pWorkItem->iPri = iPri;
	pWorkItem->iCompletionStatus = WORK_NOTCOMPLETE; //To being with Work item is not complete
	return pWorkItem;
}

/*
This function checks if work item can be inserted to the queue or not
Accepts pointers to ThreadPool and pointer to WorkItem as arguements
Returns TRUE if work item can be inserted , else returns false
*/
BOOL CanInsertWork(PTP pTP, PWORKITEM pWk)
{
	//Parameter validation
	if (!(pTP && pWk))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		LOG_ERROR("Cant insert work:%d", GetLastError());
		return FALSE;
	}
	//if Current Waiting Worker Threads is 0, notify Control Thread for new Worker Thread Creation
	if (((PTP)pTP)->iCWWThreads == 0)
	{
		if (!(SetEvent(g_hControlThreadEvent)))
		{
			LOG_ERROR("Unable to Set g_hControlThreadEvent:%d", GetLastError());
			return FALSE;
		}
	}
	switch (pWk->iPri)
	{
	case WORKITEM_LOW:
	{
		//if Number of Work Items in Low Pri queue has reached MAXPENDINGWORKITEMS cant insert more work
		if (pTP->iNumWorkItemsPending_low == MAXPENDINGWORKITEMS)
			return FALSE;
		else
			return TRUE;
	}

	case WORKITEM_NORMAL:
	{
		//if Number of Work Items in Normal Pri queue has reached MAXPENDINGWORKITEMS cant insert more work
		if (pTP->iNumWorkItemsPending_normal == MAXPENDINGWORKITEMS)
			return FALSE;
		else
			return TRUE;
	}

	case WORKITEM_HIGH:
	{
		//if Number of Work Items in High Pri queue has reached MAXPENDINGWORKITEMS cant insert more work
		if (pTP->iNumWorkItemsPending_high == MAXPENDINGWORKITEMS)
			return FALSE;
		else
			return TRUE;
	}
	}
}

/*
This API Inserts work to the respective Pri queue
Accepts pointer to Thread Pool and pointer to work item as arguements
Returns True upon succesful insertion, else return False
*/
BOOL InsertWork(PTP pTP, PWORKITEM pWk)
{
	//Parameter validation
	if (!(pTP && pWk))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		LOG_ERROR("Cant insert work:%d", GetLastError());
		return FALSE;
	}
	//Loading DLL_Linkedlist.dll explicitly and getting the relevant functions
	HMODULE hDll_LinkedList = LoadLibraryExW(L"DLL_LinkedList.dll", NULL, 0);
	if (hDll_LinkedList == NULL)
	{
		LOG_ERROR("Unable to load DLL_LinkedList.dll:%d", GetLastError());
		return FALSE;
	}
	MYPROC1 Enqueue = (MYPROC1)GetProcAddress(hDll_LinkedList, "InsertHeadList");
	if (!Enqueue)
	{
		LOG_ERROR("Unable to GetProcAddress:%d", GetLastError());
		FreeLibrary(hDll_LinkedList);
		return FALSE;
	}
	switch (pWk->iPri)
	{
	case WORKITEM_HIGH: //High Pri Work Item
		AcquireSRWLockExclusive(&gSRWLock_TPQhigh); //Get exclusive SRW lock
		LOG_INFO("Inserting High pri Work to queue\n");
		if (Enqueue(pTP->pTPQ_high, &(pWk->list_entry))) //Queue the work item
		{
			ReleaseSRWLockExclusive(&gSRWLock_TPQhigh); //Release exclusive SRW lock
			InterlockedIncrement(&(pTP->iNumWorkItemsAdded_high)); //Update TP parameters
			InterlockedIncrement(&(pTP->iNumWorkItemsPending_high));
			LOG_INFO("Waking Worker Thread for high pri Work\n");
			if (!SetEvent(g_hWIAvailableEvent)) //Notify Worker Thread
			{
				LOG_ERROR("Unable to set g_hWIAvailableEvent[1]:%d", GetLastError());
				FreeLibrary(hDll_LinkedList);
				return FALSE;
			}
			FreeLibrary(hDll_LinkedList);
			return TRUE;
		}
		else
		{
			LOG_ERROR("Unable to Insert High pri Work to queue\n");
			ReleaseSRWLockExclusive(&gSRWLock_TPQhigh);
			FreeLibrary(hDll_LinkedList);
			return FALSE;
		}

	case WORKITEM_NORMAL: //Normal Pri Work Item (same steps as above)
		AcquireSRWLockExclusive(&gSRWLock_TPQnormal);
		LOG_INFO("Inserting Normal pri Work to queue\n");
		if (Enqueue(pTP->pTPQ_normal, &(pWk->list_entry)))
		{
			ReleaseSRWLockExclusive(&gSRWLock_TPQnormal);
			InterlockedIncrement(&(pTP->iNumWorkItemsAdded_normal));
			InterlockedIncrement(&(pTP->iNumWorkItemsPending_normal));
			LOG_INFO("Waking Worker Thread for normal pri Work\n");
			if (!SetEvent(g_hWIAvailableEvent))
			{
				LOG_ERROR("Unable to set g_hWIAvailableEvent[1]:%d", GetLastError());
				FreeLibrary(hDll_LinkedList);
				return FALSE;
			}
			FreeLibrary(hDll_LinkedList);
			return TRUE;
		}
		else
		{
			LOG_INFO("Unable to Insert Normal pri Work to queue\n");
			ReleaseSRWLockExclusive(&gSRWLock_TPQnormal);
			FreeLibrary(hDll_LinkedList);
			return FALSE;
		}

	case WORKITEM_LOW: //Low Pri Work Item (same steps as above)
		AcquireSRWLockExclusive(&gSRWLock_TPQlow);
		LOG_INFO("Inserting low pri Work to queue\n");
		if (Enqueue(pTP->pTPQ_low, &(pWk->list_entry)))
		{
			ReleaseSRWLockExclusive(&gSRWLock_TPQlow);
			InterlockedIncrement(&(pTP->iNumWorkItemsAdded_low));
			InterlockedIncrement(&(pTP->iNumWorkItemsPending_low));
			LOG_INFO("Waking Worker Thread for low pri Work\n");
			if (!SetEvent(g_hWIAvailableEvent)) //Notify Worker Thread
			{
				LOG_ERROR("Unable to set g_hWIAvailableEvent[1]:%d", GetLastError());
				FreeLibrary(hDll_LinkedList);
				return FALSE;
			}
			FreeLibrary(hDll_LinkedList);
			return TRUE;
		}
		else
		{
			LOG_INFO("Unable to Insert Low pri Work to queue\n");
			ReleaseSRWLockExclusive(&gSRWLock_TPQlow);
			FreeLibrary(hDll_LinkedList);
			return FALSE;
		}
	}
}

/*
This routine checks if a work item can be inserted first
If so it inserts the work item
Accepts pointer to Thread Pool and pointer to WorkItem as arguements
Returns TRUE if work item is inserted, else returns FALSE
*/
BOOL TryInsertWork(PTP pTP, PWORKITEM pWk)
{
	//Parameter validation
	if (!(pTP && pWk))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		LOG_ERROR("Cant insert work:%d", GetLastError());
		return FALSE;
	}
	if (CanInsertWork(pTP, pWk))
	{
		if (InsertWork(pTP, pWk))
		{
			return TRUE;
		}
	}
	LOG_ERROR("Cant insert work\n");
	return FALSE;
}

/*
This API checks if the submitted work is complete or not
Accepts pointer to Thread Pool and pointer to Work Item as input
Return TRUE is work is done, else returns FALSE
*/
BOOL IsWorkComplete(PTP pTP, PWORKITEM pWk)
{
	//Parameter validation
	if (!(pTP && pWk))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		LOG_ERROR("Cant check if work is complete:%d", GetLastError());
		return FALSE;
	}
	if (pWk->iCompletionStatus == WORK_COMPLETE) //if work is complete, return TRUE
	{
		return TRUE;
	}
	else if (pWk->iCompletionStatus == WORK_NOTCOMPLETE) //else return FALSE
	{
		return FALSE;
	}
}

/*
This API deletes the work item
Accepts pointer to Thread Pool and pointer to Work Item as arguements
Returns TRUE if work item is successfully deleted, else returns FALSE
Note:
The work item to be deleted may be in any of the states - Work Complete, Work Not Complete, Work Running
It is recommended to only Delete Work Item once it is complete
*/
BOOL DeleteWorkItem(PTP pTP, PWORKITEM pWk)
{
	//Parameter validation
	if (!(pTP && pWk))
	{
		SetLastError(ERROR_INVALID_PARAMETER);
		LOG_ERROR("Cant delete work:%d", GetLastError());
		return FALSE;
	}
	//Loading DLL_Linkedlist.dll explicitly and getting the relevant functions
	HMODULE hDll_LinkedList = LoadLibraryExW(L"DLL_LinkedList.dll", NULL, 0);
	if (hDll_LinkedList == NULL)
	{
		LOG_ERROR("Unable to load DLL_LinkedList.dll:%d", GetLastError());
		return FALSE;
	}
	MYPROC1 FindWorkItem = (MYPROC1)GetProcAddress(hDll_LinkedList, "FindEntry");
	MYPROC1 RemoveWorkItem = (MYPROC1)GetProcAddress(hDll_LinkedList, "RemoveEntry");
	if (!(FindWorkItem && RemoveWorkItem))
	{
		LOG_ERROR("Unable to GetProcAddress:%d", GetLastError());
		FreeLibrary(hDll_LinkedList);
		return FALSE;
	}
	//Get Default Process Heap Handle
	HANDLE hDefaultHeap = GetProcessHeap();
	if (hDefaultHeap == NULL)
	{
		LOG_ERROR("Unable to get handle to Default Process Heap:%d", GetLastError());
		FreeLibrary(hDll_LinkedList);
		return FALSE;
	}
	if (pWk->iCompletionStatus == WORK_COMPLETE) //Work is complete, so it is already dequeued, free it
	{
		if (HeapFree(hDefaultHeap, 0, pWk) == 0)
		{
			LOG_ERROR("Unable to free WorkItem:%d", GetLastError());
			FreeLibrary(hDll_LinkedList);
			return FALSE;
		}
		FreeLibrary(hDll_LinkedList);
		return TRUE;
	}
	//Work is not complete, so it may or may not be dequeued, also work may be in running phase, not recommended to remove
	else if (pWk->iCompletionStatus == WORK_NOTCOMPLETE)
	{
		switch (pWk->iPri)
		{
		case WORKITEM_HIGH: // High pri work item
			AcquireSRWLockShared(&gSRWLock_TPQhigh);
			if (FindWorkItem(pTP->pTPQ_high, &(pWk->list_entry)))
			{
				ReleaseSRWLockShared(&gSRWLock_TPQhigh);
				AcquireSRWLockExclusive(&gSRWLock_TPQhigh);
				if (RemoveWorkItem(pTP->pTPQ_high, &(pWk->list_entry))) //Remove work item from queue
				{
					InterlockedDecrement(&(pTP->iNumWorkItemsPending_high));
					LOG_INFO("Removed Work Item from queue\n");
				}
				ReleaseSRWLockExclusive(&gSRWLock_TPQhigh);
			}
			if (HeapFree(hDefaultHeap, 0, pWk) == 0) // free it
			{
				LOG_ERROR("Unable to free WorkItem:%d", GetLastError());
				FreeLibrary(hDll_LinkedList);
				return FALSE;
			}
			FreeLibrary(hDll_LinkedList);
			return TRUE;

		case WORKITEM_NORMAL: //same as above
			AcquireSRWLockShared(&gSRWLock_TPQnormal);
			if (FindWorkItem(pTP->pTPQ_normal, &(pWk->list_entry)))
			{
				ReleaseSRWLockShared(&gSRWLock_TPQnormal);
				AcquireSRWLockExclusive(&gSRWLock_TPQnormal);
				if (RemoveWorkItem(pTP->pTPQ_normal, &(pWk->list_entry))) //Remove work item from queue
				{
					InterlockedDecrement(&(pTP->iNumWorkItemsPending_normal));
					LOG_INFO("Removed Work Item from queue\n");
				}
				ReleaseSRWLockExclusive(&gSRWLock_TPQnormal);
			}
			if (HeapFree(hDefaultHeap, 0, pWk) == 0)
			{
				LOG_ERROR("Unable to free WorkItem:%d", GetLastError());
				FreeLibrary(hDll_LinkedList);
				return FALSE;
			}
			FreeLibrary(hDll_LinkedList);
			return TRUE;

		case WORKITEM_LOW: //same as above
			AcquireSRWLockShared(&gSRWLock_TPQlow);
			if (FindWorkItem(pTP->pTPQ_low, &(pWk->list_entry)))
			{
				ReleaseSRWLockShared(&gSRWLock_TPQlow);
				AcquireSRWLockExclusive(&gSRWLock_TPQlow);
				if (RemoveWorkItem(pTP->pTPQ_low, &(pWk->list_entry))) //Remove work item from queue
				{
					InterlockedDecrement(&(pTP->iNumWorkItemsPending_low));
					LOG_INFO("Removed Work Item from queue\n");
				}
				ReleaseSRWLockExclusive(&gSRWLock_TPQlow);
			}
			if (HeapFree(hDefaultHeap, 0, pWk) == 0)
			{
				LOG_ERROR("Unable to free WorkItem:%d", GetLastError());
				FreeLibrary(hDll_LinkedList);
				return FALSE;
			}
			FreeLibrary(hDll_LinkedList);
			return TRUE;
		}
	}
}

/*
This routine provides Thread Pool statistics information to the client
Accepts pinter to Thread Pool and pointer to a structure where the Thread Pool Statistics needs to be written to
Returns TRUE if Stats are updated successfully, else returns false
*/
BOOL GetTPStats(PTP pTP, PTPSTATS pTPStats)
{
	if (pTP && pTPStats)
	{
		pTPStats->iCurrentRunningThreads = pTP->iCRWThreads;
		pTPStats->iCurrentWaitingThreads = pTP->iCWWThreads;
		pTPStats->iNumWorkItemsAdded_high = pTP->iNumWorkItemsAdded_high;
		pTPStats->iNumWorkItemsAdded_low = pTP->iNumWorkItemsAdded_low;
		pTPStats->iNumWorkItemsAdded_normal = pTP->iNumWorkItemsAdded_normal;
		pTPStats->iNumWorkItemsHandled_high = pTP->iNumWorkItemsHandled_high;
		pTPStats->iNumWorkItemsHandled_low = pTP->iNumWorkItemsHandled_low;
		pTPStats->iNumWorkItemsHandled_normal = pTP->iNumWorkItemsHandled_normal;
		pTPStats->iNumWorkItemsPending_high = pTP->iNumWorkItemsPending_high;
		pTPStats->iNumWorkItemsPending_low = pTP->iNumWorkItemsPending_low;
		pTPStats->iNumWorkItemsPending_normal = pTP->iNumWorkItemsPending_normal;

		return TRUE;
	}
	return FALSE;
}

/*
This routine Deletes the TP
It is recommended that the client calls this only after all the work items are completed and freed, else the behaviour is undefined
Accepts pointer to Thread pool as arguement
Returns TRUE upon successful deletion of TP, else return FALSE
If Deletion of TP fails, the state of the TP is undefined and client should no longer use the TP
It can either create a new TP or terminate
*/
BOOL DeleteTP(PTP pTP)
{
	//Set g_hDeleteTPEvent, this notifies all Worker Threads and Control Thread to terminate
	if (!SetEvent(g_hDeleteTPEvent))
	{
		LOG_ERROR("Unable to Set g_hDeleteTPEvent:%d\n", GetLastError());
		return FALSE;
	}
	HANDLE hDefaultHeap = GetProcessHeap();
	if (hDefaultHeap == NULL) //if it fails return NULL
	{
		LOG_ERROR("Unable to get handle to Default Process Heap:%d", GetLastError());
		return FALSE;
	}
	//Wait for Control and Worker Threads to terminate
	while ((pTP->iCRWThreads) || (pTP->iCWWThreads))
	{
		Sleep(1000);
	}
	LOG_INFO("Closed all TP threads\n");
	//Close all the Events created
	if (!(CloseHandle(g_hControlThreadEvent) && CloseHandle(g_hWIAvailableEvent) && CloseHandle(g_hKillWorkerThreadTimer) && CloseHandle(g_hDeleteTPEvent)))
	{
		LOG_ERROR("Unable to Close handle to one or more Events:%d\n", GetLastError());
		return FALSE;
	}
	LOG_INFO("Successfully closed all Event handles\n");

	//Loading DLL_Linkedlist.dll explicitly and getting the relevant functions
	HMODULE hDll_LinkedList = LoadLibraryExW(L"DLL_LinkedList.dll", NULL, 0);
	if (hDll_LinkedList == NULL)
	{
		LOG_ERROR("Unable to load DLL_LinkedList.dll:%d", GetLastError());
		return FALSE;
	}
	MYPROC3 DeleteQueue = (MYPROC3)GetProcAddress(hDll_LinkedList, "DeleteList");
	if (!DeleteQueue)
	{
		LOG_ERROR("Unable to GetProcAddress:%d", GetLastError());
		FreeLibrary(hDll_LinkedList);
		return FALSE;
	}
	//Free the 3 pri queues
	if (!(DeleteQueue(pTP->pTPQ_high) && DeleteQueue(pTP->pTPQ_normal) && DeleteQueue(pTP->pTPQ_low)))
	{
		LOG_ERROR("Unable to Free Pri queues\n");
		FreeLibrary(hDll_LinkedList);
		return FALSE;
	}
	LOG_INFO("Successfully closed all Pri Queues\n");

	if (HeapFree(hDefaultHeap, 0, pTP) == 0)
	{
		LOG_ERROR("Unable to free pTP:%d", GetLastError());
		FreeLibrary(hDll_LinkedList);
		return FALSE;
	}
	LOG_INFO("Successfully deleted TP\n");
	FreeLibrary(hDll_LinkedList);
	return TRUE;
}


