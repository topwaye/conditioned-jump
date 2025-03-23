# CONDITIONED-JUMP OPERATING SYSTEM 1.1.0

[FUNCTION OF CONDITIONED-JUMP]

kernel is a function (i.e. func_x()) called by main functions (i.e. main())

what does kernel do? it transforms parallelizing tasks into serializing kernel to maintain memory lines

there are 2 types of jmp per CPU, which are active jmp and passive jmp
for user-oriented developers, they do NOT consider passive jmp at all
for kernel-oriented developers, they have to consider both of them

in user area, active jmp and passive jmp do NOT bother each other
in kernel area, active jmp and passive jmp do bother each other
in kernel area, if no passive jmp is wanted, then disable passive jmp by cli instruction

note that both types of jmp are in the same one task execution line

[ADDRESS CONCEPT OF CONDITIONED-JUMP]

printk ( "%x\n", get_free_page ( ) );

the return page address is a relative address based on a segment starting at 3GB, NOT an absolute address
this relative address can be interpreted as a value of 3GB + this relative address by CPU, and then
by looking up the page table, a physical address can be found, which is equal to this relative address
i.e. relative addresses == physical addresses

get_free_page allocates a physical address number which is equal to a relative address based on a segment
starting at 3GB, due to we installed pages to kernel page table one by one in range of 1GB when booting
if a physical address number is out of range of 1GB, we can NOT use this physical address number as a relative
address straightforwardly. we have to install this physical address number to kernel page table before using it

lgdt instruction setups CS register. after lgdt, CPU is running on 3GB address

286 only allocates physical addresses as relative addresses before reading and writing relative addresses
386 allocates and installs physical addresses to relative addresses before reading and writing relative addresses
allocation and installation are unrelated to each other, address installation differentiates 386 from 286

we should use a relative address allocator [M], but we use a physical address allocator [PM] instead
which includes addresses out of range of 1GB

    [M]  relative addresses
    -------------------------
    |||||||||||||||||||||||||
    |||||||||||||||||||||||||
    |       installed       |                               uninstalled
    |||||||||||||||||||||||||
    |||||||||||||||||||||||||
    +-----------------------+------------------------+------------------------+-----------------------+
                                        [PM]  physical addresses

[MAIN LOGIC OF CONDITIONED-JUMP 286]

                                        active jmp / passive jmp
            +--------------------------------------------------------------------------------------+
            |                            +-----------------------------------------------------+   |
            |                            |                          +----------------------+   |   |
           a|                           c|                         e|                     f|  d|  b|
    ---------------------      ---------------------      ---------------------      ---------------------
    [PM]   task                [PM]     task              [PM]     task              [PM]    kernel

task execution lines (jmps) are intermittent
memory lines are intermittent
kernel is a function
tasks are functions
M is each memory allocator for physical addresses

    alloc
{bottom edge}

     jmp, jmpback
(1) a->b, a<-b
(2) a->b, c<-d
(3) a->b,
    e->f, e<-f
        , a<-b

(1) never stop at the 2nd level jmp, for serializing kernel (i.e. no task switch):

          -----------       -----------
          | level 1 |       | level 2 |
    -------         ---------         -------->
                     kernel           interrupt

(2) only stop at the 1st level jmp, for parallelizing tasks:

          -----------
          | level 1 |
    -------         -------->
     task            kernel

          -----------------------------
          |          level 1          |
    -------                           -------->
     task                             interrupt

[VARIANT LOGIC OF CONDITIONED-JUMP 386]

    222222222221111111111222222222222222221111111111222222222222222222111111111111111111111111111111111111

                                        active jmp / passive jmp

          +------+                   +------+                    +------+
         a|     b|                  c|     d|                   e|     f|
    ---------------------      ---------------------      ---------------------      ---------------------
    [M]  task + kernel         [M]  task + kernel         [M]   task + kernel        [M]     kernel
                                     ---------------
                                  [PM] physical memory

task execution lines (jmps) are intermittent
memory lines are intermittent
kernel is a function
tasks are functions
M is each memory allocator for relative addresses

    alloc
{top edge}

    alloc
{bottom edge}

    install
{bottom edge}->{top edge}

    install
(1) page->task
(2) page->kernel->kernel->kernel->kernel

     jmp, jmpback
(1) a->b, a<-b
(2) a->b, c<-d
(3) a->b,
    e->f, e<-f
        , a<-b

(1) never stop at the 2nd level jmp, for serializing kernel (i.e. no task switch):

          -----------       -----------
          | level 1 |       | level 2 |
    -------         ---------         -------->
                     kernel           interrupt

(2) only stop at the 1st level jmp, for parallelizing tasks:

          -----------
          | level 1 |
    -------         -------->
     task            kernel

          -----------------------------
          |          level 1          |
    -------                           -------->
     task                             interrupt

[MEMORY ALLOCATOR OF CONDITIONED-JUMP]

Linux Buddy System has fragments, Conditioned-Jump does NOT have fragments

area = [128K]  
subarea = [2^n] !> [4K, 8K, 16K, 32K, 64K]  
struct multiple_page_area  
{  
     unsigned long order;               /* each subarea size = 2^n pages */  
     unsigned long bitfield;            /* is subarea in use ? 128K/4K+ <= 32, sizeof(unsigned long) = 32 */  
     unsigned long * subareas;          /* point to first subarea(page) */  
     struct multiple_page_area * next;  /* point to next struct area */  
};  

area = [4K]  
subarea = [2^n] !> [32, 64, 128, 256, 512, 1K, 2K]  
struct single_page_area  
{  
     unsigned long order;               /* each subarea size = 2^n bytes */  
     unsigned long bitfield[4];         /* is subarea in use ? 4K/32+ <= 128, sizeof(unsigned long) = 32 */  
     unsigned long * subareas;          /* point to first subarea(page) */  
     struct single_page_area * next;    /* point to next struct area */  
};  

[FILE SYSTEM OF CONDITIONED-JUMP]

             buffer1
           -->  |
           -->  |                device1
           -->  |              -->  |
                               -->  |
           -->  |              -->  |
           -->  |
           -->  |
             buffer2

disks = remoras on chessboard model
buffers = firework model : hash table + sorted table

interrupt handlers never use file system, there is no need to mutex accessing buffer tables

never to stop at the 2nd level jmp, which means there is no another user accessing buffer tables with us
which means we are the only one accessing buffer tables, which means there is no task switch when accessing
which means kernel is serialized accessing automatically

[SOURCE CODE OF CONDITIONED-JUMP]

Conditioned-Jump, based on Linux 1.1.0, is not a real world OS
Running Conditioned-Jump inside Qemu/Bochs with GDB is a typical scenario

[PURPOSE OF CONDITIONED-JUMP]

Conditioned-Jump depicts the infrastructure of a historic Linux kernel. Operating system newbies,
hardware hackers or people with too much time can use Conditioned-Jump to learn more about 80386
architecture and the basic framework from which a great OS was built

Happy Hacking



TOP WAYE
2025.1.25
