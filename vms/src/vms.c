#include "vms.h"

#include "mmu.h"
#include "pages.h"

#include <string.h>
#include <stdio.h>

static int arr[MAX_PAGES] = {0}; // all pages in p0
// static int len = 0;

void page_fault_handler(void* virtual_address, int level, void* page_table) {
    uint64_t *cur_l0_entry = vms_page_table_pte_entry(page_table, virtual_address, level);
    /* If the entry is not valid or does not have read permission */
    if (!vms_pte_valid(cur_l0_entry) || level) {
        return;
    }
    // printf("%d\n",vms_pte_custom(cur_l0_entry));
    if (vms_pte_custom(cur_l0_entry)) {
        /* If custom set is 1, it is supposed to have write bit = 1 */
        vms_pte_write_set(cur_l0_entry);
        /* Get the page that the current entry is pointing at */
        uint64_t cur_p0_ppn = vms_pte_get_ppn(cur_l0_entry);
        void* old_p0 = vms_ppn_to_page(cur_p0_ppn);
        /* Get the index of the corresponding page */
        int idx = vms_get_page_index(old_p0);
        
        if (arr[idx] > 1) { /* if page has two pointers pointing at it */
            /* copy p0 for child process and make l0 entry points to the new p0 */
            void* new_p0 = vms_new_page();
            memcpy(new_p0, old_p0, PAGE_SIZE);
            uint64_t new_p0_ppn = vms_page_to_ppn(new_p0);
            vms_pte_set_ppn(cur_l0_entry, new_p0_ppn);
            // arr[idx] --;
            int new_idx = vms_get_page_index(page_table);
            printf("new index: %d\n", new_idx);
            arr[new_idx] = 1;
            
        }
        if (arr[idx] >= 1) {
            arr[idx] --;
            printf("index decremented: %d, %d\n", idx, arr[idx]);
            vms_pte_valid_set(cur_l0_entry);
            vms_pte_read_set(cur_l0_entry);
            vms_pte_write_set(cur_l0_entry);
            vms_pte_custom_clear(cur_l0_entry);
        }
        
    }

}

void set_all_bits(uint64_t* p_entry, uint64_t* c_entry){
    if (vms_pte_valid(p_entry)){
        vms_pte_valid_set(c_entry);
    }
    if (vms_pte_read(p_entry)){
        vms_pte_read_set(c_entry);
    }
    if (vms_pte_write(p_entry)){
        vms_pte_write_set(c_entry);
    }
    if (vms_pte_custom(p_entry)){
        vms_pte_custom_set(c_entry);
    }
}

void* vms_fork_copy() {
    /* Get parent root page table (L2) */
    void* parent_l2 = vms_get_root_page_table();
    /* Get child root page table (L2) */
    void* child_l2 = vms_new_page();
    /* Copy L2 content*/
    memcpy(child_l2, parent_l2, PAGE_SIZE);
    for (int i=0; i < NUM_PTE_ENTRIES; ++i) {
        uint64_t *parent_l2_entry = vms_page_table_pte_entry_from_index(parent_l2, i);
        uint64_t *child_l2_entry = vms_page_table_pte_entry_from_index(child_l2, i);
        if (vms_pte_valid(parent_l2_entry)) {
            void* child_l1 = vms_new_page();
            /* Copy L1 content */
            uint64_t parent_l1_ppn = vms_pte_get_ppn(parent_l2_entry);
            void* parent_l1 = vms_ppn_to_page(parent_l1_ppn); // convert ppn to page table *
            memcpy(child_l1, parent_l1, PAGE_SIZE);            
            for (int j=0; j < NUM_PTE_ENTRIES; ++j) {
                uint64_t *parent_l1_entry = vms_page_table_pte_entry_from_index(parent_l1, j);
                uint64_t *child_l1_entry = vms_page_table_pte_entry_from_index(child_l1, j);
                if (vms_pte_valid(parent_l1_entry)) {     
                    void* child_l0 = vms_new_page();               
                    /* Copy L1 content */
                    uint64_t parent_l0_ppn = vms_pte_get_ppn(parent_l1_entry);
                    void* parent_l0 = vms_ppn_to_page(parent_l0_ppn); // convert ppn to page table *
                    memcpy(child_l0, parent_l0, PAGE_SIZE);
                    for (int k=0; k < NUM_PTE_ENTRIES; k++) {
                        uint64_t *parent_l0_entry = vms_page_table_pte_entry_from_index(parent_l0, k);
                        uint64_t *child_l0_entry = vms_page_table_pte_entry_from_index(child_l0, k);
                        if (vms_pte_valid(parent_l0_entry)) {
                            void* child_p0 = vms_new_page();
                            /* Copy p0 content */
                            uint64_t parent_p0_ppn = vms_pte_get_ppn(parent_l0_entry);
                            void* parent_p0 = vms_ppn_to_page(parent_p0_ppn); // convert ppn to page table *
                            /* copy p0 ppn to l0 child entry */
                            memcpy(child_p0, parent_p0, PAGE_SIZE);
                            uint64_t child_p0_ppn = vms_page_to_ppn(child_p0);
                            vms_pte_set_ppn(child_l0_entry, child_p0_ppn); // child entry in l2, child ppn in l1
                            set_all_bits(child_l0_entry, parent_l0_entry);
                        }

                    }
                    /* copy l1 ppn to l2 child entry */
                    uint64_t child_l0_ppn = vms_page_to_ppn(child_l0);
                    vms_pte_set_ppn(child_l1_entry, child_l0_ppn); // child entry in l2, child ppn in l1
                    set_all_bits(child_l1_entry, parent_l1_entry);
                }
            }
            /* copy l1 ppn to l2 child entry */
            uint64_t child_l1_ppn = vms_page_to_ppn(child_l1);
            vms_pte_set_ppn(child_l2_entry, child_l1_ppn); // child entry in l2, child ppn in l1
            set_all_bits(child_l2_entry, parent_l2_entry);
        }

    }

    return child_l2;
}

void set_bits_except_write(uint64_t* p_entry, uint64_t* c_entry){
    if (vms_pte_valid(p_entry)){
        vms_pte_valid_set(c_entry);
    }
    if (vms_pte_read(p_entry)){
        vms_pte_read_set(c_entry);
    }
    if (vms_pte_write(p_entry)){
        vms_pte_write_set(c_entry);
    }
    if (vms_pte_custom(p_entry)){
        vms_pte_custom_set(c_entry);
    }
}

void recursive_cow(void *parent_l, void *child_l, int level) {
    memcpy(child_l, parent_l, PAGE_SIZE);
    for (int i=0; i < NUM_PTE_ENTRIES; ++i) {
        uint64_t *parent_entry = vms_page_table_pte_entry_from_index(parent_l, i);
        uint64_t *child_entry = vms_page_table_pte_entry_from_index(child_l, i);
        if (vms_pte_valid(parent_entry)) {
            if (level > 0) {
                void* child_nl = vms_new_page();
                /* child entry points to make next level's ppn */
                uint64_t parent_nl_ppn = vms_pte_get_ppn(parent_entry);
                void* parent_nl = vms_ppn_to_page(parent_nl_ppn);
                recursive_cow(parent_nl, child_nl, level-1);
                uint64_t child_nl_ppn = vms_page_to_ppn(child_nl);
                vms_pte_set_ppn(child_entry, child_nl_ppn);
                set_all_bits(child_entry, parent_entry);
            }
            else { // at level 0
                uint64_t parent_p0_ppn = vms_pte_get_ppn(parent_entry);
                vms_pte_set_ppn(child_entry, parent_p0_ppn);
                set_bits_except_write(child_entry, parent_entry);
                if (vms_pte_write(parent_entry)||vms_pte_custom(parent_entry)) {
                    vms_pte_write_clear(parent_entry);
                    vms_pte_custom_set(parent_entry);
                    vms_pte_write_clear(child_entry);
                    vms_pte_custom_set(child_entry);
                    void* parent_p0 = vms_ppn_to_page(parent_p0_ppn);
                    int idx_p = vms_get_page_index(parent_p0);
                    arr[idx_p] += 2; /* the parent's p0 has two pointers pointing at it */
                    // int idx_c = vms_get_page_index(child_l);
                    // arr[idx_c] += 2;
                    // printf("cow: %d, %d\n",idx_p, idx_c);
                    // printf("write bit for child: %d\n", vms_pte_write(child_entry));
                }
            }
        }
    }
}

void* vms_fork_copy_on_write() {
    void* parent_l2 = vms_get_root_page_table();
    /* Get child root page table (L2) */
    void* child_l2 = vms_new_page();
    recursive_cow(parent_l2, child_l2, 2);
    return child_l2;
}
