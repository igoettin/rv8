
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
	struct tagged_cache : MEMORY
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
                
                //Arrays to hold cache entries and data
		cache_entry_t cache_key[num_entries * num_ways];
		u8 cache_data[cache_size];

                //RAM size and base
                uintmax_t default_ram_base = 0x80000000ULL;
                uintmax_t default_ram_size = 0x40000000ULL;

                //Statistics
                u64 hit_count = 0, miss_count = 0, load_count = 0, store_count = 0, num_evicted_lines = 0;
                double hit_rate = ((double)(hit_count)/(double)(hit_count + miss_count));

                tagged_cache(std::shared_ptr<memory_type> _mem, UX _write_policy = cache_write_back) : mem(_mem), write_policy(_write_policy) {
                    static_assert(page_shift == (cache_line_shift + num_entries_shift), "Page shift == cache_line_shift + num_entries_shift");
                    for (size_t i = 0; i < num_entries * num_ways; i++) {
                        cache_key[i].status = cache_line_empty;
                        cache_key[i].LRU_count = 0;
                        cache_key[i].state = cache_state_shared; //Using shared state to represent non-dirty data.  
                    }
                }
                tagged_cache(UX _write_policy = cache_write_back) : write_policy(_write_policy){
                    mem = std::make_shared<MEMORY>();
                    static_assert(page_shift == (cache_line_shift + num_entries_shift), "Page shift == cache_line_shift + num_entries_shift");
                    for(size_t i = 0; i < num_entries * num_ways; i++) {
                        cache_key[i].status = cache_line_empty;
                        cache_key[i].LRU_count = 0;
                        cache_key[i].state = cache_state_shared;
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
                buserror_t allocate(UX mpa, u8 op, UX index_for_entry){
                    UX mpa_masked = mpa & cache_line_mask;
                    UX index_for_data, offset;
                    //Traverse the cache line forward. Load/Store the data corresponding to each address into memory.
                    for(index_for_data = index_for_entry << cache_line_shift, offset = 0  
                        ; offset <= cache_line_offset_mask
                        ; index_for_data++, offset++, mpa_masked++){
                        if(op == 'S'){ 
                            if(mem->store(mpa_masked,cache_data[index_for_data])) return -1;  
                        }
                        else{
                            u8 loaded_val;
                            if(mem->load(mpa_masked,loaded_val)) return -1;
                            //printf("loaded_val is %x\n");
                            cache_data[index_for_data] = loaded_val;
                        }
                    }
                    return 0;
                }
                
                template<typename T> 
                buserror_t load(UX mpa, T & val){
                    load_count++;
                  //  printf("Loading.. Hit count: %lld, Miss Count: %lld, Hit Rate %lf, Load_Count: %lld, Store_Count: %lld, Num Evicted: %lld\n",
                  //    hit_count,miss_count,hit_rate,load_count,store_count,num_evicted_lines); 
                    if((mpa < default_ram_base) || (mpa > (default_ram_base + default_ram_size))){
                        return mem->load(mpa, val);
                    }
                    else {
                        //printf("Loading from cache at mpa: %llx...\n",mpa);
                        return access_cache(mpa, 'L', val);
                        /*
                        if(sizeof(val) == 1){
                            u8 memVal;
                            mem->load(mpa, memVal);
                            if(memVal != val){
                                printf("MEM INCONSISTENT WITH CACHE\n");                            
                                printf("Size of val input is %llx\n",sizeof(val));
                            }
                            printf("Val loaded from cache was %llx for mpa: %llx\n",val,mpa);
                            printf("Actual value in memory is %llx\n",memVal);
                        }
                        else if(sizeof(val) == 2) {
                            u16 memVal;
                            mem->load(mpa, memVal);
                            if(memVal != val){
                                printf("MEM INCONSISTENT WITH CACHE\n");                            
                                printf("Size of val input is %llx\n",sizeof(val));
                            }
                            printf("Val loaded from cache was %llx for mpa: %llx\n",val,mpa);
                            printf("Actual value in memory is %llx\n",memVal);
                        }
                        else if(sizeof(val) == 4) {
                            u32 memVal;
                            mem->load(mpa, memVal);
                            if(memVal != val){
                                printf("MEM INCONSISTENT WITH CACHE\n");                            
                                printf("Size of val input is %llx\n",sizeof(val));
                            }
                            printf("Val loaded from cache was %llx for mpa: %llx\n",val,mpa);
                            printf("Actual value in memory is %llx\n",memVal);
                        
                        } else {
                            u64 memVal;
                            mem->load(mpa, memVal);
                            if(memVal != val){
                                printf("MEM INCONSISTENT WITH CACHE\n");                            
                                printf("Size of val input is %llx\n",sizeof(val));
                            }
                            printf("Val loaded from cache was %llx for mpa: %llx\n",val,mpa);
                            printf("Actual value in memory is %llx\n",memVal);
                        
                       } */
                        return 0;
                    }
                    
                }
                
                template<typename T>
                buserror_t store(UX mpa, T val){
                    store_count++;
                    if(mpa < default_ram_base || (mpa > (default_ram_base + default_ram_size))){
                        return mem->store(mpa,val);
                    }
                    else {
                        //printf("Storing val %llx from mpa %llx into cache",val,mpa);
                        return access_cache(mpa, 'S', val);
                        //printf("Done storing into cache\n");
                        //u64 memVal;
                        //mem->load(mpa,memVal);
                        //printf("After store, mpa %llx was updated with val %llx\n",mpa,memVal);
                    }
                    
                }
                
                /*
                *Loads a value from the cache_data array. Possible value types are u8, u16, u32, and u64.
                *
                *@param index_for_data is the index for the first byte in the cache_data array.
                *@param val will be updated with the value loaded from the cache_data array.
                *
                */
                template<typename T>
                buserror_t load_val(UX mpa, UX index_for_data, T & val){
                    //printf("index_for_data with added part: %llx index_for_data by itself %llx\n", index_for_data + (sizeof(val) - 1), index_for_data);
                    if(((index_for_data + (sizeof(val) - 1)) >> cache_line_shift) != (index_for_data >> cache_line_shift)){ 
                        //printf("IN!\n");
                        UX index_for_entry = (index_for_data >> cache_line_shift) + 1;
                        UX amount_to_add = (cache_line_offset_mask - (mpa & cache_line_offset_mask)) + 1;
                        //printf("amount to add is %llx\n",amount_to_add);
                        UX mpa_to_fill_bits = mpa + amount_to_add;
                        //printf("mpa to fill bits is %llx\n",mpa_to_fill_bits);
                        cache_entry_t * ent = &cache_key[index_for_entry];
                        if(ent->status != cache_line_empty) {
                            //If the line is dirty, write its contents to mem
                            if(write_policy == cache_write_back && ent->state == cache_state_modified){
                                if(allocate(ent->pcln << cache_line_shift, 'S', index_for_entry)) return -1;
                                ent->state = cache_state_shared; 
                            }
                            //Set the LRU counter for the current line to be 0, and update all the other lines in the set.
                            ent->LRU_count = 0;
                            update_LRU_counters(ent->pcln & num_entries_mask, ent);
                        }
                        //Load the block from memory into the cache.
                        if(allocate(mpa_to_fill_bits, 'L', index_for_entry)) return -1; 
                        ent->pcln = mpa_to_fill_bits >> cache_line_shift;
                        ent->ppn = ent->pcln >> num_entries_shift;
                        ent->status = cache_line_filled;
                        last_access = cache_line_must_evict;
                        num_evicted_lines++;
                    }
                    size_t shift_amt = 0;
                    u16 cast_val_16 = *reinterpret_cast<u16*>(&val);
                    u32 cast_val_32 = *reinterpret_cast<u32*>(&val);
                    u64 cast_val_64 = *reinterpret_cast<u64*>(&val);
                    cast_val_16 = 0; cast_val_32 = 0; cast_val_64 = 0;
                    //Take each byte in the cache_data array and shift it over byte(s) amount
                    //so that the full value is created.
                    if(sizeof(val) == 1)
                        val = cache_data[index_for_data];
                    else if(sizeof(val) == 2) {
                        val = cast_val_16;
                        for(size_t i = 0; i < sizeof(val); i++){
                            cast_val_16 += ((u16)cache_data[index_for_data + i] << shift_amt);
                            shift_amt += 8;
                            //printf("cast_val_16 is %llx\n",cast_val_16);
                        }
                        val = cast_val_16;
                        //printf("Val is %llx\n",val);
                        //printf("size of val is %llx\n",sizeof(val));
                    }
                    else if(sizeof(val) == 4){
                        for(size_t i = 0; i < sizeof(val); i++){
                            cast_val_32 += ((u32)cache_data[index_for_data + i] << shift_amt);
                            shift_amt += 8;
                        }
                        val = cast_val_32;
                    }
                    else{
                        for(size_t i = 0; i < sizeof(val); i++){
                            cast_val_64 += ((u64)cache_data[index_for_data + i] << shift_amt);
                            shift_amt += 8;
                        }
                        val = cast_val_64;
                    }
                    return 0;
                }
                
                /*
                *Stores a value into the cache_data array. Possible value types are u8, u16, u32, and u64.
                *
                *@param mpa is the machine physical address. This is needed in the event write-through policy is used.
                *@param index_for_data is the index for the first byte in the cache_data array.
                *@param val is the value that will be stored into the cache_data array. One byte at a time.
                *
                */
                template<typename T>
                buserror_t store_val(UX mpa, UX index_for_data, T val){
                    u8 cast_val_8 = *reinterpret_cast<u8*>(&val);
                    u16 cast_val_16 = *reinterpret_cast<u16*>(&val);
                    u32 cast_val_32 = *reinterpret_cast<u32*>(&val);
                    u64 cast_val_64 = *reinterpret_cast<u64*>(&val);
                    //Assign current_byte to val so that only a byte is retrieved,
                    //store that, then right shift val a byte amount to get the next byte. 
                    if(sizeof(val) == 1){
                            cache_data[index_for_data] = cast_val_8;
                            if(write_policy == cache_write_through){
                                //printf("In store_val, storing 8 bit val %llx to mpa %llx\n", cast_val_8, mpa);
                                if(mem->store(mpa++,cast_val_8)) return -1;
                                //u8 memVal;
                                //mem->load(mpa - 1, memVal);
                                //printf("In store_val, memory now has val %llx for mpa %llx\n",cast_val_8, mpa-1);

                            }
                    }
                    else if(sizeof(val) == 2)
                        for(size_t i = 0; i < sizeof(val); i++){
                            u8 current_byte = cast_val_16;
                            cache_data[index_for_data + i] = current_byte;
                            if(write_policy == cache_write_through){
                                //printf("In store_val, storing 8 bit val %llx to mpa %llx\n", current_byte, mpa);
                                if(mem->store(mpa++,current_byte)) return -1;
                                //u8 memVal;
                                //mem->load(mpa - 1, memVal);
                                //printf("In store_val, memory now has val %llx for mpa %llx\n",current_byte, mpa-1);
                            }
                            cast_val_16 >>= 8;
                        }
                    else if(sizeof(val) == 4)
                        for(size_t i = 0; i < sizeof(val); i++){
                            u8 current_byte = cast_val_32;
                            cache_data[index_for_data + i] = current_byte;
                            if(write_policy == cache_write_through){
                                //printf("In store_val, storing 8 bit val %llx to mpa %llx\n", current_byte, mpa);
                                if(mem->store(mpa++,current_byte)) return -1;
                                //u8 memVal;
                                //mem->load(mpa - 1, memVal);
                                //printf("In store_val, memory now has val %llx for mpa %llx\n",current_byte, mpa-1);
                            }
                            cast_val_32 >>= 8;
                        }
                    else{
                        for(size_t i = 0; i < sizeof(val); i++){
                            u8 current_byte = cast_val_64;
                            cache_data[index_for_data + i] = current_byte;
                            if(write_policy == cache_write_through){
                                //printf("In store_val, storing 8 bit val %llx to mpa %llx\n", current_byte, mpa);
                                if(mem->store(mpa++,current_byte)) return -1;
                                //u64 memVal;
                                //mem->load(mpa - 1, memVal);
                                //printf("In store_val, memory now has val %llx for mpa %llx\n",memVal, mpa-1);
                            }
                            cast_val_64 >>= 8;
                        }
                   }
                   return 0;
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
                template<typename T>
                buserror_t access_cache(UX mpa, u8 op, T & val){
                    //Lookup the mpa in the cache
                    std::pair<cache_entry_t *, UX> result = lookup_cache_line(mpa);
                    cache_entry_t * ent = result.first; UX index_for_entry = result.second;
                    UX index_for_data = ((index_for_entry << cache_line_shift) | (mpa & cache_line_offset_mask)); 
                    //Check for a hit
                    if(ent->status == cache_line_hit){
                        //Hit was found, set the status to filled. 
                        ent->status = cache_line_filled;
                        last_access = cache_line_hit;
                        hit_count++;
                    }

                    //No hit was found, evict a block
                    else if(ent->status == cache_line_must_evict){
                        //If the line is dirty, write its contents to mem
                        if(write_policy == cache_write_back && ent->state == cache_state_modified){
                            if(allocate(ent->pcln << cache_line_shift, 'S', index_for_entry)) return -1;
                            ent->state = cache_state_shared; 
                        }
                        //Set the LRU counter for the current line to be 0, and update all the other lines in the set.
                        ent->LRU_count = 0;
                        update_LRU_counters(ent->pcln & num_entries_mask, ent);
                        //Load the block from memory into the cache.
                        if(allocate(mpa, 'L', index_for_entry)) return -1; 
                        ent->pcln = mpa >> cache_line_shift;
                        ent->ppn = ent->pcln >> num_entries_shift;
                        ent->status = cache_line_filled;
                        last_access = cache_line_must_evict;
                        miss_count++;
                        num_evicted_lines++;
                    }
                    //No hit was found, but an empty line was found.
                    else if(ent->status == cache_line_empty){
                        if(allocate(mpa,'L', index_for_entry)) return -1;
                        ent->pcln = mpa >> cache_line_shift;
                        ent->ppn = ent->pcln >> num_entries_shift;
                        ent->status = cache_line_filled;
                        last_access = cache_line_empty;
                        miss_count++;
                    }
                    //If write through policy is used and mem access is a store,
                    //store the contents of the mpa directly to cache and main mem.
                    if(op == 'S' && write_policy == cache_write_through){
                        if(store_val(mpa,index_for_data,val)) return -1;
                        //u8 memVal;
                        //mem->load(mpa,memVal);
                        //printf("After accessing cache and storing, val is %llx for mpa %llx\n",memVal,mpa);
                    }
                    //If write back policy is used and mem access is a store,
                    //set the cache state to modified (aka dirty) and store to cache only
                    else if(op == 'S' && write_policy == cache_write_back){
                        ent->state = cache_state_modified;
                        if(store_val(mpa,index_for_data,val)) return -1;
                    }
                    //If loading, load from the cache_data array into val;
                    else{
                       /* if(sizeof(val) == 2){
                            u16 cast_val_16 = *reinterpret_cast<u16*>(&val);
                            load_val(mpa,index_for_data,cast_val_16);
                            val = cast_val_16;
                            val &= 65535;
                            printf("val at the end of it is %llx\n while cast_val-16 is %llx\n",val,cast_val_16);
                        }*/
                        if(load_val(mpa,index_for_data,val)) return -1;
                    }
                    return 0;
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
