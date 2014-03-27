#include "drivers/bytedev.h"
#include "drivers/dev.h"
#include "util/debug.h"

static void write_chars(bytedev_t *bd, int count, char c){
    int i;
    for (i = 0; i < count; i++){
    }
}

static void test_dev_null(){
    dbg(DBG_TEST, "testing dev null\n");

    char buf[1000];

    bytedev_t *dn = bytedev_lookup(MEM_NULL_DEVID);

    KASSERT(dn->cd_ops->read(dn, 0, buf, 500) == 0);

    KASSERT(dn->cd_ops->write(dn, 0, buf, 800 == 800));

    dbg(DBG_TESTPASS, "all dev null tests passed\n");
}

static void test_dev_zero(){
    dbg(DBG_TEST, "testing dev zero\n");

    char buf[1000];

    int i;
    for (i = 0; i < 1000; i++){
        buf[i] = 'a';
    }

    bytedev_t *dz = bytedev_lookup(MEM_ZERO_DEVID);

    KASSERT(dz->cd_ops->read(dz, 0, buf, 1000) == 1000);

    int j;
    for (j = 0; j < 1000; j++){
        KASSERT(buf[j] == '\0');
    }

    dbg(DBG_TESTPASS, "all dev zero tests passed\n");
}

void run_memdev_tests(){
    dbg(DBG_TEST, "testing memory devices\n");
    test_dev_null();
    test_dev_zero();

    dbg(DBG_TESTPASS, "all memory device tests passed!\n");
}
