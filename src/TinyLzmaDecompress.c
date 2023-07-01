
#include "TinyLzmaDecompress.h"



// the code only use these basic types :
//    int      : as return code
//    uint8_t  : as compressed and uncompressed data, as LZMA state
//    uint16_t : as probabilities of range coder
//    uint32_t : as generic integers
//    size_t   : as data length



#define RET_IF_ERROR(expression)  {     \
    int res = expression;               \
    if (res != R_OK)                    \
        return res;                     \
}



/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// common useful functions
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static uint32_t bitsReverse (uint32_t bits, uint32_t bit_count) {
    uint32_t revbits = 0;
    for (; bit_count>0; bit_count--) {
        revbits <<= 1;
        revbits |= (bits & 1);
        bits >>= 1;
    }
    return revbits;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// Range Decoder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define   RANGE_CODE_NORMALIZE_THRESHOLD           (1 << 24)
#define   RANGE_CODE_MOVE_BITS                     5
#define   RANGE_CODE_N_BIT_MODEL_TOTAL_BITS        11
#define   RANGE_CODE_BIT_MODEL_TOTAL               (1 << RANGE_CODE_N_BIT_MODEL_TOTAL_BITS)
#define   RANGE_CODE_HALF_PROBABILITY              (RANGE_CODE_BIT_MODEL_TOTAL >> 1)


typedef struct {
    uint32_t       code;
    uint32_t       range;
    const uint8_t *p_src;
    const uint8_t *p_src_limit;
    uint8_t        overflow;
} RangeDecoder_t;


static void rangeDecodeNormalize (RangeDecoder_t *d) {
    if (d->range < RANGE_CODE_NORMALIZE_THRESHOLD) {
        if (d->p_src != d->p_src_limit) {
            d->range <<= 8;
            d->code  <<= 8;
            d->code  |= (uint32_t)(*(d->p_src));
            d->p_src ++;
        } else {
            d->overflow = 1;
        }
    }
}


static RangeDecoder_t newRangeDecoder (const uint8_t *p_src, size_t src_len) {
    RangeDecoder_t coder;
    coder.code        = 0;
    coder.range       = 0;
    coder.p_src       = p_src;
    coder.p_src_limit = p_src + src_len;
    coder.overflow    = 0;
    rangeDecodeNormalize(&coder);
    rangeDecodeNormalize(&coder);
    rangeDecodeNormalize(&coder);
    rangeDecodeNormalize(&coder);
    rangeDecodeNormalize(&coder);
    coder.range       = 0xFFFFFFFF;
    return coder;
}


static uint32_t rangeDecodeIntByFixedProb (RangeDecoder_t *d, uint32_t bit_count) {
    uint32_t val=0, b;
    for (; bit_count>0; bit_count--) {
        rangeDecodeNormalize(d);
        d->range >>= 1;
        d->code -= d->range;
        b = !(1 & (d->code >> 31));
        if (!b)
            d->code += d->range;
        val <<= 1;
        val  |= b;
    }
    return val;
}


static uint32_t rangeDecodeBit (RangeDecoder_t *d, uint16_t *p_prob) {
    uint32_t prob = *p_prob;
    uint32_t bound;
    rangeDecodeNormalize(d);
    bound = (d->range >> RANGE_CODE_N_BIT_MODEL_TOTAL_BITS) * prob;
    if (d->code < bound) {
        d->range = bound;
        *p_prob = (uint16_t)(prob + ((RANGE_CODE_BIT_MODEL_TOTAL - prob) >> RANGE_CODE_MOVE_BITS));
        return 0;
    } else {
        d->range -= bound;
        d->code  -= bound;
        *p_prob = (uint16_t)(prob - (prob >> RANGE_CODE_MOVE_BITS));
        return 1;
    }
}


static uint32_t rangeDecodeInt (RangeDecoder_t *d, uint16_t *p_prob, uint32_t bit_count) {
    uint32_t val = 1;
    uint32_t i;
    for (i=0; i<bit_count; i++) {
        if ( ! rangeDecodeBit(d, p_prob+val-1) ) {                // get bit 0
            val <<= 1;
        } else {                                                  // get bit 1
            val <<= 1;
            val  |= 1;
        }
    }
    return val & ((1<<bit_count)-1) ;
}


static uint32_t rangeDecodeByteMatched (RangeDecoder_t *d, uint16_t *p_prob, uint32_t match_byte) {
    uint32_t i, val = 1, off0 = 0x100, off1;                       // off0 and off1 can only be 0x000 or 0x100
    for (i=0; i<8; i++) {
        match_byte <<= 1;
        off1 = off0;
        off0 &= match_byte;
        if ( ! rangeDecodeBit(d, (p_prob+(off0+off1+val-1))) ) {  // get bit 0
            val <<= 1;
            off0 ^= off1;
        } else {                                                  // get bit 1
            val <<= 1;
            val  |= 1;
        }
    }
    return val & 0xFF;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LZMA Decoder
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define   N_STATES                                  12
#define   N_LIT_STATES                              7

#define   MAX_LC_SPEC                               8
#define   MAX_LC                                    4
#define   N_PREV_BYTE_LC_MSBS                       (1 << MAX_LC)

#define   MAX_LP_SPEC                               4
#define   MAX_LP                                    4
#define   N_LIT_POS_STATES                          (1 << MAX_LP)

#define   MAX_PB_SPEC                               4
#define   MAX_PB                                    4
#define   N_POS_STATES                              (1 << MAX_PB)                                                                 // all probabilities are init to 50% (half probability)


typedef enum {          // packet_type
    PKT_LIT,
    PKT_MATCH,
    PKT_SHORTREP,
    PKT_REP0,           // LONGREP0
    PKT_REP1,           // LONGREP1
    PKT_REP2,           // LONGREP2
    PKT_REP3            // LONGREP3
} PACKET_t;


static uint8_t stateTransition (uint8_t state, PACKET_t type) {
    switch (state) {
        case  0 : return (type==PKT_LIT) ?  0 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  1 : return (type==PKT_LIT) ?  0 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  2 : return (type==PKT_LIT) ?  0 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  3 : return (type==PKT_LIT) ?  0 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  4 : return (type==PKT_LIT) ?  1 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  5 : return (type==PKT_LIT) ?  2 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  6 : return (type==PKT_LIT) ?  3 : (type==PKT_MATCH) ?  7 : (type==PKT_SHORTREP) ?  9 :  8;
        case  7 : return (type==PKT_LIT) ?  4 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        case  8 : return (type==PKT_LIT) ?  5 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        case  9 : return (type==PKT_LIT) ?  6 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        case 10 : return (type==PKT_LIT) ?  4 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        case 11 : return (type==PKT_LIT) ?  5 : (type==PKT_MATCH) ? 10 : (type==PKT_SHORTREP) ? 11 : 11;
        default : return 0xFF;                                                                              // 0xFF is invalid state which will never appear
    }
}


#define   INIT_PROBS(probs)                         {               \
    size_t i;                                                       \
    size_t array_size = ( sizeof(probs) / sizeof(uint16_t) );       \
    uint16_t *array1d = (uint16_t*)(probs);                         \
    for (i=0; i<array_size; i++)                                    \
        array1d[i] = RANGE_CODE_HALF_PROBABILITY;                   \
}      


static int lzmaDecode (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len, uint8_t lc, uint8_t lp, uint8_t pb) {
    const uint32_t lc_shift = (8 - lc);
    const uint32_t lc_mask  = (1 << lc) - 1;
    const uint32_t lp_mask  = (1 << lp) - 1;
    const uint32_t pb_mask  = (1 << pb) - 1;
    
    uint8_t  state = 0;                           // valid value : 0~12
    size_t   pos   = 0;                           // position of uncompressed data (p_dst)
    uint32_t rep0  = 1;
    uint32_t rep1  = 1;
    uint32_t rep2  = 1;
    uint32_t rep3  = 1;
    
    RangeDecoder_t coder = newRangeDecoder(p_src, src_len);
    
    // probability arrays ---------------------------------------
    uint16_t probs_is_match     [N_STATES] [N_POS_STATES] ;
    uint16_t probs_is_rep       [N_STATES] ;
    uint16_t probs_is_rep0      [N_STATES] ;
    uint16_t probs_is_rep0_long [N_STATES] [N_POS_STATES] ;
    uint16_t probs_is_rep1      [N_STATES] ;
    uint16_t probs_is_rep2      [N_STATES] ;
    uint16_t probs_literal      [N_PREV_BYTE_LC_MSBS] [N_LIT_POS_STATES] [3*(1<<8)];
    uint16_t probs_dist_slot    [4]  [(1<<6)-1];
    uint16_t probs_dist_special [10] [(1<<5)-1];
    uint16_t probs_dist_align   [(1<<4)-1];
    uint16_t probs_len_choice   [2];
    uint16_t probs_len_choice2  [2];
    uint16_t probs_len_low      [2] [N_POS_STATES] [(1<<3)-1];
    uint16_t probs_len_mid      [2] [N_POS_STATES] [(1<<3)-1];
    uint16_t probs_len_high     [2] [(1<<8)-1];
    
    INIT_PROBS(probs_is_match);
    INIT_PROBS(probs_is_rep);
    INIT_PROBS(probs_is_rep0);
    INIT_PROBS(probs_is_rep0_long);
    INIT_PROBS(probs_is_rep1);
    INIT_PROBS(probs_is_rep2);
    INIT_PROBS(probs_literal);
    INIT_PROBS(probs_dist_slot);
    INIT_PROBS(probs_dist_special);
    INIT_PROBS(probs_dist_align);
    INIT_PROBS(probs_len_choice);
    INIT_PROBS(probs_len_choice2);
    INIT_PROBS(probs_len_low);
    INIT_PROBS(probs_len_mid);
    INIT_PROBS(probs_len_high);
    
    while (pos < *p_dst_len) {                                                                                // main loop
        const uint32_t literal_pos_state = lp_mask & (uint32_t)pos;
        const uint32_t pos_state         = pb_mask & (uint32_t)pos;
        
        if (coder.overflow)
            return R_ERR_INPUT_OVERFLOW;
        
        if ( !rangeDecodeBit(&coder, &probs_is_match[state][pos_state]) ) {                                   // decoded bit sequence = 0 (packet LIT)
            uint32_t prev_byte_lc_msbs=0, curr_byte;
            
            if (pos > 0)
                prev_byte_lc_msbs = lc_mask & (p_dst[pos-1] >> lc_shift);
            
            if (state < N_LIT_STATES) {
                curr_byte = rangeDecodeInt(&coder, probs_literal[prev_byte_lc_msbs][literal_pos_state], 8);
            } else {
                if (pos < (size_t)rep0)
                    return R_ERR_DATA;
                curr_byte = rangeDecodeByteMatched(&coder, probs_literal[prev_byte_lc_msbs][literal_pos_state], p_dst[pos-rep0]);
            }
            
            //PRINTF("[LZMAd]   @%08lx PKT_LIT   decoded_byte=%02x   state=%d\n", pos, curr_byte, state);
            
            if (pos == *p_dst_len)
                return R_ERR_OUTPUT_OVERFLOW;
            
            p_dst[pos] = (uint8_t)curr_byte;
            pos ++;
            
            state = stateTransition(state, PKT_LIT);
            
        } else {                                                                                             // decoded bit sequence = 1 (packet MATCH, SHORTREP, or LONGREP*)
            uint32_t dist=0, len=0;
            
            if ( !rangeDecodeBit(&coder, &probs_is_rep[state]) ) {                                           // decoded bit sequence = 10 (packet MATCH)
                rep3 = rep2;
                rep2 = rep1;
                rep1 = rep0;
                //PRINTF("[LZMAd]   @%08lx PKT_MATCH   state=%d\n", pos, state);
                state = stateTransition(state, PKT_MATCH);
            } else if ( !rangeDecodeBit(&coder, &probs_is_rep0[state]) ) {                                   // decoded bit sequence = 110 (packet SHORTREP or LONGREP0)
                dist = rep0;
                if ( !rangeDecodeBit(&coder, &probs_is_rep0_long[state][pos_state]) ) {                      // decoded bit sequence = 1100 (packet SHORTREP)
                    len = 1;
                    //PRINTF("[LZMAd]   @%08lx PKT_SHORTREP   state=%d\n", pos, state);
                    state = stateTransition(state, PKT_SHORTREP);
                } else {                                                                                     // decoded bit sequence = 1101 (packet LONGREP0)
                    //PRINTF("[LZMAd]   @%08lx PKT_REP0   state=%d\n", pos, state);
                    state = stateTransition(state, PKT_REP0);
                }
            } else if ( !rangeDecodeBit(&coder, &probs_is_rep1[state]) ) {                                   // decoded bit sequence = 1110 (packet LONGREP1)
                dist = rep1;
                rep1 = rep0;
                //PRINTF("[LZMAd]   @%08lx PKT_REP1   state=%d\n", pos, state);
                state = stateTransition(state, PKT_REP1);
            } else if ( !rangeDecodeBit(&coder, &probs_is_rep2[state]) ) {                                   // decoded bit sequence = 11110 (packet LONGREP2)
                dist = rep2;
                rep2 = rep1;
                rep1 = rep0;
                //PRINTF("[LZMAd]   @%08lx PKT_REP2   state=%d\n", pos, state);
                state = stateTransition(state, PKT_REP2);
            } else {                                                                                         // decoded bit sequence = 11111 (packet LONGREP3)
                dist = rep3;
                rep3 = rep2;
                rep2 = rep1;
                rep1 = rep0;
                //PRINTF("[LZMAd]   @%08lx PKT_REP3   state=%d\n", pos, state);
                state = stateTransition(state, PKT_REP3);
            }
            
            if (len == 0) {                                                                                  // unknown length, need to decode
                if      ( !rangeDecodeBit(&coder, &probs_len_choice [dist==0]) )
                    len =   2 + rangeDecodeInt(&coder, probs_len_low[dist==0][pos_state], 3);                // len = 2~9
                else if ( !rangeDecodeBit(&coder, &probs_len_choice2[dist==0]) )
                    len =  10 + rangeDecodeInt(&coder, probs_len_mid[dist==0][pos_state], 3);                // len = 10~17
                else
                    len =  18 + rangeDecodeInt(&coder,probs_len_high[dist==0], 8);                           // len = 18~273
                //PRINTF("[LZMAd]   len = %u\n", len);
            }
            
            if (dist == 0) {                                                                                 // unknown distance, need to decode
                const uint32_t len_min5_minus2 = (len>5) ? 3 : (len-2);
                uint32_t dist_slot;
                uint32_t mid_bcnt = 0;
                uint32_t low_bcnt = 0;
                uint32_t low_bits = 0;
                
                dist_slot = rangeDecodeInt(&coder, probs_dist_slot[len_min5_minus2], 6);                     // decode distance slot (0~63)
                
                if        (dist_slot >=14) {                                                                 // dist slot = 14~63
                    dist = (2 | (dist_slot & 1));                                                            // high 2 bits of dist
                    mid_bcnt = (dist_slot >> 1) - 5;
                    low_bcnt = 4;
                } else if (dist_slot >=4 ) {                                                                 // dist slot = 4~13
                    dist = (2 | (dist_slot & 1));                                                            // high 2 bits of dist
                    low_bcnt = (dist_slot >> 1) - 1;
                } else {                                                                                     // dist slot = 0~3
                    dist = dist_slot;
                }
                
                dist <<= mid_bcnt;
                dist  |= rangeDecodeIntByFixedProb(&coder, mid_bcnt);
                
                if      (dist_slot >=14)                                                                     // dist slot = 14~63
                    low_bits = rangeDecodeInt(&coder, probs_dist_align               , low_bcnt);
                else if (dist_slot >=4 )                                                                     // dist slot = 4~13
                    low_bits = rangeDecodeInt(&coder, probs_dist_special[dist_slot-4], low_bcnt);
                
                low_bits = bitsReverse(low_bits, low_bcnt);
                
                dist <<= low_bcnt;
                dist  |= low_bits;
                
                if (dist == 0xFFFFFFFF) {
                    //PRINTF("[LZMAd]   meet end marker\n");
                    break;
                }
                
                dist ++;
                
                //PRINTF("[LZMAd]   dist_slot = %u   dist = %u\n", dist_slot, dist);
            }
            
            rep0 = dist;
            
            if (dist > pos || dist == 0)
                return R_ERR_DATA;
            
            for (; len>0; len--) {
                if (pos == *p_dst_len)
                    return R_ERR_OUTPUT_OVERFLOW;
                p_dst[pos] = p_dst[pos-dist];
                pos ++;
            }
        }
    }
    
    *p_dst_len = pos;
    
    return R_OK;
}




/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// LZMA decompress (include parsing ".lzma" format's header)
/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define   LZMA_HEADER_LEN                           13
#define   LZMA_DIC_MIN                              (1 << 12)


static int parseLzmaHeader (const uint8_t *p_src, uint8_t *p_lc, uint8_t *p_lp, uint8_t *p_pb, uint32_t *p_dict_len, size_t *p_uncompressed_len, uint32_t *p_uncompressed_len_known) {
    uint8_t byte0 = p_src[0];
    
    *p_dict_len = ((uint32_t)p_src[1] ) | ((uint32_t)p_src[2] <<8) | ((uint32_t)p_src[3] <<16) | ((uint32_t)p_src[4] <<24) ;
    
    if (*p_dict_len < LZMA_DIC_MIN)
        *p_dict_len = LZMA_DIC_MIN;
    
    if (p_src[5] == 0xFF && p_src[6] == 0xFF && p_src[7] == 0xFF && p_src[8] == 0xFF && p_src[9] == 0xFF && p_src[10] == 0xFF && p_src[11] == 0xFF && p_src[12] == 0xFF) {
        *p_uncompressed_len_known = 0;
    } else {
        uint32_t i;
        *p_uncompressed_len_known = 1;
        *p_uncompressed_len = 0;
        for (i=0; i<8; i++) {
            if (i < sizeof(size_t)) {
                *p_uncompressed_len |= (((size_t)p_src[5+i]) << (i<<3));    // get (sizeof(size_t)) bytes from p_src, and put it to (*p_uncompressed_len)
            } else if (p_src[5+i] > 0) {
                return R_ERR_OUTPUT_OVERFLOW;                               // uncompressed length overflow from the machine's memory address limit
            }
        }
    }

    *p_lc = (uint8_t)(byte0 % 9);
    byte0 /= 9;
    *p_lp = (uint8_t)(byte0 % 5);
    *p_pb = (uint8_t)(byte0 / 5);
    
    if (*p_lc > MAX_LC_SPEC || *p_lp > MAX_LP_SPEC || *p_pb > MAX_PB_SPEC)
        return R_ERR_UNSUPPORTED;
    
    return R_OK;
}


int tinyLzmaDecompress (const uint8_t *p_src, size_t src_len, uint8_t *p_dst, size_t *p_dst_len) {
    uint8_t  lc, lp, pb;                                             // lc=0~8   lp=0~4   pb=0~4
    uint32_t dict_len, uncompressed_len_known;
    size_t   uncompressed_len = 0;
    
    if (src_len < LZMA_HEADER_LEN)
        return R_ERR_INPUT_OVERFLOW;
    
    RET_IF_ERROR( parseLzmaHeader(p_src, &lc, &lp, &pb, &dict_len, &uncompressed_len, &uncompressed_len_known) )
    
    //PRINTF("[LZMAd] lc=%d   lp=%d   pb=%d   dict_len=%u\n", lc, lp, pb, dict_len);
    
    if (lc > MAX_LC || lp > MAX_LP || pb > MAX_PB)
        return R_ERR_NOT_YET_SUPPORTED;
    
    if (uncompressed_len_known) {
        if (uncompressed_len > *p_dst_len)
            return R_ERR_OUTPUT_OVERFLOW;
        *p_dst_len = uncompressed_len;
        //PRINTF("[LZMAd] uncompressed length = %lu (parsed from header)\n"                                  , *p_dst_len);
    } else {
        //PRINTF("[LZMAd] uncompressed length is not in header, decoding using output buffer length = %lu\n" , *p_dst_len);
    }
    
    RET_IF_ERROR( lzmaDecode(p_src+LZMA_HEADER_LEN, src_len-LZMA_HEADER_LEN, p_dst, p_dst_len, lc, lp, pb) );
    
    if (uncompressed_len_known && uncompressed_len != *p_dst_len)
        return R_ERR_OUTPUT_LEN_MISMATCH;
    
    return R_OK;
}

