#include "makemove.h"
#include "ttable.h"
#include "board.h"
#include "hashkey.h"
#include "init.h"
#include "io.h"
#include "makemove.h"
#include "movegen.h"

// Remove a piece from a square
void ClearPiece(const int piece, const int from, S_Board* pos) {
    int color = Color[piece];
    HashKey(pos, PieceKeys[piece][from]);
    pop_bit(pos->bitboards[piece], from);
    pos->pieces[from] = EMPTY;
    pop_bit(pos->occupancies[BOTH], from);
    pop_bit(pos->occupancies[color], from);
}

// Add a piece to a square
void AddPiece(const int piece, const int to, S_Board* pos) {
    int color = Color[piece];
    set_bit(pos->bitboards[piece], to);
    set_bit(pos->occupancies[color], to);
    set_bit(pos->occupancies[BOTH], to);
    pos->pieces[to] = piece;
    HashKey(pos, PieceKeys[piece][to]);
}

// Remove a piece from a square while also deactivating the nnue weights tied to the piece
void ClearPieceNNUE(const int piece, const int sq, S_Board* pos) {
    nnue.clear(pos->accumulator, piece, sq);
    ClearPiece(piece, sq, pos);
}

// Add a piece to a square while also activating the nnue weights tied to the piece
void AddPieceNNUE(const int piece, const int to, S_Board* pos) {
    nnue.add(pos->accumulator, piece, to);
    AddPiece(piece, to, pos);
}

// Move a piece from square to to square from without updating the NNUE weights
void MovePiece(const int piece, const int from, const int to, S_Board* pos) {
    ClearPiece(piece, from, pos);
    AddPiece(piece, to, pos);
}

// Move a piece from square to to square from
void MovePieceNNUE(const int piece, const int from, const int to, S_Board* pos) {
    nnue.move(pos->accumulator, piece, from, to);
    MovePiece(piece, from, to, pos);
}

void UpdateCastlingPerms(S_Board* pos, int source_square, int target_square) {
    // Xor the old castling key from the zobrist key
    HashKey(pos, CastleKeys[pos->GetCastlingPerm()]);
    // update castling rights
    pos->castleperm &= castling_rights[source_square];
    pos->castleperm &= castling_rights[target_square];
    // Xor the new one
    HashKey(pos, CastleKeys[pos->GetCastlingPerm()]);
}

void inline HashKey(S_Board* pos, ZobristKey key) {
    pos->posKey ^= key;
}

// make move on chess board
void MakeMove(const int move, S_Board* pos) {
    // Store position variables for rollback purposes
    pos->history[pos->hisPly].fiftyMove = pos->fiftyMove;
    pos->history[pos->hisPly].enPas = pos->enPas;
    pos->history[pos->hisPly].castlePerm = pos->castleperm;
    pos->history[pos->hisPly].checkers = pos->checkers;

    // Store position key in the array of searched position
    pos->played_positions.emplace_back(pos->posKey);

    pos->accumulatorStack.emplace_back(pos->accumulator);
    // parse move
    const int source_square = From(move);
    const int target_square = To(move);
    const int piece = Piece(move);
    const int promoted_piece = GetPiece(getPromotedPiecetype(move),pos->side);
    // parse move flag
    const bool capture = IsCapture(move);
    const bool double_push = isDP(move);
    const bool enpass = isEnpassant(move);
    const bool castling = IsCastle(move);
    const bool promotion = isPromo(move);
    // increment fifty move rule counter
    pos->fiftyMove++;

    int NORTH = pos->side == WHITE ? 8 : -8;

    // handle enpassant captures
    if (enpass) {
        ClearPieceNNUE(GetPiece(PAWN, pos->side ^ 1), target_square + NORTH, pos);
        pos->fiftyMove = 0;
    }
    // handling capture moves
    else if (capture) {
        int piececap = pos->pieces[target_square];
        assert(piececap != EMPTY);
        ClearPieceNNUE(piececap, target_square, pos);

        pos->history[pos->hisPly].capture = piececap;
        // a capture was played so reset 50 move rule counter
        pos->fiftyMove = 0;
    }

    // if a pawn was moved reset the 50 move rule counter
    if (GetPieceType(piece) == PAWN)
        pos->fiftyMove = 0;

    // increment ply counters
    pos->hisPly++;
    // Remove the piece fom the square it moved from
    ClearPieceNNUE(piece, source_square, pos);
    // Set the piece to the destination square, if it was a promotion we directly set the promoted piece
    AddPieceNNUE(promotion ? promoted_piece : piece, target_square, pos);

    // Reset EP square
    if (GetEpSquare(pos) != no_sq)
        HashKey(pos, enpassant_keys[GetEpSquare(pos)]);

    // reset enpassant square
    pos->enPas = no_sq;

    // handle double pawn push
    if (double_push) {
        pos->enPas = target_square + NORTH;

        // hash enpassant
        HashKey(pos, enpassant_keys[GetEpSquare(pos)]);
    }

    // handle castling moves
    if (castling) {
        // switch target square
        switch (target_square) {
            // white castles king side
        case (g1):
            // move H rook
            MovePieceNNUE(WR, h1, f1, pos);
            break;

            // white castles queen side
        case (c1):
            // move A rook
            MovePieceNNUE(WR, a1, d1, pos);
            break;

            // black castles king side
        case (g8):
            // move H rook
            MovePieceNNUE(BR, h8, f8, pos);
            break;

            // black castles queen side
        case (c8):
            // move A rook
            MovePieceNNUE(BR, a8, d8, pos);
            break;
        }
    }

    UpdateCastlingPerms(pos, source_square, target_square);

    // change side
    pos->ChangeSide();
    // Xor the new side into the key
    HashKey(pos, SideKey);

    // Speculative prefetch of the TT entry
    TTPrefetch(pos->posKey);
    pos->checkers = IsInCheck(pos, pos->side);
}

// make move on chess board that we know won't be reverted (so we can skip storing history information), it also avoid updating nnue
void MakeMoveLight(const int move, S_Board* pos) {
    // Store position variables for rollback purposes
    pos->history[pos->hisPly].fiftyMove = pos->fiftyMove;
    pos->history[pos->hisPly].enPas = pos->enPas;
    pos->history[pos->hisPly].castlePerm = pos->castleperm;
    pos->history[pos->hisPly].checkers = pos->checkers;
    // Store position key in the array of searched position
    pos->played_positions.emplace_back(pos->posKey);

    // parse move
    int source_square = From(move);
    int target_square = To(move);
    int piece = Piece(move);
    int promoted_piece = GetPiece(getPromotedPiecetype(move), pos->side);
    // parse move flag
    bool capture = IsCapture(move);
    bool double_push = isDP(move);
    bool enpass = isEnpassant(move);
    bool castling = IsCastle(move);
    bool promotion = isPromo(move);
    // increment fifty move rule counter
    pos->fiftyMove++;

    int NORTH = pos->side == WHITE ? 8 : -8;

    // handle enpassant captures
    if (enpass) {
        ClearPiece(GetPiece(PAWN, pos->side ^ 1), target_square + NORTH, pos);

        pos->fiftyMove = 0;
    }

    // handling capture moves
    else if (capture) {
        int piececap = pos->pieces[target_square];

        ClearPiece(piececap, target_square, pos);

        // a capture was played so reset 50 move rule counter
        pos->fiftyMove = 0;
    }

    // if a pawn was moves reset the 50 move rule counter
    if (GetPieceType(piece) == PAWN)
        pos->fiftyMove = 0;

    // increment ply counters
    pos->hisPly++;
    // Remove the piece fom the square it moved from
    ClearPiece(piece, source_square, pos);
    // Set the piece to the destination square, if it was a promotion we directly set the promoted piece
    AddPiece(promotion ? promoted_piece : piece, target_square, pos);

    // Reset EP square
    if (GetEpSquare(pos) != no_sq)
        HashKey(pos, enpassant_keys[GetEpSquare(pos)]);
    // reset enpassant square
    pos->enPas = no_sq;

    // handle double pawn push
    if (double_push) {
        pos->enPas = target_square + NORTH;

        // hash enpassant
        HashKey(pos, enpassant_keys[GetEpSquare(pos)]);
    }

    // handle castling moves
    if (castling) {
        // switch target square
        switch (target_square) {
            // white castles king side
        case (g1):
            // move H rook
            MovePiece(WR, h1, f1, pos);
            break;

            // white castles queen side
        case (c1):
            // move A rook
            MovePiece(WR, a1, d1, pos);
            break;

            // black castles king side
        case (g8):
            // move H rook
            MovePiece(BR, h8, f8, pos);
            break;

            // black castles queen side
        case (c8):
            // move A rook
            MovePiece(BR, a8, d8, pos);
            break;
        }
    }

    UpdateCastlingPerms(pos, source_square, target_square);

    // change side
    pos->ChangeSide();

    // Xor the new side into the key
    HashKey(pos, SideKey);
}

void UnmakeMove(const int move, S_Board* pos) {
    // quiet moves

    pos->hisPly--;

    pos->enPas = pos->history[pos->hisPly].enPas;
    pos->fiftyMove = pos->history[pos->hisPly].fiftyMove;
    pos->castleperm = pos->history[pos->hisPly].castlePerm;
    pos->checkers = pos->history[pos->hisPly].checkers;

    // parse move
    const int source_square = From(move);
    const int target_square = To(move);
    const int piece = Piece(move);
    // parse move flag
    const bool capture = IsCapture(move);
    const bool enpass = isEnpassant(move);
    const bool castling = IsCastle(move);
    const bool promotion = isPromo(move);
  
    pos->accumulator = pos->accumulatorStack.back();
    pos->accumulatorStack.pop_back();

    // handle pawn promotions
    if (promotion) {
        int promoted_piece = GetPiece(getPromotedPiecetype(move),pos->side^1);
        ClearPiece(promoted_piece, target_square, pos);
    }

    // move piece
    MovePiece(piece, target_square, source_square, pos);

    int SOUTH = pos->side == WHITE ? -8 : 8;

    // handle enpassant captures
    if (enpass) {
        AddPiece(GetPiece(PAWN, pos->side), target_square + SOUTH, pos);
    }

    // handle castling moves
    if (castling) {
        // switch target square
        switch (target_square) {
            // white castles king side
        case (g1):
            // move H rook
            MovePiece(WR, f1, h1, pos);
            break;

            // white castles queen side
        case (c1):
            // move A rook
            MovePiece(WR, d1, a1, pos);
            break;

            // black castles king side
        case (g8):
            // move H rook
            MovePiece(BR, f8, h8, pos);
            break;

            // black castles queen side
        case (c8):
            // move A rook
            MovePiece(BR, d8, a8, pos);
            break;
        }
    }

    // handling capture moves
    if (capture && !enpass) {
        // Retrieve the captured piece we have to restore
        int piececap = pos->history[pos->hisPly].capture;
        AddPiece(piececap, target_square, pos);
    }

    // change side
    pos->ChangeSide();

    // restore zobrist key (done at the end to avoid overwriting the value while
    // moving pieces bacl to their place)
    pos->posKey = pos->played_positions.back();
    pos->played_positions.pop_back();
}

// MakeNullMove handles the playing of a null move (a move that doesn't move any piece)
void MakeNullMove(S_Board* pos) {
    pos->played_positions.emplace_back(pos->posKey);

    if (GetEpSquare(pos) != no_sq)
        HashKey(pos, enpassant_keys[GetEpSquare(pos)]);

    pos->history[pos->hisPly].fiftyMove = pos->fiftyMove;
    pos->history[pos->hisPly].enPas = pos->enPas;
    pos->history[pos->hisPly].castlePerm = pos->castleperm;
    pos->history[pos->hisPly].checkers = pos->checkers;

    pos->enPas = no_sq;

    pos->ChangeSide();
    pos->hisPly++;
    HashKey(pos, SideKey);
}

// Take back a null move
void TakeNullMove(S_Board* pos) {
    pos->hisPly--;

    pos->castleperm = pos->history[pos->hisPly].castlePerm;
    pos->fiftyMove = pos->history[pos->hisPly].fiftyMove;
    pos->enPas = pos->history[pos->hisPly].enPas;
    pos->checkers = pos->history[pos->hisPly].checkers;

    pos->ChangeSide();
    pos->posKey = pos->played_positions.back();
    pos->played_positions.pop_back();
}
