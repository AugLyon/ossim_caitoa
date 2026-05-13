/*
 * Copyright (C) 2026 pdnguyen of HCMC University of Technology VNU-HCM
 */

/* Caitoa release
 * Source Code License Grant: The authors hereby grant to Licensee
 * personal permission to use and modify the Licensed Source Code
 * for the sole purpose of studying while attending the course CO2018.
 */

// #ifdef MM_PAGING
/*
 * System Library
 * Memory Module Library libmem.c
 */

#include "string.h"
#include "mm.h"
#include "mm64.h"
#include "syscall.h"
#include "libmem.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <pthread.h>
#include "print_debug.h"
static pthread_mutex_t mmvm_lock = PTHREAD_MUTEX_INITIALIZER;
/*enlist_vm_freerg_list - add new rg to freerg_list
 *@mm: memory region
 *@rg_elmt: new region
 *
 */
int enlist_vm_freerg_list(struct mm_struct *mm, struct vm_rg_struct *rg_elmt)
{
  struct vm_rg_struct *rg_node = mm->mmap->vm_freerg_list;
  struct vm_rg_struct *prev = NULL;
  while (rg_node != NULL)
  {
    if (rg_elmt->rg_end == rg_node->rg_start)
    {
      rg_elmt->rg_end = rg_node->rg_end;
      struct vm_rg_struct *tmp = rg_node;
      if (prev)
        prev->rg_next = rg_node->rg_next;
      else
        mm->mmap->vm_freerg_list = rg_node->rg_next;
      rg_node = rg_node->rg_next;
      free(tmp);
      continue;
    }
    if (rg_node->rg_end == rg_elmt->rg_start)
    {
      rg_node->rg_end = rg_elmt->rg_end;
      free(rg_elmt);
      return 0;
    }
    prev = rg_node;
    rg_node = rg_node->rg_next;
  }
  rg_elmt->rg_next = mm->mmap->vm_freerg_list;
  mm->mmap->vm_freerg_list = rg_elmt;
  return 0;
}

/*get_symrg_byid - get mem region by region ID
 *@mm: memory region
 *@rgid: region ID act as symbol index of variable
 *
 */
struct vm_rg_struct *get_symrg_byid(struct mm_struct *mm, int rgid)
{
  if (mm == NULL)
  {
    return NULL;
  }
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
    return NULL;

  return &mm->symrgtbl[rgid];
}

/*__alloc - allocate a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *@alloc_addr: address of allocated memory region
 *
 */
int __alloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /*Allocate at the toproof */
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct rgnode;
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);
  addr_t inc_sz = 0;
  if (get_free_vmrg_area(caller, vmaid, size, &rgnode) == 0)
  {
    caller->mm->symrgtbl[rgid].rg_start = rgnode.rg_start;
    caller->mm->symrgtbl[rgid].rg_end = rgnode.rg_end;

    *alloc_addr = rgnode.rg_start;
    int start_pgn = (*alloc_addr) / PAGING64_PAGESZ;
    int end_pgn = ((*alloc_addr) + size - 1) / PAGING64_PAGESZ;
    int dummy_fpn;
    for (int pgn = start_pgn; pgn <= end_pgn; pgn++)
    {
      pg_getpage(caller->mm, pgn, &dummy_fpn, caller);
    }
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
  /* TODO get_free_vmrg_area FAILED handle the region management (Fig.6)*/

  /*Attempt to increate limit to get space */
#ifdef MM64
  inc_sz = PAGING64_PAGE_ALIGNSZ(size);
#else
  inc_sz = PAGING_PAGE_ALIGNSZ(size);
#endif
  addr_t old_sbrk;
  old_sbrk = cur_vma->sbrk;

  /* TODO INCREASE THE LIMIT
   * SYSCALL 1 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_INC_OP;
  regs.a2 = vmaid;
#ifdef MM64
  regs.a3 = inc_sz;
#else
  regs.a3 = PAGING_PAGE_ALIGNSZ(size);
#endif
  int ret = _syscall(caller->krnl, caller->pid, 17, &regs); /* SYSCALL 17 sys_memmap */
  if (ret == -1)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  /*Successful increase limit */
  caller->mm->symrgtbl[rgid].rg_start = old_sbrk;
  caller->mm->symrgtbl[rgid].rg_end = old_sbrk + size;

  *alloc_addr = old_sbrk;
  addr_t actual_inc_size = regs.a3;
  if (actual_inc_size > size)
  {
    struct vm_rg_struct *leftover_rg = malloc(sizeof(struct vm_rg_struct));
    leftover_rg->rg_start = old_sbrk + size;
    leftover_rg->rg_end = old_sbrk + actual_inc_size;
    leftover_rg->rg_next = NULL;
    enlist_vm_freerg_list(caller->mm, leftover_rg);
  }
  int start_pgn_inc = (*alloc_addr) / PAGING64_PAGESZ;
  int end_pgn_inc = ((*alloc_addr) + size - 1) / PAGING64_PAGESZ;
  int dummy_fpn_inc;
  for (int pgn = start_pgn_inc; pgn <= end_pgn_inc; pgn++)
  {
    pg_getpage(caller->mm, pgn, &dummy_fpn_inc, caller);
  }
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__free - remove a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __free(struct pcb_t *caller, int vmaid, int rgid)
{
  pthread_mutex_lock(&mmvm_lock);

  /* TODO: Manage the collect freed region to freerg_list */
  struct vm_rg_struct *rgnode = get_symrg_byid(caller->mm, rgid);
  if (rgnode == NULL || (rgnode->rg_start == 0 && rgnode->rg_end == 0))
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *freerg_node = malloc(sizeof(struct vm_rg_struct));
  freerg_node->rg_start = rgnode->rg_start;
  freerg_node->rg_end = rgnode->rg_end;
  freerg_node->rg_next = NULL;

  rgnode->rg_start = rgnode->rg_end = 0;
  rgnode->rg_next = NULL;

  /*enlist the obsoleted memory region */
  enlist_vm_freerg_list(caller->mm, freerg_node);

  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*liballoc - PAGING-based allocate a region memory
 *@proc:  Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */
int liballoc(struct pcb_t *proc, addr_t size, uint32_t reg_index)
{
  addr_t addr;
  int val = __alloc(proc, 0, reg_index, size, &addr);
  if (val == -1)
  {
    return -1;
  }
  proc->regs[reg_index] = addr;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  /* By default using vmaid = 0 */
  return val;
}

/*libfree - PAGING-based free a region memory
 *@proc: Process executing the instruction
 *@size: allocated size
 *@reg_index: memory region ID (used to identify variable in symbole table)
 */

int libfree(struct pcb_t *proc, uint32_t reg_index)
{
  int val = __free(proc, 0, reg_index);
  if (val == -1)
  {
    return -1;
  }
  proc->regs[reg_index] = 0;
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return val; // val;
}

/*pg_getpage - get the page in ram
 *@mm: memory region
 *@pagenum: PGN
 *@framenum: return FPN
 *@caller: caller
 *
 */
int pg_getpage(struct mm_struct *mm, int pgn, int *fpn, struct pcb_t *caller)
{

  addr_t pte = pte_get_entry(caller, pgn);

  if (!PAGING_PAGE_PRESENT(pte))
  { /* Page is not online, make it actively living */
    printf("DEBUG: Page not present\n");
    addr_t free_fpn;
    int is_swapped = (pte & PAGING_PTE_SWAPPED_MASK) != 0;
    addr_t target_swpfpn = 0;
    if (is_swapped)
    {
      target_swpfpn = GETVAL(pte, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT);
    }
    if (MEMPHY_get_freefp(caller->krnl->mram, &free_fpn) == 0)
    {
      printf("DEBUG: Free Ram available\n");
      if (is_swapped)
      {
        struct sc_regs regs;
        regs.a1 = SYSMEM_SWP_OP;
        regs.a2 = target_swpfpn;
        regs.a3 = free_fpn;
        regs.a4 = 1;
        _syscall(caller->krnl, caller->pid, 17, &regs);
        MEMPHY_put_freefp(caller->krnl->active_mswp, target_swpfpn);
      }
      pte_set_fpn(caller, pgn, free_fpn);
      enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
      *fpn = free_fpn;
    }
    else
    {
      printf("DEBUG: Find victim pgae\n");
      addr_t vicpgn, swpfpn;
      addr_t vicfpn;
      addr_t vicpte;
      struct sc_regs regs;

      /* TODO Initialize the target frame storing our variable */
      addr_t tgtfpn;
      /* TODO: Play with your paging theory here */
      /* Find victim page */
      if (find_victim_page(caller->mm, &vicpgn) == -1)
      {
        return -1;
      }
      printf("\n--- SWAP OUT TRIGGERED ---\n");
      printf("RAM is full! Evicting Virtual Page Number (PGN): %ld to Swap.\n", (long)vicpgn);
      if (MEMPHY_get_freefp(caller->krnl->active_mswp, &swpfpn) == -1)
      {
        return -1;
      }

      /* TODO: Implement swap frame from MEMRAM to MEMSWP and vice versa*/

      /* TODO copy victim frame to swap
       * SWP(vicfpn <--> swpfpn)
       * SYSCALL 1 sys_memmap
       */
      vicpte = pte_get_entry(caller, vicpgn);
      vicfpn = PAGING_FPN(vicpte);
      regs.a1 = SYSMEM_SWP_OP;
      regs.a2 = vicfpn;
      regs.a3 = swpfpn;
      regs.a4 = 0;
      _syscall(caller->krnl, caller->pid, 17, &regs);
      tgtfpn = vicfpn;
      pte_set_swap(caller, vicpgn, 0, swpfpn);
      /* Update page table */
      // pte_set_swap(...);
      if (is_swapped)
      {
        regs.a1 = SYSMEM_SWP_OP;
        regs.a2 = target_swpfpn;
        regs.a3 = tgtfpn;
        regs.a4 = 1;
        _syscall(caller->krnl, caller->pid, 17, &regs);

        MEMPHY_put_freefp(caller->krnl->active_mswp, target_swpfpn);
      }
      /* Update its online status of the target page */
      // pte_set_fpn(...);
      pte_set_fpn(caller, pgn, tgtfpn);
      enlist_pgn_node(&caller->mm->fifo_pgn, pgn);
      *fpn = tgtfpn;
    }
  }
  else
  {
    printf("DEBUG: Page in Ram\n");
    *fpn = PAGING_FPN(pte_get_entry(caller, pgn));
  }
  return 0;
}

/*pg_getval - read value at given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_getval(struct mm_struct *mm, int addr, BYTE *data, struct pcb_t *caller)
{
  int pgn = addr / 4096;
  int off = addr & 0xFFF;
  int fpn;

  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
    return -1; /* invalid page access */

  addr_t phyaddr = (fpn << PAGING64_ADDR_PT_SHIFT) + off;

  /* TODO
   *  MEMPHY_read(caller->krnl->mram, phyaddr, data);
   *  MEMPHY READ
   *  SYSCALL 17 sys_memmap with SYSMEM_IO_READ
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_READ;
  regs.a2 = phyaddr;
  int ret = _syscall(caller->krnl, caller->pid, 17, &regs);
  if (ret == -1)
  {
    return -1;
  }
  *data = (BYTE)regs.a3;
  return 0;
}

/*pg_setval - write value to given offset
 *@mm: memory region
 *@addr: virtual address to acess
 *@value: value
 *
 */
int pg_setval(struct mm_struct *mm, int addr, BYTE value, struct pcb_t *caller)
{
  int pgn = addr / 4096;
  int off = addr & 0xFFF;
  int fpn;

  /* Get the page to MEMRAM, swap from MEMSWAP if needed */
  if (pg_getpage(mm, pgn, &fpn, caller) != 0)
  {
    return -1; /* invalid page access */
  }
  addr_t phyaddr = (fpn << PAGING64_ADDR_PT_SHIFT) + off;
  /* TODO
   *  MEMPHY_write(caller->krnl->mram, phyaddr, value);
   *  MEMPHY WRITE with SYSMEM_IO_WRITE
   * SYSCALL 17 sys_memmap
   */
  struct sc_regs regs;
  regs.a1 = SYSMEM_IO_WRITE;
  regs.a2 = phyaddr;
  regs.a3 = value;
  int ret = _syscall(caller->krnl, caller->pid, 17, &regs);
  if (ret == -1)
  {
    return -1;
  }
  return 0;
}

/*__read - read value in region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __read(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);
  /* TODO Invalid memory identify */
  if (currg == NULL || currg->rg_start + offset >= currg->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  int ret = pg_getval(caller->mm, currg->rg_start + offset, data, caller);
  if (ret == -1)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libread - PAGING-based read a region memory */
int libread(
    struct pcb_t *proc, // Process executing the instruction
    uint32_t source,    // Index of source register
    addr_t offset,      // Source address = [source] + [offset]
    uint32_t *destination)
{
  BYTE data;
  int val = __read(proc, 0, source, offset, &data);
  if (val == -1)
  {
    return -1;
  }
  uint32_t reg_index = *destination;
  proc->regs[reg_index] = (addr_t)data;
  printf("VALIDATION: Read value '%d' from offset %ld\n", data, (long)offset);
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif

  return val;
}

/*__write - write a region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@offset: offset to acess in memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: allocated size
 *
 */
int __write(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  pthread_mutex_lock(&mmvm_lock);
  struct vm_rg_struct *currg = get_symrg_byid(caller->mm, rgid);

  if (currg == NULL || currg->rg_start + offset >= currg->rg_end) /* Invalid memory identify */
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  int ret = pg_setval(caller->mm, currg->rg_start + offset, value, caller);
  if (ret == -1)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libwrite - PAGING-based write a region memory */
int libwrite(
    struct pcb_t *proc,   // Process executing the instruction
    BYTE data,            // Data to be wrttien into memory
    uint32_t destination, // Index of destination register
    addr_t offset)
{
  int val = __write(proc, 0, destination, offset, data);
  if (val == -1)
  {
    return -1;
  }
#ifdef IODUMP
  /* TODO dump IO content (if needed) */
#ifdef PAGETBL_DUMP
  print_pgtbl(proc, 0, -1); // print max TBL
#endif
#endif
  return val;
}

/*libkmem_malloc- alloc region memory in kmem
 *@caller: caller
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 */

int libkmem_malloc(struct pcb_t *caller, addr_t size, addr_t reg_index)
{
  /* TODO: provide OS level management
   *       and forward the request to helper
   */
  addr_t addr;
  int val = __kmalloc(caller, -1, reg_index, size, &addr);

  /* TODO: provide OS kmem allocation validation
   */
  if (val != 0)
  {
    return -1;
  }
  caller->regs[reg_index] = addr;
  return 0;
}

/*kmalloc - alloc region memory in kmem
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@size: memory size
 *@alloc_addr: allocated address
 */
int is_frame_free(struct memphy_struct *mp, int fpn)
{
  struct framephy_struct *fp = mp->free_fp_list;
  while (fp != NULL)
  {
    if (fp->fpn == fpn)
      return 1;
    fp = fp->fp_next;
  }
  return 0;
}
void claim_physical_frame(struct memphy_struct *mp, int fpn)
{
  struct framephy_struct *prev = NULL;
  struct framephy_struct *curr = mp->free_fp_list;

  while (curr != NULL)
  {
    if (curr->fpn == fpn)
    {
      if (prev == NULL)
      {
        mp->free_fp_list = curr->fp_next;
      }
      else
      {
        prev->fp_next = curr->fp_next;
      }
      curr->fp_next = mp->used_fp_list;
      mp->used_fp_list = curr;
      return;
    }
    prev = curr;
    curr = curr->fp_next;
  }
}
addr_t __kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */
  fflush(stdout);
  pthread_mutex_lock(&mmvm_lock);
  printf("DEBUG: kmalloc step 2 (mutex locked)\n");
  fflush(stdout);
  struct krnl_t *krnl = caller->krnl;
  addr_t kernel_base = 0xff11000000000000ULL;
  addr_t page_size = PAGING64_PAGESZ;
  addr_t max_pgn = PAGING64_MAX_PGN;
  addr_t num_pages = (int)(PAGING64_PAGE_ALIGNSZ(size) / page_size);
  if (num_pages <= 0)
  {
    num_pages = 1;
  }
  addr_t start_fpn = -1;
  int consecutive_free_frames = 0;
  int max_frames = krnl->mram->maxsz / page_size;
  printf("DEBUG: kmalloc find free frame\n");
  fflush(stdout);
  for (int i = 0; i < max_frames; i++)
  {
    if (is_frame_free(krnl->mram, i))
    {
      if (consecutive_free_frames == 0)
        start_fpn = i;
      consecutive_free_frames++;
      if (consecutive_free_frames == num_pages)
        break;
    }
    else
    {
      start_fpn = -1;
      consecutive_free_frames = 0;
    }
  }

  if (start_fpn == -1 || consecutive_free_frames < num_pages)
  {
    printf("DEBUG: kmalloc unsufficient free frame\n");
    fflush(stdout);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  addr_t base_pgn = kernel_base / page_size;
  addr_t start_pgn = -1;
  int consecutive_free_pages = 0;
  printf("DEBUG: kmalloc find free page\n");
  fflush(stdout);
  for (addr_t i = base_pgn; i < base_pgn + 100000; i++)
  {
    addr_t current_pte = pte_get_kernel_entry(krnl, i);
    if (current_pte == 0)
    {
      if (consecutive_free_pages == 0)
        start_pgn = i;
      consecutive_free_pages++;
      if (consecutive_free_pages == num_pages)
      {
        break;
      }
    }
    else
    {
      start_pgn = -1;
      consecutive_free_pages = 0;
    }
  }
  if (consecutive_free_pages < num_pages || start_pgn == -1)
  {
    printf("DEBUG: kmalloc unsufficient free page\n");
    fflush(stdout);
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  addr_t start_addr = (addr_t)start_pgn * page_size;
  *alloc_addr = start_addr;

  for (int i = 0; i < num_pages; i++)
  {
    addr_t current_pgn = start_pgn + i;
    addr_t current_fpn = start_fpn + i;
    claim_physical_frame(krnl->mram, current_fpn);
    pte_set_kernel_fpn(krnl, current_pgn, current_fpn);
  }
  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ)
  {
    krnl->mm->symrgtbl[rgid].rg_start = start_addr;
    krnl->mm->symrgtbl[rgid].rg_end = start_addr + size;
  }
  printf("DEBUG: kmalloc step 3 (logic complete, unlocking)\n");
  fflush(stdout);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}
addr_t internal__kmalloc(struct pcb_t *caller, int vmaid, int rgid, addr_t size, addr_t *alloc_addr)
{
  /* TODO: provide OS kernel memory allocation
   *       update krnl_pgd for OS kernel level management */
  struct krnl_t *krnl = caller->krnl;
  addr_t kernel_base = 0xff11000000000000ULL;
  addr_t page_size = PAGING64_PAGESZ;
  addr_t max_pgn = PAGING64_MAX_PGN;
  addr_t num_pages = (int)(PAGING64_PAGE_ALIGNSZ(size) / page_size);
  if (num_pages <= 0)
  {
    num_pages = 1;
  }
  addr_t start_fpn = -1;
  int consecutive_free_frames = 0;
  int max_frames = krnl->mram->maxsz / page_size;
  printf("DEBUG: kmalloc find free frame\n");
  fflush(stdout);
  for (int i = 0; i < max_frames; i++)
  {
    if (is_frame_free(krnl->mram, i))
    {
      if (consecutive_free_frames == 0)
        start_fpn = i;
      consecutive_free_frames++;
      if (consecutive_free_frames == num_pages)
        break;
    }
    else
    {
      start_fpn = -1;
      consecutive_free_frames = 0;
    }
  }

  if (start_fpn == -1 || consecutive_free_frames < num_pages)
  {
    return -1;
  }
  addr_t base_pgn = kernel_base / page_size;
  addr_t start_pgn = -1;
  int consecutive_free_pages = 0;
  fflush(stdout);
  for (addr_t i = base_pgn; i < base_pgn + 100000; i++)
  {
    addr_t current_pte = pte_get_kernel_entry(krnl, i);
    if (current_pte == 0)
    {
      if (consecutive_free_pages == 0)
        start_pgn = i;
      consecutive_free_pages++;
      if (consecutive_free_pages == num_pages)
      {
        break;
      }
    }
    else
    {
      start_pgn = -1;
      consecutive_free_pages = 0;
    }
  }
  if (consecutive_free_pages < num_pages || start_pgn == -1)
  {
    return -1;
  }

  addr_t start_addr = (addr_t)start_pgn * page_size;
  *alloc_addr = start_addr;

  for (int i = 0; i < num_pages; i++)
  {
    addr_t current_pgn = start_pgn + i;
    addr_t current_fpn = start_fpn + i;
    claim_physical_frame(krnl->mram, current_fpn);
    pte_set_kernel_fpn(krnl, current_pgn, current_fpn);
  }
  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ)
  {
    krnl->mm->symrgtbl[rgid].rg_start = start_addr;
    krnl->mm->symrgtbl[rgid].rg_end = start_addr + size;
  }
  return 0;
}

/*libkmem_cache_pool_create - create cache pool in kmem
 *@caller: caller
 *@size: memory size
 *@align: alignment size of each cache slot (identical cache slot size)
 *@cache_pool_id: cache pool ID
 */
int libkmem_cache_pool_create(struct pcb_t *caller, uint32_t size, uint32_t align, uint32_t cache_pool_id)
{
  /* TODO: provide OS level management */

  // struct krnl_t *krnl = caller->krnl;
  // krnl->kcpooltbl...
  // krnl->krnl_pgd ...
  if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_SYMTBL_SZ)
  {
    return -1;
  }
  pthread_mutex_lock(&mmvm_lock);
  printf("DEBUG: kmem_cache_create START (locking)\n");
  fflush(stdout);
  struct krnl_t *krnl = caller->krnl;
  addr_t pool_base_addr;

  int val = internal__kmalloc(caller, -1, cache_pool_id, PAGING64_PAGESZ, &pool_base_addr);
  if (val != 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  krnl->mm->kcpooltbl[cache_pool_id].size = size;
  krnl->mm->kcpooltbl[cache_pool_id].align = align;
  krnl->mm->kcpooltbl[cache_pool_id].storage = pool_base_addr;
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*libkmem_cache_alloc - allocate cache slot in cache pool, cache slot has identical size
 * the allocated size is embedded in pool management mechanism
 *@caller: caller
 *@cache_pool_id: cache pool ID
 *@reg_index: memory region index
 */
int libkmem_cache_alloc(struct pcb_t *proc, uint32_t cache_pool_id, uint32_t reg_index)
{
  addr_t addr;
  addr = __kmem_cache_alloc(proc, -1, reg_index, cache_pool_id, &addr);

  // krnl->kcpooltbl...
  // krnl->krnl_pgd ...
  if (addr == 0)
  {
    return -1;
  }
  proc->regs[reg_index] = addr;
  return 0;
}

/*kmem_cache_alloc - alloc region memory in kmem cache
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@cache_pool_id: cached pool ID
 *@alloc_addr: allocated address
 */

addr_t __kmem_cache_alloc(struct pcb_t *caller, int vmaid, int rgid, int cache_pool_id, addr_t *alloc_addr)
{
  /* TODO: provide OS level management */
  /* TODO: provide OS level management */

  // struct krnl_t *krnl = caller->krnl;
  // krnl->symrgtbl...
  // krnl->kcpooltbl...
  // krnl->krnl_pgd ...
  pthread_mutex_lock(&mmvm_lock);
  struct krnl_t *krnl = caller->krnl;
  if (cache_pool_id < 0 || cache_pool_id >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
  if (krnl->mm->kcpooltbl[cache_pool_id].size == 0 || krnl->mm->kcpooltbl[cache_pool_id].storage == 0)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return 0;
  }
  addr_t addr = krnl->mm->kcpooltbl[cache_pool_id].storage;
  if (addr + krnl->mm->kcpooltbl[cache_pool_id].size > krnl->mm->symrgtbl[cache_pool_id].rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return 0; // Out of memory in this cache pool
  }
  *alloc_addr = addr;
  krnl->mm->kcpooltbl[cache_pool_id].storage += krnl->mm->kcpooltbl[cache_pool_id].size;
  if (rgid >= 0 && rgid < PAGING_MAX_SYMTBL_SZ)
  {
    krnl->mm->symrgtbl[rgid].rg_start = addr;
    krnl->mm->symrgtbl[rgid].rg_end = addr + krnl->mm->kcpooltbl[cache_pool_id].size;
  }

  pthread_mutex_unlock(&mmvm_lock);
  return addr;
}

int libkmem_copy_from_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_user_mem(...)
  //__write_kernel_mem(...);
  BYTE data;
  for (uint32_t i = 0; i < size; i++)
  {
    if (__read_user_mem(caller, 0, source, offset + i, &data) == -1)
    {
      return -1;
    }
    if (__write_kernel_mem(caller, 0, destination, offset + i, data) == -1)
    {
      return -1;
    }
  }
  return 0;
}

int libkmem_copy_to_user(struct pcb_t *caller, uint32_t source, uint32_t destination, uint32_t offset, uint32_t size)
{
  /* TODO: provide OS level management kmem
   */
  /*
   * TODO: Map kernel address range
   */
  //__read_kernel_mem(...)
  //__write_user_mem(...);
  BYTE data;
  for (uint32_t i = 0; i < size; i++)
  {
    if (__read_kernel_mem(caller, 0, source, offset + i, &data) == -1)
    {
      return -1;
    }
    if (__write_user_mem(caller, 0, destination, offset + i, data) == -1)
    {
      return -1;
    }
  }
  return 0;
}

/*__read_kernel_mem - read value in kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS memory operator for kernel memory region */
  // krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy
  pthread_mutex_lock(&mmvm_lock);
  struct krnl_t *krnl = caller->krnl;
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }

  struct vm_rg_struct *currg = &krnl->mm->symrgtbl[rgid];
  if (currg->rg_start + offset >= currg->rg_end)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  addr_t v_addr = currg->rg_start + offset;
  addr_t pgn = PAGING_PGN(v_addr);
  addr_t off = PAGING_OFFST(v_addr);

  addr_t pte = pte_get_kernel_entry(krnl, pgn);
  if (!PAGING_PAGE_PRESENT(pte))
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  addr_t fpn = PAGING_FPN(pte);
  addr_t p_addr = (fpn << PAGING64_ADDR_PT_SHIFT) + off;
  MEMPHY_read(krnl->mram, p_addr, data);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__write_kernel_mem - write a kernel region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_kernel_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS memory operator for kernel memory region */
  // krnl->krnl_pgd ... or krnl->pgd ... based on kmem implementation strategy
  pthread_mutex_lock(&mmvm_lock);
  struct krnl_t *krnl = caller->krnl;
  if (rgid < 0 || rgid >= PAGING_MAX_SYMTBL_SZ)
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  struct vm_rg_struct *currg = &krnl->mm->symrgtbl[rgid];
  if ((currg->rg_start == 0 && currg->rg_end == 0) || (currg->rg_start + offset >= currg->rg_end))
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  addr_t v_addr = currg->rg_start + offset;
  addr_t pgn = PAGING_PGN(v_addr);
  addr_t off = PAGING_OFFST(v_addr);

  addr_t pte = pte_get_kernel_entry(krnl, pgn);
  if (!PAGING_PAGE_PRESENT(pte))
  {
    pthread_mutex_unlock(&mmvm_lock);
    return -1;
  }
  addr_t fpn = PAGING_FPN(pte);
  addr_t p_addr = (fpn << PAGING64_ADDR_PT_SHIFT) + off;
  MEMPHY_write(krnl->mram, p_addr, value);
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*__read_user_mem - read value in user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __read_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE *data)
{
  /* TODO: provide OS level management user memory access */
  // krnl->pgd ...
  return __read(caller, vmaid, rgid, offset, data);
}

/*__write_user_mem - write a user region memory
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@rgid: memory region ID (used to identify variable in symbole table)
 *@offset: offset to acess in memory region
 *@value: data value
 */
int __write_user_mem(struct pcb_t *caller, int vmaid, int rgid, addr_t offset, BYTE value)
{
  /* TODO: provide OS level management user memory access */
  // krnl->pgd ...
  return __write(caller, vmaid, rgid, offset, value);
}

/*free_pcb_memphy - collect all memphy of pcb
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@incpgnum: number of page
 */
int free_pcb_memph(struct pcb_t *caller)
{
  pthread_mutex_lock(&mmvm_lock);
  int pagenum;
  struct vm_area_struct *vma = caller->mm->mmap;
  while (vma)
  {
    addr_t addr = vma->vm_start;
    while (addr < vma->sbrk)
    {
      addr_t pgn = PAGING_PGN(addr);
      addr_t pte = pte_get_entry(caller, pgn);

      if (PAGING_PAGE_PRESENT(pte))
      {
        MEMPHY_put_freefp(caller->krnl->mram, PAGING_FPN(pte));
      }
      else if ((pte & PAGING_PTE_SWAPPED_MASK) != 0)
      {
        MEMPHY_put_freefp(caller->krnl->active_mswp, GETVAL(pte, PAGING_PTE_SWPOFF_MASK, PAGING_PTE_SWPOFF_LOBIT));
      }
      addr += PAGING64_PAGESZ;
    }
    vma = vma->vm_next;
  }
  if (caller->mm->pgd)
  {
    for (int i = 0; i < 512; i++)
    {
      if (caller->mm->pgd[i])
      {
        addr_t *p4d = (addr_t *)caller->mm->pgd[i];
        for (int j = 0; j < 512; j++)
        {
          if (p4d[j])
          {
            addr_t *pud = (addr_t *)p4d[j];
            for (int k = 0; k < 512; k++)
            {
              if (pud[k])
              {
                addr_t *pmd = (addr_t *)pud[k];
                for (int m = 0; m < 512; m++)
                {
                  if (pmd[m])
                    free((void *)pmd[m]);
                }
                free((void *)pud[k]);
              }
            }
            free((void *)p4d[j]);
          }
        }
        free((void *)caller->mm->pgd[i]);
      }
    }
    free(caller->mm->pgd);
  }
  struct pgn_t *pg_node = caller->mm->fifo_pgn;
  while (pg_node != NULL)
  {
    struct pgn_t *next = pg_node->pg_next;
    free(pg_node);
    pg_node = next;
  }
  caller->mm->fifo_pgn = NULL;
  pthread_mutex_unlock(&mmvm_lock);
  return 0;
}

/*find_victim_page - find victim page
 *@caller: caller
 *@pgn: return page number
 *
 */
int find_victim_page(struct mm_struct *mm, addr_t *retpgn)
{
  struct pgn_t *pg = mm->fifo_pgn;

  /* TODO: Implement the theorical mechanism to find the victim page */
  if (!pg)
  {
    return -1;
  }
  *retpgn = pg->pgn;
  mm->fifo_pgn = pg->pg_next;
  free(pg);

  return 0;
}

/*get_free_vmrg_area - get a free vm region
 *@caller: caller
 *@vmaid: ID vm area to alloc memory region
 *@size: allocated size
 *
 */
int get_free_vmrg_area(struct pcb_t *caller, int vmaid, int size, struct vm_rg_struct *newrg)
{
  struct vm_area_struct *cur_vma = get_vma_by_num(caller->mm, vmaid);

  struct vm_rg_struct *rgit = cur_vma->vm_freerg_list;

  if (rgit == NULL)
    return -1;

  /* Probe unintialized newrg */
  newrg->rg_start = newrg->rg_end = -1;

  /* Traverse on list of free vm region to find a fit space */
  while (rgit != NULL)
  {
    if (rgit->rg_start + size <= rgit->rg_end)
    { /* Current region has enough space */
      newrg->rg_start = rgit->rg_start;
      newrg->rg_end = rgit->rg_start + size;

      /* Update left space in chosen region */
      if (rgit->rg_start + size < rgit->rg_end)
      {
        rgit->rg_start = rgit->rg_start + size;
      }
      else
      { /*Use up all space, remove current node */
        /*Clone next rg node */
        struct vm_rg_struct *nextrg = rgit->rg_next;

        /*Cloning */
        if (nextrg != NULL)
        {
          rgit->rg_start = nextrg->rg_start;
          rgit->rg_end = nextrg->rg_end;

          rgit->rg_next = nextrg->rg_next;

          free(nextrg);
        }
        else
        {                                /*End of free list */
          rgit->rg_start = rgit->rg_end; // dummy, size 0 region
          rgit->rg_next = NULL;
        }
      }
      break;
    }
    else
    {
      rgit = rgit->rg_next; // Traverse next rg
    }
  }

  if (newrg->rg_start == -1) // new region not found
    return -1;

  return 0;
}

// #endif
