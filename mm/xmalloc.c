/*
 * [MEMORY ALLOCATOR OF CONDITIONED-JUMP]
 *
 * Linux Buddy System has fragments, Conditioned-Jump does NOT have fragments.
 *
 * +-----------+ +-----------+ +-----------+ +-----------+ +-----------+ +-----------+ +-----------+
 * |     |     | |           | |  |  |  |  | |     |     | |           | |     |     | |  |  |  |  |
 * |     |     | |           | |  |  |  |  | |     |     | |           | |     |     | |  |  |  |  |
 * |-----+-----| |-----------| |--+--+--+--| |-----+-----| |-----------| |-----+-----| |-----+-----|
 * |     |     | |           | |  |  |  |  | |     |     | |           | |     |     | |  |  |  |  |
 * |     |     | |           | |  |  |  |  | |     |     | |           | |     |     | |  |  |  |  |
 * +-----------+ +-----------+ +-----------+ +-----------+ +-----------+ +-----------+ +-----------+
 *     area          area        area(dir)       area          area          area        area(dir)
 *
 * area = [128K]
 * subarea = [2^n] !> [4K, 8K, 16K, 32K, 64K]
 * struct mul_page_area
 * {
 *  unsigned long order;               // each subarea size = 2^n pages
 *  unsigned long bitfield;            // is subarea in use ? 128K/4K+ <= 32, sizeof(unsigned long) = 32
 *  unsigned long * subareas;          // point to first subarea(page)
 *  struct mul_page_area * next;  // point to next struct area
 * };
 *
 * area = [4K]
 * subarea = [2^n] !> [32, 64, 128, 256, 512, 1K, 2K]
 * struct sin_page_area
 * {
 *  unsigned long order;               // each subarea size = 2^n bytes
 *  unsigned long bitfield[4];         // is subarea in use ? 4K/32+ <= 128, sizeof(unsigned long) = 32
 *  unsigned long * subareas;          // point to first subarea(page)
 *  struct sin_page_area * next;    // point to next struct area
 * };
 *
 * TOP WAYE
 * 2024.11.15
*/

#include <linux/mm.h>
#include <linux/xmalloc.h>

extern int try_to_free_page ( void );

static void on_init_raw_mem ( void * page_area, unsigned long size, void * far_page_area, unsigned long far_size );
static void on_alloc_raw_mem ( void * page_area, unsigned long size );
static void on_free_raw_mem ( void * page_area, unsigned long size );
static void on_alloc_one_page_mem ( void * page_area, unsigned long size );
static void on_free_one_page_mem ( void * page_area, unsigned long size );

struct mem_allocator mem_alloc_min =
{
    on_init_raw_mem,
    on_alloc_raw_mem,
    on_free_raw_mem,
    on_alloc_one_page_mem,
    on_free_one_page_mem,
    NULL,
    NULL,
    { 0 },
    /* header of spa_directory, NOT in use, except the 1st item */
    {
        { { 0 }, { 0 }, 0      }, /* pointer to next directory array, UNUSED, free node count */
        { { 0 }, { 0 }, 0      }, /* pointer to free nodes, UNUSED, current free node count */
        { { 0 }, { 0 }, 0      }  /* end of array */
    },
    /* header of mpa_directory, NOT in use, except the 1st item */
    {
        { { 0 }, { 0 }, 0      }, /* pointer to next directory array, UNUSED, free node count */
        { { 0 }, { 0 }, 0      }, /* pointer to free nodes, UNUSED, current free node count */
        { { 0 }, { 0 }, 0      }  /* end of array */
    },
    /* header of hpa_directory, NOT in use, except the 1st item */
    {
        { NULL, NULL, NULL, 0  }, /* pointer to next directory array, pointer to prior directory array, UNUSED , free node count */
        { NULL, NULL, NULL, 0  }, /* pointer to free nodes, UNUSED, UNUSED, current free node count */
        { NULL, NULL, NULL, 0  }  /* pointer to hot nodes, UNUSED, UNUSED, UNUSED */
        /* new nodes go here */
        /* link does NOT need end of array { NULL, NULL, NULL,  0 } */
    },
    /* header of spa_directory, NOT in use, except the 1st item */
    {
        { { 0 }, { 0 }, 0      }, /* pointer to next directory array, UNUSED, free node count */
        { { 0 }, { 0 }, 0      }, /* pointer to free nodes, UNUSED, current free node count */
        { { 0 }, { 0 }, 0      }  /* end of array */
    },
    /* header of mpa_directory, NOT in use, except the 1st item */
    {
        { { 0 }, { 0 }, 0      }, /* pointer to next directory array, UNUSED, free node count */
        { { 0 }, { 0 }, 0      }, /* pointer to free nodes, UNUSED, current free node count */
        { { 0 }, { 0 }, 0      }  /* end of array */
    },
    /* header of hpa_directory, NOT in use, except the 1st item */
    {
        { NULL, NULL, NULL, 0  }, /* pointer to next directory array, pointer to prior directory array, UNUSED , free node count */
        { NULL, NULL, NULL, 0  }, /* pointer to free nodes, UNUSED, UNUSED, current free node count */
        { NULL, NULL, NULL, 0  }  /* pointer to hot nodes, UNUSED, UNUSED, UNUSED */
        /* new nodes go here */
        /* link does NOT need end of array { NULL, NULL, NULL,  0 } */
    }
};

struct mem_allocator mem_alloc_max =
{
    on_init_raw_mem,
    on_alloc_raw_mem,
    on_free_raw_mem,
    on_alloc_one_page_mem,
    on_free_one_page_mem,
    NULL,
    NULL,
    { 0 },
    /* header of spa_directory, NOT in use, except the 1st item */
    {
        { { 0 }, { 0 }, 0      }, /* pointer to next directory array, UNUSED, free node count */
        { { 0 }, { 0 }, 0      }, /* pointer to free nodes, UNUSED, current free node count */
        { { 0 }, { 0 }, 32     }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 64     }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 128    }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 256    }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 512    }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 1024   }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 2048   }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 0      }  /* end of array */
    },
    /* header of mpa_directory, NOT in use, except the 1st item */
    {
        { { 0 }, { 0 }, 0      }, /* pointer to next directory array, UNUSED, free node count */
        { { 0 }, { 0 }, 0      }, /* pointer to free nodes, UNUSED, current free node count */
        { { 0 }, { 0 }, 4096   }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 8192   }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 16384  }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 32768  }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 65536  }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 131072 }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 0      }  /* end of array */
    },
    /* header of hpa_directory, NOT in use, except the 1st item */
    {
        { NULL, NULL, NULL, 0  }, /* pointer to next directory array, pointer to prior directory array, UNUSED , free node count */
        { NULL, NULL, NULL, 0  }, /* pointer to free nodes, UNUSED, UNUSED, current free node count */
        { NULL, NULL, NULL, 0  }  /* pointer to hot nodes, UNUSED, UNUSED, UNUSED */
        /* new nodes go here */
        /* link does NOT need end of array { NULL, NULL, NULL,  0 } */
    },
    /* header of spa_directory, NOT in use, except the 1st item */
    {
        { { 0 }, { 0 }, 0      }, /* pointer to next directory array, UNUSED, free node count */
        { { 0 }, { 0 }, 0      }, /* pointer to free nodes, UNUSED, current free node count */
        { { 0 }, { 0 }, 32     }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 64     }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 128    }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 256    }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 512    }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 1024   }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 2048   }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 0      }  /* end of array */
    },
    /* header of mpa_directory, NOT in use, except the 1st item */
    {
        { { 0 }, { 0 }, 0      }, /* pointer to next directory array, UNUSED, free node count */
        { { 0 }, { 0 }, 0      }, /* pointer to free nodes, UNUSED, current free node count */
        { { 0 }, { 0 }, 4096   }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 8192   }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 16384  }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 32768  }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 65536  }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 131072 }, /* pointer to hot nodes */
        { { 0 }, { 0 }, 0      }  /* end of array */
    },
    /* header of hpa_directory, NOT in use, except the 1st item */
    {
        { NULL, NULL, NULL, 0  }, /* pointer to next directory array, pointer to prior directory array, UNUSED , free node count */
        { NULL, NULL, NULL, 0  }, /* pointer to free nodes, UNUSED, UNUSED, current free node count */
        { NULL, NULL, NULL, 0  }  /* pointer to hot nodes, UNUSED, UNUSED, UNUSED */
        /* new nodes go here */
        /* link does NOT need end of array { NULL, NULL, NULL,  0 } */
    }
};

unsigned long xfree2 ( void * address )
{
    return hit_page_dir_area ( & mem_alloc_max, address );
}

void xinitialize ( void * area, unsigned long size, void * far_area, unsigned long far_size )
{
    init_mem_allocator ( & mem_alloc_max, area, size, far_area, far_size, XALLOC_MINIMAL_SYSTEM );
}

unsigned long xcalculate ( unsigned long size )
{
    unsigned long calc_size;

    calc_size = MIN_SUBAREA_SIZE;

    while ( calc_size < size )
        calc_size += calc_size < FIXED_PAGE_AREA_SIZE ? calc_size : FIXED_PAGE_AREA_SIZE;

    return calc_size;
}

void * xalloc ( unsigned long * size, int flag )
{
    void * subarea;

    /*
     *  1. xalloc flag = FAR: -> FAR MEMORY -> NEAR MEMORY -> RETURN
     *  2. xalloc flag = NEAR: -> NEAR MEMORY -> RETURN
     *  3. xalloc flag = URGENT: -> NEAR MEMORY -> URGENT NEAR MEMORY -> RETURN
     */

    if ( xcalculate ( * size ) > ONE_PAGE_AREA_SIZE / 2 ) /*  NOT ONE_PAGE_AREA_SIZE */
    {
        if ( flag == XALLOC_FAR_MEM )
            if ( subarea = seek_mul_page_dir_area ( & mem_alloc_max, size, XALLOC_FAR_MEM ) )
                return subarea;

        if ( subarea = seek_mul_page_dir_area ( & mem_alloc_max, size, XALLOC_NEAR_MEM ) )
            return subarea;

        if ( flag == XALLOC_URGENT_NEAR_MEM )
            if ( subarea = seek_mul_page_dir_area ( & mem_alloc_max, size, XALLOC_URGENT_NEAR_MEM ) )
                return subarea;
    }
    else
    {
        if ( flag == XALLOC_FAR_MEM )
            if ( subarea = seek_sin_page_dir_area ( & mem_alloc_max, size, XALLOC_FAR_MEM ) )
                return subarea;

        if ( subarea = seek_sin_page_dir_area ( & mem_alloc_max, size, XALLOC_NEAR_MEM ) )
            return subarea;

        if ( flag == XALLOC_URGENT_NEAR_MEM )
            if ( subarea = seek_sin_page_dir_area ( & mem_alloc_max, size, XALLOC_URGENT_NEAR_MEM ) )
                return subarea;
    }

    return NULL;
}

void * xalloc_o ( unsigned long order, int flag )
{
    unsigned long size;

    size = ONE_PAGE_AREA_SIZE << order;
    return xalloc ( & size, flag );
}

unsigned long xfree ( void * address )
{
    unsigned long ret;

    /*
     *  -> NEAR MEMORY -> FAR MEMORY -> RETURN
     *  'NEAR' AUTO GOES INTO URGENT NEAR MEMORY HERE, IF ANY
     */

    if ( ret = go_sin_page_dir_area ( & mem_alloc_max, address, XALLOC_NEAR_MEM ) )
        return ret;

    if ( ret = go_sin_page_dir_area ( & mem_alloc_max, address, XALLOC_FAR_MEM ) )
        return ret;

    if ( ret = go_mul_page_dir_area ( & mem_alloc_max, address, XALLOC_NEAR_MEM ) )
        return ret;

    if ( ret = go_mul_page_dir_area ( & mem_alloc_max, address, XALLOC_FAR_MEM ) )
        return ret;

    return 0;
}

/*
 * void * p [ 10 ];
 * void test ( void * area, unsigned long len = 40 * 1024 * 1024 )
 * {
 *     xinitialize ( area, len );
 *     for ( i = 0; i < 10; i ++ ) p [ i ] = xalloc ( & size = 262145, 131071, 4097, 4095, 100 );
 *     for ( i = 0; i < 10; i ++ ) size = xfree2 ( p [ i ] );
 *     for ( i = 0; i < 10; i ++ ) p [ i ] = xalloc_o ( 7 );
 *     for ( i = 0; i < 10; i ++ ) size = xfree2 ( p [ i ] );
 * }
 */

static void on_init_raw_mem ( void * page_area, unsigned long size, void * far_page_area, unsigned long far_size )
{
    nr_free_pages = size >> PAGE_SHIFT; /* size/PAGE_SIZE */
    nr_free_pages += far_size >> PAGE_SHIFT; /* far_size/PAGE_SIZE */
}

static void on_alloc_raw_mem ( void * page_area, unsigned long size )
{
    nr_free_pages -= size >> PAGE_SHIFT; /* size/PAGE_SIZE */
}

static void on_free_raw_mem ( void * page_area, unsigned long size )
{
    nr_free_pages += size >> PAGE_SHIFT; /* size/PAGE_SIZE */
}

static void on_alloc_one_page_mem ( void * page_area, unsigned long size )
{
    /* NOT nr_free_pages -1 */
    mem_map [ MAP_NR ( (unsigned long) page_area ) ] ++;
}

static void on_free_one_page_mem ( void * page_area, unsigned long size )
{
    /* NOT nr_free_pages +1 */
    mem_map [ MAP_NR ( (unsigned long) page_area ) ] --;
}

/*
 * Free_page() adds the page to the free lists. This is optimized for
 * fast normal cases (no error jumps taken normally).
 *
 * The way to optimize jumps for gcc-2.2.2 is to:
 *  - select the "normal" case and put it inside the if () { XXX }
 *  - no else-statements if you can avoid them
 *
 * With the above two rules, you get a straight-line execution path
 * for the normal case, giving better asm-code.
 */

void free_pages ( unsigned long addr, unsigned long order )
{
    unsigned long flag;
    unsigned short * map;

    if ( addr >= high_memory ) /* NOT > ; max high_memory == 1G */
        return;

    map = mem_map + MAP_NR ( addr );
    if ( ! * map )
    {
        printk ( "Trying to free free memory (%08lx): memory probabably corrupted\n", addr );
        return;
    }
    
    if ( * map & MAP_PAGE_RESERVED )
        return;

    if ( * map == 1 ) 
    {
        save_flags ( flag );
        cli ( );
        
        if ( ! xfree2 ( ( void * ) addr ) )
        {
            restore_flags ( flag );
            return;
        }

        restore_flags ( flag );
    }

    ( * map ) --;
}

unsigned long __get_free_pages(int priority, unsigned long order)
{
    unsigned long flags;
    void * addr;

    if (intr_count && priority != GFP_ATOMIC)
    {
        static int count = 0;
        if (++count < 5)
        {
            printk("gfp called nonatomically from interrupt %08lx\n", ((unsigned long *)&priority)[-1]);
            priority = GFP_ATOMIC;
        }
    }

    save_flags ( flags );
    cli( );

    if ( addr = xalloc_o ( order, priority ) )
    {
        restore_flags ( flags );

        mem_map [ MAP_NR ( ( unsigned long ) addr ) ] ++;

        return ( unsigned long ) addr;
    }
    
    restore_flags ( flags );
    
    /* may NOT free a granularity, there is no use to repeat, just delay to return */
    if ( priority == GFP_KERNEL || priority == GFP_USER )
        try_to_free_page ( ); 

    return 0;
}

/*
 * Show free area list (used inside shift_scroll-lock stuff)
 * We also calculate the percentage fragmentation. We do this by counting the
 * memory on each free list with the exception of the first item on the list.
 */
void show_free_areas ( void )
{
    printk ( "Free pages: %6dkB\n", nr_free_pages << ( PAGE_SHIFT - 10 ) );
}

void * kmalloc ( size_t size, int priority )
{
    unsigned long flags;
    void * addr;

    if (intr_count && priority != GFP_ATOMIC)
    {
        static int count = 0;
        if (++count < 5)
        {
            printk("gfp called nonatomically from interrupt %08lx\n", ((unsigned long *)&priority)[-1]);
            priority = GFP_ATOMIC;
        }
    }

    save_flags ( flags );
    cli( );

    if ( addr = xalloc ( & size, priority ) )
    {
        restore_flags ( flags );
            
        if ( size >= PAGE_SIZE ) /* when < PAGE_SIZE,  +1 on_alloc_one_page_mem */
            mem_map [ MAP_NR ( ( unsigned long ) addr ) ] ++;
            
        return addr;
    }

    restore_flags ( flags );
    
    /* may NOT free a granularity, there is no use to repeat, just delay to return */
    if ( priority == GFP_KERNEL || priority == GFP_USER )
        try_to_free_page ( ); 

    return NULL;
}

void kfree_s ( void * addr, int size )
{
    unsigned long flag;
    unsigned short * map;

    if ( ( unsigned long ) addr >= high_memory ) /* NOT > ; max high_memory == 1G */
        return;

    map = mem_map + MAP_NR ( ( unsigned long ) addr );
    if ( ! * map )
    {
        printk( "Trying to free free memory (%08lx): memory probabably corrupted\n", ( unsigned long ) addr );
        return;
    }
    
    if ( * map & MAP_PAGE_RESERVED )
        return;
    
    save_flags ( flag );
    cli ( );

    /* always * map == 1, because kmalloc is private allocation */
    if ( xfree2 ( addr ) < PAGE_SIZE ) /* when < PAGE_SIZE,  -1 on_free_one_page_mem */
    {
        restore_flags ( flag );
        return;
    }

    restore_flags ( flag );
    
    ( * map ) --;
}
