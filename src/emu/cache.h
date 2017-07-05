
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

		UX      pcln  : mpa_bits;      /* Physical Cache Line Number */
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
                
                //Variables for holding memory, write policy, etc.
                std::shared_ptr<memory_type> mem;
                UX write_policy;
		UX last_access;
                
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

                /*
                *Updates all cache entries' LRU counters in the set, except for the most recently accessed item
                *The caller must set the most recently accessed LRU_counter to 0.
                *
                *@param index_for_set is the index of the set for the cache_key (unshifted to account for associativity)
                *@param entry_to_skip is a pointer to the entry that was recently accessed, and does not need to have its LRU counter updated.
                *
                */
                void update_LRU_counters(UX index_for_set, cache_entry_t *entry_to_skip){
                    index_for_set <<= num_ways_shift;
                    for(size_t i = 0; i < num_ways; i++)
                        if(&cache_key[index_for_set + i] != entry_to_skip)
                            cache_key[index_for_set + i].LRU_count++;
                }

                /*
                *Go through the entire cache line and load/store every corresponding memory address into/from memory.
                *
                *@param UX mpa is the machine physical address for the line
                *@param signifies the operation. This must be 'S' (store) or 'L' (load).
                *@param index_for_entry is the index of the block within the cache_key array. 
                *index_for_entry is left shifted cache_line_shift times in this function to load/store each mem address's values from/to the cache_data array.
                */
                void load_or_store_into_mem(UX mpa, u8 op, UX index_for_entry){
                    UX mpa_masked = mpa & cache_line_mask;
                    UX index_for_data, offset;
                    //Traverse the cache line forward. Load/Store the data corresponding to each address into memory.
                    for(index_for_data = index_for_entry << cache_line_shift, offset = 0  
                        ; offset <= cache_line_offset_mask
                        ; index_for_data++, offset++, mpa_masked++)
                        op == 'S' ? mem->store(mpa_masked,cache_data[index_for_data]) : mem->load(mpa_masked,cache_data[index_for_data]);
                }
                
                /*
                *Given an mpa, access the cache to find the corresponding cache_entry, if it exists. 
                *If it's not there, main memory will be accessed to supply it.
                *
                *@param mpa is the machine physical address
                *@param op is the operation that will be performed. op can only be 'S' (store) or 'L' (load).
                *@param val is the value that will be stored into memory at the specified address. This variable is ignored on a load.
                *@precondition The caller must access the TLB and translate the UX va to UX mpa and provide it to this function.
                *@return the value that was stored/loaded into the cache is returned from where it is stored in the cache_data array.
                *
                */
                u8 access_cache(UX mpa, u8 op, u8 val = 0){
                    //Lookup the mpa in the cache
                    std::pair<cache_entry_t *, UX> result = lookup_cache_line(mpa);
                    cache_entry_t * ent = result.first; UX index_for_entry = result.second;
                    UX index_for_data = ((index_for_entry << cache_line_shift) | (mpa & cache_line_offset_mask)); 
                    //Check for a hit
                    if(ent->status == cache_line_hit){
                        //Hit was found, set the status to filled. 
                        ent->status = cache_line_filled;
                        last_access = cache_line_hit;
                    }

                    //No hit was found, evict a block
                    else if(ent->status == cache_line_must_evict){
                        //If the line is dirty, write its contents to mem
                        if(write_policy == cache_write_back && ent->state == cache_state_modified){
                            load_or_store_into_mem(ent->pcln << cache_line_shift, 'S', index_for_entry);
                            ent->state = cache_state_shared; 
                        }
                        //Set the LRU counter for the current line to be 0, and update all the other lines in the set.
                        ent ->LRU_count = 0;
                        update_LRU_counters(ent->pcln & num_entries_mask, ent);
                        //Load the block from memory into the cache.
                        load_or_store_into_mem(mpa, 'L', index_for_entry); 
                        ent->pcln = mpa >> cache_line_shift;
                        ent->ppn = ent->pcln >> num_entries_shift;
                        ent->status = cache_line_filled;
                        last_access = cache_line_must_evict;
                    }
                    //No hit was found, but an empty line was found.
                    else if(ent->status == cache_line_empty){
                        load_or_store_into_mem(mpa,'L', index_for_entry);
                        ent->pcln = mpa >> cache_line_shift;
                        ent->ppn = ent->pcln >> num_entries_shift;
                        ent->status = cache_line_filled;
                        last_access = cache_line_empty;
                    }
                    //If write through policy is used and mem access is a store,
                    //store the contents of the mpa directly to memory.
                    if(op == 'S' && write_policy == cache_write_through){
                        cache_data[index_for_data] = val;
                        mem->store(mpa,val);
                    }
                    //If write back policy is used and mem access is a store,
                    //set the cache state to modified (aka dirty)
                    if(op == 'S' && write_policy == cache_write_back){
                        ent->state = cache_state_modified;
                        cache_data[index_for_data] = val;
                    }
                    return cache_data[index_for_data];
                }

		/*
                * Performs a lookup in the cache to determine if there is a cache line corresponding to the provided mpa.
                *
                * @param mpa is the machine physical address that is being looked up.
		* @return Returns a cache line entry that is marked as a hit, line to evict, or empty line.
                * @return also returns the index in cache_key where that entry is found.
                *
                */
                std::pair<cache_entry_t*,UX> lookup_cache_line(UX mpa)
		{
                    //Variables to record entry, position, and misc data as we search the set
                    cache_entry_t *empty_cache_line = nullptr, *line_to_evict = nullptr;
                    UX emptyLineIndex = 0, maxLRUIndex = 0;
                    u8 found_empty_line = 0, maxLRU = 0;

                    //Variables to hold the entry and ppn
                    UX pcln = mpa >> cache_line_shift;
		    UX entry = pcln & num_entries_mask;
                    UX ppn = pcln >> num_entries_shift;
                    
                    for (size_t i = 0; i < num_ways; i++) {
                        //Index to lookup in cache_key
                        UX index = ((entry << num_ways_shift) + i);
                        //cache entry at index location
                        cache_entry_t *ent = cache_key + index;
                        //Check if hit
                        if (ent->ppn == ppn) {
                            ent->status = cache_line_hit;
                            return std::make_pair(ent,index);
			}
                        //Record an empty block if there is one available. This may be used if we cannot find a hit.
                        else if(!found_empty_line && ent->status == cache_line_empty){
                            found_empty_line = 1;
                            empty_cache_line = ent;
                            emptyLineIndex = index;
                        }
                        //Record this block as a potential block to evict if it has a higher/equal LRU count. 
                        else if(ent->LRU_count >= maxLRU){
                            maxLRU = ent->LRU_count;
                            line_to_evict = ent;
                            maxLRUIndex = index;
                        }
		    }
                    //Return the empty block if that's all that could be found
                    if(found_empty_line) 
                        return std::make_pair(empty_cache_line,emptyLineIndex); 
                    //Return the block we're going to evict if no hit or empty block was found.
                    else { 
                        line_to_evict->status = cache_line_must_evict;
                        return std::make_pair(line_to_evict,maxLRUIndex);
                    }
		}

	};

	template <const size_t cache_size, const size_t cache_ways, const size_t cache_line_size>
	using tagged_cache_rv32 = tagged_cache<param_rv32,cache_size,cache_ways,cache_line_size>;

	template <const size_t cache_size, const size_t cache_ways, const size_t cache_line_size>
	using tagged_cache_rv64 = tagged_cache<param_rv64,cache_size,cache_ways,cache_line_size>;
        
}

#endif
