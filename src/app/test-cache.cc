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
    u8 memVal;

    // add some RAM to the mem
    mem->add_ram(0x2ABCDE, /*1GB*/0x40000000LL - 0x2ABCDE);
    
    //////////////////////////
    //Direct mapped, write through tests.
    //////////////////////////
    
    printf("Running tests for direct mapped, write through cache...\n");
    
    tagged_cache<param_rv64,4096,1,1024> cache_dm(mem,cache_write_through);
    //Store a value into an empty line in the cache, load to see if we got a hit.
    u8 y = cache_dm.access_cache(0x2ABCDE, 'S', 23); 
    assert(cache_dm.last_access == cache_line_empty);
    u8 y2 = cache_dm.access_cache(0x2ABCDE,'L');
    assert(cache_dm.last_access == cache_line_hit);
    //Check the same value was returned, and that the value exists in memory.
    assert(y == y2);
    assert(((cache_dm.lookup_cache_line(0x2ABCDE).first->ppn << 12) + 0xCDE) == cache_dm.mem->segments.front()->mpa);
    assert(cache_dm.mem->segments.front()->mpa == mem->segments.front()->mpa);
    //Evict the same block with a new tag
    u8 z = cache_dm.access_cache(0x2ACCDA, 'S', 75);
    assert(cache_dm.last_access == cache_line_must_evict);
    u8 z2 = cache_dm.access_cache(0x2ACCDA, 'L');
    assert(cache_dm.last_access = cache_line_hit);
    //Check we get 75 back
    assert(z == z2);
    assert(z2 != y);
    //Check the ppn was changed
    assert(cache_dm.lookup_cache_line(0x2ACCDA).first->ppn != 0x2ab);
    //Load the previous tag back in, check that it still retains its old value from main memory
    u8 z3 = cache_dm.access_cache(0x2abcde, 'L');
    assert(cache_dm.last_access == cache_line_must_evict);
    assert(z3 == y);
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
    //Load some values into memory
    y = cache_dm_wb.access_cache(0x11cc0, 'S', 23);
    assert(cache_dm_wb.last_access == cache_line_empty);
    y2 = cache_dm_wb.access_cache(0x11cd3, 'S', 14);
    assert(cache_dm_wb.last_access == cache_line_empty);
    z = cache_dm_wb.access_cache(0x11cbf, 'S', 35);
    assert(cache_dm_wb.last_access == cache_line_empty);
    //Check that each of those values are not in memory yet.
    mem->load(0x11cc0, memVal);
    assert(memVal != y);
    mem->load(0x11cd3, memVal);
    assert(memVal != y2);
    mem->load(0x11cbf, memVal);
    assert(memVal != z);
    //Save new values into those addresses into the cache. Verify they are hits.
    y = cache_dm_wb.access_cache(0x11cc0, 'S', 65);
    assert(cache_dm_wb.last_access == cache_line_hit);
    y2 = cache_dm_wb.access_cache(0x11cd3, 'S', 151);
    assert(cache_dm_wb.last_access == cache_line_hit);
    z = cache_dm_wb.access_cache(0x11cbf, 'S', 240);
    assert(cache_dm_wb.last_access == cache_line_hit);
    //Check again that each of the values saved are not in memory yet.
    mem->load(0x11cc0, memVal);
    assert(memVal != y);
    mem->load(0x11cd3, memVal);
    assert(memVal != y2);
    mem->load(0x11cbf, memVal);
    assert(memVal != z);
    //Overwrite the addresses in the cache, this should trigger the writes to memory.
    cache_dm_wb.access_cache(0x22cc3, 'L');
    cache_dm_wb.access_cache(0x32cda, 'L');
    cache_dm_wb.access_cache(0xa4cbf, 'L');
    //Check the values in memory with what we previously had saved in the cache
    mem->load(0x11cc0, memVal);
    assert(memVal == y);
    mem->load(0x11cd3, memVal);
    assert(memVal == y2);
    mem->load(0x11cbf, memVal);
    assert(memVal == z);
   
    ///////////////////////////
    //Set Associative, write through tests.
    ///////////////////////////

    printf("Running tests for set associative, write through cache...\n");
    mem->clear_segments();
    mem->add_ram(0x30000, 0x40000000LL - 0x30000);
    tagged_cache<param_rv64,8192, 2, 256> cache_sa_wt(mem, cache_write_through);
    //Store some values into a set
    y = cache_sa_wt.access_cache(0x41a22, 'S', 110);
    assert(cache_sa_wt.last_access == cache_line_empty);
    y2 = cache_sa_wt.access_cache(0x55a00, 'S', 33);
    assert(cache_sa_wt.last_access == cache_line_empty);
    //Check if memory has the values since we write through to memory.
    mem->load(0x41a22, memVal);
    assert(memVal == y);
    mem->load(0x55a00, memVal);
    assert(memVal == y2);
    //Write to the same addresses again.
    y = cache_sa_wt.access_cache(0x41a22, 'S', 202);
    assert(cache_sa_wt.last_access == cache_line_hit);
    y2 = cache_sa_wt.access_cache(0x55a00, 'S', 88);
    assert(cache_sa_wt.last_access == cache_line_hit);
    //Check memory again to see if the values were updated.
    mem->load(0x41a22, memVal);
    assert(memVal == y);
    mem->load(0x55a00, memVal);
    assert(memVal == y2);
    //Overwrite the cache lines with new addresses
    z = cache_sa_wt.access_cache(0x99a11, 'S', 134);
    assert(cache_sa_wt.last_access == cache_line_must_evict);
    z2 = cache_sa_wt.access_cache(0x77a33, 'S', 22);
    assert(cache_sa_wt.last_access == cache_line_must_evict);
    //Check that memory was updated for these new addresses.
    mem->load(0x99a11, memVal);
    assert(memVal == z);
    mem->load(0x77a33, memVal);
    assert(memVal == z2);
    //Load the old addresses back into the cache
    y = cache_sa_wt.access_cache(0x41a22, 'L');
    assert(cache_sa_wt.last_access == cache_line_must_evict);
    y2 = cache_sa_wt.access_cache(0x55a00, 'L');
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
    cache_sa.access_cache(0x20000,'S',76);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x3f0de, 'S', 55);
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x45021, 'L');
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x23025, 'S', 23);
    assert(cache_sa.last_access == cache_line_empty);
    //Access the cache to see if we get a hit on the data we loaded.
    u8 x = cache_sa.access_cache(0x3f0de, 'L');
    assert(cache_sa.last_access == cache_line_hit);
    u8 x2 = cache_sa.access_cache(0x23025, 'L');
    assert(cache_sa.last_access == cache_line_hit);
    u8 x3 = cache_sa.access_cache(0x20000, 'L');
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
    cache_sa.access_cache(0x540f2, 'S', 11);
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x320a1, 'L');
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x9b02a, 'L');
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0xff034, 'S', 252);
    assert(cache_sa.last_access == cache_line_must_evict);
    //Check main memory again for the data. Everything should have been written back.
    mem->load(0x20000,memVal);
    assert(x3 == memVal);
    mem->load(0x3f0de, memVal);
    assert(x == memVal);
    mem->load(0x23025, memVal);
    assert(x2 == memVal);
    //Load the old data back into the cache. Check that it returns the same values from memory.
    x = cache_sa.access_cache(0x20000, 'L');
    assert(cache_sa.last_access == cache_line_must_evict);
    x2 = cache_sa.access_cache(0x3f0de, 'L');
    assert(cache_sa.last_access == cache_line_must_evict);
    x3 = cache_sa.access_cache(0x23025, 'L');
    assert(cache_sa.last_access == cache_line_must_evict);
    assert(x == 76 && x2 == 55 && x3 == 23);
    //Check that the dirty line for 0x540f2 was written to memory
    mem->load(0x540f2, memVal);
    assert(memVal == 11);
    //Test multiple cache hits on a single line
    x = cache_sa.access_cache(0xffffa02, 'S', 52);
    assert(cache_sa.last_access == cache_line_empty);
    x2 = cache_sa.access_cache(0xffffaff, 'S', 37);
    assert(cache_sa.last_access == cache_line_hit);
    x3 = cache_sa.access_cache(0xffffa32, 'S', 101);
    assert(cache_sa.last_access == cache_line_hit);
    y = cache_sa.access_cache(0xffffa10, 'S', 41);
    assert(cache_sa.last_access == cache_line_hit);
    y2 = cache_sa.access_cache(0xffffa77, 'S', 74);
    assert(cache_sa.last_access == cache_line_hit);
    //Ensure none have been written to memory yet
    mem->load(0xffffa02, memVal);
    assert(memVal != x);
    mem->load(0xffffaff, memVal);
    assert(memVal != x2);
    mem->load(0xffffa32, memVal);
    assert(memVal != x3);
    mem->load(0xffffa10, memVal);
    assert(memVal != y);
    mem->load(0xffffa77, memVal);
    assert(memVal != y2);
    //Write new blocks into the set to overwrite the block with 0xffff tag
    cache_sa.access_cache(0x1010a22, 'L');
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x6042aee, 'L');
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x3432a11, 'L');
    assert(cache_sa.last_access == cache_line_empty);
    cache_sa.access_cache(0x11b3a00, 'L');
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x2221a33, 'L');
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x4325a11, 'L');
    assert(cache_sa.last_access == cache_line_must_evict);
    cache_sa.access_cache(0x9821a00, 'L');
    assert(cache_sa.last_access == cache_line_must_evict);
    //Check that memory was updated
    mem->load(0xffffa02, memVal);
    assert(memVal == x);
    mem->load(0xffffaff, memVal);
    assert(memVal == x2);
    mem->load(0xffffa32, memVal);
    assert(memVal == x3);
    mem->load(0xffffa10, memVal);
    assert(memVal == y);
    mem->load(0xffffa77, memVal);
    assert(memVal == y2);
    //Load some values into memory manually
    mem->store(0x33afb22, 77);
    mem->store(0x33afb99, 12);
    mem->store(0x33afb1a, 204);
    mem->store(0x33afb3e, 117);
    //Load the memory addresses into the cache, see if we get a hit (after first miss).
    x = cache_sa.access_cache(0x33afb22, 'L');
    assert(cache_sa.last_access == cache_line_empty);
    x2 = cache_sa.access_cache(0x33afb99, 'L');
    assert(cache_sa.last_access == cache_line_hit);
    x3 = cache_sa.access_cache(0x33afb1a, 'L');
    assert(cache_sa.last_access == cache_line_hit);
    y = cache_sa.access_cache(0x33afb3e, 'L');
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
    printf("All tests passed!\n");
}       



