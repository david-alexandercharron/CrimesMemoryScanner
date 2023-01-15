#include "Memory.h"
#include <Windows.h>
#include <iostream>

/// All the different flags that determine if a Memory Block is writable
#define WRITABLE (PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY)

/// Macro that will query an individual flag of a Memory Block scan.
#define IS_IN_SEARCH(mb, offset) (mb->searchmask[(offset)/8] & (1 << ((offset) % 8)))

/// Macro that will clear the corresponding flag to discard it from a search
#define REMOVE_FROM_SEARCH(mb, offset) mb->searchmask[(offset)/8] &= ~(1 << ((offset) % 8))


/// Basic data structure that will hold information about one memory block from a remote process
typedef struct _MEMBLOCK
{
    HANDLE hProc;               // Handle to the process that we are interested in
    unsigned char* addr;        // Pointer to the base address
    int size;                   // Size
    unsigned char* buffer;      // Local buffer that will contain a cloned copy of the data
    unsigned char* searchmask;  // Searchmask flag for each byte
    int matches;                // Count of search matches
    int data_size;              // Data size can be 1,2 or 4. (Byte, WORD or DWORD)
    struct _MEMBLOCK* next;     // Chain these structures together in a linked list

} MEMBLOCK;

/// Search Condition Enum that will hold the different conditions of a search
enum class SEARCH_CONDITION
{
    COND_UNCONDITIONAL,     // Unconditional search
    COND_EQUALS,            // Condition to match a value
    COND_INCREASED,         // Condition to search for increased values
    COND_DECREASED,         // Condition to search for decreased values
};

/// <summary> Get a pointer to a memory block </summary>
/// <param name="hProc"> Handle to the process that we are creating a memory block for </param>
/// <param name="meminfo"> Pointer to a structure called memory basic information returned by VirtualQueryX </param>
/// /// <param name="date_size"> Data size can be 1,2 or 4. (Byte, WORD or DWORD) </param>
/// <returns> Pointer to a memory block </returns>
MEMBLOCK* create_memblock(HANDLE hProc, MEMORY_BASIC_INFORMATION* meminfo, int data_size)
{
    MEMBLOCK* mb = (MEMBLOCK*)malloc(sizeof(MEMBLOCK));

    if (mb)
    {
        mb->hProc = hProc;                                                  // Process Handler
        mb->addr = (unsigned char*)meminfo->BaseAddress;                    // Base of the MEMBLOCK
        mb->size = meminfo->RegionSize;                                     // Size
        mb->buffer = (unsigned char*)malloc(meminfo->RegionSize);           // Base of the buffer address
        mb->searchmask = (unsigned char*)malloc(meminfo->RegionSize / 8);   // Allocate one bit to the searchmask buffer (1 or 2)
        memset(mb->searchmask, 0xFF, meminfo->RegionSize / 8);              // Set every flag to true (0xFF in bytes)
        mb->matches = meminfo->RegionSize;                                  // Number of matches will be the numbers of bytes in a buffer
        mb->data_size = data_size;                                          // Data size can be 1,2 or 4. (Byte, WORD or DWORD)
        mb->next = NULL;                                                    // No next element yet
    }

    return mb;
}


/// <summary> Free a memory block </summary>
/// <param name="mb"> Pointer to the memory block to free </param>
void free_memblock(MEMBLOCK* mb)
{
    if (mb)
    {
        if (mb->buffer)
        {
            free(mb->buffer);
        }

        if (mb->searchmask)
        {
            free(mb->searchmask);
        }

        free(mb);
    }
}

/// <summary> Update a memory block </summary>
/// <param name="mb"> Pointer to the memory block to update </param>
/// <param name="condition"> Search condition </param>
/// <param name="value"> Value to search for </param>
void update_memblock(MEMBLOCK* mb, SEARCH_CONDITION condition, unsigned int value)
{
    // Return if there is already no matches
    if (mb->matches == 0) return;

    static unsigned char tempbuf[128 * 1024];       // Local Buffer in blocks of 128k
    unsigned int bytes_left = mb->size;             // Bytes left to read
    unsigned int total_read = 0;                    // Total read
    unsigned int bytes_to_read;                     // Bytes to read
    unsigned long bytes_read;                       // Bytes read

    mb->matches = 0;

    while (bytes_left)
    {

        // Read Process Memory
        bytes_to_read = (bytes_left > sizeof(tempbuf)) ? sizeof(tempbuf) : bytes_left;
        ReadProcessMemory(mb->hProc, mb->addr + total_read, tempbuf, bytes_to_read, &bytes_read);
        if (bytes_read != bytes_to_read) break;

        // Filter depending on the Search Condition
        if (condition == SEARCH_CONDITION::COND_UNCONDITIONAL)
        {
            memset(mb->searchmask + (total_read / 8), 0xFF, bytes_read / 8);
            mb->matches += bytes_read;
        }
        else
        {
            for (unsigned int offset = 0; offset < bytes_read; offset += mb->data_size)
            {
                if (IS_IN_SEARCH(mb, (total_read + offset)))
                {
                    bool is_match = false;                  // Is there a match variable
                    unsigned int temp_value;                // Current Value
                    unsigned int prev_value = 0;            // Previous Value

                    // Cast to the correct data type (BYTE, WORD or DWORD)
                    switch (mb->data_size)
                    {
                        // BYTE
                    case 1:
                        temp_value = tempbuf[offset];
                        prev_value = *((unsigned char*)&mb->buffer[total_read + offset]);
                        break;

                        // WORD
                    case 2:
                        temp_value = *((unsigned short*)&tempbuf[offset]);
                        prev_value = *((unsigned short*)&mb->buffer[total_read + offset]);
                        break;

                        // DWORD
                    case 4:
                    default:
                        temp_value = *((unsigned int*)&tempbuf[offset]);
                        prev_value = *((unsigned int*)&mb->buffer[total_read + offset]);
                        break;
                    }

                    // Check the condition
                    switch (condition)
                    {
                        // Condition to match a value
                    case SEARCH_CONDITION::COND_EQUALS:
                        is_match = (temp_value == value);
                        break;

                        // Condition that value increased
                    case SEARCH_CONDITION::COND_INCREASED:
                        is_match = (temp_value > prev_value);
                        std::cout << "asdf " << prev_value << std::endl;
                        break;

                        // Condition that value increased
                    case SEARCH_CONDITION::COND_DECREASED:
                        is_match = (temp_value < prev_value);
                        break;

                    default:
                        break;
                    }

                    // Check if we found a match
                    if (is_match)
                    {
                        mb->matches++;
                    }
                    else
                    {
                        REMOVE_FROM_SEARCH(mb, (total_read + offset));
                    }

                }
            }

        }

        // Copy bytes to the buffer
        memcpy(mb->buffer + total_read, tempbuf, bytes_read);

        bytes_left -= bytes_read;
        total_read += bytes_read;

    }

    mb->size = total_read;

}

/// <summary> Create Memory Scanner </summary>
/// <param name="pid"> PID of the process to scan </param>
/// <param name="data_size"> Data size can be 1,2 or 4. (Byte, WORD or DWORD) </param>
/// <returns> Pointer to a memory block </returns>
MEMBLOCK* create_scan(unsigned int pid, int data_size)
{
    MEMBLOCK* mb_list = NULL;               // Memory Block
    MEMORY_BASIC_INFORMATION meminfo;       // Memory Basic Information structure for VirtualQueryEx
    unsigned char* addr = 0;                // Pointer to keep track of the address that we've passed to VirtualQueryEx

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);     // Handle Process

    if (hProc)
    {
        while (1)
        {
            // When addr hits the end of the memory, it will return 0
            if (VirtualQueryEx(hProc, addr, &meminfo, sizeof(meminfo)) == 0)
            {
                break;
            }

            // Discard Memory Blocks that are not writable and that have been reserved but are unused (MEM_COMMIT)
            if ((meminfo.State & MEM_COMMIT) && (meminfo.Protect & WRITABLE))
            {
                MEMBLOCK* mb = create_memblock(hProc, &meminfo, data_size);
                if (mb)
                {
                    mb->next = mb_list;
                    mb_list = mb;
                }
            }


            addr = (unsigned char*)meminfo.BaseAddress + meminfo.RegionSize;
        }
    }

    return mb_list;
}

/// <summary> Free memory blocks from a scan </summary>
/// <param name="mb_list"> Pointer to the memory block list to free </param>
void free_scan(MEMBLOCK* mb_list)
{
    CloseHandle(mb_list->hProc);

    while (mb_list)
    {
        MEMBLOCK* mb = mb_list;
        mb_list = mb_list->next;
        free_memblock(mb);
    }
}


/// <summary> Update memory blocks from a scan </summary>
/// <param name="mb_list"> Pointer to the memory block list to update </param>
void update_scan(MEMBLOCK* mb_list, SEARCH_CONDITION condition, unsigned int value)
{
    MEMBLOCK* mb = mb_list;

    while (mb)
    {
        update_memblock(mb, condition, value);
        mb = mb->next;
    }
}

/// <summary> Dump Scan Info function </summary>
/// <param name="mb_list"> Pointer to the memory block list to print </param>
void dump_scan_info(MEMBLOCK* mb_list)
{
    MEMBLOCK* mb = mb_list;

    while (mb)
    {

        printf("0x%08x %d\r\n", mb->addr, mb->size);

        for (int i = 0; i < mb->size; i++)
        {
            printf("%02x", mb->buffer[i]);
        }

        printf("\r\n");

        mb = mb->next;
    }
}


/// <summary> Write a value to process memory </summary>
/// <param name="hProc"> Handle to the process that we are writing to </param>
/// <param name="data_size"> Size of the data we are writing to </param>
/// <param name="addr"> Address that we are writing to </param>
/// <param name="value"> Value that we are writing to </param>
void poke(HANDLE hProc, int data_size, unsigned int addr, unsigned int value)
{
    if (WriteProcessMemory(hProc, (void*)addr, &value, data_size, NULL) == 0)
    {
        printf("Writing to process memory has failed\r\n");
    }
}

/// <summary> Read a value from process memory </summary>
/// <param name="hProc"> Handle to the process that we are reading from </param>
/// <param name="data_size"> Size of the data we are reading from </param>
/// <param name="addr"> Address that we are reading from </param>
/// <returns> Value read from process memory </returns>
unsigned int peek(HANDLE hProc, int data_size, unsigned int addr)
{
    unsigned int value = 0;

    if (ReadProcessMemory(hProc, (void*)addr, &value, data_size, NULL) == 0)
    {
        printf("Reading from process memory has failed\r\n");
    }

    return value;
}

/// <summary> Print Memory Scan Matches </summary>
/// <param name="mb_list"> Pointer to the memory block list scan </param>
void print_matches(MEMBLOCK* mb_list)
{
    MEMBLOCK* mb = mb_list;

    while (mb)
    {
        for (int offset = 0; offset < mb->size; offset += mb->data_size)
        {
            if (IS_IN_SEARCH(mb, offset))
            {
                unsigned int val = peek(mb->hProc, mb->data_size, (long long unsigned int)mb->addr + offset);
                printf("0x%08x: 0x%08x (%d) and %x\r\n", mb->addr + offset, val, val, offset);

                char message[50];
                sprintf_s(message, "0x%08x: 0x%08x (%d) and %x\r\n", mb->addr + offset, val, val, offset);
                OutputDebugStringA(message);
            }
        }
        mb = mb->next;
    }
}

/// <summary> Get Matches Count </summary>
/// <param name="mb_list"> Pointer to the memory block list scan </param>
int get_matches_count(MEMBLOCK* mb_list)
{
    MEMBLOCK* mb = mb_list;
    int counter = 0;

    while (mb)
    {
        counter += mb->matches;
        mb = mb->next;
    }

    return counter;
}


/// <summary> Convert a string to an integer </summary>
/// /// <param name="s"> Reference to a string </param>
unsigned int str2int(char* s)
{
    // Use base 10 by default
    int base = 10;

    // Check if the refernse to the string is in hex
    if (s[0] == '0' && s[1] == 'x')
    {
        base = 16;
        s += 2;
    }

    return strtoul(s, NULL, base);
}


void Memory::test()
{
    
    MEMBLOCK* scan = create_scan(20552, 4);

    if (scan)
    {
        printf("Searching equal\n\n");

        MEMBLOCK* mb = scan;
        while (mb)
        {
            update_memblock(mb, SEARCH_CONDITION::COND_EQUALS, 1);
            //printf("Found : %d", get_matches_count(scan));

            int x = get_matches_count(scan);
            char message[50];
            sprintf_s(message, "\nDone scanning... The value of x is: %d\n\n", x);
            OutputDebugStringA(message);


            mb = mb->next;
        }
        print_matches(scan);


        /*printf("Searching increased\n\n");

        mb = scan;

        while(mb)
        {
            update_memblock(mb, COND_INCREASED, 0);
            //printf("Found : %d", get_matches_count(scan));


            mb = mb->next;
        }
        print_matches(scan);*/

        free_scan(scan);
    }

}