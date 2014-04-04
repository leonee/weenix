#include "test/vmmtest.h"

#include "util/debug.h"

#include "vm/vmmap.h"

static void test_vmm_find_range_simple(){
    dbg(DBG_TEST, "beginning simple vmm_find_range tests\n");

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

    /* range which is just above the size of the largest range of vmareas */
    KASSERT(vmmap_find_range(vmm, 12, VMMAP_DIR_LOHI) == -1);

    dbg(DBG_TEST, "simple vmm_find_range tests passed\n");
}

static void test_vmm_find_range_complex(){
    dbg(DBG_TEST, "beginning complex vmm_find_range tests\n");

    vmmap_t *vmm = vmmap_create();

    vmarea_t zero_to_ten;
    zero_to_ten.vma_start = 0;
    zero_to_ten.vma_end = 10;

    vmarea_t ten_to_twenty;
    ten_to_twenty.vma_start = 10;
    ten_to_twenty.vma_end = 20;

    vmarea_t twenty_to_thirty;
    twenty_to_thirty.vma_start = 20;
    twenty_to_thirty.vma_end = 30;

    vmarea_t fifty_to_fiftyfive;
    fifty_to_fiftyfive.vma_start = 50;
    fifty_to_fiftyfive.vma_end = 55;

    list_insert_tail(&vmm->vmm_list, &zero_to_ten.vma_plink);
    list_insert_tail(&vmm->vmm_list, &ten_to_twenty.vma_plink);
    list_insert_tail(&vmm->vmm_list, &twenty_to_thirty.vma_plink);
    list_insert_tail(&vmm->vmm_list, &fifty_to_fiftyfive.vma_plink);

    KASSERT(vmmap_find_range(vmm, 10, VMMAP_DIR_LOHI) == 0);
    KASSERT(vmmap_find_range(vmm, 15, VMMAP_DIR_LOHI) == 0);
    KASSERT(vmmap_find_range(vmm, 15, VMMAP_DIR_HILO) == 10);
    KASSERT(vmmap_find_range(vmm, 20, VMMAP_DIR_LOHI) == 0);
    KASSERT(vmmap_find_range(vmm, 20, VMMAP_DIR_HILO) == 10);
    KASSERT(vmmap_find_range(vmm, 29, VMMAP_DIR_LOHI) == 0);
    KASSERT(vmmap_find_range(vmm, 29, VMMAP_DIR_HILO) == 0);
    KASSERT(vmmap_find_range(vmm, 30, VMMAP_DIR_LOHI) == 0);
    KASSERT(vmmap_find_range(vmm, 30, VMMAP_DIR_HILO) == 0);
    KASSERT(vmmap_find_range(vmm, 31, VMMAP_DIR_LOHI) == -1);
    KASSERT(vmmap_find_range(vmm, 31, VMMAP_DIR_HILO) == -1);
    KASSERT(vmmap_find_range(vmm, 50, VMMAP_DIR_HILO) == -1);
    KASSERT(vmmap_find_range(vmm, 100, VMMAP_DIR_HILO) == -1);
    
    dbg(DBG_TEST, "complex vmm_find_range tests passed\n");
}

static void test_vmm_find_range_one_element(){
    dbg(DBG_TEST, "testing vmm_find_range() on one-element lists\n");

    vmmap_t *vmm = vmmap_create();

    vmarea_t zero_to_ten;
    zero_to_ten.vma_start = 0;
    zero_to_ten.vma_end = 10;

    list_insert_tail(&vmm->vmm_list, &zero_to_ten.vma_plink);

    KASSERT(vmmap_find_range(vmm, 10, VMMAP_DIR_LOHI) == 0);
    KASSERT(vmmap_find_range(vmm, 10, VMMAP_DIR_HILO) == 0);
    KASSERT(vmmap_find_range(vmm, 11, VMMAP_DIR_LOHI) == -1);
    KASSERT(vmmap_find_range(vmm, 11, VMMAP_DIR_HILO) == -1);

    dbg(DBG_TEST, "vmm_find_range() one-elements tests passed\n");
}

void test_vmm_find_range(){
    dbg(DBG_TEST, "testing vmm_find_range()\n");

    test_vmm_find_range_simple();
    test_vmm_find_range_complex();
    test_vmm_find_range_one_element();

    dbg(DBG_TEST, "vmm_find_range() tests passed\n");
}

void test_vmmap_is_range_empty(){
    dbg(DBG_TEST, "testing vmmap_is_range_empty()\n");

    vmmap_t *vmm = vmmap_create();


    vmarea_t ten_to_twenty;
    ten_to_twenty.vma_start = 10;
    ten_to_twenty.vma_end = 20;

    list_insert_tail(&vmm->vmm_list, &ten_to_twenty.vma_plink);

    /* key:
     *    [        ] Existing VM Area
     *  *******      Range for which we're testing emptiness
     */

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
