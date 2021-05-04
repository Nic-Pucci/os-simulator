Design Choices & Policies:

1. This simulator implementation uses a modified version of Round Robin for Multilevel-Queues:
	Higher priority level processes strictly pre-empt lower levels 

	To avoid starvation, there is a CPU burst count expiry mechanism that can either PROMOTE or DEMOTE the priority level of a given process. 

2. Starvation Solution: PCB Priority Level Promotion/Demotion
	The macro MAX_CPU_BURSTS (= 5) defines how many CPU bursts a process can have until it gets either DEMOTED or PROMOTED to the next designated ready queue.
	
	If a process begins at a priority level > lowest level, then after every 5 cycles in a given queue, the process will be DEMOTED down a priority level.

	This will occur until the process reaches the lowest queue. Then, the process will be PROMOTED up a priority level every time it expires in a given queue.

	This policy was set in place to avoid the starvation of lower priority processes, where higher priority processes hog the CPU indefinately. By enforcing CPU bursts expiring constraints, it allows for the adaptation of priority levels for hogging processes. 

	If a particular process runs for a very long time, it wouldn't be able to cause starvation for lower priority processes. Instead, it would get demoted to lower tiers. Once it reaches the lowest level, it can be brought back up to the top priority queue again. 

	This policy would also address the changes in scheduling needs for running processes.

	NOTE: It is possible that a steady flow of new highest priority processes can starve lower processes. However, this scenario is probably not very realistic. There would most likely be breaks between oncomming processes, and each of them would realistically have different priority levels. In addition, not every oncomming process will have extremely long running times. Taking all of these factors in account, the policy could prevent realistic starvation scenarios.

3. INIT Process & Send/Receive
	This implementation allows for processes to be able to send messages to INIT.

	INIT is able to both Send and Receive, however, it strictly NEVER blocks.

	Messages can still be sent to blocked user processes. Once a process is unblocked, then it can receive the message with the receive command.

4. Automatic Pre-empting vs. Q
	There are a few cases where automatic pre-empting occurs:
		a. When INIT is running, and a user process is created or unblocked. When this happens, the simulation will boot INIT and run the ready process.

		b. When a process blocks itself, the next ready process will automatically be run next.

	All other scenarios require the user to use the quantum command to pre-empt a running process.