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

	typedef tagged_tlb_rv64<128> tlb_type;
	typedef mmu_soft_rv64 mmu_type;

	printf("tlb_type::size                : %tu\n", tlb_type::size);
	printf("tlb_type::key_size            : %tu\n", tlb_type::key_size);
	printf("tlb_type::mask                : 0x%08tx\n", tlb_type::mask);

	mmu_type mmu;

	// insert entry for VA 0x10000 into the TLB (PDID=0, ASID=0, VA=0x10000, PPN=1, PTE.bits=DAGURWXV)
	mmu.l1_dtlb.insert(/* PDID */ 0, /* ASID */ 0, /* VA */ 0x10000, /* PTE level */ 2, /* PTE.bits */ 0xff, /* PPN */ 0x1, /* PTE UVA */ 0);

	// test that PPN 1 is returned for (PDID=0, ASID=0, VA=0x10000) -> PPN=1, PTE.bits=DAGURWXV)
	tlb_type::tlb_entry_t *tlb_ent = mmu.l1_dtlb.lookup(/* PDID */ 0, /* ASID */ 0, /* VA */ 0x10000);
	assert(tlb_ent != nullptr);
	assert(tlb_ent->ppn == 0x1);
	assert(tlb_ent->pteb == 0xff);

	// test that invalid_ppn is returned for (PDID=0, ASID=0, VA=0x11000)
	assert(mmu.l1_dtlb.lookup(/* PDID */ 0, /* ASID */ 0, /* VA */ 0x11000) == nullptr);

	// test that invalid_ppn is returned for (PDID=0, ASID=1, VA=0x11000)
	assert(mmu.l1_dtlb.lookup(/* PDID */ 0, /* ASID */ 1, /* VA */ 0x10000) == nullptr);

	// flush the L1 DTLB
	mmu.l1_dtlb.flush(0);

	// test that invalid_ppn is returned for (VA=0x10000, ASID=0)
	assert(mmu.l1_dtlb.lookup(/* PDID */ 0, /* ASID */ 0, /* VA */ 0x10000) == nullptr);

	// add RAM to the MMU emulation (exclude zero page)
	mmu.mem->add_ram(0x1000, /*1GB*/0x40000000LL - 0x1000);

	// look up the User Virtual Address for a Machine Physical Adress
	memory_segment<typename tlb_type::UX> *segment = nullptr;
	addr_t uva = mmu.mem->mpa_to_uva(segment, 0x1000);
	assert(segment);
	assert(uva == mmu.mem->segments.front()->uva + 0x0LL);
        
        typedef processor_runloop<processor_privileged<processor_rv64imafdc_model<decode,processor_priv_rv64imafd,mmu_soft_rv64>>> process_type;
        process_type myProc; 
        //mmu.store(myProc, 0x20000, 327);
        int x = 477;
        printf("x is %d\n",x);
        mmu.load(myProc, 0x20000, x);
        printf("x is %d\n",x);

        tagged_cache<param_rv64,4096,1,1024, user_memory<u64>, process_type, mmu_type> myCache(myProc, mmu);
        //myCache.access_cache(0x20000, 'W'); 
        //myCache.access_cache(0x30000, 'W');

}
