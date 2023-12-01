#include <types.h>
#include <mmap.h>
#include <fork.h>
#include <v2p.h>
#include <page.h>

//  You may define macros and other helper functions here
void protect_pfn(struct exec_context *current, u64 addr, int length, int prot)
{
    if (current == NULL)
    {
        return -EINVAL;
    }
    if ((addr < MMAP_AREA_START || addr > MMAP_AREA_END) && addr != 0)
    {
        return -EINVAL;
    }
    if (length % 4096 != 0)
    {
        return -EINVAL;
    }
    if (prot != PROT_READ && prot != (PROT_READ | PROT_WRITE))
    {
        return -EINVAL;
    }
    int pages = length / 4096;
    int j = 0;
    for (int i = 0; i < pages; i++)
    {
        j = 4096 * i;
        u64 *vir_pgd = (u64 *)osmap(current->pgd);
        u64 offset_pgd = ((addr + j) >> (39)) & 0x1FF;
        u64 *entryaddr_pgd = (vir_pgd + offset_pgd);
        u64 pgd_entry = *(entryaddr_pgd);

        u64 *v_pud_t;
        u64 offset_pud = ((addr + j) >> 30) & 0x1FF;
        u64 pud_entry;

        if (pgd_entry & 1 == 1)
        {
            u64 *v_pud = osmap(pgd_entry >> 12);
            v_pud_t = v_pud + offset_pud;
            pud_entry = *(v_pud_t);
        }
        else
        {
            continue;
        }

        u64 *v_pmd_t;
        u64 pmd_entry;
        u64 offset_pmd = ((addr + j) >> 21) & 0x1FF;

        if (pud_entry & 1 == 1)
        {
            u64 *v_pmd = osmap(pud_entry >> 12);
            v_pmd_t = v_pmd + offset_pmd;
            pmd_entry = *(v_pmd_t);
        }
        else
        {
            continue;
        }

        u64 *v_pte_t;
        u64 pte_entry;
        u64 offset_pte = ((addr + j) >> 12) & 0x1FF;

        if (pmd_entry & 1 == 1)
        {
            u64 *v_pte = osmap(pmd_entry >> 12);
            v_pte_t = v_pte + offset_pte;
            pte_entry = *(v_pte_t);
        }
        else
        {
            continue;
        }

        if (pte_entry & 1 == 1)
        {
            if (get_pfn_refcount(pte_entry >> 12) > 1)
            {
                if (prot == PROT_READ)
                {
                    *(v_pte_t) = *(v_pte_t) & (u64)(~(1 << 3));
                }
                asm volatile("invlpg (%0)" ::"r"(addr + j));
                return;
            }
            if (prot == PROT_READ)
            {
                *(v_pte_t) = *(v_pte_t) & (u64)(~(1 << 3));
            }
            else if (prot == (PROT_READ | PROT_WRITE))
            {
                *(v_pte_t) = *(v_pte_t) | (1 << 3);
            }
            asm volatile("invlpg (%0)" ::"r"((addr + j)));
            continue;
        }
        else
        {
            continue;
        }
    }
}

void free_pfn(struct exec_context *current, u64 addr, int length)
{
    if (current == NULL)
    {
        return -EINVAL;
    }
    if ((addr < MMAP_AREA_START || addr > MMAP_AREA_END) && addr != 0)
    {
        return -EINVAL;
    }
    if (length % 4096 != 0)
    {
        return -EINVAL;
    }

    int pages = length / 4096;
    int j = 0;
    for (int i = 0; i < pages; i++)
    {
        j = 4096 * i;
        u64 *vir_pgd = osmap(current->pgd);
        u64 offset_pgd = ((addr + j) >> (39)) & 0x1FF;
        u64 *entryaddr_pgd = (vir_pgd + offset_pgd);
        u64 pgd_entry = *(entryaddr_pgd);

        u64 *v_pud_t;
        u64 offset_pud = ((addr + j) >> 30) & 0x1FF;
        u64 pud_entry;

        if (pgd_entry & 1 == 1)
        {
            u64 *v_pud = osmap(pgd_entry >> 12);
            v_pud_t = v_pud + offset_pud;
            pud_entry = *(v_pud_t);
        }
        else
        {
            continue;
        }

        u64 *v_pmd_t;
        u64 pmd_entry;
        u64 offset_pmd = ((addr + j) >> 21) & 0x1FF;

        if (pud_entry & 1 == 1)
        {
            u64 *v_pmd = osmap(pud_entry >> 12);
            v_pmd_t = v_pmd + offset_pmd;
            pmd_entry = *(v_pmd_t);
        }
        else
        {
            continue;
        }

        u64 *v_pte_t;
        u64 pte_entry;
        u64 offset_pte = ((addr + j) >> 12) & 0x1FF;

        if (pmd_entry & 1 == 1)
        {
            u64 *v_pte = osmap(pmd_entry >> 12);
            v_pte_t = v_pte + offset_pte;
            pte_entry = *(v_pte_t);
        }
        else
        {
            continue;
        }

        if (pte_entry & 1 == 1)
        {
            if (get_pfn_refcount(pte_entry >> 12) == 1)
            {
                put_pfn(pte_entry >> 12);
                os_pfn_free(USER_REG, ((u64)(*(v_pte_t)) >> 12));
            }
            *(v_pte_t) = 0;
            asm volatile("invlpg (%0)" ::"r"((addr + j)));
            continue;
        }
        else
        {
            continue;
        }
    }
}

int page_table_walk_all(struct exec_context *current, u64 addr, int error_code)
{

    u64 *vir_pgd = osmap(current->pgd);
    u64 offset_pgd = (addr >> 39) & 0x1FF;
    u64 *entryaddr_pgd = (vir_pgd + offset_pgd);
    u64 pgd_entry = *(entryaddr_pgd);

    u64 *v_pud_t;
    u64 offset_pud = (addr >> 30) & 0x1FF;
    u64 pud_entry;

    if (pgd_entry & 1 == 1)
    {
        *(entryaddr_pgd) = *(entryaddr_pgd) | 0x19;
        asm volatile("invlpg (%0)" ::"r"(addr));
        u64 *v_pud = osmap(pgd_entry >> 12);
        v_pud_t = v_pud + offset_pud;
        pud_entry = *(v_pud_t);
    }
    else
    {
        u32 pud = os_pfn_alloc(OS_PT_REG);
        *(entryaddr_pgd) = pud << 12;
        *(entryaddr_pgd) = *(entryaddr_pgd) | 0x19;
        asm volatile("invlpg (%0)" ::"r"(addr));
        u64 *v_pud = osmap(pud);
        v_pud_t = v_pud + offset_pud;
        pud_entry = *(v_pud_t);
    }

    u64 *v_pmd_t;
    u64 pmd_entry;
    u64 offset_pmd = (addr >> 21) & 0x1FF;

    if (pud_entry & 1 == 1)
    {
        *(v_pud_t) = *(v_pud_t) | 0x19;
        asm volatile("invlpg (%0)" ::"r"(addr));
        u64 *v_pmd = osmap(pud_entry >> 12);
        v_pmd_t = v_pmd + offset_pmd;
        pmd_entry = *(v_pmd_t);
    }
    else
    {
        u32 pmd = os_pfn_alloc(OS_PT_REG);
        *(v_pud_t) = pmd << 12;
        *(v_pud_t) = *(v_pud_t) | 0x19;
        asm volatile("invlpg (%0)" ::"r"(addr));
        u64 *v_pmd = osmap(pmd);
        v_pmd_t = v_pmd + offset_pmd;
        pmd_entry = *(v_pmd_t);
    }

    u64 *v_pte_t;
    u64 pte_entry;
    u64 offset_pte = (addr >> 12) & 0x1FF;

    if (pmd_entry & 1 == 1)
    {
        *(v_pmd_t) = *(v_pmd_t) | 0x19;
        asm volatile("invlpg (%0)" ::"r"(addr));
        u64 *v_pte = osmap(pmd_entry >> 12);
        v_pte_t = v_pte + offset_pte;
        pte_entry = *(v_pte_t);
    }
    else
    {
        u32 pte = os_pfn_alloc(OS_PT_REG);
        *(v_pmd_t) = pte << 12;
        *(v_pmd_t) = *(v_pmd_t) | 0x19;
        asm volatile("invlpg (%0)" ::"r"(addr));
        u64 *v_pte = osmap(pte);
        v_pte_t = v_pte + offset_pte;
        pte_entry = *(v_pte_t);
    }

    if (pte_entry & 1 == 1)
    {
        return 1;
    }
    else
    {
        u32 pfn = os_pfn_alloc(USER_REG);
        *(v_pte_t) = pfn << 12;
        *(v_pte_t) = *(v_pte_t) | 0x11 | ((error_code & 2) << 2);
        asm volatile("invlpg (%0)" ::"r"(addr));
        return 1;
    }
}

void copy_pte(u64 addr, struct exec_context *p_ctx, struct exec_context *c_ctx)
{

    u64 *v_pgd_parent = osmap(p_ctx->pgd);
    u64 offset_pgd = (addr >> 39) & 0x1FF;
    u64 *v_pgd_t_parent = (v_pgd_parent + offset_pgd);
    u64 pgd_entry_parent = *(v_pgd_t_parent);

    u64 *v_pgd_child = osmap(c_ctx->pgd);
    u64 *v_pgd_t_child = (v_pgd_child + offset_pgd);
    u64 pgd_entry_child = *(v_pgd_t_child);

    u64 *v_pud_t_parent;
    u64 *v_pud_t_child;
    u64 offset_pud = (addr >> 30) & 0x1FF;
    u64 pud_entry_parent;
    u64 pud_entry_child;

    if (pgd_entry_parent & 1 == 1)
    {
        u64 *v_pud_parent = osmap(pgd_entry_parent >> 12);
        v_pud_t_parent = v_pud_parent + offset_pud;
        pud_entry_parent = *(v_pud_t_parent);
        if (pgd_entry_child & 1 == 1)
        {
            *(v_pgd_t_child) = *(v_pgd_t_child) | 0x19;
            asm volatile("invlpg (%0)" ::"r"(addr));
            u64 *v_pud_child = osmap(pgd_entry_child >> 12);
            v_pud_t_child = v_pud_child + offset_pud;
            pud_entry_child = *(v_pud_t_child);
        }
        else
        {
            u32 pud = os_pfn_alloc(OS_PT_REG);
            *(v_pgd_t_child) = pud << 12;
            *(v_pgd_t_child) = *(v_pgd_t_child) | 0x19;
            asm volatile("invlpg (%0)" ::"r"(addr));
            u64 *v_pud_child = osmap(pud);
            v_pud_t_child = v_pud_child + offset_pud;
            pud_entry_child = *(v_pud_t_child);
        }
    }
    else
    {
        *(v_pgd_t_child) = *(v_pgd_t_child) & (~(1 << 0));
        asm volatile("invlpg (%0)" ::"r"(addr));
        return;
    }

    u64 *v_pmd_t_parent;
    u64 *v_pmd_t_child;
    u64 offset_pmd = (addr >> 21) & 0x1FF;
    u64 pmd_entry_parent;
    u64 pmd_entry_child;

    if (pud_entry_parent & 1 == 1)
    {
        u64 *v_pmd_parent = osmap(pud_entry_parent >> 12);
        v_pmd_t_parent = v_pmd_parent + offset_pmd;
        pmd_entry_parent = *(v_pmd_t_parent);
        if (pud_entry_child & 1 == 1)
        {
            *(v_pud_t_child) = *(v_pud_t_child) | 0x19;
            asm volatile("invlpg (%0)" ::"r"(addr));
            u64 *v_pmd_child = osmap(pud_entry_child >> 12);
            v_pmd_t_child = v_pmd_child + offset_pmd;
            pmd_entry_child = *(v_pmd_t_child);
        }
        else
        {
            u32 pmd = os_pfn_alloc(OS_PT_REG);
            *(v_pud_t_child) = pmd << 12;
            *(v_pud_t_child) = *(v_pud_t_child) | 0x19;
            asm volatile("invlpg (%0)" ::"r"(addr));
            u64 *v_pmd_child = osmap(pmd);
            v_pmd_t_child = v_pmd_child + offset_pmd;
            pmd_entry_child = *(v_pmd_t_child);
        }
    }
    else
    {
        *(v_pud_t_child) = *(v_pud_t_child) & (~(1 << 0));
        asm volatile("invlpg (%0)" ::"r"(addr));
        return;
    }

    u64 *v_pte_t_parent;
    u64 *v_pte_t_child;
    u64 offset_pte = (addr >> 12) & 0x1FF;
    u64 pte_entry_parent;
    u64 pte_entry_child;

    if (pmd_entry_parent & 1 == 1)
    {
        u64 *v_pte_parent = osmap(pmd_entry_parent >> 12);
        v_pte_t_parent = v_pte_parent + offset_pte;
        pte_entry_parent = *(v_pte_t_parent);
        if (pmd_entry_child & 1 == 1)
        {
            *(v_pmd_t_child) = *(v_pmd_t_child) | 0x19;
            asm volatile("invlpg (%0)" ::"r"(addr));
            u64 *v_pte_child = osmap(pmd_entry_child >> 12);
            v_pte_t_child = v_pte_child + offset_pte;
            pte_entry_child = *(v_pte_t_child);
        }
        else
        {
            u32 pte = os_pfn_alloc(OS_PT_REG);
            *(v_pmd_t_child) = pte << 12;
            *(v_pmd_t_child) = *(v_pmd_t_child) | 0x19;
            asm volatile("invlpg (%0)" ::"r"(addr));
            u64 *v_pte_child = osmap(pte);
            v_pte_t_child = v_pte_child + offset_pte;
            pte_entry_child = *(v_pte_t_child);
        }
    }
    else
    {
        *(v_pmd_t_child) = *(v_pmd_t_child) & (~(1 << 0));
        asm volatile("invlpg (%0)" ::"r"(addr));
        return;
    }

    if (pte_entry_parent & 1 == 1)
    {
        *(v_pte_t_parent) = *(v_pte_t_parent) & (~(1 << 3));
        *(v_pte_t_parent) = *(v_pte_t_parent) | (1 << 0) | (1 << 4);
        *(v_pte_t_child) = *(v_pte_t_parent);
        get_pfn(pte_entry_parent >> 12);
        asm volatile("invlpg (%0)" ::"r"(addr));
    }
    else
    {
        *(v_pte_t_child) = *(v_pte_t_child) & (~(1 << 0));
        asm volatile("invlpg (%0)" ::"r"(addr));
        return;
    }

    return;
}

//  You must not declare and use any static/global variables

/**
 * mprotect System call Implementation.
 */

long vm_area_mprotect(struct exec_context *current, u64 addr, int length, int prot)
{
    if (current == NULL)
    {
        return -EINVAL;
    }
    if (addr % 4096 != 0)
    {
        return -EINVAL;
    }
    if (!(prot == PROT_READ || (prot == (PROT_READ | PROT_WRITE))))
    {
        return -EINVAL;
    }
    if (length > (2 * 1024 * 1024))
    {
        return -EINVAL;
    }
    if (length <= 0)
    {
        return -EINVAL;
    }

    int length_update = ((length + 4095) / 4096) * 4096;
    int left_m, right_m = 0;
    // printk("length_update = %d\n",length_update);
    // printk("addr from which to be updated = %x\n",addr);

    struct vm_area *iterator_vm_area = current->vm_area;
    struct vm_area *prev_vm_area = NULL;
    struct vm_area *lneighbor_update = NULL;
    struct vm_area *left_neigbhour = NULL;

    while (iterator_vm_area->vm_next != NULL)
    {
        if (addr == iterator_vm_area->vm_start)
        {
            // printk("start address aligning with start of a block\n");
            if (prev_vm_area != NULL)
            {
                left_neigbhour = prev_vm_area;
            }
            lneighbor_update = iterator_vm_area;
            break;
        }
        else if (addr < iterator_vm_area->vm_next->vm_start && addr >= iterator_vm_area->vm_end)
        {
            // printk("start address between two blocks\n");
            left_neigbhour = iterator_vm_area;
            lneighbor_update = iterator_vm_area->vm_next;
            break;
        }
        else if (addr < iterator_vm_area->vm_end && addr > iterator_vm_area->vm_start)
        {
            // printk("start address between one block\n");
            // if(stats->num_vm_area < 128){
            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            insert_vm_area->access_flags = iterator_vm_area->access_flags;
            insert_vm_area->vm_start = addr;
            insert_vm_area->vm_end = iterator_vm_area->vm_end;
            insert_vm_area->vm_next = iterator_vm_area->vm_next;
            iterator_vm_area->vm_next = insert_vm_area;
            stats->num_vm_area++;
            // }
            iterator_vm_area->vm_end = addr;
            left_neigbhour = iterator_vm_area;
            lneighbor_update = insert_vm_area;
            break;
        }
        else
        {
            // printk("didnt enter anywhere i dont know whyyy\n");
        }
        prev_vm_area = iterator_vm_area;
        iterator_vm_area = iterator_vm_area->vm_next;
    }

    if (iterator_vm_area->vm_next == NULL && iterator_vm_area != NULL)
    {
        if (addr == iterator_vm_area->vm_start)
        {
            // printk("start address aligning with start of last block\n");
            if (prev_vm_area != NULL)
            {
                left_neigbhour = prev_vm_area;
            }
            lneighbor_update = iterator_vm_area;
        }
        else if (addr < iterator_vm_area->vm_next->vm_start && addr >= iterator_vm_area->vm_end)
        {
            // printk("start address between two blocks\n");
            left_neigbhour = iterator_vm_area;
            lneighbor_update = iterator_vm_area->vm_next;
        }
        else if (addr < iterator_vm_area->vm_end && addr > iterator_vm_area->vm_start)
        {
            // printk("start address between one block which is the last block\n");
            // if(stats->num_vm_area < 128){
            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            insert_vm_area->access_flags = iterator_vm_area->access_flags;
            insert_vm_area->vm_start = addr;
            insert_vm_area->vm_end = iterator_vm_area->vm_end;
            insert_vm_area->vm_next = iterator_vm_area->vm_next;
            iterator_vm_area->vm_next = insert_vm_area;
            stats->num_vm_area++;
            // printk("stats.num_vm_area increasing left= %d\n",stats->num_vm_area);
            iterator_vm_area->vm_end = addr;
            left_neigbhour = iterator_vm_area;
            lneighbor_update = insert_vm_area;
        }
    }

    iterator_vm_area = current->vm_area;
    struct vm_area *right_neigbhour = NULL;
    u64 end_addr = addr + length_update;
    // printk("end_addr from which to be updated = %x\n",end_addr);

    while (iterator_vm_area->vm_next != NULL)
    {
        if (end_addr == iterator_vm_area->vm_end)
        {
            // printk("end address at block's end address\n");
            right_neigbhour = iterator_vm_area->vm_next;
            break;
        }
        else if (end_addr > iterator_vm_area->vm_end && end_addr <= iterator_vm_area->vm_next->vm_start)
        {
            // printk("end address between two blocks\n");
            right_neigbhour = iterator_vm_area->vm_next;
            break;
        }
        else if (end_addr < iterator_vm_area->vm_end && end_addr > iterator_vm_area->vm_start)
        {
            // printk("end address between one block\n");
            // if(stats->num_vm_area < 128){
            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            insert_vm_area->access_flags = iterator_vm_area->access_flags;
            insert_vm_area->vm_start = end_addr;
            insert_vm_area->vm_end = iterator_vm_area->vm_end;
            insert_vm_area->vm_next = iterator_vm_area->vm_next;
            iterator_vm_area->vm_next = insert_vm_area;
            stats->num_vm_area++;
            iterator_vm_area->vm_end = end_addr;
            right_neigbhour = insert_vm_area;
            break;
        }
        iterator_vm_area = iterator_vm_area->vm_next;
    }
    // printk("end addr = %x\n",end_addr);
    if (iterator_vm_area->vm_next == NULL && iterator_vm_area != NULL)
    {
        if (end_addr > iterator_vm_area->vm_start && end_addr < iterator_vm_area->vm_end)
        {
            // printk("end address between the last block\n");
            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            insert_vm_area->access_flags = iterator_vm_area->access_flags;
            insert_vm_area->vm_start = end_addr;
            insert_vm_area->vm_end = iterator_vm_area->vm_end;
            insert_vm_area->vm_next = iterator_vm_area->vm_next;
            iterator_vm_area->vm_next = insert_vm_area;
            stats->num_vm_area++;
            // printk("stats.num_vm_area increasing right= %d\n",stats->num_vm_area);
            iterator_vm_area->vm_end = end_addr;
            right_neigbhour = insert_vm_area;
        }
        else if (end_addr > iterator_vm_area->vm_end && end_addr < MMAP_AREA_END)
        {
            // printk("end address after all the blocks\n");
            right_neigbhour = NULL;
        }
        else if (end_addr = iterator_vm_area->vm_end)
        {
            // printk("end address at last block's end address\n");
            right_neigbhour = NULL;
        }
    }

    iterator_vm_area = current->vm_area;
    prev_vm_area = NULL;
    if (lneighbor_update == NULL)
    {
        // printk("lneighbor_update == NULL\n");
        return 0;
    }
    while (iterator_vm_area->vm_next != NULL)
    {
        if (lneighbor_update == iterator_vm_area)
        {
            // printk("lneighbor_update found in between\n");
            break;
        }
        prev_vm_area = iterator_vm_area;
        iterator_vm_area = iterator_vm_area->vm_next;
        // printk("lneighbor_update not found in middle\n");
    }

    if (iterator_vm_area->vm_next == NULL)
    {
        if (iterator_vm_area == lneighbor_update)
        {
            // printk("lneighbor_update found at end\n");
        }
    }

    while (iterator_vm_area != right_neigbhour)
    {
        if (prev_vm_area->access_flags == prot && iterator_vm_area->vm_next->access_flags == prot)
        {
            if (prev_vm_area->vm_end == iterator_vm_area->vm_start && iterator_vm_area->vm_end == iterator_vm_area->vm_next->vm_start)
            {
                protect_pfn(current, iterator_vm_area->vm_start, iterator_vm_area->vm_end - iterator_vm_area->vm_start, prot);
                prev_vm_area->vm_end = iterator_vm_area->vm_next->vm_end;
                prev_vm_area->vm_next = iterator_vm_area->vm_next->vm_next;

                struct vm_area *del_vm_area = iterator_vm_area;
                os_free(del_vm_area, sizeof(struct vm_area));
                stats->num_vm_area--;

                del_vm_area = iterator_vm_area->vm_next;
                os_free(del_vm_area, sizeof(struct vm_area));
                stats->num_vm_area--;
            }
        }
        else if (prev_vm_area->access_flags == prot)
        {
            if (prev_vm_area->vm_end == iterator_vm_area->vm_start)
            {
                protect_pfn(current, iterator_vm_area->vm_start, iterator_vm_area->vm_end - iterator_vm_area->vm_start, prot);
                prev_vm_area->vm_end = iterator_vm_area->vm_end;
                prev_vm_area->vm_next = iterator_vm_area->vm_next;

                struct vm_area *del_vm_area = iterator_vm_area;
                os_free(del_vm_area, sizeof(struct vm_area));
                stats->num_vm_area--;
            }
        }
        else if (iterator_vm_area->vm_next->access_flags == prot)
        {
            if (iterator_vm_area->vm_end == iterator_vm_area->vm_next->vm_start)
            {
                protect_pfn(current, iterator_vm_area->vm_start, iterator_vm_area->vm_end - iterator_vm_area->vm_start, prot);
                prev_vm_area->vm_next = iterator_vm_area->vm_next;
                iterator_vm_area->vm_next->vm_start = iterator_vm_area->vm_start;

                struct vm_area *del_vm_area = iterator_vm_area;
                os_free(del_vm_area, sizeof(struct vm_area));
                stats->num_vm_area--;
            }
        }
        else
        {
            protect_pfn(current, iterator_vm_area->vm_start, iterator_vm_area->vm_end - iterator_vm_area->vm_start, prot);
            iterator_vm_area->access_flags = prot;
        }
        // printk("stats->num_vm_area after deleting = %d\n",stats->num_vm_area);
        prev_vm_area = iterator_vm_area;
        iterator_vm_area = iterator_vm_area->vm_next;
    }

    // printk("stats->num_vm_area before deleting = %d\n",stats->num_vm_area);
    // if(right_neigbhour->vm_start < lneighbor_update->vm_start){
    //     // printk("what the fuck\n");
    // }

    return 0;
}

/**
 * mmap system call implementation.
 */
long vm_area_map(struct exec_context *current, u64 addr, int length, int prot, int flags)
{
    if (current == NULL)
    {
        return -EINVAL;
    }
    if (flags == MAP_FIXED && !addr)
    {
        return -EINVAL;
    }
    if (!(flags == 0 || flags == MAP_FIXED))
    {
        return -EINVAL;
    }
    if (!(prot == PROT_READ || (prot == (PROT_READ | PROT_WRITE))))
    {
        return -EINVAL;
    }
    if (length <= 0)
    {
        return -EINVAL;
    }
    if (length > (2 * 1024 * 1024))
    {
        return -EINVAL;
    }
    if (addr % 4096 != 0)
    {
        return -EINVAL;
    }

    struct vm_area *head_of_vm_area = current->vm_area;
    int left_merge, right_merge = 0;

    int length_allot = ((length + 4095) / 4096) * 4096;

    if (head_of_vm_area == NULL)
    {
        if (stats->num_vm_area < 128)
        {
            struct vm_area *dummy = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            dummy->access_flags = 0;
            dummy->vm_start = MMAP_AREA_START;
            dummy->vm_end = dummy->vm_start + 4096;
            dummy->vm_next = NULL;
            current->vm_area = dummy;
            stats->num_vm_area++;
        }
        else
        {
            return -EINVAL;
        }
    }

    struct vm_area *temp = current->vm_area;

    if (addr == 0)
    {
        while (temp->vm_next != NULL)
        {
            if (temp->vm_next->vm_start - temp->vm_end >= length_allot)
            {
                // mergability check
                if (temp->vm_end + length_allot == temp->vm_next->vm_start && prot == temp->vm_next->access_flags)
                {
                    if (prot == temp->access_flags)
                    {
                        left_merge = 1;
                    }
                    right_merge = 1;
                }
                else if (prot == temp->access_flags)
                {
                    // printk("left merge flag lesgo");
                    left_merge = 1;
                }
                else
                {
                    left_merge = 0;
                    right_merge = 0;
                }
                if (left_merge == 1 && right_merge == 1)
                {
                    if (stats->num_vm_area > 0)
                    {
                        unsigned long start_new = temp->vm_end;
                        temp->vm_end = temp->vm_next->vm_end;
                        temp->vm_next = temp->vm_next->vm_next;
                        struct vm_area *del_vm_area = temp->vm_next;
                        os_free(del_vm_area, sizeof(struct vm_area));
                        stats->num_vm_area--;
                        return start_new;
                    }
                    else
                    {
                        return -EINVAL;
                    }
                }
                else if (left_merge == 1)
                {
                    unsigned long start_new = temp->vm_end;
                    // printk("left merge done lesgo at addr = %x",start_new);
                    temp->vm_end = temp->vm_end + length_allot;
                    return start_new;
                }
                else if (right_merge == 1)
                {
                    temp->vm_next->vm_start = temp->vm_next->vm_start - length_allot;
                    return temp->vm_next->vm_start;
                }
                else
                {
                    if (stats->num_vm_area < 128)
                    {
                        struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                        insert_vm_area->access_flags = prot;
                        insert_vm_area->vm_start = temp->vm_end;
                        // printk("why no left merge :(- addr= %x",insert_vm_area->vm_start);
                        insert_vm_area->vm_end = insert_vm_area->vm_start + length_allot;
                        insert_vm_area->vm_next = temp->vm_next;
                        temp->vm_next = insert_vm_area;
                        stats->num_vm_area++;
                        return insert_vm_area->vm_start;
                    }
                    else
                    {
                        return -EINVAL;
                    }
                }
            }
            temp = temp->vm_next;
        }
        if (temp->vm_end + length_allot <= MMAP_AREA_END)
        {
            // printk("prot = %d\n", prot);
            // printk("temp->access_flags = %d\n", temp->access_flags);
            if (prot == temp->access_flags)
            {
                unsigned long start_new = temp->vm_end;
                temp->vm_end = temp->vm_end + length_allot;
                return start_new;
            }
            if (stats->num_vm_area < 128)
            {
                struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                insert_vm_area->access_flags = prot;
                insert_vm_area->vm_start = temp->vm_end;
                insert_vm_area->vm_end = insert_vm_area->vm_start + length_allot;
                insert_vm_area->vm_next = NULL;
                temp->vm_next = insert_vm_area;
                stats->num_vm_area++;
                return insert_vm_area->vm_start;
            }
            else
            {
                return -EINVAL;
            }
        }
    }
    else if (addr != 0)
    {
        if (flags == MAP_FIXED)
        {
            // printk("entered flags =MAP_fixed");
            int check = 1;
            while (temp->vm_next != NULL && check == 1)
            {
                if (addr < temp->vm_next->vm_start)
                {
                    check = 0;
                }
                if (temp->vm_next->vm_start - temp->vm_end >= length_allot)
                {
                    if (addr >= temp->vm_end && addr < temp->vm_next->vm_start && addr + length_allot > temp->vm_end && addr + length_allot <= temp->vm_next->vm_start)
                    {
                        // mergability check
                        if (addr + length_allot == temp->vm_next->vm_start && prot == temp->vm_next->access_flags)
                        {
                            if (addr == temp->vm_end && prot == temp->access_flags)
                            {
                                left_merge = 1;
                            }
                            right_merge = 1;
                        }
                        else if (addr == temp->vm_end)
                        {
                            if (prot == temp->access_flags)
                            {
                                left_merge = 1;
                            }
                        }
                        else
                        {
                            left_merge = 0;
                            right_merge = 0;
                        }
                        if (left_merge == 1 && right_merge == 1)
                        {
                            if (stats->num_vm_area > 0)
                            {
                                temp->vm_end = temp->vm_next->vm_end;
                                temp->vm_next = temp->vm_next->vm_next;
                                struct vm_area *del_vm_area = temp->vm_next;
                                os_free(del_vm_area, sizeof(struct vm_area));
                                stats->num_vm_area--;
                            }
                            else
                            {
                                return -EINVAL;
                            }
                        }
                        else if (left_merge == 1)
                        {
                            temp->vm_end = temp->vm_end + length_allot;
                        }
                        else if (right_merge == 1)
                        {
                            temp->vm_next->vm_start = addr;
                        }
                        else
                        {
                            if (stats->num_vm_area < 128)
                            {
                                struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                                insert_vm_area->access_flags = prot;
                                insert_vm_area->vm_start = addr;
                                insert_vm_area->vm_end = insert_vm_area->vm_start + length_allot;
                                insert_vm_area->vm_next = temp->vm_next;
                                stats->num_vm_area++;
                                temp->vm_next = insert_vm_area;
                            }
                            else
                            {
                                return -EINVAL;
                            }
                        }
                        return addr;
                    }
                }
                temp = temp->vm_next;
            }
            if (addr >= temp->vm_end)
            {
                if (addr + length_allot <= MMAP_AREA_END)
                {
                    if (addr == temp->vm_end)
                    {
                        if (prot == temp->access_flags)
                        {
                            temp->vm_end = temp->vm_end + length_allot;
                            return addr;
                        }
                    }
                    if (stats->num_vm_area < 128)
                    {
                        struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                        insert_vm_area->access_flags = prot;
                        insert_vm_area->vm_start = addr;
                        insert_vm_area->vm_end = insert_vm_area->vm_start + length_allot;
                        insert_vm_area->vm_next = NULL;
                        temp->vm_next = insert_vm_area;
                        stats->num_vm_area++;
                        return insert_vm_area->vm_start;
                    }
                    else
                    {
                        return -EINVAL;
                    }
                }
            }
            return -EINVAL;
        }
        else if (flags == 0)
        {
            // printk("entered flags =0");
            int check = 1;
            while (temp->vm_next != NULL && check == 1)
            {
                if (addr < temp->vm_next->vm_start)
                {
                    check = 0;
                }
                if (temp->vm_next->vm_start - temp->vm_end >= length_allot)
                {
                    if (addr >= temp->vm_end && addr < temp->vm_next->vm_start && addr + length_allot > temp->vm_end && addr + length_allot <= temp->vm_next->vm_start)
                    {
                        // mergability check
                        if (addr + length_allot == temp->vm_next->vm_start && prot == temp->vm_next->access_flags)
                        {
                            if (addr == temp->vm_end && prot == temp->access_flags)
                            {
                                left_merge = 1;
                            }
                            right_merge = 1;
                        }
                        else if (addr == temp->vm_end)
                        {
                            if (prot == temp->access_flags)
                            {
                                left_merge = 1;
                            }
                        }
                        else
                        {
                            left_merge = 0;
                            right_merge = 0;
                        }
                        if (left_merge == 1 && right_merge == 1)
                        {
                            if (stats->num_vm_area > 0)
                            {
                                temp->vm_end = temp->vm_next->vm_end;
                                temp->vm_next = temp->vm_next->vm_next;
                                struct vm_area *del_vm_area = temp->vm_next;
                                os_free(del_vm_area, sizeof(struct vm_area));
                                stats->num_vm_area--;
                            }
                            else
                            {
                                return -EINVAL;
                            }
                        }
                        else if (left_merge == 1)
                        {
                            temp->vm_end = temp->vm_end + length_allot;
                        }
                        else if (right_merge == 1)
                        {
                            temp->vm_next->vm_start = addr;
                        }
                        else
                        {
                            if (stats->num_vm_area < 128)
                            {
                                struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                                insert_vm_area->access_flags = prot;
                                insert_vm_area->vm_start = addr;
                                // printk("why no left merge :(- addr= %x",insert_vm_area->vm_start);
                                insert_vm_area->vm_end = insert_vm_area->vm_start + length_allot;
                                insert_vm_area->vm_next = temp->vm_next;
                                stats->num_vm_area++;
                                temp->vm_next = insert_vm_area;
                            }
                            else
                            {
                                return -EINVAL;
                            }
                        }
                        // printk("inserted in between and at correct addr");
                        return addr;
                    }
                }
                temp = temp->vm_next;
            }
            if (addr >= temp->vm_end)
            {
                if (addr + length_allot <= MMAP_AREA_END)
                {
                    if (addr == temp->vm_end)
                    {
                        if (prot == temp->access_flags)
                        {
                            temp->vm_end = temp->vm_end + length_allot;
                            // printk("inserted et end, merged left and at correct addr");
                            return addr;
                        }
                    }
                    if (stats->num_vm_area < 128)
                    {
                        struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                        insert_vm_area->access_flags = prot;
                        insert_vm_area->vm_start = addr;
                        insert_vm_area->vm_end = insert_vm_area->vm_start + length_allot;
                        insert_vm_area->vm_next = NULL;
                        temp->vm_next = insert_vm_area;
                        stats->num_vm_area++;
                        // printk("inserted at end and at correct addr");
                        return insert_vm_area->vm_start;
                    }
                    else
                    {
                        return -EINVAL;
                    }
                }
            }

            struct vm_area *temp = current->vm_area;
            int left_merge, right_merge = 0;

            while (temp->vm_next != NULL)
            {
                if (temp->vm_next->vm_start - temp->vm_end >= length_allot)
                {
                    // mergability check
                    if (temp->vm_end + length_allot == temp->vm_next->vm_start && prot == temp->vm_next->access_flags)
                    {
                        if (prot == temp->access_flags)
                        {
                            left_merge = 1;
                        }
                        right_merge = 1;
                    }
                    else if (prot == temp->access_flags)
                    {
                        left_merge = 1;
                    }
                    else
                    {
                        left_merge = 0;
                        right_merge = 0;
                    }
                    if (left_merge == 1 && right_merge == 1)
                    {
                        if (stats->num_vm_area > 0)
                        {
                            unsigned long start_new = temp->vm_end;
                            temp->vm_end = temp->vm_next->vm_end;
                            temp->vm_next = temp->vm_next->vm_next;
                            struct vm_area *del_vm_area = temp->vm_next;
                            os_free(del_vm_area, sizeof(struct vm_area));
                            stats->num_vm_area--;
                            // printk("inserted middle lr merge");
                            return start_new;
                        }
                        else
                        {
                            return -EINVAL;
                        }
                    }
                    else if (left_merge == 1)
                    {
                        unsigned long start_new = temp->vm_end;
                        temp->vm_end = temp->vm_end + length_allot;
                        // printk("inserted middle l merge");
                        return start_new;
                    }
                    else if (right_merge == 1)
                    {
                        unsigned long start_new = temp->vm_next->vm_start;
                        temp->vm_next->vm_start = temp->vm_next->vm_start - length_allot;
                        // printk("inserted middle r merge");
                        return start_new;
                    }
                    else
                    {
                        if (stats->num_vm_area < 128)
                        {
                            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                            insert_vm_area->access_flags = prot;
                            insert_vm_area->vm_start = temp->vm_end;
                            // printk("why no left merge here :(- addr= %x",insert_vm_area->vm_start);
                            insert_vm_area->vm_end = insert_vm_area->vm_start + length_allot;
                            insert_vm_area->vm_next = temp->vm_next;
                            temp->vm_next = insert_vm_area;
                            stats->num_vm_area++;
                            // printk("inserted middle no merge");
                            return insert_vm_area->vm_start;
                        }
                        else
                        {
                            return -EINVAL;
                        }
                    }
                }
                temp = temp->vm_next;
            }
            if (temp->vm_end + length_allot <= MMAP_AREA_END)
            {
                if (prot == temp->access_flags)
                {
                    unsigned long start_new = temp->vm_end;
                    temp->vm_end = temp->vm_end + length_allot;
                    // printk("inserted end l merge");
                    return start_new;
                }
                if (stats->num_vm_area < 128)
                {
                    struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
                    insert_vm_area->access_flags = prot;
                    insert_vm_area->vm_start = temp->vm_end;
                    insert_vm_area->vm_end = insert_vm_area->vm_start + length_allot;
                    insert_vm_area->vm_next = NULL;
                    temp->vm_next = insert_vm_area;
                    stats->num_vm_area++;
                    // printk("inserted end no merge");
                    return insert_vm_area->vm_start;
                }
                else
                {
                    return -EINVAL;
                }
            }
        }
    }
    // printk("some unexpected error);
    return -EINVAL;
}

/**
 * munmap system call implemenations
 */

long vm_area_unmap(struct exec_context *current, u64 addr, int length)
{
    if (current == NULL)
    {
        return -EINVAL;
    }
    if (addr % 4096 != 0)
    {
        return -EINVAL;
    }
    if (length > (2 * 1024 * 1024))
    {
        return -EINVAL;
    }
    if (length <= 0)
    {
        return -EINVAL;
    }

    int length_remove = ((length + 4095) / 4096) * 4096;
    // printk("length_remove = %d\n",length_remove);
    // printk("addr from which to be removed = %x\n",addr);

    struct vm_area *iterator_vm_area = current->vm_area;
    struct vm_area *prev_vm_area = NULL;
    struct vm_area *lneighbor_delete = NULL;
    struct vm_area *left_neigbhour = NULL;

    while (iterator_vm_area->vm_next != NULL)
    {
        if (addr == iterator_vm_area->vm_start)
        {
            // printk("start address aligning with start of a block\n");
            if (prev_vm_area != NULL)
            {
                left_neigbhour = prev_vm_area;
            }
            lneighbor_delete = iterator_vm_area;
            break;
        }
        else if (addr < iterator_vm_area->vm_next->vm_start && addr >= iterator_vm_area->vm_end)
        {
            // printk("start address between two blocks\n");
            left_neigbhour = iterator_vm_area;
            lneighbor_delete = iterator_vm_area->vm_next;
            break;
        }
        else if (addr < iterator_vm_area->vm_end && addr > iterator_vm_area->vm_start)
        {
            // printk("start address between one block\n");
            // if(stats->num_vm_area < 128){
            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            insert_vm_area->access_flags = iterator_vm_area->access_flags;
            insert_vm_area->vm_start = addr;
            insert_vm_area->vm_end = iterator_vm_area->vm_end;
            insert_vm_area->vm_next = iterator_vm_area->vm_next;
            iterator_vm_area->vm_next = insert_vm_area;
            stats->num_vm_area++;
            // }
            iterator_vm_area->vm_end = addr;
            left_neigbhour = iterator_vm_area;
            lneighbor_delete = insert_vm_area;
            break;
        }
        else
        {
            //  printk("didnt enter anywhere i dont know whyyy\n");
        }
        prev_vm_area = iterator_vm_area;
        iterator_vm_area = iterator_vm_area->vm_next;
    }

    if (iterator_vm_area->vm_next == NULL && iterator_vm_area != NULL)
    {
        if (addr == iterator_vm_area->vm_start)
        {
            // printk("start address aligning with start of last block\n");
            if (prev_vm_area != NULL)
            {
                left_neigbhour = prev_vm_area;
            }
            lneighbor_delete = iterator_vm_area;
        }
        else if (addr < iterator_vm_area->vm_next->vm_start && addr >= iterator_vm_area->vm_end)
        {
            // printk("start address between two blocks\n");
            left_neigbhour = iterator_vm_area;
            lneighbor_delete = iterator_vm_area->vm_next;
        }
        else if (addr < iterator_vm_area->vm_end && addr > iterator_vm_area->vm_start)
        {
            // printk("start address between one block which is the last block\n");
            // if(stats->num_vm_area < 128){
            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            insert_vm_area->access_flags = iterator_vm_area->access_flags;
            insert_vm_area->vm_start = addr;
            insert_vm_area->vm_end = iterator_vm_area->vm_end;
            insert_vm_area->vm_next = iterator_vm_area->vm_next;
            iterator_vm_area->vm_next = insert_vm_area;
            stats->num_vm_area++;
            // printk("stats.num_vm_area increasing left= %d\n",stats->num_vm_area);
            // }
            iterator_vm_area->vm_end = addr;
            left_neigbhour = iterator_vm_area;
            lneighbor_delete = insert_vm_area;
        }
    }

    iterator_vm_area = current->vm_area;
    struct vm_area *right_neigbhour = NULL;
    u64 end_addr = addr + length_remove;
    // printk("end_addr from which to be removed = %x\n",end_addr);

    while (iterator_vm_area->vm_next != NULL)
    {
        if (end_addr == iterator_vm_area->vm_end)
        {
            // printk("end address at block's end address\n");
            right_neigbhour = iterator_vm_area->vm_next;
            break;
        }
        else if (end_addr > iterator_vm_area->vm_end && end_addr <= iterator_vm_area->vm_next->vm_start)
        {
            // printk("end address between two blocks\n");
            right_neigbhour = iterator_vm_area->vm_next;
            break;
        }
        else if (end_addr < iterator_vm_area->vm_end && end_addr > iterator_vm_area->vm_start)
        {
            // printk("end address between one block\n");
            // if(stats->num_vm_area < 128){
            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            insert_vm_area->access_flags = iterator_vm_area->access_flags;
            insert_vm_area->vm_start = end_addr;
            insert_vm_area->vm_end = iterator_vm_area->vm_end;
            insert_vm_area->vm_next = iterator_vm_area->vm_next;
            iterator_vm_area->vm_next = insert_vm_area;
            stats->num_vm_area++;
            // }
            iterator_vm_area->vm_end = end_addr;
            right_neigbhour = insert_vm_area;
            break;
        }
        iterator_vm_area = iterator_vm_area->vm_next;
    }
    // printk("end addr = %x\n",end_addr);
    if (iterator_vm_area->vm_next == NULL && iterator_vm_area != NULL)
    {
        if (end_addr > iterator_vm_area->vm_start && end_addr < iterator_vm_area->vm_end)
        {
            // printk("end address between the last block\n");
            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            insert_vm_area->access_flags = iterator_vm_area->access_flags;
            insert_vm_area->vm_start = end_addr;
            insert_vm_area->vm_end = iterator_vm_area->vm_end;
            insert_vm_area->vm_next = iterator_vm_area->vm_next;
            iterator_vm_area->vm_next = insert_vm_area;
            stats->num_vm_area++;
            // printk("stats.num_vm_area increasing right= %d\n",stats->num_vm_area);
            iterator_vm_area->vm_end = end_addr;
            right_neigbhour = insert_vm_area;
        }
        else if (end_addr > iterator_vm_area->vm_end && end_addr < MMAP_AREA_END)
        {
            // printk("end address after all the blocks\n");
            right_neigbhour = NULL;
        }
        else if (end_addr = iterator_vm_area->vm_end)
        {
            // printk("end address at last block's end address\n");
            right_neigbhour = NULL;
        }
    }

    iterator_vm_area = current->vm_area;
    if (lneighbor_delete == NULL)
    {
        // printk("lneighbor_delete == NULL\n");
        return 0;
    }
    while (iterator_vm_area->vm_next != NULL)
    {
        if (lneighbor_delete == iterator_vm_area)
        {
            left_neigbhour->vm_next = right_neigbhour;
            // printk("lneighbor_delete found in between\n");
            break;
        }
        iterator_vm_area = iterator_vm_area->vm_next;
        // printk("lneighbor_delete not found in middle\n");
    }

    if (iterator_vm_area->vm_next == NULL)
    {
        if (iterator_vm_area == lneighbor_delete)
        {
            // printk("lneighbor_delete found at end\n");
        }
    }

    while (iterator_vm_area != right_neigbhour)
    {
        struct vm_area *next = iterator_vm_area->vm_next;
        os_free(iterator_vm_area, sizeof(struct vm_area));
        free_pfn(current, iterator_vm_area->vm_start, iterator_vm_area->vm_end - iterator_vm_area->vm_start);
        if (stats->num_vm_area > 0)
        {
            stats->num_vm_area--;
            // printk("stats.num_vm_area after deleting= %d\n",stats->num_vm_area);
        }
        else
        {
            return 0;
        }
        // printk("stats->num_vm_area after deleting = %d\n",stats->num_vm_area);
        iterator_vm_area = next;
    }
    // printk("end of code for munmap\n");
    return 0;
}

/**
 * Function will invoked whenever there is page fault for an address in the vm area region
 * created using mmap
 */

long vm_area_pagefault(struct exec_context *current, u64 addr, int error_code)
{
    if (current == NULL)
    {
        return -EINVAL;
    }

    if (0 < addr && addr < MMAP_AREA_START + 4096)
    {
        return -EINVAL;
    }

    if (addr > MMAP_AREA_END)
    {
        return -EINVAL;
    }

    if (error_code != 0x4 && error_code != 0x6 && error_code != 0x7)
    {
        return -EINVAL;
    }

    if (current->vm_area == NULL)
    {
        return -EINVAL;
    }

    error_code &= 0b111;
    struct vm_area *head_of_vm_area = current->vm_area;

    int found_vm = -1;

    while (head_of_vm_area->vm_next != NULL)
    {
        if (head_of_vm_area->vm_start > addr && head_of_vm_area->vm_end <= addr)
        {
            found_vm = 0;
            return -EINVAL;
        }
        if (head_of_vm_area->vm_start <= addr && head_of_vm_area->vm_end > addr)
        {
            found_vm = 1;
            break;
        }
        head_of_vm_area = head_of_vm_area->vm_next;
    }

    if (found_vm == -1)
    {
        if (head_of_vm_area->vm_start <= addr && head_of_vm_area->vm_end > addr)
        {
            found_vm = 1;
        }
        else
        {
            return -EINVAL;
        }
    }

    if (found_vm == 1)
    {
        if ((error_code == 6 || error_code == 7) && head_of_vm_area->access_flags == PROT_READ)
        {
            return -EINVAL;
        }
        if (error_code == 7 && head_of_vm_area->access_flags == (PROT_READ | PROT_WRITE))
        {
            handle_cow_fault(current, addr, head_of_vm_area->access_flags);
            return 1;
        }
    }

    page_table_walk_all(current, addr, error_code);
}

/**
 * cfork system call implemenations
 * The parent returns the pid of child process. The return path of
 * the child process is handled separately through the calls at the
 * end of this function (e.g., setup_child_context etc.)
 */

long do_cfork()
{
    u32 pid;
    struct exec_context *new_ctx = get_new_ctx();
    struct exec_context *ctx = get_current_ctx();
    /* Do not modify above lines
     *
     * */
    // printk("address of first vm_area %x %x\n", ctx -> vm_area -> vm_next, new_ctx -> vm_area -> vm_next);

    new_ctx->ppid = ctx->pid;

    pid = new_ctx->pid;

    new_ctx->state = ctx->state;

    new_ctx->type = ctx->type;

    new_ctx->os_stack_pfn = ctx->os_stack_pfn;

    new_ctx->os_rsp = ctx->os_rsp;

    new_ctx->used_mem = ctx->used_mem;

    new_ctx->regs = ctx->regs;

    new_ctx->alarm_config_time = ctx->alarm_config_time;

    new_ctx->ticks_to_sleep = ctx->ticks_to_sleep;

    new_ctx->ticks_to_alarm = ctx->ticks_to_alarm;

    new_ctx->ctx_threads = ctx->ctx_threads;

    new_ctx->pending_signal_bitmap = ctx->pending_signal_bitmap;

    new_ctx->pgd = os_pfn_alloc(OS_PT_REG);

    if (new_ctx->pgd == 0)
    {
        return -EINVAL;
    }

    for (int i = 0; i < MAX_MM_SEGS; i++)
    {
        new_ctx->mms[i] = ctx->mms[i];
    }

    for (int i = 0; i < MAX_SIGNALS; i++)
    {
        new_ctx->sighandlers[i] = ctx->sighandlers[i];
    }

    for (int i = 0; i < CNAME_MAX; i++)
    {
        new_ctx->name[i] = ctx->name[i];
    }

    for (int i = 0; i < MAX_OPEN_FILES; i++)
    {
        new_ctx->files[i] = ctx->files[i];
    }

    struct vm_area *p_vm_area = ctx->vm_area;
    struct vm_area *p_vm_area_temp = p_vm_area;

    new_ctx->vm_area = NULL;

    if (p_vm_area_temp != NULL)
    {
        struct vm_area *dummy = (struct vm_area *)os_alloc(sizeof(struct vm_area));
        dummy->access_flags = 0;
        dummy->vm_start = MMAP_AREA_START;
        dummy->vm_end = dummy->vm_start + 4096;
        dummy->vm_next = NULL;
        new_ctx->vm_area = dummy;
    }
    p_vm_area_temp = p_vm_area_temp->vm_next;

    struct vm_area *c_vm_area = new_ctx->vm_area;
    struct vm_area *c_vm_area_temp = c_vm_area;

    if (new_ctx->vm_area != NULL)
    {
        while (p_vm_area_temp != NULL)
        {
            struct vm_area *insert_vm_area = (struct vm_area *)os_alloc(sizeof(struct vm_area));
            insert_vm_area->access_flags = p_vm_area_temp->access_flags;
            insert_vm_area->vm_start = p_vm_area_temp->vm_start;
            insert_vm_area->vm_end = p_vm_area_temp->vm_end;
            c_vm_area_temp->vm_next = insert_vm_area;
            c_vm_area_temp = insert_vm_area;
            p_vm_area_temp = p_vm_area_temp->vm_next;
        }
        c_vm_area_temp->vm_next = NULL;
    }

    p_vm_area_temp = p_vm_area->vm_next;

    if (p_vm_area != NULL)
    {
        while (p_vm_area_temp != NULL)
        {
            for (u64 page_addr = p_vm_area_temp->vm_start; page_addr < p_vm_area_temp->vm_end; page_addr += 4096)
            {
                copy_pte(page_addr, ctx, new_ctx);
            }
            p_vm_area_temp = p_vm_area_temp->vm_next;
        }
    }

    // printk("address of first vm_area %x %x\n", ctx -> vm_area -> vm_next, new_ctx -> vm_area -> vm_next);

    int i = 0;
    while (i != 3)
    {
        for (u64 page_addr = ctx->mms[i].start; page_addr < ctx->mms[i].next_free; page_addr += 4096)
        {
            // printk("fork: mms segment [%d]\n",i);
            copy_pte(page_addr, ctx, new_ctx);
        }
        i++;
    }

    // printk("do fork before copy_pte\n");
    u64 page_addr = ctx->mms[3].start;
    while (page_addr < ctx->mms[3].end)
    {
        copy_pte(page_addr, ctx, new_ctx);
        page_addr += 4096;
    }
    // printk("do fork after copy_pte\n");

    /*
     * The remaining part must not be changed
     */
    copy_os_pts(ctx->pgd, new_ctx->pgd);
    do_file_fork(new_ctx);
    setup_child_context(new_ctx);
    return pid;
}

/* Cow fault handling, for the entire user address space
 * For address belonging to memory segments (i.e., stack, data)
 * it is called when there is a CoW violation in these areas.
 *
 * For vm areas, your fault handler 'vm_area_pagefault'
 * should invoke this function
 * */

long handle_cow_fault(struct exec_context *current, u64 vaddr, int access_flags)
{
    // printk("addr %x access_flags %x pid %d\n", vaddr, access_flags, current->pid);
    u64 *v_pgd = osmap(current->pgd);
    u64 offset_pgd = (vaddr >> (39)) & 0x1FF;
    u64 *v_pgd_t = (v_pgd + offset_pgd);
    u64 pgd_entry = *(v_pgd_t);

    u64 *v_pud_t;
    u64 offset_pud = (vaddr >> 30) & 0x1FF;
    u64 pud_entry;

    if (pgd_entry & 1 == 1)
    {
        u64 *v_pud = osmap((pgd_entry >> 12));
        v_pud_t = v_pud + offset_pud;
        pud_entry = *(v_pud_t);
    }
    else
    {
        return -EINVAL;
    }

    u64 *v_pmd_t;
    u64 offset_pmd = (vaddr >> 21) & 0x1FF;
    u64 pmd_entry;

    if (pud_entry & 1 == 1)
    {
        u64 *v_pmd = osmap((pud_entry >> 12));
        v_pmd_t = v_pmd + offset_pmd;
        pmd_entry = *(v_pmd_t);
    }
    else
    {
        return -EINVAL;
    }

    u64 *v_pte_t;
    u64 offset_pte = (vaddr >> 12) & 0x1FF;
    u64 pte_entry;

    if (pmd_entry & 1 == 1)
    {
        u64 *v_pte = osmap((pmd_entry) >> 12);
        v_pte_t = v_pte + offset_pte;
        pte_entry = *(v_pte_t);
    }
    else
    {
        return -EINVAL;
    }

    if (pte_entry & 1 == 1)
    {
        int refcount = get_pfn_refcount((pte_entry >> 12));
        if (refcount > 1)
        {
            u32 pfn = os_pfn_alloc(USER_REG);
            if (pfn == 0)
            {
                return -EINVAL;
            }

            put_pfn((pte_entry >> 12) & 0xFFFFFFFF);
            memcpy((char *)osmap(pfn), (char *)osmap((pte_entry >> 12) & 0xFFFFFFFF), 4096);
            *(v_pte_t) = (pfn << 12);
            *(v_pte_t) = *(v_pte_t) | (*(v_pte_t) & 0xFFF);
        }

        if (access_flags == (PROT_READ | PROT_WRITE))
        {
            *(v_pte_t) = *(v_pte_t) | 0x19;
        }
        else
        {
            *(v_pte_t) = *(v_pte_t) | 0x11;
        }
        asm volatile("invlpg (%0)" ::"r"(vaddr));
    }
    else
    {
        return -EINVAL;
    }

    return 1;
}