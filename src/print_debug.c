#include "print_debug.h"
#include <stdlib.h>
#include <string.h>

void print_frame_list(const char *name, struct framephy_struct *head)
{
    printf("%s: ", name);

    if (!head)
    {
        printf("(empty)\n");
        return;
    }

    while (head)
    {
        printf("[%lu] ", (unsigned long)head->fpn);
        head = head->fp_next;
    }

    printf("\n");
}

void print_storage_int(struct memphy_struct *m)
{
    if (!m || !m->storage)
    {
        printf("storage is NULL\n");
        return;
    }

    printf("STORAGE\n");

    int page_size = PAGING64_PAGESZ;
    int ints_per_page = page_size / sizeof(int);
    int total_pages = m->maxsz / page_size;

    int limit_per_line = 16;

    for (int pg = 0; pg < total_pages; pg++)
    {
        printf("[PAGE %d] : ",
               pg);

        for (int i = 0; i < limit_per_line; i++)
        {
            int idx = pg * ints_per_page + i;
            int *p = (int *)(m->storage + idx * sizeof(int));
            printf("%08x ", *p);
        }

        printf("\n");
    }
}

void print_memphy(const char *name, struct memphy_struct *m)
{
    printf("%s | size:%d\n",
           name,
           m->maxsz);

    print_storage_int(m);
    print_frame_list("FREE frames", m->free_fp_list);
    print_frame_list("USED frames", m->used_fp_list);
    printf("======================\n");
}

void print_kernel_info(struct krnl_t *os)
{

    print_memphy("RAM", os->mram);
    print_memphy("ACTIVE SWAP", os->active_mswp);

    print_mm_struct(os->mm);
}

uint32_t pte_get_entry_print(struct mm_struct *mm, addr_t pgn)
{
    addr_t pgd_idx = 0, p4d_idx = 0, pud_idx = 0, pmd_idx = 0, pt_idx = 0;

    get_pd_from_pagenum(pgn,
                        &pgd_idx,
                        &p4d_idx,
                        &pud_idx,
                        &pmd_idx,
                        &pt_idx);

    addr_t *p4d = (addr_t *)mm->pgd[pgd_idx];
    addr_t *pud = (addr_t *)p4d[p4d_idx];
    addr_t *pmd = (addr_t *)pud[pud_idx];
    addr_t *pt = (addr_t *)pmd[pmd_idx];
    return (uint32_t)pt[pt_idx];
}

void print_mm_struct(struct mm_struct *mm)
{
    printf("MM STRUCT\n");

    if (!mm)
    {
        printf("mm = NULL\n");
        return;
    }

    printf("PAGE TABLE: ");

    for (int i = 0; i < 6; i++)
    {
        uint32_t pte = pte_get_entry_print(mm, i);

        if (PAGING_PAGE_PRESENT(pte))
            printf("[page=%d -> frame=%u] ", i, PAGING_PTE_FPN(pte));
        else if (pte & PAGING_PTE_SWAPPED_MASK)
            printf("[page=%d -> swap=%u] ", i, PAGING_PTE_SWP(pte));
        else
            printf("[page=%d -> none] ", i);
    }

    printf("\n");

    /* --- VMA list --- */
    struct vm_area_struct *vma = mm->mmap;

    if (!vma)
        printf("  (empty)\n");

    while (vma)
    {
        printf("VMA | id=%lu | start=%lu | end=%lu | sbrk=%lu | ",
               vma->vm_id,
               (unsigned long)vma->vm_start,
               (unsigned long)vma->vm_end,
               (unsigned long)vma->sbrk);

        struct vm_rg_struct *rg = vma->vm_freerg_list;
        printf("free regions: ");

        if (!rg)
        {
            printf("(empty)");
        }
        else
        {
            for (; rg; rg = rg->rg_next)
            {
                printf("[%lu-%lu]",
                       (unsigned long)rg->rg_start,
                       (unsigned long)rg->rg_end);

                if (rg->rg_next)
                    printf(" -> ");
            }
        }

        printf("\n");

        vma = vma->vm_next;
    }

    /* --- Symbol table --- */
    printf("SYMBOL TABLE: ");

    int empty = 1;

    for (int i = 0; i < PAGING_MAX_SYMTBL_SZ; i++)
    {
        if (mm->symrgtbl[i].rg_start != 0 ||
            mm->symrgtbl[i].rg_end != 0)
        {
            printf("[rg%d:%lu-%lu] ",
                   i,
                   (unsigned long)mm->symrgtbl[i].rg_start,
                   (unsigned long)mm->symrgtbl[i].rg_end);
            empty = 0;
        }
    }

    if (empty)
        printf("(empty)");

    printf("\n");

    /* --- FIFO page list --- */
    printf("FIFO PAGE LIST: ");

    struct pgn_t *p = mm->fifo_pgn;

    if (!p)
    {
        printf("(empty)");
    }
    else
    {
        for (; p; p = p->pg_next)
        {
            printf("[%lu]", (unsigned long)p->pgn);

            if (p->pg_next)
                printf(" -> ");
        }
    }

    printf("\n================================ END ================================\n\n");
}