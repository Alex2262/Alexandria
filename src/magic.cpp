#include "magic.h"
#include "attack.h"
#include "random.h"

// get bishop attacks
Bitboard GetBishopAttacks(const int square, Bitboard occupancy) {
    // get bishop attacks assuming current board occupancy
    occupancy &= bishop_masks[square];
    occupancy *= bishop_magic_numbers[square];
    occupancy >>= 64 - bishop_relevant_bits[square];

    // return bishop attacks
    return bishop_attacks[square][occupancy];
}

// get rook attacks
Bitboard GetRookAttacks(const int square, Bitboard occupancy) {
    // get rook attacks assuming current board occupancy
    occupancy &= rook_masks[square];
    occupancy *= rook_magic_numbers[square];
    occupancy >>= 64 - rook_relevant_bits[square];

    // return rook attacks
    return rook_attacks[square][occupancy];
}

// get queen attacks
Bitboard GetQueenAttacks(const int square, Bitboard occupancy) {
    return GetBishopAttacks(square, occupancy) | GetRookAttacks(square, occupancy);
}
