
//
//  cache.h
//

#ifndef rv_cache_h
#define rv_cache_h

namespace riscv {

	/*
	 * cache_state
	 */

	enum cache_state {
		cache_state_modified  = 0b011,                   /* only copy, modified */
		cache_state_owned     = 0b110,                   /* several copies, modify permission */
		cache_state_exclusive = 0b010,                   /* only copy, unmodified */
		cache_state_shared    = 0b100,                   /* several copies, no modify permission */
		cache_state_invalid   = 0b000,                   /* not valid, must be fetched */
		cache_state_mask      = 0b111,
	};
        
        enum cache_line_status {
            cache_line_empty      = 0b00, /*Cache line is empty and not being used.*/
            cache_line_hit        = 0b01, /*Cache line has the ppn we're looking for*/
            cache_line_must_evict = 0b10, /*Cache line must be evicted to make room for another*/
            cache_line_filled     = 0b11, /*Cache line is filled with data but does not need to be evicted yet.*/
        };

	/*
	 * tagged_cache_entry
	 *
	 * protection domain, address space and physically tagged virtually indexed cache entry with page attributes
	 *
	 * cache[PDID:ASID:VA] = STATE:PPN:DATA
	 */

	template <typename PARAM, const size_t cache_line_size>
	struct tagged_cache_entry
	{

		typedef typename PARAM::UX UX;

		enum : UX {
			cache_line_shift =    ctz_pow2(cache_line_size),
			va_bits =             (sizeof(UX) << 3) - cache_line_shift,
			state_bits =          cache_line_shift,
			asid_bits =           PARAM::asid_bits,
			ppn_bits =            PARAM::ppn_bits,
			vcln_limit =          (1ULL<<va_bits)-1,
			ppn_limit =           (1ULL<<ppn_bits)-1,
			asid_limit =          (1ULL<<asid_bits)-1
		};

		static_assert(asid_bits + ppn_bits == 32 || asid_bits + ppn_bits == 64 ||
			asid_bits + ppn_bits == 128, "asid_bits + ppn_bits == (32, 64, 128)");

		/* Cache entry attributes */

		UX      vcln  : va_bits;       /* Virtual Cache Line Number */
		UX      state : state_bits;    /* Cache State */
		UX      ppn   : ppn_bits;      /* Physical Page Number */
		UX      asid  : asid_bits;     /* Address Space Identifier */
		pdid_t  pdid;                  /* Protection Domain Identifer */
		u8*     data;                  /* Cache Data */
                UX      status: 2;             /* Status of the entry (i.e. line is a hit, empty, or must be evicted)*/

		tagged_cache_entry() :
			 vcln(vcln_limit),
			 state(cache_state_invalid),
			 ppn(ppn_limit),
			 asid(asid_limit),
			 pdid(-1),
			 data(nullptr) {}

		tagged_cache_entry(UX vcln, UX asid, UX ppn) :
			vcln(vcln),
			state(cache_state_invalid),
			ppn(ppn),
			asid(asid),
			pdid(0),
			data(nullptr) {}
	};


	/*
	 * tagged_cache
	 *
	 * protection domain, address space and physically tagged, virtually indexed set associative cache
	 *
	 * cache[PDID:ASID:VA] = STATE:PPN:DATA
	 */

	template <typename PARAM, const size_t cache_size, const size_t cache_ways, const size_t cache_line_size,
		typename MEMORY = user_memory<typename PARAM::UX>> 
	struct tagged_cache
	{
		static_assert(ispow2(cache_size), "cache_size must be a power of 2");
		static_assert(ispow2(cache_ways), "cache_ways must be a power of 2");
		static_assert(ispow2(cache_line_size), "cache_line_size must be a power of 2");

		typedef typename PARAM::UX UX;
		typedef MEMORY memory_type;
		typedef tagged_cache_entry<PARAM,cache_line_size> cache_entry_t;
                std::shared_ptr<memory_type> mem;

		enum : UX {
			size =                cache_size,
			line_size =           cache_line_size,
			num_ways =            cache_ways,
			num_entries =         size / (num_ways * line_size),
			key_size =            sizeof(cache_entry_t),
			total_size =          cache_size + sizeof(cache_entry_t) * num_entries * num_ways,

			num_entries_shift =   ctz_pow2(num_entries),
			cache_line_shift =    ctz_pow2(cache_line_size),
			num_ways_shift =      ctz_pow2(num_ways),

			cache_line_mask =   ~((UX(1) << cache_line_shift) - 1),
			num_entries_mask =    (UX(1) << num_entries_shift) - 1,
                        vpn_mask        =    ~((UX(1) << (num_entries_shift + cache_line_shift))-1),
                        data_index_mask =    ((UX(1) << (cache_line_shift + num_entries_shift))-1),
                        cache_line_offset_mask = ((UX(1) << cache_line_shift)-1),

			asid_bits =           PARAM::asid_bits,
			ppn_bits =            PARAM::ppn_bits
		};

                

		// TODO - map cache index and data into the machine address space with user_memory::add_segment

		cache_entry_t cache_key[num_entries * num_ways];
		u8 cache_data[cache_size];

		tagged_cache(std::shared_ptr<memory_type> _mem) : mem(_mem)
		{
                        static_assert(page_shift == (cache_line_shift + num_entries_shift), "Page shift == cache_line_shift + num_entries_shift");
                        for (size_t i = 0; i < num_entries * num_ways; i++) {
				cache_key[i].data = cache_data + i * cache_line_size;
                                cache_key[i].status = cache_line_empty;
			}
		}

		void flush(memory_type &mem)
		{
			for (size_t i = 0; i < num_entries * num_ways; i++) {
				// TODO - flush this line to memory
				cache_key[i] = cache_entry_t();
			}
		}

		void flush(memory_type &mem, UX pdid)
		{
			for (size_t i = 0; i < num_entries * num_ways; i++) {
				if (cache_key[i].pdid != pdid) continue;
				// TODO - flush this line to memory
				cache_key[i] = cache_entry_t();
			}
		}

		void flush(memory_type &mem, UX pdid, UX asid)
		{
			for (size_t i = 0; i < num_entries * num_ways; i++) {
				if (cache_key[i].pdid != pdid && cache_key[i].asid != asid) continue;
				// TODO - flush this line to memory
				cache_key[i] = cache_entry_t();
			}
		}
                
                //Go through the line and load/store the contents for each element in the cache line.
                void ld_str_into_mem(UX mpa, u8 op){
                    UX mpa_masked = mpa & cache_line_mask;
                    UX index_for_data, offset;
                    //Traverse the cache line forward. Store the data corresponding to each address into memory.
                    for(index_for_data = mpa_masked & data_index_mask, offset = 0  
                        ; offset <= cache_line_offset_mask
                        ; index_for_data++, offset++, mpa_masked++)
                        op == 'S' ? mem->store(mpa_masked,cache_data[index_for_data]) : mem->load(mpa_masked,cache_data[index_for_data]);
                }
                
                //Given an mpa, access the cache to find the corresponding cache_entry, if it exists.
                //val will be written to the cache if op is 'S' and read from cache if op is 'L'
                //The caller must access the TLB and translate the UX va to UX mpa and provide it to this function.
                u8 access_cache(UX mpa, u8 op, u8 val = 0){
                    cache_entry_t * ent = lookup_cache_line(mpa);
                    //Write thru policy so write to mem
                    if(op == 'S'){
                        cache_data[mpa & data_index_mask] = val;
                        mem->store(mpa,val);
                    }
                    if(ent->status == cache_line_hit){
                        printf("We got a hit!\n");
                        ent->status = cache_line_filled;
                    }
                    else if(ent->status == cache_line_must_evict){
                        //TODO LRU Replacement, associativity, and writing policies
                        printf("We have a miss! We must evict a block.\n");
                        if(op == 'L')
                            ld_str_into_mem(mpa, 'L'); 
                        ent->ppn = mpa >> (num_entries_shift  + cache_line_shift);
                        ent->status = cache_line_filled;
                    }
                    else if(ent->status == cache_line_empty){
                        printf("We have a miss! We must fill an empty block\n");
                        if(op == 'W')
                            ld_str_into_mem(mpa,'L');
                        ent->ppn = mpa >> (num_entries_shift + cache_line_shift);
                        ent->state = cache_line_filled;
                    }
                    return cache_data[mpa & data_index_mask];
                }

		// cache line is virtually indexed but physically tagged.
		// Returns a cache line entry that could be a hit, line to evict, or empty line.
                // Also returns the code to define the entry, 0 == empty, 1 == hit, 2 == evict
                cache_entry_t* lookup_cache_line(UX mpa)
		{
                    cache_entry_t *empty_cache_line, *line_to_evict;
                    u8 found_empty_line = 0;
		    UX pcln = mpa >> cache_line_shift;
		    UX entry = pcln & num_entries_mask;
                    UX ppn = pcln >> num_entries_shift;
		    cache_entry_t *ent = cache_key + (entry); //<< num_ways_shift removed here, may need to put back.
		    for (size_t i = 0; i < num_ways; i++) {
			if (ent->ppn == ppn) {
			    ent->status = cache_line_hit;
                            return ent;
			}
                        else if(!found_empty_line && ent->status == cache_line_empty){
                            found_empty_line = 1;
                            empty_cache_line = ent;
                        }
                        else{
                            //TODO Implement replace policy, LRU?
                            line_to_evict = ent;
                            ent->status = cache_line_must_evict;
                        }
                        ent++;
		    }
		    
                    return found_empty_line ? empty_cache_line : line_to_evict;
		}

		// caller got a cache miss or invalid ppn from TLB and wants to allocate
		cache_entry_t* alloc_cache_line(memory_type &mem, UX pdid, UX asid, UX va)
		{
			// TODO - choose a way, if there is no free way, flush and allocate
		}

		// caller got a cache line with an invalid ppn and wants to invalidate
		void invalidate_cache_line(memory_type &mem, cache_entry_t *ent)
		{
			// TODO - flush to memory and invalidate cache line
		}

	};


        
	template <const size_t cache_size, const size_t cache_ways, const size_t cache_line_size>
	using tagged_cache_rv32 = tagged_cache<param_rv32,cache_size,cache_ways,cache_line_size>;

	template <const size_t cache_size, const size_t cache_ways, const size_t cache_line_size>
	using tagged_cache_rv64 = tagged_cache<param_rv64,cache_size,cache_ways,cache_line_size>;
        
}

#endif
