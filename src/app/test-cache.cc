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
#include "user-memory.h"
#include "tlb-soft.h"
#include "mmu-soft.h"
#include "interp.h"
#include "processor-model.h"
#include "queue.h"
#include "console.h"
#include "device-rom-boot.h"
#include "device-rom-sbi.h"
#include "device-rom-string.h"
#include "device-config.h"
#include "device-rtc.h"
#include "device-timer.h"
#include "device-plic.h"
#include "device-uart.h"
#include "device-mipi.h"
#include "device-gpio.h"
#include "device-rand.h"
#include "device-htif.h"
#include "processor-priv-1.9.h"
#include "debug-cli.h"
#include "processor-runloop.h"
#include "cache.h"
#include "processor-proxy.h"
#include "mmu-proxy.h"

#include "asmjit.h"

#include "jit-decode.h"
#include "jit-emitter-rv32.h"
#include "jit-emitter-rv64.h"
#include "jit-fusion.h"
#include "jit-runloop.h"

#include "assembler.h"
#include "jit.h"


using namespace riscv;

int main(int argc, char *argv[])
{       

    assert(page_shift == 12);
    assert(page_size == 4096);

    assert(sizeof(sv32_va) == 4);
    assert(sizeof(sv32_pa) == 8);
    assert(sizeof(sv32_pte) == 4);

    assert(sizeof(sv39_va) == 8);
    assert(sizeof(sv39_pa) == 8);
    assert(sizeof(sv39_pte) == 8);

    assert(sizeof(sv48_va) == 8);
    assert(sizeof(sv48_pa) == 8);
    assert(sizeof(sv48_pte) == 8);

    typedef mmu_soft_rv64 mmu_type;

    mmu_type mmu;

    // add RAM to the MMU emulation (exclude zero page)
    mmu.mem->add_ram(0x2ABCDE, /*1GB*/0x40000000LL - 0x2ABCDE);
    
    //////////////////////////
    //Direct mapped, write through tests.
    //////////////////////////

    tagged_cache<param_rv64,4096,1,1024> cache_dm(mmu.mem,cache_write_through);
    //Store a value into the cache, load to see if we got a hit.
    u8 y = cache_dm.access_cache(0x2ABCDE, 'S', 23); 
    u8 y2 = cache_dm.access_cache(0x2ABCDE,'L');
    //Check the same value was returned, and that the value exists in memory.
    assert(y == y2);
    assert(((cache_dm.lookup_cache_line(0x2ABCDE)->ppn << 12) + 0xCDE) == cache_dm.mem->segments.front()->mpa);
    assert(cache_dm.mem->segments.front()->mpa == mmu.mem->segments.front()->mpa);
    //Evict the same block with a new tag
    u8 z = cache_dm.access_cache(0x2ACCDA, 'S', 75);
    u8 z2 = cache_dm.access_cache(0x2ACCDA, 'L');
    //Check the ppn was changed
    assert(z == z2);
    assert(z2 != y);
    assert(cache_dm.lookup_cache_line(0x2ACCDA)->ppn != 0x2ab);
    //Load the previous tag back in, check that it still retains its old value from main memory
    u8 z3 = cache_dm.access_cache(0x2abcde, 'L');
    assert(z3 == y);
    assert(z3 == y2);
    //Lookup the 0x2accda mpa, check that the ppn there is for 0x2ab since we loaded it in.
    assert(cache_dm.lookup_cache_line(0x2accda)->ppn == 0x2ab);
}


