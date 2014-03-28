#include "test/vmmtest.h"

#include "util/debug.h"

#include "vm/vmmap.h"

void test_vmm_find_range(){
    dbg(DBG_TEST, "testing vmm_find_range()\n");

    vmmap_t *vmm = vmmap_create();

    vmarea_t zero_to_ten;
    zero_to_ten.vma_start = 0;
    zero_to_ten.vma_end = 10;

    vmarea_t five_to_ten;
    five_to_ten.vma_start = 5;
    five_to_ten.vma_end = 10;

    vmarea_t twenty_to_thirty;
    twenty_to_thirty.vma_start = 20;
    twenty_to_thirty.vma_end = 30;

    vmarea_t thirty_to_thirtyone;
    thirty_to_thirtyone.vma_start = 30;
    thirty_to_thirtyone.vma_end = 31;

    list_insert_tail(&vmm->vmm_list, &zero_to_ten.vma_plink);
    list_insert_tail(&vmm->vmm_list, &five_to_ten.vma_plink);
    list_insert_tail(&vmm->vmm_list, &twenty_to_thirty.vma_plink);
    list_insert_tail(&vmm->vmm_list, &thirty_to_thirtyone.vma_plink);

    /* simple positive tests */
    KASSERT(vmmap_find_range(vmm, 3, VMMAP_DIR_LOHI) == 0);
    KASSERT(vmmap_find_range(vmm, 3, VMMAP_DIR_HILO) == 20);

    /* simple negative tests */ 
    KASSERT(vmmap_find_range(vmm, 20, VMMAP_DIR_LOHI) == -1);
    KASSERT(vmmap_find_range(vmm, 20, VMMAP_DIR_HILO) == -1);

    /* finding small ranges */
    KASSERT(vmmap_find_range(vmm, 1, VMMAP_DIR_LOHI) == 0);
    KASSERT(vmmap_find_range(vmm, 1, VMMAP_DIR_HILO) == 30);
    KASSERT(vmmap_find_range(vmm, 0, VMMAP_DIR_LOHI) == 0);
    KASSERT(vmmap_find_range(vmm, 0, VMMAP_DIR_HILO) == 30);
    
    /* range which is the size of a vmarea */
    KASSERT(vmmap_find_range(vmm, 10, VMMAP_DIR_LOHI) == 0);

    /* range which is just above the size of a vmarea */
    KASSERT(vmmap_find_range(vmm, 11, VMMAP_DIR_LOHI) == -1);

    dbg(DBG_TEST, "vmm_find_range() tests passed\n");
}

void test_vmmap_is_range_empty(){
    dbg(DBG_TEST, "testing vmmap_is_range_empty()\n");

    vmmap_t *vmm = vmmap_create();


    vmarea_t ten_to_twenty;
    ten_to_twenty.vma_start = 10;
    ten_to_twenty.vma_end = 20;

    list_insert_tail(&vmm->vmm_list, &ten_to_twenty.vma_plink);

    /*       [  ****    ]      */
    KASSERT(vmmap_is_range_empty(vmm, 13, 4) == 0);

    /* ****  [          ]      */
    KASSERT(vmmap_is_range_empty(vmm, 0, 5) == 1);

    /*       [          ] ***  */
    KASSERT(vmmap_is_range_empty(vmm, 25, 30) == 1);

    /* ******[          ]      */
    KASSERT(vmmap_is_range_empty(vmm, 0, 9) == 1);

    /* ******[*         ]      */
    KASSERT(vmmap_is_range_empty(vmm, 0, 10) == 0);

    /*      *[*****     ]      */
    KASSERT(vmmap_is_range_empty(vmm, 9, 15) == 0);

    /*       [*****     ]      */
    KASSERT(vmmap_is_range_empty(vmm, 10, 15) == 0);

    /*       [          ]***** */
    KASSERT(vmmap_is_range_empty(vmm, 20, 25) == 1);

    /*       [         *]***** */
    KASSERT(vmmap_is_range_empty(vmm, 19, 25) == 0);

    /*       [    ******]*     */
    KASSERT(vmmap_is_range_empty(vmm, 15, 20) == 0);

    /*       [    ******]      */
    KASSERT(vmmap_is_range_empty(vmm, 15, 19) == 0);

    /*       [**********]      */
    KASSERT(vmmap_is_range_empty(vmm, 10, 19) == 0);

    /*     **[**********]**    */
    KASSERT(vmmap_is_range_empty(vmm, 7, 23) == 0);

    dbg(DBG_TEST, "vmmap_is_range_empty() tests passed\n");
}


void run_vmm_tests(){
    dbg(DBG_TEST, "starting vmm tests\n");

    test_vmm_find_range();
    test_vmmap_is_range_empty();

    dbg(DBG_TESTPASS, "all vmm tests passed!\n");
}
