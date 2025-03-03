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

#include <linux/xmcore.h>
#include <linux/string.h>

void init_raw_area ( struct mem_allocator * mem_alloc, void ** area, unsigned long * size )
{
    int bit_count;
    unsigned long subarea_size;
    unsigned char * this_area, * aligned_area;
    struct raw_area * area_raw;

    this_area = ( unsigned char * ) * area;
    aligned_area = ( unsigned char * ) ( ( unsigned long ) * area & FIXED_PAGE_AREA_MASK ); /* NOT ONE_PAGE_AREA_MASK */

    if ( aligned_area < this_area )
    {
        /* align to one fixed page area border */
        if ( * size >= FIXED_PAGE_AREA_SIZE )
        {
            aligned_area += FIXED_PAGE_AREA_SIZE;
            * size -= aligned_area - this_area;
        }
        else /* do nothing here, nothing will happen when allocating memory later */
            aligned_area = this_area;
    }

    subarea_size = FIXED_PAGE_AREA_SIZE;
    bit_count = * size / subarea_size;
    area_raw = & mem_alloc -> raw_memory;
    memset ( area_raw, 0, sizeof ( struct raw_area ) );
    area_raw -> bit_count = bit_count;
    area_raw -> subarea_set = aligned_area;
    * area = aligned_area; /* return value */
    * size = subarea_size * bit_count; /* return value */
}

void init_far_raw_area ( struct mem_allocator * mem_alloc, void ** area, unsigned long * size )
{
    int bit_count;
    unsigned long subarea_size, last_area_size, delta;
    unsigned char * this_area, * aligned_area, * last_area;
    struct raw_area * area_raw;

    this_area = ( unsigned char * ) * area;
    aligned_area = ( unsigned char * ) ( ( unsigned long ) * area & FIXED_PAGE_AREA_MASK ); /* NOT ONE_PAGE_AREA_MASK */

    if ( aligned_area < this_area )
    {
        /* align to one fixed page area border */
        if ( * size >= FIXED_PAGE_AREA_SIZE )
        {
            aligned_area += FIXED_PAGE_AREA_SIZE;
            * size -= aligned_area - this_area;
        }
        else /* do nothing here, nothing will happen when allocating memory later */
            aligned_area = this_area;
    }

    subarea_size = FIXED_PAGE_AREA_SIZE;
    area_raw = & mem_alloc -> raw_memory;
    last_area = ( unsigned char * ) area_raw -> subarea_set;
    last_area_size = subarea_size * area_raw -> bit_count;

    if ( aligned_area < last_area + last_area_size )
    {
        delta = last_area + last_area_size - aligned_area;
        if ( * size >= delta )
             * size -= delta;
        else
            * size = 0;
        aligned_area = last_area + last_area_size;
    }
    bit_count = * size / subarea_size;
    
    /* bits followed by far_bits closely, though two memory may be separated far away from each other */
    area_raw -> far_bit_count = bit_count;
    area_raw -> far_subarea_set = aligned_area;
    * area = aligned_area; /* return value */
    * size = subarea_size * bit_count; /* return value */
}

int go_raw_area ( struct mem_allocator * mem_alloc, void * page_area, unsigned long size )
{
    int index, result_index, byte_index, max_bit_count, min_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size, delta;
    unsigned char * this_subarea_set, * this_page_area, * base;
    struct raw_area * area_raw;

    /* do NOT need to adjust the input parameter, if not fixed page area aligned, the algorithm fail */
    if ( ( void * ) ( ( unsigned long ) page_area & FIXED_PAGE_AREA_MASK ) < page_area )
        return 0;

    this_page_area = ( unsigned char * ) page_area;
    subarea_size = FIXED_PAGE_AREA_SIZE;
    min_bit_count = size / subarea_size + ( size % subarea_size > 0 );
    area_raw = & mem_alloc -> raw_memory;
    max_bit_count = area_raw -> bit_count;
    base = this_subarea_set = ( unsigned char * ) area_raw -> subarea_set;

    delta = this_page_area - base; /* already aligned */
    delta /= subarea_size;

    result_index = -1; /* init */
    for ( index = 0 + delta; index < max_bit_count; index ++ )
    {
        /* quitting must be here, do NOT move this line */
        if ( result_index != -1 )
        { 
            if ( result_index + min_bit_count == index )
                break;
        }
        else /* result_index == -1 */
        {
            if ( this_page_area != this_subarea_set + subarea_size * index )
                continue;
            result_index = index;
        }

        mask = 0x1 << index % 8;
        byte_index = index / 8;
        bit_field = area_raw -> bit_field [ byte_index ];
        if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
            return 0;
    }
    if ( result_index == -1 )
        return 0;
    if ( result_index + min_bit_count > index ) /* comparison been truncated */
        return 0;

    for ( index = result_index; index < result_index + min_bit_count; index ++ )
    {
        mask = 0x1 << index % 8;
        byte_index = index / 8;
        area_raw -> bit_field [ byte_index ] &= ~mask; /* clear bit */
    }

    return 1;
}

int go_far_raw_area ( struct mem_allocator * mem_alloc, void * page_area, unsigned long size )
{
    int index, index0, result_index, byte_index, max_bit_count, min_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size, delta;
    unsigned char * this_subarea_set, * this_page_area, * base;
    struct raw_area * area_raw;

    /* do NOT need to adjust the input parameter, if not fixed page area aligned, the algorithm fail */
    if ( ( void * ) ( ( unsigned long ) page_area & FIXED_PAGE_AREA_MASK ) < page_area )
        return 0;

    this_page_area = ( unsigned char * ) page_area;
    subarea_size = FIXED_PAGE_AREA_SIZE;
    min_bit_count = size / subarea_size + ( size % subarea_size > 0 );
    area_raw = & mem_alloc -> raw_memory;
    max_bit_count = area_raw -> far_bit_count;
    index0 = area_raw -> bit_count; /* bits followed by far_bits closely, though two memory may be separated far away from each other */
    base = this_subarea_set = ( unsigned char * ) area_raw -> far_subarea_set;

    delta = this_page_area - base; /* already aligned */
    delta /= subarea_size;

    result_index = -1; /* init */
    for ( index = index0 + delta; index < index0 + max_bit_count; index ++ )
    {
        /* quitting must be here, do NOT move this line */
        if ( result_index != -1 )
        {
            if ( result_index + min_bit_count == index )
                break;
        }
        else /* result_index == -1 */
        {
            if ( this_page_area != this_subarea_set + subarea_size * ( index -  index0 ) ) /* patch!!! */
                continue;
            result_index = index;
        }

        mask = 0x1 << index % 8;
        byte_index = index / 8;
        bit_field = area_raw -> bit_field [ byte_index ];
        if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
            return 0;
    }
    if ( result_index == -1 )
        return 0;
    if ( result_index + min_bit_count > index ) /* comparison been truncated */
        return 0;

    for ( index = result_index; index < result_index + min_bit_count; index ++ )
    {
        mask = 0x1 << index % 8;
        byte_index = index / 8;
        area_raw -> bit_field [ byte_index ] &= ~mask; /* clear bit */
    }

    return 1;
}

void * seek_urgent_raw_area ( struct mem_allocator * mem_alloc, unsigned long * size )
{
    int index, result_index, byte_index, max_bit_count, min_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size, delta;
    unsigned char * this_subarea_set;
    struct raw_area * area_raw;

    /* if not return, a new memory will be allocated, though there is no harm */
    if ( * size == 0 )
        return NULL;

    subarea_size = FIXED_PAGE_AREA_SIZE;
    min_bit_count = * size / subarea_size + ( * size % subarea_size > 0 );
    area_raw = & mem_alloc -> raw_memory;
    this_subarea_set = ( unsigned char * ) area_raw -> subarea_set;
    max_bit_count = * ( area_raw -> delta + 0 );
    max_bit_count += * ( area_raw -> delta + 1 ) ;

    delta = * ( area_raw -> delta + 0 );

    result_index = -1; /* init */
    for ( index = 0 + delta; index < max_bit_count; index ++ )
    {
        /* quitting must be here, do NOT move this line */
        if ( result_index != -1 && result_index + min_bit_count ==  index )
                break;

        mask = 0x1 << index % 8;
        byte_index = index / 8;
        bit_field = area_raw -> bit_field [ byte_index ];
        if ( bit_field & mask )
        {
            result_index = -1; /* reinit */
            continue;
        }
        
        if ( result_index == -1 )
            result_index = index;
     }
    if ( result_index == -1 )
        return NULL;
    if ( result_index + min_bit_count > index ) /* comparison been truncated */
        return NULL;

    for ( index = result_index; index < result_index + min_bit_count; index ++ )
    {
        mask = 0x1 << index % 8;
        byte_index = index / 8;
        area_raw -> bit_field [ byte_index ] |= mask; /* set bit */
    }

    * size = subarea_size * min_bit_count; /* output result */
    return this_subarea_set + subarea_size * result_index;
}

void * seek_raw_area ( struct mem_allocator * mem_alloc, unsigned long * size )
{
    int index, result_index, byte_index, max_bit_count, min_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size, delta;
    unsigned char * this_subarea_set;
    struct raw_area * area_raw;

    /* if not return, a new memory will be allocated, though there is no harm */
    if ( * size == 0 )
        return NULL;

    subarea_size = FIXED_PAGE_AREA_SIZE;
    min_bit_count = * size / subarea_size + ( * size % subarea_size > 0 );
    area_raw = & mem_alloc -> raw_memory;
    this_subarea_set = ( unsigned char * ) area_raw -> subarea_set;
    max_bit_count = area_raw -> bit_count;

    delta = * ( area_raw -> delta + 0 );
    delta += * ( area_raw -> delta + 1 );

    result_index = -1; /* init */
    for ( index = 0 + delta; index < max_bit_count; index ++ )
    {
        /* quitting must be here, do NOT move this line */
        if ( result_index != -1 && result_index + min_bit_count ==  index )
                break;

        mask = 0x1 << index % 8;
        byte_index = index / 8;
        bit_field = area_raw -> bit_field [ byte_index ];
        if ( bit_field & mask )
        {
            result_index = -1; /* reinit */
            continue;
        }
        
        if ( result_index == -1 )
            result_index = index;
     }
    if ( result_index == -1 )
        return NULL;
    if ( result_index + min_bit_count > index ) /* comparison been truncated */
        return NULL;

    for ( index = result_index; index < result_index + min_bit_count; index ++ )
    {
        mask = 0x1 << index % 8;
        byte_index = index / 8;
        area_raw -> bit_field [ byte_index ] |= mask; /* set bit */
    }

    * size = subarea_size * min_bit_count; /* output result */
    return this_subarea_set + subarea_size * result_index;
}

void * seek_far_raw_area ( struct mem_allocator * mem_alloc, unsigned long * size )
{
    int index, index0, result_index, byte_index, max_bit_count, min_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size;
    unsigned char * this_subarea_set;
    struct raw_area * area_raw;

    /* if not return, a new memory will be allocated, though there is no harm */
    if ( * size == 0 )
        return NULL;

    subarea_size = FIXED_PAGE_AREA_SIZE;
    min_bit_count = * size / subarea_size + ( * size % subarea_size > 0 );
    area_raw = & mem_alloc -> raw_memory;
    this_subarea_set = ( unsigned char * ) area_raw -> far_subarea_set;
    max_bit_count = area_raw -> far_bit_count;
    index0 = area_raw -> bit_count; /* bits followed by far_bits closely, though two memory may be separated far away from each other */

    result_index = -1; /* init */
    for ( index = index0; index < index0 + max_bit_count; index ++ )
    {
        /* quitting must be here, do NOT move this line */
        if ( result_index != -1 && result_index + min_bit_count == index )
            break;

        mask = 0x1 << index % 8;
        byte_index = index / 8;
        bit_field = area_raw -> bit_field [ byte_index ];
        if ( bit_field & mask )
        {
            result_index = -1; /* reinit */
            continue;
        }

        if ( result_index == -1 )
            result_index = index;
    }
    if ( result_index == -1 )
        return NULL;
    if ( result_index + min_bit_count > index ) /* comparison been truncated */
        return NULL;

    for ( index = result_index; index < result_index + min_bit_count; index ++ )
    {
        mask = 0x1 << index % 8;
        byte_index = index / 8;
        area_raw -> bit_field [ byte_index ] |= mask; /* set bit */
    }

    * size = subarea_size * min_bit_count; /* output result */
    return this_subarea_set + subarea_size * ( result_index - index0 ); /* patch!!! */
}

void * alloc_init_raw_mem ( struct mem_allocator * mem_alloc, unsigned long * size )
{
    void * page_area;

    page_area = seek_raw_area ( mem_alloc, size );
    if ( page_area )
        mem_alloc -> on_alloc_raw_mem ( page_area, * size );
    return page_area;
}

/* write hash table */
void * alloc_raw_mem ( struct mem_allocator * mem_alloc, struct hpa_directory * hpa_entry, unsigned long * size, int flag )
{
    unsigned long delta;
    unsigned char * page_area, * base, * far_base;

    if ( flag == XALLOC_FAR_MEM )
    {
        page_area = ( unsigned char * ) seek_far_raw_area ( mem_alloc, size );
        if ( page_area )
        {
            far_base = ( unsigned char * ) mem_alloc -> raw_memory . far_subarea_set;
            delta = page_area - far_base; /* already aligned */
            delta /= ONE_PAGE_AREA_SIZE;
            ( mem_alloc -> far_hash_table + delta ) -> hpa_entry = hpa_entry;

            mem_alloc -> on_alloc_raw_mem ( page_area, * size );
        }
        return page_area;
    }

    if ( flag == XALLOC_URGENT_NEAR_MEM )
        page_area = ( unsigned char * ) seek_urgent_raw_area ( mem_alloc, size );
    else
        page_area = ( unsigned char * ) seek_raw_area ( mem_alloc, size );
    
    if ( page_area )
    {
        base = ( unsigned char * ) mem_alloc -> raw_memory . subarea_set;
        delta = page_area - base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        ( mem_alloc -> hash_table + delta ) -> hpa_entry = hpa_entry;

        mem_alloc -> on_alloc_raw_mem ( page_area, * size );
    }
    return page_area;
}

void * alloc_raw_granularity ( struct mem_allocator * mem_alloc, struct mul_page_area * area_mpa, int flag )
{
    unsigned long size, delta;
    unsigned char * page_area, * base, * far_base;

    size = FIXED_PAGE_AREA_SIZE; /* NOT ONE_PAGE_AREA_SIZE */

    if ( flag == XALLOC_FAR_MEM )
    {
        page_area = ( unsigned char * ) seek_far_raw_area ( mem_alloc, & size );
        if ( page_area )
        {
            far_base = ( unsigned char * ) mem_alloc -> raw_memory . far_subarea_set;
            delta = page_area - far_base; /* already aligned */
            delta /= ONE_PAGE_AREA_SIZE;
            ( mem_alloc -> far_hash_table + delta ) -> mpa = area_mpa;

            mem_alloc -> on_alloc_raw_mem ( page_area, size );
        }
        return page_area;
    }

    if ( flag == XALLOC_URGENT_NEAR_MEM )
        page_area = ( unsigned char * ) seek_urgent_raw_area ( mem_alloc, & size );
    else
        page_area = ( unsigned char * ) seek_raw_area ( mem_alloc, & size );

    if ( page_area )
    {
        base = ( unsigned char * ) mem_alloc -> raw_memory . subarea_set;
        delta = page_area - base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        ( mem_alloc -> hash_table + delta ) -> mpa = area_mpa;

        mem_alloc -> on_alloc_raw_mem ( page_area, size );
    }
    return page_area;
}

int free_raw_mem ( struct mem_allocator * mem_alloc, void * page_area, unsigned long size )
{
    int ret;
    unsigned long delta;
    unsigned char * this_page_area, * base, * far_base;

    this_page_area = ( unsigned char * ) page_area; /* already aligned */
    /* internal call, parameters are always correct */
    far_base = ( unsigned char * ) mem_alloc -> raw_memory . far_subarea_set;
    if ( ( unsigned char * ) page_area < far_base )
    {
        base = ( unsigned char * ) mem_alloc -> raw_memory . subarea_set;
        delta = this_page_area - base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        ( mem_alloc -> hash_table + delta ) -> hpa_entry = NULL; /* reinit */

        ret = go_raw_area ( mem_alloc, page_area, size );
    }
    else
    {
        delta = this_page_area - far_base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        ( mem_alloc -> far_hash_table + delta ) -> hpa_entry = NULL; /* reinit */

        ret = go_far_raw_area ( mem_alloc, page_area, size );
    }

    /* internal call, return values are always correct */
    mem_alloc -> on_free_raw_mem ( page_area, size );
    return ret;
}

int free_raw_granularity ( struct mem_allocator * mem_alloc, void * page_area )
{
    int ret;
    unsigned long delta;
    unsigned char * this_page_area, * base, * far_base;

    this_page_area = ( unsigned char * ) page_area; /* already aligned */
    /* internal call, parameters are always correct */
    far_base = ( unsigned char * ) mem_alloc -> raw_memory . far_subarea_set;
    if ( ( unsigned char * ) page_area < far_base )
    {
        base = ( unsigned char * ) mem_alloc -> raw_memory . subarea_set;
        delta = this_page_area - base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        ( mem_alloc -> hash_table + delta ) -> mpa = NULL; /* reinit */

        ret = go_raw_area ( mem_alloc, page_area, FIXED_PAGE_AREA_SIZE ); /* NOT ONE_PAGE_AREA_SIZE */
    }
    else
    {
        delta = this_page_area - far_base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        ( mem_alloc -> far_hash_table + delta ) -> mpa = NULL; /* reinit */

        ret = go_far_raw_area ( mem_alloc, page_area, FIXED_PAGE_AREA_SIZE ); /* NOT ONE_PAGE_AREA_SIZE */
    }
    /* internal call, return values are always correct */
    mem_alloc -> on_free_raw_mem ( page_area, FIXED_PAGE_AREA_SIZE );
    return ret;
}

void init_page_context ( struct mem_allocator * mem_alloc, unsigned long * size, unsigned long far_size )
{
    int count;
    unsigned long page_context_size, far_page_context_size, total;
    void * area_set;

    count = page_context_size = * size / ONE_PAGE_AREA_SIZE; /* 'size' is already aligned */
    page_context_size *= sizeof ( struct page_context );
    far_page_context_size = far_size / ONE_PAGE_AREA_SIZE; /* 'far_size' is already aligned */
    far_page_context_size *= sizeof ( struct page_context );

    total = page_context_size + far_page_context_size;
    area_set = alloc_init_raw_mem ( mem_alloc, & total );
    total = area_set == NULL ? 0 : total; /* patch!!! */
    memset ( area_set, 0, total );
    mem_alloc -> hash_table = ( struct page_context * ) area_set;
    mem_alloc -> far_hash_table = ( struct page_context * ) area_set + count;
    * ( mem_alloc -> raw_memory . delta + 0 ) = total / FIXED_PAGE_AREA_SIZE; /* already aligned */
    * size -= total; /* patch!!! */ 
}

void init_urgent_context ( struct mem_allocator * mem_alloc, unsigned long * size, unsigned long urgent_size )
{
    int bit_count;
    unsigned long subarea_size;

    /* 'urgent_size' may be a malicious value from user input */
    if ( urgent_size > * size )
        urgent_size = * size;

    subarea_size = FIXED_PAGE_AREA_SIZE;
    bit_count = urgent_size / subarea_size + ( urgent_size % subarea_size > 0 );

    * ( mem_alloc -> raw_memory . delta + 1 ) = bit_count;
    * size -= subarea_size * bit_count; /* patch!!! */
}

void init_mem_allocator ( struct mem_allocator * mem_alloc, void * area, unsigned long size, void * far_area, unsigned long far_size, unsigned long urgent_size )
{
    struct sin_page_area * header_header_spa;
    struct spa_directory * spa_dir;
    struct mul_page_area * header_header_mpa;
    struct mpa_directory * mpa_dir;
    struct hpa_directory * header_header_hpa_entry;
    struct hpa_directory * hpa_dir;

    /* patch!!! set up dul link */
    spa_dir = mem_alloc -> spa_dir_header;
    header_header_spa = & ( spa_dir + 0 ) -> chain;
    header_header_spa -> next = header_header_spa;
    header_header_spa -> prior = header_header_spa;
    /* patch!!! set up dul link */
    mpa_dir = mem_alloc -> mpa_dir_header;
    header_header_mpa = & ( mpa_dir + 0 ) -> chain;
    header_header_mpa -> next = header_header_mpa;
    header_header_mpa -> prior = header_header_mpa;
    /* patch!!! set up dul link */
    hpa_dir = mem_alloc -> hpa_dir_header;
    header_header_hpa_entry = hpa_dir + 0;
    header_header_hpa_entry -> next = header_header_hpa_entry;
    header_header_hpa_entry -> prior = header_header_hpa_entry;

    /* patch!!! set up dul link */
    spa_dir = mem_alloc -> far_spa_dir_header;
    header_header_spa = & ( spa_dir + 0 ) -> chain;
    header_header_spa -> next = header_header_spa;
    header_header_spa -> prior = header_header_spa;
    /* patch!!! set up dul link */
    mpa_dir = mem_alloc -> far_mpa_dir_header;
    header_header_mpa = & ( mpa_dir + 0 ) -> chain;
    header_header_mpa -> next = header_header_mpa;
    header_header_mpa -> prior = header_header_mpa;
    /* patch!!! set up dul link */
    hpa_dir = mem_alloc -> far_hpa_dir_header;
    header_header_hpa_entry = hpa_dir + 0;
    header_header_hpa_entry -> next = header_header_hpa_entry;
    header_header_hpa_entry -> prior = header_header_hpa_entry;

    init_raw_area ( mem_alloc, & area, & size );
    init_far_raw_area ( mem_alloc, & far_area, & far_size );
    mem_alloc -> on_init_raw_mem ( area, size, far_area, far_size );
    init_page_context ( mem_alloc, & size, far_size );
    init_urgent_context ( mem_alloc, & size, urgent_size );
}

/*
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
256K area for one of directory areas, NOT 256K area for one of data areas
*/
void init_huge_page_dir_area ( struct mem_allocator * mem_alloc, struct hpa_directory * last_area_hpa_dir, void * area, int flag )
{
    int index, count;
    unsigned long area_size, dir_size, hpa_entry_size;
    unsigned long position;
    unsigned char * this_area;
    struct hpa_directory * hpa_entry, * header_hpa_entry, * header_last_hpa_entry, * free_header_hpa_entry;
    struct hpa_directory * hpa_dir;

    hpa_dir = flag == XALLOC_FAR_MEM ? mem_alloc -> far_hpa_dir_header : mem_alloc -> hpa_dir_header;
    dir_size = sizeof ( struct hpa_directory ) * HPA_DIR_ARRAY_LEN; /* 3 items */
    hpa_entry_size = sizeof ( struct hpa_directory );
    area_size = FIXED_PAGE_AREA_SIZE;
    position = 0;

    /* copy array of hpa_directory to this area */
    this_area = ( unsigned char * ) area;
    memcpy ( this_area, hpa_dir, dir_size );
    this_area += dir_size;
    position += dir_size;

    /* patch!!! set up dul link */
    hpa_dir = ( struct hpa_directory * ) area;
    for ( index = 0; index < HPA_DIR_ARRAY_LEN; index ++ )
    {
        header_hpa_entry = hpa_dir + index;
        header_hpa_entry -> next = header_hpa_entry;
        header_hpa_entry -> prior = header_hpa_entry;
    }

    /* set up the fourth hpa_directory struct */
    hpa_entry = ( struct hpa_directory * ) this_area;
    memset ( hpa_entry, 0, hpa_entry_size );
    /* and be linked to mpa_directory */
    free_header_hpa_entry = hpa_dir + 1;
    hpa_entry -> next = free_header_hpa_entry;
    hpa_entry -> prior = free_header_hpa_entry;
    free_header_hpa_entry -> prior = hpa_entry;
    free_header_hpa_entry -> next = hpa_entry;
    /* finish setting up the fourth hpa_directory struct */
    this_area += hpa_entry_size;
    position += hpa_entry_size;
    count = 1; /* NOT 4 */

    /* setup the rest of mul_page_area structs */
    while ( position + hpa_entry_size <= area_size )
    {
        hpa_entry = ( struct hpa_directory * ) this_area;
        memset ( hpa_entry, 0, hpa_entry_size );
        /* append into free list */
        hpa_entry -> next = free_header_hpa_entry;
        hpa_entry -> prior = free_header_hpa_entry -> prior;
        free_header_hpa_entry -> prior -> next = hpa_entry;
        free_header_hpa_entry -> prior = hpa_entry;
        /* finish setting up the current node */
        this_area += hpa_entry_size;
        position += hpa_entry_size;
        count ++;
    }

    /* free node count */
    ( hpa_dir + 0 ) -> size = count;
    ( hpa_dir + 1 ) -> size = count;

    /* link this area to last area (last_area_mpa_dir) out there */
    header_hpa_entry = hpa_dir + 0;
    header_last_hpa_entry = last_area_hpa_dir + 0;
    header_hpa_entry -> next = header_last_hpa_entry -> next;
    header_hpa_entry -> prior = header_last_hpa_entry;
    header_last_hpa_entry -> next -> prior = header_hpa_entry;
    header_last_hpa_entry -> next = header_hpa_entry;
}

unsigned long go_huge_page_dir_area ( struct mem_allocator * mem_alloc, void * page_area, int flag )
{
    /* patch!!! may be a malicious 'size' from user input, so do NOT use 'size' as user input */

    unsigned long area_set_size;
    unsigned char * this_area_set, * this_page_area;
    void * area;
    struct hpa_directory * hpa_entry, * header_hpa_entry,
                         * header_header_hpa_entry, * free_header_hpa_entry;
    struct hpa_directory * hpa_dir;

    /* do NOT need to adjust the input parameter, if not page aligned, the algorithm fail */
    if ( ( void * ) ( ( unsigned long ) page_area & FIXED_PAGE_AREA_MASK ) < page_area )
        return 0;

    this_page_area = ( unsigned char * ) page_area;
    hpa_dir = flag == XALLOC_FAR_MEM ? mem_alloc -> far_hpa_dir_header : mem_alloc -> hpa_dir_header;
    header_header_hpa_entry = hpa_dir + 0;
    while ( 1 )
    {
        /* jump to next directory area */
        header_hpa_entry = hpa_dir + 0;
        area = header_hpa_entry = header_hpa_entry -> next;
        if ( header_hpa_entry == header_header_hpa_entry ) /* if ( ! area ) */
            break;

        /* this new directory area is where we just jumped into */
        hpa_dir = ( struct hpa_directory * ) area;
        header_hpa_entry = hpa_dir + XPA_DIR_START_INDEX;
        hpa_entry = header_hpa_entry -> next;
        while ( hpa_entry != header_hpa_entry )
        {
            this_area_set = ( unsigned char * ) hpa_entry -> area_set;

            if ( this_page_area == this_area_set )
            {
                area_set_size = hpa_entry -> size;
                /* 2. furthermore: remove hot node and data memory area this hot node holds */
                /* remove raw memory */
                free_raw_mem ( mem_alloc, this_area_set, area_set_size ); /* operate memory from the internal, not the external users, never fail */
                /* link out hot node */
                hpa_entry -> prior -> next = hpa_entry -> next;
                hpa_entry -> next -> prior = hpa_entry -> prior;
                /* link into free list */
                hpa_entry -> size = 0; /* reinit */
                hpa_entry -> area_set = NULL; /* reinit */
                free_header_hpa_entry = hpa_dir + 1;
                hpa_entry -> next = free_header_hpa_entry -> next;
                hpa_entry -> prior = free_header_hpa_entry;
                free_header_hpa_entry -> next -> prior = hpa_entry;
                free_header_hpa_entry -> next = hpa_entry;

                /* 3. furthermore: remove directory memory area */
                if ( ++ ( hpa_dir + 1 ) -> size == ( hpa_dir + 0 ) -> size ) /* compare 2 values of free node count */
                {
                    /* link out hot directory node */
                    header_hpa_entry = hpa_dir + 0;
                    header_hpa_entry -> prior -> next = header_hpa_entry -> next;
                    header_hpa_entry -> next -> prior = header_hpa_entry -> prior;
                    /* remove raw granularity memory */
                    free_raw_granularity ( mem_alloc, area ); /* operate memory from the internal, not the external users, never fail */
                }
                return area_set_size;
            }
            hpa_entry = hpa_entry -> next;
        }
    }
    return 0;
}

unsigned long hit_huge_page_dir_area ( struct mem_allocator * mem_alloc, struct hpa_directory * hpa_entry, void * page_area )
{
    /* patch!!! may be a malicious 'size' from user input, so do NOT use 'size' as user input */

    unsigned long area_set_size;
    unsigned char * this_area_set;
    void * area;
    struct hpa_directory * header_hpa_entry, * free_header_hpa_entry;
    struct hpa_directory * hpa_dir;

    /* do NOT need to adjust the input parameter, if not fixed page area aligned, the algorithm fail */
    if ( ( void * ) ( ( unsigned long ) page_area & FIXED_PAGE_AREA_MASK ) < page_area )
        return 0;
 
    /* this new directory area is where we just jumped into */
    area_set_size = hpa_entry -> size;
    this_area_set = ( unsigned char * ) hpa_entry -> area_set;    
    area = ( void * ) ( ( unsigned long ) hpa_entry & FIXED_PAGE_AREA_MASK ); /* always != NULL */
    hpa_dir = ( struct hpa_directory * ) area;

    /* 2. furthermore: remove hot node and data memory area this hot node holds */
    /* remove raw memory */
    free_raw_mem ( mem_alloc, this_area_set, area_set_size ); /* operate memory from the internal, not the external users, never fail */
    /* link out hot node */
    hpa_entry -> prior -> next = hpa_entry -> next;
    hpa_entry -> next -> prior = hpa_entry -> prior;
    /* link into free list */
    hpa_entry -> size = 0; /* reinit */
    hpa_entry -> area_set = NULL; /* reinit */
    free_header_hpa_entry = hpa_dir + 1;
    hpa_entry -> next = free_header_hpa_entry -> next;
    hpa_entry -> prior = free_header_hpa_entry;
    free_header_hpa_entry -> next -> prior = hpa_entry;
    free_header_hpa_entry -> next = hpa_entry;

    /* 3. furthermore: remove directory memory area */
    if ( ++ ( hpa_dir + 1 ) -> size == ( hpa_dir + 0 ) -> size ) /* compare 2 values of free node count */
    {
        /* link out hot directory node */
        header_hpa_entry = hpa_dir + 0;
        header_hpa_entry -> prior -> next = header_hpa_entry -> next;
        header_hpa_entry -> next -> prior = header_hpa_entry -> prior;
        /* remove raw granularity memory */
        free_raw_granularity ( mem_alloc, area ); /* operate memory from the internal, not the external users, never fail */
    }
    return area_set_size;
}

void * seek_huge_page_dir_area ( struct mem_allocator * mem_alloc, unsigned long * size, int flag )
{
    unsigned char * area_set;
    void * area;
    struct hpa_directory * header_hpa_entry, * header_header_hpa_entry, * free_header_hpa_entry, * free_hpa_entry;
    struct hpa_directory * hpa_dir;

    /* if not return, a new memory will be allocated, though there is no harm */
    if ( * size == 0 )
        return NULL;

    hpa_dir = flag == XALLOC_FAR_MEM ? mem_alloc -> far_hpa_dir_header : mem_alloc -> hpa_dir_header;
    header_header_hpa_entry = hpa_dir + 0;
    while ( 1 )
    {
        /* jump to next directory area */
        header_hpa_entry = hpa_dir + 0;
        area = header_hpa_entry = header_hpa_entry -> next;
        if ( header_hpa_entry == header_header_hpa_entry ) /* if ( ! area ) */
        {
            area = alloc_raw_granularity ( mem_alloc, NULL, flag == XALLOC_URGENT_NEAR_MEM ? XALLOC_URGENT_NEAR_MEM : XALLOC_NEAR_MEM ); /* patch!!! */
            if ( ! area )
                break;
            init_huge_page_dir_area ( mem_alloc, hpa_dir, area, flag );
        }

        /* this new directory area is where we just jumped into */
        hpa_dir = ( struct hpa_directory * ) area;
        header_hpa_entry = hpa_dir + XPA_DIR_START_INDEX;

        free_header_hpa_entry = hpa_dir + 1;
        free_hpa_entry = free_header_hpa_entry -> next;
        if ( free_hpa_entry != free_header_hpa_entry )
        {
            area_set = ( unsigned char * ) alloc_raw_mem ( mem_alloc, free_hpa_entry, size, flag ); /* NOT alloc_one_page_mem, allocate_granularity_memory*/
            if ( ! area_set )
                break; /* patch!!! do NOT continue, there is no need to jump to next directory area */
            /* link out */
            free_header_hpa_entry -> next = free_hpa_entry -> next;
            free_hpa_entry -> next -> prior = free_header_hpa_entry;
            free_header_hpa_entry -> size --; /* free node count - 1 */
            /* link in */
            free_hpa_entry -> size = * size;
            free_hpa_entry -> area_set = area_set;
            free_hpa_entry -> next = header_hpa_entry -> next;
            free_hpa_entry -> prior = header_hpa_entry;
            header_hpa_entry -> next -> prior = free_hpa_entry;
            header_hpa_entry -> next = free_hpa_entry;
            return free_hpa_entry -> area_set;
        }
    }

    return NULL;
}

void * alloc_huge_mem ( struct mem_allocator * mem_alloc, unsigned long * size, int flag )
{
    return seek_huge_page_dir_area ( mem_alloc, size, flag );
}

/* read hash table */
unsigned long free_huge_mem ( struct mem_allocator * mem_alloc, void * page_area )
{
    int bit_count, far_bit_count;
    unsigned long subarea_size, size, far_size, delta;
    unsigned char * this_page_area, * base, * far_base;
    struct hpa_directory * hpa_entry;
    struct raw_area * area_raw;

    subarea_size = FIXED_PAGE_AREA_SIZE;
    area_raw = & mem_alloc -> raw_memory;
    bit_count = area_raw -> bit_count;
    base = ( unsigned char * ) area_raw -> subarea_set;
    size = subarea_size * bit_count;
    far_bit_count = area_raw -> far_bit_count;
    far_base = ( unsigned char * ) area_raw -> far_subarea_set;
    far_size = subarea_size * far_bit_count;

    /* NOT internal call, parameters are NOT always correct */
    this_page_area = ( unsigned char * ) ( ( unsigned long ) page_area & FIXED_PAGE_AREA_MASK );
    
    if ( ( unsigned char * ) page_area >= base && ( unsigned char * ) page_area < base + size )
    {
        delta = this_page_area - base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        hpa_entry = ( mem_alloc -> hash_table + delta ) -> hpa_entry;

        /* do NOT need to reinit this page area context here */
    }
    else if ( ( unsigned char * ) page_area >= far_base && ( unsigned char * ) page_area < far_base + far_size )
    {
        delta = this_page_area - far_base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        hpa_entry = ( mem_alloc -> far_hash_table + delta ) -> hpa_entry;

        /* do NOT need to reinit this page area context here */
    }
    else
    {
        return 0;
    }

    return hit_huge_page_dir_area ( mem_alloc, hpa_entry, page_area );
}

/*
+-----------+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
256K area for one of directory areas, NOT 256K area for one of data areas
*/
void init_sin_page_dir_area ( struct mem_allocator * mem_alloc, struct spa_directory * last_area_spa_dir, void * area, int flag )
{
    int index, count;
    unsigned long area_size, dir_size, spa_size;
    unsigned long position;
    unsigned char * this_area;
    struct sin_page_area * area_spa, * header_spa, * full_header_spa, * header_last_spa, * free_header_spa;
    struct spa_directory * spa_dir;

    spa_dir = flag == XALLOC_FAR_MEM ? mem_alloc -> far_spa_dir_header : mem_alloc -> spa_dir_header;
    dir_size = sizeof ( struct spa_directory ) * SPA_DIR_ARRAY_LEN;
    spa_size = sizeof ( struct sin_page_area );
    area_size = FIXED_PAGE_AREA_SIZE; /* NOT ONE_PAGE_AREA_SIZE */
    position = 0;

    /* copy array of spa_directory to this area */
    this_area = ( unsigned char * ) area;
    memcpy ( this_area, spa_dir, dir_size );
    this_area += dir_size;
    position += dir_size;

    /* patch!!! set up dul link */
    spa_dir = ( struct spa_directory * ) area;
    for ( index = 0; index < SPA_DIR_ARRAY_LEN; index ++ )
    {
        header_spa = & ( spa_dir + index ) -> chain;
        header_spa -> next = header_spa;
        header_spa -> prior = header_spa;
        full_header_spa = & ( spa_dir + index ) -> full_chain;
        full_header_spa -> next = full_header_spa;
        full_header_spa -> prior = full_header_spa;
    }

    /* set up the first sin_page_area struct */
    area_spa = ( struct sin_page_area * ) this_area;
    memset ( area_spa, 0, spa_size );
    /* and be linked to spa_directory */
    free_header_spa = & ( spa_dir + 1 ) -> chain;
    area_spa -> next = free_header_spa;
    area_spa -> prior = free_header_spa;
    free_header_spa -> prior = area_spa;
    free_header_spa -> next = area_spa;
    /* finish setting up the first sin_page_area struct */
    this_area += spa_size;
    position += spa_size;
    count = 1;

    /* setup the rest of sin_page_area structs */
    while ( position + spa_size <= area_size )
    {
        area_spa = ( struct sin_page_area * ) this_area;
        memset ( area_spa, 0, spa_size );
        /* append into free list */
        area_spa -> next = free_header_spa;
        area_spa -> prior = free_header_spa -> prior;
        free_header_spa -> prior -> next = area_spa;
        free_header_spa -> prior = area_spa;
        /* finish setting up the current node */
        this_area += spa_size;
        position += spa_size;
        count ++;
    }

    /* free node count */
    ( spa_dir + 0 ) -> size = count;
    ( spa_dir + 1 ) -> size = count;

    /* link this area to last area (last_area_spa_dir) out there */
    header_spa = & ( spa_dir + 0 ) -> chain;
    header_last_spa = & ( last_area_spa_dir + 0 ) -> chain;
    header_spa -> next = header_last_spa -> next;
    header_spa -> prior = header_last_spa;
    header_last_spa -> next -> prior = header_spa;
    header_last_spa -> next = header_spa;
}

/*
+-----------+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
|-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-|
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
256K area for one of directory areas, NOT 256K area for one of data areas
*/
void init_mul_page_dir_area ( struct mem_allocator * mem_alloc, struct mpa_directory * last_area_mpa_dir, void * area, int flag )
{
    int index, count;
    unsigned long area_size, dir_size, mpa_size;
    unsigned long position;
    unsigned char * this_area;
    struct mul_page_area * area_mpa, * header_mpa, * full_header_mpa, * header_last_mpa, * free_header_mpa;
    struct mpa_directory * mpa_dir;

    mpa_dir = flag == XALLOC_FAR_MEM ? mem_alloc -> far_mpa_dir_header : mem_alloc -> mpa_dir_header;
    dir_size = sizeof ( struct mpa_directory ) * MPA_DIR_ARRAY_LEN;
    mpa_size = sizeof ( struct mul_page_area );
    area_size = FIXED_PAGE_AREA_SIZE;
    position = 0;

    /* copy array of mpa_directory to this area */
    this_area = ( unsigned char * ) area;
    memcpy ( this_area, mpa_dir, dir_size );
    this_area += dir_size;
    position += dir_size;

    /* patch!!! set up dul link */
    mpa_dir = ( struct mpa_directory * ) area;
    for ( index = 0; index < MPA_DIR_ARRAY_LEN; index ++ )
    {
        header_mpa = & ( mpa_dir + index ) -> chain;
        header_mpa -> next = header_mpa;
        header_mpa -> prior = header_mpa;
        full_header_mpa = & ( mpa_dir + index ) -> full_chain;
        full_header_mpa -> next = full_header_mpa;
        full_header_mpa -> prior = full_header_mpa;
    }

    /* set up the first mul_page_area struct */
    area_mpa = ( struct mul_page_area * ) this_area;
    memset ( area_mpa, 0, mpa_size );
    /* and be linked to mpa_directory */
    free_header_mpa = & ( mpa_dir + 1 ) -> chain;
    area_mpa -> next = free_header_mpa;
    area_mpa -> prior = free_header_mpa;
    free_header_mpa -> prior = area_mpa;
    free_header_mpa -> next = area_mpa;
    /* finish setting up the first mul_page_area struct */
    this_area += mpa_size;
    position += mpa_size;
    count = 1;

    /* setup the rest of mul_page_area structs */
    while ( position + mpa_size <= area_size )
    {
        area_mpa = ( struct mul_page_area * ) this_area;
        memset ( area_mpa, 0, mpa_size );
        /* append into free list */
        area_mpa -> next = free_header_mpa;
        area_mpa -> prior = free_header_mpa -> prior;
        free_header_mpa -> prior -> next = area_mpa;
        free_header_mpa -> prior = area_mpa;
        /* finish setting up the current node */
        this_area += mpa_size;
        position += mpa_size;
        count ++;
    }

    /* free node count */
    ( mpa_dir + 0 ) -> size = count;
    ( mpa_dir + 1 ) -> size = count;

    /* link this area to last area (last_area_mpa_dir) out there */
    header_mpa = & ( mpa_dir + 0 ) -> chain;
    header_last_mpa = & ( last_area_mpa_dir + 0 ) -> chain;
    header_mpa -> next = header_last_mpa -> next;
    header_mpa -> prior = header_last_mpa;
    header_last_mpa -> next -> prior = header_mpa;
    header_last_mpa -> next = header_mpa;
}

unsigned long go_mul_page_dir_area ( struct mem_allocator * mem_alloc, void * page_area, int flag )
{
    /* patch!!! may be a malicious 'size' from user input, so do NOT use 'size' as user input */

    int index, result_index, byte_index, max_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size;
    unsigned char * this_subarea_set, * this_page_area, * fixed_page_area;
    void * area;
    struct mul_page_area * area_mpa, * header_mpa, * full_mpa, * full_header_mpa,
                         * header_header_mpa, * free_header_mpa;
    struct mpa_directory * mpa_dir;
    
    /* do NOT need to adjust the input parameter, if not page aligned, the algorithm fail */
    if ( ( void * ) ( ( unsigned long ) page_area & ONE_PAGE_AREA_MASK ) < page_area )
        return 0;

    this_page_area = ( unsigned char * ) page_area;
    fixed_page_area = ( unsigned char * ) ( ( unsigned long ) page_area & FIXED_PAGE_AREA_MASK );

    mpa_dir = flag == XALLOC_FAR_MEM ? mem_alloc -> far_mpa_dir_header : mem_alloc -> mpa_dir_header;
    header_header_mpa = & ( mpa_dir + 0 ) -> chain;
    while ( 1 )
    {
        /* jump to next directory area */
        header_mpa = & ( mpa_dir + 0 ) -> chain;
        area = header_mpa = header_mpa -> next;
        if ( header_mpa == header_header_mpa ) /* if ( ! area ) */
            break;

        /* this new directory area is where we just jumped into */
        mpa_dir = ( struct mpa_directory * ) area;
        for ( result_index = XPA_DIR_START_INDEX; ( mpa_dir + result_index ) -> size; result_index ++ )
        {
            subarea_size = ( mpa_dir + result_index ) -> size;
            max_bit_count = FIXED_PAGE_AREA_SIZE / subarea_size;

            /* walk through full chain */
            full_header_mpa = & ( mpa_dir + result_index ) -> full_chain;
            full_mpa = full_header_mpa -> next;
            while ( full_mpa != full_header_mpa )
            {
                this_subarea_set = ( unsigned char * ) full_mpa -> subarea_set;
                if ( fixed_page_area == this_subarea_set )
                {
                    for ( index = 0; index < max_bit_count; index ++ )
                    {
                        if ( this_page_area == this_subarea_set + subarea_size * index )
                        {
                            mask = 0x1 << index % 8;
                            byte_index = index / 8;
                            bit_field = full_mpa -> bit_field [ byte_index ];
                            if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
                                return 0;
                            /* 1. all done */
                            full_mpa -> bit_field [ byte_index ] &= ~mask; /* clear bit */
                            full_mpa -> bit_count --; /* always >= 2 before subtraction */

                            /* link out full node */
                            full_mpa -> prior -> next = full_mpa -> next;
                            full_mpa -> next -> prior = full_mpa -> prior;
                            /* link into hot list */
                            header_mpa = & ( mpa_dir + result_index ) -> chain;
                            full_mpa -> next = header_mpa -> next;
                            full_mpa -> prior = header_mpa;
                            header_mpa -> next -> prior = full_mpa;
                            header_mpa -> next = full_mpa;
                            return subarea_size;
                        }
                    }
                }
                full_mpa = full_mpa -> next;
            }

            /* walk through hot chain */
            header_mpa = & ( mpa_dir + result_index ) -> chain;
            area_mpa = header_mpa -> next;
            while ( area_mpa != header_mpa )
            {
                this_subarea_set = ( unsigned char * ) area_mpa -> subarea_set;
                if ( fixed_page_area == this_subarea_set )
                {
                    for ( index = 0; index < max_bit_count; index ++ )
                    {
                        if ( this_page_area == this_subarea_set + subarea_size * index )
                        {
                            mask = 0x1 << index % 8;
                            byte_index = index / 8;
                            bit_field = area_mpa -> bit_field [ byte_index ];
                            if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
                                return 0;
                            /* 1. all done */
                            area_mpa -> bit_field [ byte_index ] &= ~mask; /* clear bit */

                            /* 2. furthermore: remove hot node and data memory area this hot node holds */
                            if ( -- area_mpa -> bit_count == 0 )
                            {
                                /* remove raw granularity memory */
                                free_raw_granularity ( mem_alloc, this_subarea_set ); /* operate memory from the internal, not the external users, never fail */
                                /* link out hot node */
                                area_mpa -> prior -> next = area_mpa -> next;
                                area_mpa -> next -> prior = area_mpa -> prior;
                                /* link into free list */
                                area_mpa -> subarea_set = NULL; /* reinit */
                                area_mpa -> dir = NULL; /* reinit */
                                free_header_mpa = & ( mpa_dir + 1 ) -> chain;
                                area_mpa -> next = free_header_mpa -> next;
                                area_mpa -> prior = free_header_mpa;
                                free_header_mpa -> next -> prior = area_mpa;
                                free_header_mpa -> next = area_mpa;

                                /* 3. furthermore: remove directory memory area */
                                if ( ++ ( mpa_dir + 1 ) -> size == ( mpa_dir + 0 ) -> size ) /* compare 2 values of free node count */
                                {
                                    /* link out hot directory node */
                                    header_mpa = & ( mpa_dir + 0 ) -> chain;
                                    header_mpa -> prior -> next = header_mpa -> next;
                                    header_mpa -> next -> prior = header_mpa -> prior;
                                    /* remove raw granularity memory */
                                    free_raw_granularity( mem_alloc, area ); /* operate memory from the internal, not the external users, never fail */
                                }
                            }
                            return subarea_size;
                        }
                    }
                }
                area_mpa = area_mpa -> next;
            }
        }
    }
    return free_huge_mem ( mem_alloc, page_area ); /* NOT return 0 */
}

unsigned long hit_mul_page_dir_area ( struct mem_allocator * mem_alloc, struct mul_page_area * area_mpa, void * page_area )
{
    /* patch!!! may be a malicious 'size' from user input, so do NOT use 'size' as user input */

    int index, byte_index, max_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size;
    unsigned char * this_subarea_set, * this_page_area;
    void * area;
    struct mul_page_area * header_mpa, * free_header_mpa;
    struct mpa_directory * mpa_dir, * result_mpa_dir;

    /* do NOT need to adjust the input parameter, if not page aligned, the algorithm fail */
    if ( ( void * ) ( ( unsigned long ) page_area & ONE_PAGE_AREA_MASK ) < page_area )
        return 0;

    /* this new directory area is where we just jumped into */
    result_mpa_dir = ( struct mpa_directory * ) area_mpa -> dir; /* mpa_dir + result_index */
    subarea_size = result_mpa_dir -> size;
    max_bit_count = FIXED_PAGE_AREA_SIZE / subarea_size;
    this_subarea_set = ( unsigned char * ) area_mpa -> subarea_set;

    this_page_area = ( unsigned char * ) page_area;
    if ( area_mpa -> bit_count == max_bit_count ) /* walk through full chain */
    {
        for ( index = 0; index < max_bit_count; index ++ )
        {
            if ( this_page_area == this_subarea_set + subarea_size * index )
            {
                mask = 0x1 << index % 8;
                byte_index = index / 8;
                bit_field = area_mpa -> bit_field [ byte_index ];
                if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
                    return 0;
                /* 1. all done */
                area_mpa -> bit_field [ byte_index ] &= ~mask; /* clear bit */
                area_mpa -> bit_count --; /* always >= 2 before subtraction */

                /* link out full node */
                area_mpa -> prior -> next = area_mpa -> next;
                area_mpa -> next -> prior = area_mpa -> prior;
                /* link into hot list */
                header_mpa = & result_mpa_dir -> chain;
                area_mpa -> next = header_mpa -> next;
                area_mpa -> prior = header_mpa;
                header_mpa -> next -> prior = area_mpa;
                header_mpa -> next = area_mpa;
                return subarea_size;
            }
        }
    }
    else /* walk through hot chain */
    {
        area = ( void * ) ( ( unsigned long ) area_mpa & FIXED_PAGE_AREA_MASK ); /* always != NULL */
        mpa_dir = ( struct mpa_directory * ) area;

        for ( index = 0; index < max_bit_count; index ++ )
        {
            if ( this_page_area == this_subarea_set + subarea_size * index)
            {
                mask = 0x1 << index % 8;
                byte_index = index / 8;
                bit_field = area_mpa -> bit_field [ byte_index ];
                if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
                    return 0;
                /* 1. all done */
                area_mpa -> bit_field [ byte_index ] &= ~mask; /* clear bit */

                /* 2. furthermore: remove hot node and data memory area this hot node holds */
                if ( -- area_mpa -> bit_count == 0 )
                {
                    /* remove raw granularity memory */
                    free_raw_granularity ( mem_alloc, this_subarea_set ); /* operate memory from the internal, not the external users, never fail */
                    /* link out hot node */
                    area_mpa -> prior -> next = area_mpa -> next;
                    area_mpa -> next -> prior = area_mpa -> prior;
                    /* link into free list */
                    area_mpa -> subarea_set = NULL; /* reinit */
                    area_mpa -> dir = NULL; /* reinit */
                    free_header_mpa = & ( mpa_dir + 1 ) -> chain;
                    area_mpa -> next = free_header_mpa -> next;
                    area_mpa -> prior = free_header_mpa;
                    free_header_mpa -> next -> prior = area_mpa;
                    free_header_mpa -> next = area_mpa;

                    /* 3. furthermore: remove directory memory area */
                    if ( ++ ( mpa_dir + 1 ) -> size == ( mpa_dir + 0 ) -> size ) /* compare 2 values of free node count */
                    {
                        /* link out hot directory node */
                        header_mpa = & ( mpa_dir + 0 ) -> chain;
                        header_mpa -> prior -> next = header_mpa -> next;
                        header_mpa -> next -> prior = header_mpa -> prior;
                        /* remove raw granularity memory */
                        free_raw_granularity ( mem_alloc, area ); /* operate memory from the internal, not the external users, never fail */
                    }
                }
                return subarea_size;
            }
        }
    }
    return 0; /* NOT return free_huge_mem(mem_alloc, page_area, 0); */
}

void * seek_mul_page_dir_area ( struct mem_allocator * mem_alloc, unsigned long * size, int flag )
{
    int index, result_index, byte_index, max_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size;
    unsigned char * this_subarea_set, * subarea_set;
    void * area;
    struct mul_page_area * area_mpa, * header_mpa, * full_header_mpa, 
                         * header_header_mpa, * free_header_mpa, * free_mpa;
    struct mpa_directory * mpa_dir, * mpa_dir_header;

    /* if not return, a new memory will be allocated, though there is no harm */
    if ( * size == 0 )
        return NULL;

    mpa_dir_header = mpa_dir = flag == XALLOC_FAR_MEM ? mem_alloc -> far_mpa_dir_header : mem_alloc -> mpa_dir_header;

    result_index = -1;
    for ( index = XPA_DIR_START_INDEX; ( mpa_dir + index ) -> size; index ++ )
    {
        if ( ( mpa_dir + index ) -> size >= * size )
        {
            result_index = index;
            break;
        }
    }
    if ( result_index == -1 )
        return alloc_huge_mem ( mem_alloc, size, flag ); /* NOT return NULL */

    header_header_mpa = & ( mpa_dir + 0 ) -> chain;
    while ( 1 )
    {
        /* jump to next directory area */
        header_mpa = & ( mpa_dir + 0 ) -> chain;
        area = header_mpa = header_mpa -> next;
        if ( header_mpa == header_header_mpa ) /* if ( ! area ) */
            break;

        /* this new directory area is where we just jumped into */
        mpa_dir = ( struct mpa_directory * ) area;
        subarea_size = ( mpa_dir + result_index ) -> size;
        max_bit_count = FIXED_PAGE_AREA_SIZE / subarea_size;
        header_mpa = & ( mpa_dir + result_index ) -> chain;
        area_mpa = header_mpa -> next;
        while ( area_mpa != header_mpa )
        {
            this_subarea_set = ( unsigned char * ) area_mpa -> subarea_set;
            for ( index = 0; index < max_bit_count; index ++ )
            {
                mask = 0x1 << index % 8;
                byte_index = index / 8;
                bit_field = area_mpa -> bit_field [ byte_index ];
                if ( ! ( bit_field & mask) )
                {
                    area_mpa -> bit_field [ byte_index ] |= mask; /* set bit */
                    if ( ++ area_mpa -> bit_count == max_bit_count )
                    {
                        /* link out hot node */
                        area_mpa -> prior -> next = area_mpa -> next;
                        area_mpa -> next -> prior = area_mpa -> prior;
                        /* link into full list */
                        full_header_mpa = & ( mpa_dir + result_index ) -> full_chain;
                        area_mpa -> next = full_header_mpa -> next;
                        area_mpa -> prior = full_header_mpa;
                        full_header_mpa -> next -> prior = area_mpa;
                        full_header_mpa -> next = area_mpa;
                    }
                    * size = subarea_size;
                    return this_subarea_set + subarea_size * index;
                }
            }
            area_mpa = area_mpa -> next;
        }
    }

    mpa_dir = mpa_dir_header; /* reinit */
    while ( 1 )
    {
        /* jump to next directory area */
        header_mpa = & ( mpa_dir + 0 ) -> chain;
        area = header_mpa = header_mpa -> next;
        if ( header_mpa == header_header_mpa ) /* if ( ! area ) */
        {
            area = alloc_raw_granularity ( mem_alloc, NULL, flag == XALLOC_URGENT_NEAR_MEM ? XALLOC_URGENT_NEAR_MEM : XALLOC_NEAR_MEM ); /* patch!!! */
            if ( ! area )
                break;
            init_mul_page_dir_area ( mem_alloc, mpa_dir, area, flag );
        }

        /* this new directory area is where we just jumped into */
        mpa_dir = ( struct mpa_directory * ) area;
        subarea_size = ( mpa_dir + result_index ) -> size;
        max_bit_count = FIXED_PAGE_AREA_SIZE / subarea_size;
        header_mpa = & ( mpa_dir + result_index ) -> chain;

        free_header_mpa = & ( mpa_dir + 1 ) -> chain;
        free_mpa = free_header_mpa -> next;
        if ( free_mpa != free_header_mpa )
        {
            subarea_set = ( unsigned char * ) alloc_raw_granularity ( mem_alloc, free_mpa, flag ); /* NOT alloc_one_page_mem */
            if ( ! subarea_set )
                break; /* patch!!! do NOT continue, there is no need to jump to next directory area */
            /* link out */
            free_header_mpa -> next = free_mpa -> next;
            free_mpa -> next -> prior = free_header_mpa;
            ( mpa_dir + 1 ) -> size --; /* free node count - 1 */
            /* link in */
            free_mpa -> bit_field [ 0 ] = 0x1; /* set bit */
            free_mpa -> bit_count = 1;
            free_mpa -> subarea_set = subarea_set;
            free_mpa -> dir = mpa_dir + result_index;
            free_mpa -> next = header_mpa -> next;
            free_mpa -> prior = header_mpa;
            header_mpa -> next -> prior = free_mpa;
            header_mpa -> next = free_mpa;
            * size = subarea_size;
            return free_mpa -> subarea_set;
        }
    }

    return NULL;
}

/* write hash table */
void * alloc_one_page_mem ( struct mem_allocator * mem_alloc, struct sin_page_area * area_spa, int flag )
{    
    unsigned long size, delta;
    unsigned char * page_area, * base, * far_base;

    size = ONE_PAGE_AREA_SIZE;
    page_area = ( unsigned char * ) seek_mul_page_dir_area ( mem_alloc, & size, flag );
    if ( page_area )
    {
        if ( flag == XALLOC_FAR_MEM )
        {
            far_base = ( unsigned char * ) mem_alloc -> raw_memory . far_subarea_set;
            delta = page_area - far_base; /* already aligned */
            delta /= ONE_PAGE_AREA_SIZE;
            ( mem_alloc -> far_hash_table + delta ) -> spa = area_spa;
        }
        else
        {
            base = ( unsigned char * ) mem_alloc -> raw_memory . subarea_set;
            delta = page_area - base; /* already aligned */
            delta /= ONE_PAGE_AREA_SIZE;
            ( mem_alloc -> hash_table + delta ) -> spa = area_spa;
        }

        mem_alloc -> on_alloc_one_page_mem ( page_area, size );
    }
    return page_area;
}

unsigned long free_one_page_mem ( struct mem_allocator * mem_alloc, void * page_area )
{
    unsigned long size;
    unsigned long delta;
    unsigned char * this_page_area, * base, * far_base;
    struct mul_page_area * area_mpa;

    /* internal call, parameters are always correct */
    this_page_area = ( unsigned char * ) ( ( unsigned long ) page_area & FIXED_PAGE_AREA_MASK );
    far_base = ( unsigned char * ) mem_alloc -> raw_memory . far_subarea_set;
    if ( ( unsigned char * ) page_area < far_base )
    {
        /* patch!!! internal call, there is no malicious user input */
        base = ( unsigned char * ) mem_alloc -> raw_memory . subarea_set;
        delta = this_page_area - base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        area_mpa = ( mem_alloc -> hash_table + delta ) -> mpa;

        /* reinit this one page context */
        this_page_area = ( unsigned char * ) page_area; /* already aligned */
        delta = this_page_area - base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        ( mem_alloc -> hash_table + delta ) -> spa = NULL; /* reinit */
    }
    else
    {
        /* patch!!! internal call, there is no malicious user input */
        delta = this_page_area - far_base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        area_mpa = ( mem_alloc -> far_hash_table + delta ) -> mpa;

        /* reinit this one page context */
        this_page_area = ( unsigned char * ) page_area; /* already aligned */
        delta = this_page_area - far_base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        ( mem_alloc -> far_hash_table + delta ) -> spa = NULL; /* reinit */
    }
    /* internal call, parameters are always correct */
    size = hit_mul_page_dir_area ( mem_alloc, area_mpa, page_area );
    /* internal call, return values are always correct */
    mem_alloc -> on_free_one_page_mem ( page_area, size );
    return size;
}

unsigned long go_sin_page_dir_area ( struct mem_allocator * mem_alloc, void * address_area, int flag )
{
    /* patch!!! may be a malicious 'size' from user input, so do NOT use 'size' as user input */

    int index, result_index, byte_index, max_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size;
    unsigned char * this_subarea_set, * this_address_area, * this_page_area;
    void * area;
    struct sin_page_area * area_spa, * header_spa, * full_spa, * full_header_spa,
                         * header_header_spa, * free_header_spa;
    struct spa_directory * spa_dir;

    this_address_area = ( unsigned char * ) address_area;
    this_page_area = ( unsigned char * ) ( ( unsigned long ) address_area & ONE_PAGE_AREA_MASK );

    spa_dir = flag == XALLOC_FAR_MEM ? mem_alloc -> far_spa_dir_header : mem_alloc -> spa_dir_header;
    header_header_spa = & ( spa_dir + 0 ) -> chain;
    while ( 1 )
    {
        /* jump to next directory area */
        header_spa = & ( spa_dir + 0 ) -> chain;
        area = header_spa = header_spa -> next;
        if ( header_spa == header_header_spa ) /* if ( ! area ) */
            break;

        /* this new directory area is where we just jumped into */
        spa_dir = ( struct spa_directory * ) area;
        for ( result_index = XPA_DIR_START_INDEX; ( spa_dir + result_index ) -> size; result_index ++ )
        {
            subarea_size = ( spa_dir + result_index ) -> size;
            max_bit_count = ONE_PAGE_AREA_SIZE / subarea_size;

            /* walk through full chain */
            full_header_spa = & ( spa_dir + result_index ) -> full_chain;
            full_spa = full_header_spa -> next;
            while ( full_spa != full_header_spa )
            {
                this_subarea_set = ( unsigned char * ) full_spa -> subarea_set;
                if ( this_page_area == this_subarea_set )
                {
                    for ( index = 0; index < max_bit_count; index ++ )
                    {
                        if ( this_address_area == this_subarea_set + subarea_size * index )
                        {
                            mask = 0x1 << index % 8;
                            byte_index = index / 8;
                            bit_field = full_spa -> bit_field [ byte_index ];
                            if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
                                return 0;
                            /* 1. all done */
                            full_spa -> bit_field [ byte_index ] &= ~mask; /* clear bit */
                            full_spa -> bit_count --; /* always >=2 before subtraction */

                            /* link out full node */
                            full_spa -> prior -> next = full_spa -> next;
                            full_spa -> next -> prior = full_spa -> prior;
                            /* link into hot list */
                            header_spa = & ( spa_dir + result_index ) -> chain;
                            full_spa -> next = header_spa -> next;
                            full_spa -> prior = header_spa;
                            header_spa -> next -> prior = full_spa;
                            header_spa -> next = full_spa;
                            return subarea_size;
                        }
                    }
                }
                full_spa = full_spa -> next;
            }

            /* walk through hot chain */
            header_spa = & ( spa_dir + result_index ) -> chain;
            area_spa = header_spa -> next;
            while ( area_spa != header_spa )
            {
                this_subarea_set = ( unsigned char * ) area_spa -> subarea_set;
                if ( this_page_area == this_subarea_set )
                {
                    for ( index = 0; index < max_bit_count; index ++ )
                    {
                        if ( this_address_area == this_subarea_set + subarea_size * index )
                        {
                            mask = 0x1 << index % 8;
                            byte_index = index / 8;
                            bit_field = area_spa -> bit_field [ byte_index ];
                            if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
                                return 0;
                            /* 1. all done */
                            area_spa -> bit_field [ byte_index ] &= ~mask; /* clear bit */

                            /* 2. furthermore: remove hot node and data memory area this hot node holds */
                            if ( -- area_spa -> bit_count == 0 )
                            {
                                /* remove raw granularity memory */
                                free_one_page_mem ( mem_alloc, this_subarea_set ); /* operate memory from the internal, not the external users, never fail */
                                /* link out hot node */
                                area_spa -> prior -> next = area_spa -> next;
                                area_spa -> next -> prior = area_spa -> prior;
                                /* link into free list */
                                area_spa -> subarea_set = NULL; /* reinit */
                                area_spa -> dir = NULL; /* reinit */
                                free_header_spa = & ( spa_dir + 1 ) -> chain;
                                area_spa -> next = free_header_spa -> next;
                                area_spa -> prior = free_header_spa;
                                free_header_spa -> next -> prior = area_spa;
                                free_header_spa -> next = area_spa;

                                /* 3. furthermore: remove directory memory area */
                                if ( ++ ( spa_dir + 1 ) -> size == ( spa_dir + 0 ) -> size ) /* compare 2 values of free node count */
                                {
                                    /* link out hot directory node */
                                    header_spa = & ( spa_dir + 0 ) -> chain;
                                    header_spa -> prior -> next = header_spa -> next;
                                    header_spa -> next -> prior = header_spa -> prior;
                                    /* remove raw granularity memory */
                                    free_raw_granularity ( mem_alloc, area ); /* operate memory from the internal, not the external users, never fail */
                                }
                            }
                            return subarea_size;
                        }
                    }
                }
                area_spa = area_spa -> next;
            }
        }
    }
    return 0;
}

unsigned long hit_sin_page_dir_area ( struct mem_allocator * mem_alloc, struct sin_page_area * area_spa, void * address_area )
{
    /* patch!!! may be a malicious 'size' from user input, so do NOT use 'size' as user input */

    int index, byte_index, max_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size;
    unsigned char * this_subarea_set, * this_address_area;
    void * area;
    struct sin_page_area * header_spa, * free_header_spa;
    struct spa_directory * spa_dir, * result_spa_dir;

    /* this new directory area is where we just jumped into */
    result_spa_dir = ( struct spa_directory * ) area_spa -> dir; /* spa_dir + result_index */
    subarea_size = result_spa_dir -> size;
    max_bit_count = ONE_PAGE_AREA_SIZE / subarea_size;
    this_subarea_set = ( unsigned char * ) area_spa -> subarea_set;

    this_address_area = ( unsigned char * ) address_area;
    if ( area_spa -> bit_count == max_bit_count ) /* walk through full chain */
    {
        for ( index = 0; index < max_bit_count; index ++ )
        {
            if ( this_address_area == this_subarea_set + subarea_size * index )
            {
                mask = 0x1 << index % 8;
                byte_index = index / 8;
                bit_field = area_spa -> bit_field [ byte_index ];
                if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
                    return 0;
                /* 1. all done */
                area_spa -> bit_field [ byte_index ] &= ~mask; /* clear bit */
                area_spa -> bit_count --; /* always >=2 before subtraction */

                /* link out full node */
                area_spa -> prior -> next = area_spa -> next;
                area_spa -> next -> prior = area_spa -> prior;
                /* link into hot list */
                header_spa = & result_spa_dir -> chain;
                area_spa -> next = header_spa -> next;
                area_spa -> prior = header_spa;
                header_spa -> next -> prior = area_spa;
                header_spa -> next = area_spa;
                return subarea_size;
            }
        }
    }
    else /* walk through hot chain */
    {
        area = ( void * ) ( ( unsigned long ) area_spa & FIXED_PAGE_AREA_MASK ); /* always != NULL */
        spa_dir = ( struct spa_directory * ) area;

        for (index = 0; index < max_bit_count; index++)
        {
            if ( this_address_area == this_subarea_set + subarea_size * index )
            {
                mask = 0x1 << index % 8;
                byte_index = index / 8;
                bit_field = area_spa -> bit_field [ byte_index ];
                if ( ! ( bit_field & mask ) ) /* bit been cleared before, should NOT happen, unless the page address is just a guessing number */
                    return 0;
                /* 1. all done */
                area_spa -> bit_field [ byte_index ] &= ~mask; /* clear bit */

                /* 2. furthermore: remove hot node and data memory area this hot node holds */
                if ( -- area_spa -> bit_count == 0)
                {
                    /* remove raw granularity memory */
                    free_one_page_mem ( mem_alloc, this_subarea_set ); /* operate memory from the internal, not the external users, never fail */
                    /* link out hot node */
                    area_spa -> prior-> next = area_spa -> next;
                    area_spa -> next -> prior = area_spa -> prior;
                    /* link into free list */
                    area_spa -> subarea_set = NULL; /* reinit */
                    area_spa -> dir = NULL; /* reinit */
                    free_header_spa = & ( spa_dir + 1 ) -> chain;
                    area_spa -> next = free_header_spa -> next;
                    area_spa -> prior = free_header_spa;
                    free_header_spa -> next -> prior = area_spa;
                    free_header_spa -> next = area_spa;

                    /* 3. furthermore: remove directory memory area */
                    if ( ++ ( spa_dir + 1 ) -> size == ( spa_dir + 0 ) -> size ) /* compare 2 values of free node count */
                    {
                        /* link out hot directory node */
                        header_spa = & ( spa_dir + 0 ) -> chain;
                        header_spa -> prior -> next = header_spa -> next;
                        header_spa -> next -> prior = header_spa -> prior;
                        /* remove raw granularity memory */
                        free_raw_granularity ( mem_alloc, area ); /* operate memory from the internal, not the external users, never fail */
                    }
                }
                return subarea_size;
            }
        }
    }
    return 0;
}

void * seek_sin_page_dir_area ( struct mem_allocator * mem_alloc, unsigned long * size, int flag )
{
    int index, result_index, byte_index, max_bit_count;
    unsigned char bit_field, mask;
    unsigned long subarea_size;
    unsigned char * this_subarea_set, * subarea_set;
    void * area;
    struct sin_page_area * area_spa, * header_spa, * full_header_spa,
                         * header_header_spa, * free_header_spa, * free_spa;
    struct spa_directory * spa_dir, * spa_dir_header;
  
    /* if not return, a new memory will be allocated, though there is no harm */
    if ( * size == 0 )
        return NULL;

    spa_dir_header = spa_dir = flag == XALLOC_FAR_MEM ? mem_alloc -> far_spa_dir_header : mem_alloc -> spa_dir_header;

    result_index = -1;
    for ( index = XPA_DIR_START_INDEX; ( spa_dir + index ) -> size; index ++ )
    {
        if ( ( spa_dir + index ) -> size >= * size )
        {
            result_index = index;
            break;
        }
    }
    if ( result_index == -1 )
        return NULL;

    header_header_spa = & ( spa_dir + 0 ) -> chain;
    while ( 1 )
    {
        /* jump to next directory area */
        header_spa = & ( spa_dir + 0 ) -> chain;
        area = header_spa = header_spa -> next;
        if ( header_spa == header_header_spa ) /* if ( ! area ) */
            break;

        /* this new directory area is where we just jumped into */
        spa_dir = ( struct spa_directory * ) area;
        subarea_size = ( spa_dir + result_index ) -> size;
        max_bit_count = ONE_PAGE_AREA_SIZE / subarea_size;
        header_spa = & ( spa_dir + result_index ) -> chain;
        area_spa = header_spa -> next;
        while ( area_spa != header_spa )
        {
            this_subarea_set = ( unsigned char * ) area_spa -> subarea_set;
            for ( index = 0; index < max_bit_count; index ++ )
            {
                mask = 0x1 << index % 8;
                byte_index = index / 8;
                bit_field = area_spa -> bit_field [ byte_index ];
                if ( ! ( bit_field & mask ) )
                {
                    area_spa -> bit_field [ byte_index ] |= mask; /* set bit */
                    if ( ++ area_spa -> bit_count == max_bit_count )
                    {
                        /* link out hot node */
                        area_spa -> prior -> next = area_spa -> next;
                        area_spa -> next -> prior = area_spa -> prior;
                        /* link into full list */
                        full_header_spa = & ( spa_dir + result_index ) -> full_chain;
                        area_spa -> next = full_header_spa -> next;
                        area_spa -> prior = full_header_spa;
                        full_header_spa -> next -> prior = area_spa;
                        full_header_spa -> next = area_spa;
                    }
                    * size = subarea_size;
                    return this_subarea_set + subarea_size * index;
                }
            }
            area_spa = area_spa -> next;
        }
    }

    spa_dir = spa_dir_header; /* reinit */
    while ( 1 )
    {
        /* jump to next directory area */
        header_spa = & ( spa_dir + 0 ) -> chain;
        area = header_spa = header_spa -> next;
        if ( header_spa == header_header_spa ) /* if ( ! area ) */
        {
            area = alloc_raw_granularity ( mem_alloc, NULL, flag == XALLOC_URGENT_NEAR_MEM ? XALLOC_URGENT_NEAR_MEM : XALLOC_NEAR_MEM ); /* patch!!! */
            if ( ! area )
                break;
            init_sin_page_dir_area ( mem_alloc, spa_dir, area, flag );
        }

        /* this new directory area is where we just jumped into */
        spa_dir = ( struct spa_directory * ) area;
        subarea_size = ( spa_dir + result_index ) -> size;
        max_bit_count = ONE_PAGE_AREA_SIZE / subarea_size;
        header_spa = & ( spa_dir + result_index ) -> chain;

        free_header_spa = & ( spa_dir + 1) -> chain;
        free_spa = free_header_spa -> next;
        if ( free_spa != free_header_spa )
        {
            subarea_set = ( unsigned char * ) alloc_one_page_mem ( mem_alloc, free_spa, flag );
            if ( ! subarea_set )
                break; /* patch!!! do NOT continue, there is no need to jump to next directory area */
            /* link out */
            free_header_spa -> next = free_spa -> next;
            free_spa -> next -> prior = free_header_spa;
            ( spa_dir + 1 ) -> size --; /* free node count - 1 */
            /* link in */
            free_spa -> bit_field [ 0 ] = 0x1; /* set bit */
            free_spa -> bit_count = 1;
            free_spa -> subarea_set = subarea_set;
            free_spa -> dir = spa_dir + result_index;
            free_spa -> next = header_spa -> next;
            free_spa -> prior = header_spa;
            header_spa -> next -> prior = free_spa;
            header_spa -> next = free_spa;
            * size = subarea_size;
            return free_spa -> subarea_set;
        }
    }

    return NULL;
}

/* read hash table */
unsigned long hit_page_dir_area ( struct mem_allocator * mem_alloc, void * address_area )
{
    /* patch!!! may be a malicious 'size' from user input, so do NOT use 'size' as user input */

    int bit_count, far_bit_count;
    unsigned long subarea_size, size, far_size, delta;
    unsigned char * this_page_area, * base, * far_base;
    struct sin_page_area * area_spa;
    struct mul_page_area * area_mpa;
    struct hpa_directory * hpa_entry;
    struct raw_area * area_raw;

    subarea_size = FIXED_PAGE_AREA_SIZE;
    area_raw = & mem_alloc -> raw_memory;
    bit_count = area_raw -> bit_count;
    base = ( unsigned char * ) area_raw -> subarea_set;
    size = subarea_size * bit_count;
    far_bit_count = area_raw -> far_bit_count;
    far_base = ( unsigned char * ) area_raw -> far_subarea_set;
    far_size = subarea_size * far_bit_count;

    /* patch!!! may be a malicious 'address_area' from user input */
    if ( ( unsigned char * ) address_area >= base && ( unsigned char * ) address_area < base + size )
    {
        this_page_area = ( unsigned char * ) ( ( unsigned long ) address_area & ONE_PAGE_AREA_MASK );
        delta = this_page_area - base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        area_spa = ( mem_alloc -> hash_table + delta ) -> spa;
        if ( area_spa )
            return hit_sin_page_dir_area ( mem_alloc, area_spa, address_area );

        this_page_area = ( unsigned char * ) ( ( unsigned long ) address_area & FIXED_PAGE_AREA_MASK );
        delta = this_page_area - base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        area_mpa = ( mem_alloc -> hash_table + delta ) -> mpa;
        if ( area_mpa )
            return hit_mul_page_dir_area ( mem_alloc, area_mpa, address_area );
        hpa_entry = ( mem_alloc -> hash_table + delta ) -> hpa_entry;
        if ( hpa_entry )
            return hit_huge_page_dir_area( mem_alloc, hpa_entry, address_area );
    }
    else if ( ( unsigned char * ) address_area >= far_base && ( unsigned char * ) address_area < far_base + far_size )
    {
        this_page_area = ( unsigned char * ) ( ( unsigned long ) address_area & ONE_PAGE_AREA_MASK );
        delta = this_page_area - far_base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        area_spa = ( mem_alloc -> far_hash_table + delta ) -> spa;
        if ( area_spa )
            return hit_sin_page_dir_area ( mem_alloc, area_spa, address_area );

        this_page_area = ( unsigned char * ) ( ( unsigned long ) address_area & FIXED_PAGE_AREA_MASK );
        delta = this_page_area - far_base; /* already aligned */
        delta /= ONE_PAGE_AREA_SIZE;
        area_mpa = ( mem_alloc -> far_hash_table + delta ) -> mpa;
        if ( area_mpa )
            return hit_mul_page_dir_area ( mem_alloc, area_mpa, address_area );
        hpa_entry = ( mem_alloc -> far_hash_table + delta ) -> hpa_entry;
        if ( hpa_entry )
            return hit_huge_page_dir_area( mem_alloc, hpa_entry, address_area );
    }
    else
    {
        /* do nothing */
    }

    return 0;
}
