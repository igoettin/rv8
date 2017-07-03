
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
        
        //Enumerated types to define the status a cache line is in during a lookup.
        enum cache_line_status {
            cache_line_empty      = 0b00, /*Cache line is empty and not being used.*/
            cache_line_hit        = 0b01, /*Cache line has the ppn we're looking for*/
            cache_line_must_evict = 0b10, /*Cache line must be evicted to make room for another*/
            cache_line_filled     = 0b11, /*Cache line is filled with data but does not need to be evicted yet.*/
        };
        
        //Enumerated types to define the write to memory policy for the cache.
        enum cache_write_policy {
            cache_write_through = 0b0,
            cache_write_back    = 0b1,
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
			mpa_bits =            (sizeof(UX) << 3) - cache_line_shift,
			state_bits =          cache_line_shift,
			asid_bits =           PARAM::asid_bits,
			ppn_bits =            PARAM::ppn_bits,
			pcln_limit =          (1ULL<<mpa_bits)-1,
			ppn_limit =           (1ULL<<ppn_bits)-1,
			asid_limit =          (1ULL<<asid_bits)-1
		};

		static_assert(asid_bits + ppn_bits == 32 || asid_bits + ppn_bits == 64 ||
			asid_bits + ppn_bits == 128, "asid_bits + ppn_bits == (32, 64, 128)");

		/* Cache entry attributes */

		UX      pcln  : mpa_bits;       /* Physical Cache Line Number */
		UX      state : state_bits;    /* Cache State */
		UX      ppn   : ppn_bits;      /* Physical Page Number */
		UX      asid  : asid_bits;     /* Address Space Identifier */
		pdid_t  pdid;                  /* Protection Domain Identifer */
		u8*     data;                  /* Cache Data */
                UX      status: 2;             /* Status of the entry (i.e. line is a hit, empty, or must be evicted)*/
                UX      LRU_count;             /* Counter for Least Recently used policy */

		tagged_cache_entry() :
			 pcln(pcln_limit),
			 state(cache_state_invalid),
			 ppn(ppn_limit),
			 asid(asid_limit),
			 pdid(-1),
			 data(nullptr) {}

		tagged_cache_entry(UX pcln, UX asid, UX ppn) :
			pcln(pcln),
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
			ppn_bits =            PARAM::ppn_bits,

		};

                std::shared_ptr<memory_type> mem;
                UX write_policy;
		
                
		// TODO - map cache index and data into the machine address space with user_memory::add_segment

		cache_entry_t cache_key[num_entries * num_ways];
		u8 cache_data[cache_size];

		tagged_cache(std::shared_ptr<memory_type> _mem, UX _write_policy = cache_write_back) : mem(_mem), write_policy(_write_policy) 
		{
                    static_assert(page_shift == (cache_line_shift + num_entries_shift), "Page shift == cache_line_shift + num_entries_shift");
                    for (size_t i = 0; i < num_entries * num_ways; i++) {
			cache_key[i].data = cache_data + i * cache_line_size;
                        cache_key[i].status = cache_line_empty;
                        cache_key[i].LRU_count = 0;
			cache_key[i].state = cache_state_shared; //Using shared state to represent non-dirty data.  
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
                
                //Update all cache entries' LRU counters in the set, except for the most recently accessed item
                //The caller must set the most recently accessed LRU_counter to 0.
                void update_LRU_counters(UX index_for_set, cache_entry_t *entry_to_skip){
                    index_for_set <<= num_ways;
                    for(size_t i = 0; i < num_ways; i++)
                        if(&cache_key[index_for_set + i] != entry_to_skip)
                            cache_key[index_for_set + i].LRU_count++;
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
                //val will be written to the cache if op is 'S', otherwise the cache will load if the op is 'L'
                //The caller must access the TLB and translate the UX va to UX mpa and provide it to this function.
                u8 access_cache(UX mpa, u8 op, u8 val = 0){
                    cache_entry_t * ent = lookup_cache_line(mpa);
                    if(ent->status == cache_line_hit){
                        printf("We got a hit!\n");
                        ent->status = cache_line_filled;
                    }
                    else if(ent->status == cache_line_must_evict){
                        printf("We have a miss! We must evict a block.\n");
                        if(write_policy == cache_write_back){
                            //If the line is dirty, write its contents to mem
                            if(ent->state == cache_state_modified){
                                ld_str_into_mem(ent->pcln << cache_line_shift, 'S');
                                ent->state = cache_state_shared; 
                            }
                            //Set the LRU counter for the current line to be 0, and update all te other lines in the set.
                            ent->LRU_count = 0;
                            update_LRU_counters(mpa & num_entries_mask, ent);
                        }
                        //Load the block from memory into the cache.
                        ld_str_into_mem(mpa, 'L'); 
                        ent->pcln = mpa >> cache_line_shift;
                        ent->ppn = ent->pcln >> num_entries_shift;
                        ent->status = cache_line_filled;
                    }
                    else if(ent->status == cache_line_empty){
                        printf("We have a miss! We must fill an empty block\n");
                        ld_str_into_mem(mpa,'L');
                        ent->pcln = mpa >> cache_line_shift;
                        ent->ppn = ent->pcln >> num_entries_shift;
                        ent->status = cache_line_filled;
                    }
                    //If write through policy is used and mem access is a store,
                    //store the contents of the mpa directly to memory.
                    if(op == 'S' && write_policy == cache_write_through){
                        cache_data[mpa & data_index_mask] = val;
                        mem->store(mpa,val);
                    }
                    //If write back policy is used and mem access is a store,
                    //set the cache state to modified (aka dirty)
                    if(op == 'S' && write_policy == cache_write_back){
                        ent->state = cache_state_modified;
                        cache_data[mpa & data_index_mask] = val;
                    }

                    return cache_data[mpa & data_index_mask];
                }

		// cache line is virtually indexed but physically tagged.
		// Returns a cache line entry that could be a hit, line to evict, or empty line.
                // Also returns the code to define the entry, 0 == empty, 1 == hit, 2 == evict
                cache_entry_t* lookup_cache_line(UX mpa)
		{
                    cache_entry_t *empty_cache_line, *line_to_evict;
                    u8 found_empty_line = 0, maxLRU = 0;
		    UX pcln = mpa >> cache_line_shift;
		    UX entry = pcln & num_entries_mask;
                    UX ppn = pcln >> num_entries_shift;
                    for (size_t i = 0; i < num_ways; i++) {
                        cache_entry_t *ent = cache_key + ((entry << num_ways_shift) + i);
			if (ent->ppn == ppn) {
			    ent->status = cache_line_hit;
                            return ent;
			}
                        else if(!found_empty_line && ent->status == cache_line_empty){
                            found_empty_line = 1;
                            empty_cache_line = ent;
                        }
                        else if(ent->LRU_count >= maxLRU){
                            maxLRU = ent->LRU_count;
                            line_to_evict = ent;
                        }
		    }
                    
                    if(found_empty_line) 
                        return empty_cache_line; 
                    else { 
                        line_to_evict->status = cache_line_must_evict;
                        return line_to_evict;
                    }
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
