//
//  test-cache.cc
//

#undef NDEBUG
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>
#include <cinttypes>
#include <csignal>
#include <csetjmp>
#include <cerrno>
#include <cmath>
#include <cctype>
#include <cwchar>
#include <climits>
#include <cfloat>
#include <cfenv>
#include <limits>
#include <array>
#include <string>
#include <vector>
#include <algorithm>
#include <memory>
#include <random>
#include <deque>
#include <map>
#include <thread>
#include <atomic>
#include <type_traits>

#include "dense_hash_map"

#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>

#include "host-endian.h"
#include "types.h"
#include "fmt.h"
#include "bits.h"
#include "sha512.h"
#include "format.h"
#include "meta.h"
#include "util.h"
#include "color.h"
#include "host.h"
#include "cmdline.h"
#include "codec.h"
#include "elf.h"
#include "elf-file.h"
#include "elf-format.h"
#include "strings.h"
#include "disasm.h"
#include "alu.h"
#include "fpu.h"
#include "pte.h"
#include "pma.h"
#include "amo.h"
#include "processor-logging.h"
#include "processor-base.h"
#include "processor-impl.h"
#include "mmu-memory.h"
#include "tlb-soft.h"
#include "mmu-soft.h"
#include "interp.h"
#include "processor-model.h"
#include "queue.h"
#include "console.h"
#include "cache.h"
#include "pma.h"

using namespace riscv;

int main(int argc, char *argv[])
{       

    assert(page_shift == 12);
    assert(page_size == 4096);
   
    std::shared_ptr<user_memory<u64>> mem = std::make_shared<user_memory<u64>>();
    u8 memVal, x, x2, x3, y, y2, z, z2, z3, temp, temp2, temp3, temp4, temp5;

    // add some RAM to the mem
    mem->add_ram(0x2ABCDE, /*1GB*/0x40000000LL - 0x2ABCDE);
    
    //////////////////////////
    //Direct mapped, write through tests.
    //////////////////////////
    
    printf("Running tests for direct mapped, write through cache...\n");
    
    tagged_cache<param_rv64,4096,1,1024> cache_dm(mem,cache_write_through);
    //Store a value into an empty line in the cache, load to see if we got a hit.
    temp = 23;
    cache_dm.access_cache(0x2ABCDE, 'S', temp); 
    assert(cache_dm.last_access == cache_line_empty);
    cache_dm.access_cache(0x2ABCDE,'L', y2);
    assert(cache_dm.last_access == cache_line_hit);
    //Check the same value was returned, and that the value exists in memory.
    assert(23 == y2);
    assert(((cache_dm.lookup_cache_line(0x2ABCDE).first->ppn << 12) + 0xCDE) == cache_dm.mem->segments.front()->mpa);
    assert(cache_dm.mem->segments.front()->mpa == mem->segments.front()->mpa);
    //Evict the same block with a new tag
    temp = 75;
    cache_dm.access_cache(0x2ACCDA, 'S', temp);
    assert(cache_dm.last_access == cache_line_must_evict);
    cache_dm.access_cache(0x2ACCDA, 'L', z2);
    assert(cache_dm.last_access = cache_line_hit);
    //Check we get 75 back
    assert(z2 == 75);
    assert(z2 != 23);
    //Check the ppn was changed
    assert(cache_dm.lookup_cache_line(0x2ACCDA).first->ppn != 0x2ab);
    //Load the previous tag back in, check that it still retains its old value from main memory
    cache_dm.access_cache(0x2abcde, 'L',z3);
    assert(cache_dm.last_access == cache_line_must_evict);
    mem->load(0x2abcde, memVal);
    assert(z3 == 23);
    assert(z3 == y2);
    //Lookup the 0x2accda mpa, check that the ppn there is for 0x2ab since we loaded it in.
    assert(cache_dm.lookup_cache_line(0x2accda).first->ppn == 0x2ab);
    
    ////////////////////////
    //Direct mapped, write back tests
    ////////////////////////

    printf("Running tests for direct mapped, write back cache...\n");

    mem->clear_segments();
    mem->add_ram(0x1000, 0x40000000 - 0x1000);
    tagged_cache<param_rv64, 4096, 1, 16> cache_dm_wb(mem);
    //Store some values into memory
    temp = 23; temp2 = 14; temp3 = 35;
    cache_dm_wb.access_cache(0x11ccf, 'S', temp);
    assert(cache_dm_wb.last_access == cache_line_empty);
    cache_dm_wb.access_cache(0x11cdf, 'S', temp2);
    assert(cache_dm_wb.last_access == cache_line_empty);
    cache_dm_wb.access_cache(0x11cb0, 'S', temp3);
    assert(cache_dm_wb.last_access == cache_line_empty);
    //Check that each of those values are not in memory yet.
    mem->load(0x11ccf, memVal);
    assert(memVal != 23);
    mem->load(0x11cd3, memVal);
    assert(memVal != 14);
    mem->load(0x11cb0, memVal);
    assert(memVal != 35);
    //Save new values into those addresses into the cache. Verify they are hits.
    temp = 65; temp2 = 151; temp3 = 240;
    cache_dm_wb.access_cache(0x11ccf, 'S', temp);
    assert(cache_dm_wb.last_access == cache_line_hit);
    cache_dm_wb.access_cache(0x11cd3, 'S', temp2);
    assert(cache_dm_wb.last_access == cache_line_hit);
    cache_dm_wb.access_cache(0x11cb0, 'S', temp3);
    assert(cache_dm_wb.last_access == cache_line_hit);
    //Check again that each of the values saved are not in memory yet.
    mem->load(0x11ccf, memVal);
    assert(memVal != 65);
    mem->load(0x11cd3, memVal);
    assert(memVal != 151);
    mem->load(0x11cb0, memVal);
    assert(memVal != 240);
    //Overwrite the addresses in the cache, this should trigger the writes to memory.
    cache_dm_wb.access_cache(0x22cc3, 'L', y);
    cache_dm_wb.access_cache(0x32cda, 'L', y2);
    cache_dm_wb.access_cache(0xa4cbf, 'L', z);
    //Check the values in memory with what we previously had saved in the cache
    mem->load(0x11ccf, memVal);
    assert(memVal == 65);
    mem->load(0x11cd3, memVal);
    assert(memVal == 151);
    mem->load(0x11cb0, memVal);
    assert(memVal == 240);
   
    ///////////////////////////
    //Set Associative, write through tests.
    ///////////////////////////

    printf("Running tests for set associative, write through cache...\n");
    mem->clear_segments();
    mem->add_ram(0x30000, 0x40000000LL - 0x30000);
    tagged_cache<param_rv64,8192, 2, 256> cache_sa_wt(mem, cache_write_through);
    //Store some values into a set
    temp = 110; temp2 = 33;
    cache_sa_wt.access_cache(0x41a22, 'S', temp);
    assert(cache_sa_wt.last_access == cache_line_empty);
    cache_sa_wt.access_cache(0x55a00, 'S', temp2);
    assert(cache_sa_wt.last_access == cache_line_empty);
    //Check if memory has the values since we write through to memory.
    mem->load(0x41a22, memVal);
    assert(memVal == 110);
    mem->load(0x55a00, memVal);
    assert(memVal == 33);
    //Write to the same addresses again.
    temp = 202; temp2 = 88;
    cache_sa_wt.access_cache(0x41a22, 'S', temp);
    assert(cache_sa_wt.last_access == cache_line_hit);
    cache_sa_wt.access_cache(0x55a00, 'S', temp2);
    assert(cache_sa_wt.last_access == cache_line_hit);
    //Check memory again to see if the values were updated.
    mem->load(0x41a22, memVal);
    assert(memVal == 202);
    mem->load(0x55a00, memVal);
    assert(memVal == 88);
    //Overwrite the cache lines with new addresses
    temp = 134; temp2 = 22;
    cache_sa_wt.access_cache(0x99a11, 'S', temp);
    assert(cache_sa_wt.last_access == cache_line_must_evict);
    cache_sa_wt.access_cache(0x77a33, 'S', temp2);
    assert(cache_sa_wt.last_access == cache_line_must_evict);
    //Check that memory was updated for these new addresses.
    mem->load(0x99a11, memVal);
    assert(memVal == 134);
    mem->load(0x77a33, memVal);
    assert(memVal == 22);
    //Load the old addresses back into the cache
    cache_sa_wt.access_cache(0x41a22, 'L', y);
    assert(cache_sa_wt.last_access == cache_line_must_evict);
    cache_sa_wt.access_cache(0x55a00, 'L', y2);
    assert(cache_sa_wt.last_access == cache_line_must_evict);
    //Check that the values left in memory were carried back into the cache.
    mem->load(0x41a22, memVal);
    assert(memVal == y);
    mem->load(0x55a00, memVal);
    assert(memVal == y2);
     
    

    ///////////////////////////
    //Set Associative, write back tests.
    ///////////////////////////
    
    printf("Running tests for set associative, write back cache...\n");
    
    //Clear main memory, refill it with new RAM
    mem->clear_segments();
    mem->add_ram(0x20000, 0x40000000LL - 0x20000);
    tagged_cache<param_rv64,16384,4,256> cache_sa(mem);
    //Fill a set with data
    temp = 76; temp2 = 55; temp3 = 23;
    cache_sa.access_cache(0x20000,'S',temp);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x3f0de, 'S', temp2);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x45021, 'L', z);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x23025, 'S', temp3);
    assert(cache_sa.last_access == cache_line_empty);
    //Access the cache to see if we get a hit on the data we loaded.
    cache_sa.access_cache(0x3f0de, 'L', x);
    assert(cache_sa.last_access == cache_line_hit);
    cache_sa.access_cache(0x23025, 'L', x2);
    assert(cache_sa.last_access == cache_line_hit);
    cache_sa.access_cache(0x20000, 'L', x3);
    assert(cache_sa.last_access == cache_line_hit);
    assert(x == 55 && x2 == 23 && x3 == 76);
    //Check that main memory does not have the data
    mem->load(0x20000,memVal);
    assert(x3 != memVal);
    mem->load(0x3f0de, memVal);
    assert(x != memVal);
    mem->load(0x23025, memVal);
    assert(x2 != memVal);
    //Now evict each line in the set. 
    temp = 11; temp2 = 252;
    cache_sa.access_cache(0x540f2, 'S', temp);
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x320a1, 'L', z);
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x9b02a, 'L', z2);
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0xff034, 'S', temp2);
    assert(cache_sa.last_access == cache_line_must_evict);
    //Check main memory again for the data. Everything should have been written back.
    mem->load(0x20000,memVal);
    assert(x3 == memVal);
    mem->load(0x3f0de, memVal);
    assert(x == memVal);
    mem->load(0x23025, memVal);
    assert(x2 == memVal);
    //Load the old data back into the cache. Check that it returns the same values from memory.
    cache_sa.access_cache(0x20000, 'L', x);
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x3f0de, 'L', x2);
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x23025, 'L', x3);
    assert(cache_sa.last_access == cache_line_must_evict);
    assert(x == 76 && x2 == 55 && x3 == 23);
    //Check that the dirty line for 0x540f2 was written to memory
    mem->load(0x540f2, memVal);
    assert(memVal == 11);
    //Test multiple cache hits on a single line
    temp = 52; temp2 = 37; temp3 = 101; temp4 = 41; temp5 = 74;
    cache_sa.access_cache(0xffffa02, 'S', temp);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0xffffaff, 'S', temp2);
    assert(cache_sa.last_access == cache_line_hit);
    cache_sa.access_cache(0xffffa32, 'S', temp3);
    assert(cache_sa.last_access == cache_line_hit);
    cache_sa.access_cache(0xffffa10, 'S', temp4);
    assert(cache_sa.last_access == cache_line_hit);
    cache_sa.access_cache(0xffffa77, 'S', temp5);
    assert(cache_sa.last_access == cache_line_hit);
    //Ensure none have been written to memory yet
    mem->load(0xffffa02, memVal);
    assert(memVal != 52);
    mem->load(0xffffaff, memVal);
    assert(memVal != 37);
    mem->load(0xffffa32, memVal);
    assert(memVal != 101);
    mem->load(0xffffa10, memVal);
    assert(memVal != 41);
    mem->load(0xffffa77, memVal);
    assert(memVal != 74);
    //Write new blocks into the set to overwrite the block with 0xffff tag
    cache_sa.access_cache(0x1010a22, 'L', z);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x6042aee, 'L', z);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x3432a11, 'L', z);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x11b3a00, 'L', z);
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x2221a33, 'L', z);
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x4325a11, 'L', z);
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x9821a00, 'L', z);
    assert(cache_sa.last_access == cache_line_must_evict);
    //Check that memory was updated
    mem->load(0xffffa02, memVal);
    assert(memVal == 52);
    mem->load(0xffffaff, memVal);
    assert(memVal == 37);
    mem->load(0xffffa32, memVal);
    assert(memVal == 101);
    mem->load(0xffffa10, memVal);
    assert(memVal == 41);
    mem->load(0xffffa77, memVal);
    assert(memVal == 74);
    //Load some values into memory manually
    mem->store(0x33afb22, 77);
    mem->store(0x33afb99, 12);
    mem->store(0x33afb1a, 204);
    mem->store(0x33afb3e, 117);
    //Load the memory addresses into the cache, see if we get a hit (after first miss).
    cache_sa.access_cache(0x33afb22, 'L', x);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x33afb99, 'L', x2);
    assert(cache_sa.last_access == cache_line_hit);
    cache_sa.access_cache(0x33afb1a, 'L', x3);
    assert(cache_sa.last_access == cache_line_hit);
    cache_sa.access_cache(0x33afb3e, 'L', y);
    assert(cache_sa.last_access == cache_line_hit);
    //Check that all the values were put into the cache
    mem->load(0x33afb22, memVal);
    assert(memVal == x);
    mem->load(0x33afb99, memVal);
    assert(memVal == x2);
    mem->load(0x33afb1a, memVal);
    assert(memVal == x3);
    mem->load(0x33afb3e, memVal);
    assert(memVal == y);
    
    //////////////////////////////////////
    //Tests for values larger than one byte
    /////////////////////////////////////
    u16 temp_16, temp_16_2;
    u32 temp_32, temp_32_2;
    u64 temp_64, temp_64_2;
    printf("Running tests for values larger than one byte...\n");
    mem->clear_segments();
    mem->add_ram(0x20000, 0x40000000LL - 0x20000);
    tagged_cache<param_rv64,32768,8,64> cache_t(mem,cache_write_through);
    cache_t.default_ram_base = 0x20000;
    cache_t.default_ram_size = 0x40000000LL - 0x20000;
    temp_16 = 0x2311;
    cache_t.access_cache(0x353921, 'S', temp_16);
    cache_t.access_cache(0x353921, 'L', temp_16_2);
    assert(temp_16_2 == 0x2311);
    temp_32 = 0x821af321;
    cache_t.access_cache(0x2abcde, 'S', temp_32);
    cache_t.access_cache(0x2abcde, 'L', temp_32_2);
    assert(temp_32_2 == 0x821af321);
    temp_64 = 0x113a12481921a113;
    cache_t.store_c(0x6421aa, temp_64);
    cache_t.load_c(0x6421aa, temp_64_2);
    assert(temp_64_2 == temp_64);
    temp_64 = 0x0807060504030201;
    u8 current_byte = 0x1;
    u64 mpa = 0x6421aa;
    for(int i = 0; i < sizeof(temp_64); i++){
        cache_t.store_c(mpa++,current_byte++);
    }
    cache_t.load_c(0x6421aa, temp_64_2);
    assert(temp_64 == temp_64_2);
    u64 temp_64_3;
    cache_t.load_c(0x6421aa,temp_64_3);
    printf("temp_64_3 is %llx\n",temp_64_3);
    assert(temp_64_2 == temp_64_3);
    temp_64_3 = 0;
    mem->load(0x6421aa,temp_64_3);
    assert(temp_64_3 == temp_64_3);
    printf("All tests passed!\n");
}       



