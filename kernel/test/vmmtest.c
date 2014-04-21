#include "test/vmmtest.h"

#include "util/debug.h"

#include "vm/vmmap.h"

#include "mm/mman.h"
#include "mm/mm.h"

#define MIN_PAGENUM ADDR_TO_PN(USER_MEM_LOW) /* inclusive */
#define MAX_PAGENUM ADDR_TO_PN(USER_MEM_HIGH) /* exclusive */
#define TOTAL_RANGE MAX_PAGENUM - MIN_PAGENUM

static void test_vmm_find_range_simple(){
    dbg(DBG_TEST, "beginning simple vmm_find_range tests\n");

    vmmap_t *vmm = vmmap_create();

    vmarea_t zero_to_ten;
    zero_to_ten.vma_start = 0 + MIN_PAGENUM;
    zero_to_ten.vma_end = 10 + MIN_PAGENUM;

    vmarea_t twenty_to_thirty;
    twenty_to_thirty.vma_start = 20 + MIN_PAGENUM;
    twenty_to_thirty.vma_end = 30 + MIN_PAGENUM;

    vmarea_t thirty_to_thirtyone;
    thirty_to_thirtyone.vma_start = 30 + MIN_PAGENUM;
    thirty_to_thirtyone.vma_end = 31 + MIN_PAGENUM;

    list_insert_tail(&vmm->vmm_list, &zero_to_ten.vma_plink);
    list_insert_tail(&vmm->vmm_list, &twenty_to_thirty.vma_plink);
    list_insert_tail(&vmm->vmm_list, &thirty_to_thirtyone.vma_plink);

    /* simple positive tests */
    KASSERT(vmmap_find_range(vmm, 3, VMMAP_DIR_LOHI) == 10 + MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, 3, VMMAP_DIR_HILO) == MAX_PAGENUM - 3);

    /* simple negative tests */ 
    KASSERT(vmmap_find_range(vmm, TOTAL_RANGE, VMMAP_DIR_LOHI) == -1);
    KASSERT(vmmap_find_range(vmm, TOTAL_RANGE, VMMAP_DIR_HILO) == -1);

    /* finding small ranges */
    KASSERT(vmmap_find_range(vmm, 1, VMMAP_DIR_LOHI) == 10 + MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, 1, VMMAP_DIR_HILO) == MAX_PAGENUM - 1);
    KASSERT(vmmap_find_range(vmm, 0, VMMAP_DIR_LOHI) == 0 + MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, 0, VMMAP_DIR_HILO) == MAX_PAGENUM);
    
    /*vmmap_destroy(vmm);*/

    dbg(DBG_TEST, "simple vmm_find_range tests passed\n");
}

static void test_vmm_find_range_complex(){
    dbg(DBG_TEST, "beginning complex vmm_find_range tests\n");

    vmmap_t *vmm = vmmap_create();
    
    /* range which is the entire address space */
    KASSERT(vmmap_find_range(vmm, TOTAL_RANGE, VMMAP_DIR_LOHI) == MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, TOTAL_RANGE, VMMAP_DIR_HILO) == MIN_PAGENUM);

    vmarea_t near_bottom;
    near_bottom.vma_start = 10 + MIN_PAGENUM;
    near_bottom.vma_end = 20 + MIN_PAGENUM;

    vmarea_t almost_near_bottom;
    almost_near_bottom.vma_start = 35 + MIN_PAGENUM;
    almost_near_bottom.vma_end = 40 + MIN_PAGENUM;

    vmarea_t near_top;
    near_top.vma_start = MAX_PAGENUM - 20;
    near_top.vma_end = MAX_PAGENUM - 10;

    vmarea_t almost_near_top;
    almost_near_top.vma_start = MAX_PAGENUM - 40;
    almost_near_top.vma_end = MAX_PAGENUM - 35;

    list_insert_tail(&vmm->vmm_list, &near_bottom.vma_plink);
    list_insert_tail(&vmm->vmm_list, &almost_near_bottom.vma_plink);
    list_insert_tail(&vmm->vmm_list, &almost_near_top.vma_plink);
    list_insert_tail(&vmm->vmm_list, &near_top.vma_plink);

    /* range which is the size of a gap between two VMA's*/
    KASSERT(vmmap_find_range(vmm, 15, VMMAP_DIR_LOHI) == 20 + MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, 15, VMMAP_DIR_HILO) == MAX_PAGENUM - 35);

    /* range which is just above the size of a gap between two VMA's */
    KASSERT(vmmap_find_range(vmm, 16, VMMAP_DIR_LOHI) == 40 + MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, 16, VMMAP_DIR_HILO) == MAX_PAGENUM - 56);

    /* range which is the size of a gap between a VMA and the address-space limit */
    KASSERT(vmmap_find_range(vmm, 10, VMMAP_DIR_LOHI) == MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, 10, VMMAP_DIR_HILO) == MAX_PAGENUM - 10);

    /* range which is just above the size of a gap between a VMA and the
     *  address-space limit */
    KASSERT(vmmap_find_range(vmm, 11, VMMAP_DIR_LOHI) == 20 + MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, 11, VMMAP_DIR_HILO) == MAX_PAGENUM - 31);

    uint32_t largest_gapsize = (MAX_PAGENUM - 40) - (MIN_PAGENUM + 40); 

    /* range which is the size of the largest gap */
    KASSERT(vmmap_find_range(vmm, largest_gapsize, VMMAP_DIR_LOHI) ==
            40 + MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, largest_gapsize, VMMAP_DIR_HILO) ==
            40 + MIN_PAGENUM);

    /* range which is just above the size of the largest gap */    
    KASSERT(vmmap_find_range(vmm, largest_gapsize + 1, VMMAP_DIR_LOHI) == -1);
    KASSERT(vmmap_find_range(vmm, largest_gapsize + 1, VMMAP_DIR_HILO) == -1);
        
    /* range which is larger that the address space size */ 
    KASSERT(vmmap_find_range(vmm, TOTAL_RANGE + 1, VMMAP_DIR_LOHI) == -1);
    KASSERT(vmmap_find_range(vmm, TOTAL_RANGE + 1, VMMAP_DIR_HILO) == -1);

    /*vmmap_destroy(vmm);*/
    
    dbg(DBG_TEST, "complex vmm_find_range tests passed\n");
}

static void test_vmm_find_range_one_element(){
    dbg(DBG_TEST, "testing vmm_find_range() on one-element lists\n");

    vmmap_t *vmm = vmmap_create();

    vmarea_t zero_to_ten;
    zero_to_ten.vma_start = 0 + MIN_PAGENUM;
    zero_to_ten.vma_end = 10 + MIN_PAGENUM;

    list_insert_tail(&vmm->vmm_list, &zero_to_ten.vma_plink);

    KASSERT(vmmap_find_range(vmm, 10, VMMAP_DIR_LOHI) == 10 + MIN_PAGENUM);
    KASSERT(vmmap_find_range(vmm, 10, VMMAP_DIR_HILO) == MAX_PAGENUM - 10);

    KASSERT(vmmap_find_range(vmm, TOTAL_RANGE, VMMAP_DIR_LOHI) == -1);
    KASSERT(vmmap_find_range(vmm, TOTAL_RANGE, VMMAP_DIR_HILO) == -1);

    /*vmmap_destroy(vmm);*/

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

    /*vmmap_destroy(vmm);*/

    dbg(DBG_TEST, "vmmap_is_range_empty() tests passed\n");
}

static void validate_vmarea(vmarea_t *v, uint32_t start, uint32_t end, uint32_t off){
    dbg(DBG_TEST, "attempting to validate start == %d, end == %d, off == %d\n", 
            start, end, off);

    if (v->vma_start != start || v->vma_end != end || v->vma_off != off){
        dbg(DBG_TEST, "actual start: %d\n", v->vma_start);
        dbg(DBG_TEST, "actual end: %d\n", v->vma_end);
        dbg(DBG_TEST, "actual offset: %d\n", v->vma_off);
    }

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
    zero_to_onehundred.vma_obj = NULL;
    zero_to_onehundred.vma_prot = PROT_NONE;
    list_link_init(&zero_to_onehundred.vma_plink);
    zero_to_onehundred.vma_flags = MAP_SHARED;

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

    vmmap_remove(vmm, 30, 30); /* remove (30, 60] */

    /* should be (0, 30], (60, 100], (150, 160], (160, 170], (170, 180] */
    list_link_t *curr_link = (&vmm->vmm_list)->l_next;
    vmarea_t *currarea = list_item(curr_link, vmarea_t, vma_plink);
    validate_vmarea(currarea, 0, 30, 10);

    curr_link = curr_link->l_next;
    currarea = list_item(curr_link, vmarea_t, vma_plink);
    validate_vmarea(currarea, 60, 100, 70);

    curr_link = curr_link->l_next;
    currarea = list_item(curr_link, vmarea_t, vma_plink);
    validate_vmarea(currarea, 150, 160, 20);
  
    curr_link = curr_link->l_next;
    currarea = list_item(curr_link, vmarea_t, vma_plink);
    validate_vmarea(currarea, 160, 170, 0);

    curr_link = curr_link->l_next;
    currarea = list_item(curr_link, vmarea_t, vma_plink);
    validate_vmarea(currarea, 170, 180, 0);

    curr_link = NULL;
    currarea = NULL;

    vmmap_remove(vmm, 155, 20); /* remove (155, 175] */

    /* should be (0, 30], (60, 100], (150, 155], (175, 180] */
    list_link_t *curr_link2 = (&vmm->vmm_list)->l_next;
    vmarea_t *currarea2 = list_item(curr_link2, vmarea_t, vma_plink);
    validate_vmarea(currarea2, 0, 30, 10);

    curr_link2 = curr_link2->l_next;
    currarea2 = list_item(curr_link2, vmarea_t, vma_plink);
    validate_vmarea(currarea2, 60, 100, 70);

    curr_link2 = curr_link2->l_next;
    currarea2 = list_item(curr_link2, vmarea_t, vma_plink);
    validate_vmarea(currarea2, 150, 155, 20);
  
    curr_link2 = curr_link2->l_next;
    currarea2 = list_item(curr_link2, vmarea_t, vma_plink);
    validate_vmarea(currarea2, 175, 180, 5);

    KASSERT(curr_link2->l_next == &vmm->vmm_list);

    /*vmmap_destroy(vmm);*/
}

static void test_case_1_edge(){
    vmmap_t *vmm = vmmap_create();

    vmarea_t zero_to_onehundred;
    zero_to_onehundred.vma_start = 0;
    zero_to_onehundred.vma_end = 100;
    zero_to_onehundred.vma_off = 0;
    zero_to_onehundred.vma_obj = NULL;
    zero_to_onehundred.vma_prot = PROT_NONE;
    list_link_init(&zero_to_onehundred.vma_plink);
    list_link_init(&zero_to_onehundred.vma_olink);
    zero_to_onehundred.vma_flags = MAP_SHARED;

    list_insert_tail(&vmm->vmm_list, &zero_to_onehundred.vma_plink);

    list_t throwaway_list;
    list_init(&throwaway_list);

    list_insert_tail(&throwaway_list, &zero_to_onehundred.vma_olink);

    vmmap_remove(vmm, 1, 98);

    list_link_t *currlink = (&vmm->vmm_list)->l_next;
    vmarea_t *currarea = list_item(currlink, vmarea_t, vma_plink);
    validate_vmarea(currarea, 0, 1, 0);

    currlink = currlink->l_next;
    currarea = list_item(currlink, vmarea_t, vma_plink);
    validate_vmarea(currarea, 99, 100, 99);

    KASSERT(currlink->l_next == &vmm->vmm_list);

    /*vmmap_destroy(vmm);*/
}

static void test_case_2_edge(){
    vmmap_t *vmm = vmmap_create();

    vmarea_t onefifty_to_onesixty;
    onefifty_to_onesixty.vma_start = 150;
    onefifty_to_onesixty.vma_end = 160;
    onefifty_to_onesixty.vma_off = 0;
    list_insert_tail(&vmm->vmm_list, &onefifty_to_onesixty.vma_plink);

    vmmap_remove(vmm, 159, 5);

    list_link_t *currlink = (&vmm->vmm_list)->l_next;
    vmarea_t *currarea = list_item(currlink, vmarea_t, vma_plink);
    validate_vmarea(currarea, 150, 159, 0);

    KASSERT(currlink->l_next == &vmm->vmm_list);

    /*vmmap_destroy(vmm);*/
}

static void test_case_3_edge(){
    vmmap_t *vmm = vmmap_create();

    vmarea_t onesixty_to_oneseventy;
    onesixty_to_oneseventy.vma_start = 160;
    onesixty_to_oneseventy.vma_end = 170;
    onesixty_to_oneseventy.vma_off = 0;

    list_insert_tail(&vmm->vmm_list, &onesixty_to_oneseventy.vma_plink);

    vmmap_remove(vmm, 155, 6);

    list_link_t *currlink = (&vmm->vmm_list)->l_next;
    vmarea_t *currarea = list_item(currlink, vmarea_t, vma_plink);
    validate_vmarea(currarea, 161, 170, 1);

    KASSERT(currlink->l_next == &vmm->vmm_list);

    /*vmmap_destroy(vmm);*/
}

static void test_case_4_edge(){
    vmmap_t *vmm = vmmap_create();

    vmarea_t onesixty_to_oneseventy;
    onesixty_to_oneseventy.vma_start = 160;
    onesixty_to_oneseventy.vma_end = 170;
    onesixty_to_oneseventy.vma_off = 0;

    list_insert_tail(&vmm->vmm_list, &onesixty_to_oneseventy.vma_plink);

    vmmap_remove(vmm, 160, 10);

    list_link_t *currlink = (&vmm->vmm_list)->l_next;

    KASSERT((&vmm->vmm_list)->l_next == &vmm->vmm_list);

    /*vmmap_destroy(vmm);*/
}

static void test_no_overlap_edge(){
    vmmap_t *vmm = vmmap_create();

    vmarea_t onesixty_to_oneseventy;
    onesixty_to_oneseventy.vma_start = 160;
    onesixty_to_oneseventy.vma_end = 170;
    onesixty_to_oneseventy.vma_off = 0;

    list_insert_tail(&vmm->vmm_list, &onesixty_to_oneseventy.vma_plink);

    vmmap_remove(vmm, 155, 5);

    list_link_t *currlink = (&vmm->vmm_list)->l_next;
    vmarea_t *currarea = list_item(currlink, vmarea_t, vma_plink);
    validate_vmarea(currarea, 160, 170, 0);

    KASSERT(currlink->l_next == &vmm->vmm_list);

    vmmap_remove(vmm, 170, 10);

    list_link_t *currlink2 = (&vmm->vmm_list)->l_next;
    vmarea_t *currarea2 = list_item(currlink2, vmarea_t, vma_plink);
    validate_vmarea(currarea2, 160, 170, 0);

    KASSERT(currlink2->l_next == &vmm->vmm_list);

    /*vmmap_destroy(vmm);*/
}

static void test_vmmap_remove(){
    dbg(DBG_TEST, "starting vmmap_remove tests\n");

    test_vmmap_remove_simple();

    dbg(DBG_TEST, "starting vmmap_remove edge case tests\n");
    test_case_1_edge();
    test_case_2_edge();
    test_case_3_edge();
    test_case_4_edge();
    test_no_overlap_edge();
    dbg(DBG_TEST, "vmmap_remove edge case tests passed\n");

    dbg(DBG_TEST, "vmmap_remove() tests passed\n");
}


void run_vmm_tests(){
    dbg(DBG_TEST, "starting vmm tests\n");

    test_vmm_find_range();
    test_vmmap_is_range_empty();
    /*test_vmmap_remove();*/

    dbg(DBG_TESTPASS, "all vmm tests passed!\n");
}
