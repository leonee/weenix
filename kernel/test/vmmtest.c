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

    vmmap_destroy(vmm);

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

    vmmap_destroy(vmm);
    
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

    vmmap_destroy(vmm);

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

    vmmap_destroy(vmm);

    dbg(DBG_TEST, "vmmap_is_range_empty() tests passed\n");
}

static void validate_vmarea(vmarea_t *v, uint32_t start, uint32_t end, uint32_t off){
    dbg(DBG_TEST, "attempting to validate start == %d, end == %d, off == %d\n", 
            start, end, off);

    KASSERT(v->vma_start == start);
    KASSERT(v->vma_end == end);
    KASSERT(v->vma_off == off);
}

static void test_vmmap_remove_simple(){
    vmmap_t *vmm = vmmap_create();

    vmarea_t zero_to_onehundred;
    zero_to_onehundred.vma_start = 0;
    zero_to_onehundred.vma_end = 100;
    zero_to_onehundred.vma_off = 10;

    vmarea_t onefifty_to_onesixty;
    onefifty_to_onesixty.vma_start = 150;
    onefifty_to_onesixty.vma_end = 160;
    onefifty_to_onesixty.vma_off = 20;

    vmarea_t onesixty_to_oneseventy;
    onesixty_to_oneseventy.vma_start = 160;
    onesixty_to_oneseventy.vma_end = 170;
    onesixty_to_oneseventy.vma_off = 0;

    vmarea_t oneseventy_to_oneeighty;
    oneseventy_to_oneeighty.vma_start = 170;
    oneseventy_to_oneeighty.vma_end = 180;
    oneseventy_to_oneeighty.vma_off = 0;

    list_insert_tail(&vmm->vmm_list, &zero_to_onehundred.vma_plink);
    list_insert_tail(&vmm->vmm_list, &onefifty_to_onesixty.vma_plink);
    list_insert_tail(&vmm->vmm_list, &onesixty_to_oneseventy.vma_plink);
    list_insert_tail(&vmm->vmm_list, &oneseventy_to_oneeighty.vma_plink);

    vmmap_remove(vmm, 30, 60);
    vmmap_remove(vmm, 155, 175);

    /* should be (0, 30], (60, 100], (150, 155], (175, 180] */
    list_link_t *curr_link = (&vmm->vmm_list)->l_next;
    vmarea_t *currarea = list_item(curr_link, vmarea_t, vma_plink);
    validate_vmarea(currarea, 0, 30, 10);

    curr_link = curr_link->l_next;
    currarea = list_item(curr_link, vmarea_t, vma_plink);
    validate_vmarea(currarea, 60, 100, 70);

    curr_link = curr_link->l_next;
    currarea = list_item(curr_link, vmarea_t, vma_plink);
    validate_vmarea(currarea, 150, 155, 25);
  
    curr_link = curr_link->l_next;
    currarea = list_item(curr_link, vmarea_t, vma_plink);
    validate_vmarea(currarea, 175, 180, 5);

    KASSERT(curr_link->l_next == &vmm->vmm_list);

    vmmap_destroy(vmm);
}

static void test_vmmap_remove(){
    dbg(DBG_TEST, "starting vmmap_remove tests\n");

    test_vmmap_remove_simple();

    dbg(DBG_TEST, "vmmap_remove() tests passed\n");
}


void run_vmm_tests(){
    dbg(DBG_TEST, "starting vmm tests\n");

    test_vmm_find_range();
    test_vmmap_is_range_empty();
    test_vmmap_remove();

    dbg(DBG_TESTPASS, "all vmm tests passed!\n");
}
