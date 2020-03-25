/*
  This file is part of Leela Chess Zero.
  Copyright (C) 2018-2019 The LCZero Authors

  Leela Chess is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Leela Chess is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Leela Chess.  If not, see <http://www.gnu.org/licenses/>.

  Additional permission under GNU GPL version 3 section 7

  If you modify this Program, or any covered work, by linking or
  combining it with NVIDIA Corporation's libraries from the NVIDIA CUDA
  Toolkit and the NVIDIA CUDA Deep Neural Network library (or a
  modified version of those libraries), containing parts covered by the
  terms of the respective license agreement, the licensors of this
  Program grant you additional permission to convey the resulting work.
*/

#include "neural/encoder.h"

#include <algorithm>

namespace lczero {

namespace {
const int kMoveHistory = 8;
const int kPlanesPerBoard = 13;
const int kAuxPlaneBase = kPlanesPerBoard * kMoveHistory;
}  // namespace

InputPlanes EncodePositionForNN(
    const PositionHistory& history,
    int history_planes,
    FillEmptyHistory fill_empty_history
) {
  InputPlanes result(kAuxPlaneBase + 8);

  {
    const ChessBoard& board = history.Last().GetBoard();
    const bool we_are_black = board.flipped();

    // - Plane 104 for positions of rooks (both white and black) which have
    // a-side (queenside) castling right.
    // - Plane 105 for positions of rooks (both white and black) which have
    // h-side (kingside) castling right.
    const auto& castlings = board.castlings();
    result[kAuxPlaneBase + 0].mask =
        ((castlings.we_can_000() ? BoardSquare(ChessBoard::A1).as_board() : 0) |
          (castlings.they_can_000() ? BoardSquare(ChessBoard::A8).as_board() : 0))
        << castlings.queenside_rook();
    result[kAuxPlaneBase + 1].mask =
        ((castlings.we_can_00() ? BoardSquare(ChessBoard::A1).as_board() : 0) |
          (castlings.they_can_00() ? BoardSquare(ChessBoard::A8).as_board() : 0))
        << castlings.kingside_rook();

    if (we_are_black) result[kAuxPlaneBase + 4].SetAll();
    result[kAuxPlaneBase + 5].Fill(history.Last().GetNoCaptureNoPawnPly());
    // Plane kAuxPlaneBase + 6 used to be movecount plane, now it's all zeros.
    // Plane kAuxPlaneBase + 7 is all ones to help NN find board edges.
    result[kAuxPlaneBase + 7].SetAll();
  }

  bool flip = false;
  int history_idx = history.GetLength() - 1;
  for (int i = 0; i < std::min(history_planes, kMoveHistory);
       ++i, --history_idx) {
    const Position& position =
        history.GetPositionAt(history_idx < 0 ? 0 : history_idx);
    const ChessBoard& board =
        flip ? position.GetThemBoard() : position.GetBoard();
    if (history_idx < 0 && fill_empty_history == FillEmptyHistory::NO) break;
    // Board may be flipped so compare with position.GetBoard().
    if (history_idx < 0 && fill_empty_history == FillEmptyHistory::FEN_ONLY &&
        position.GetBoard() == ChessBoard::kStartposBoard) {
      break;
    }

    const int base = i * kPlanesPerBoard;
    result[base + 0].mask = (board.ours() & board.pawns()).as_int();
    result[base + 1].mask = (board.ours() & board.knights()).as_int();
    result[base + 2].mask = (board.ours() & board.bishops()).as_int();
    result[base + 3].mask = (board.ours() & board.rooks()).as_int();
    result[base + 4].mask = (board.ours() & board.queens()).as_int();
    result[base + 5].mask = (board.ours() & board.kings()).as_int();

    result[base + 6].mask = (board.theirs() & board.pawns()).as_int();
    result[base + 7].mask = (board.theirs() & board.knights()).as_int();
    result[base + 8].mask = (board.theirs() & board.bishops()).as_int();
    result[base + 9].mask = (board.theirs() & board.rooks()).as_int();
    result[base + 10].mask = (board.theirs() & board.queens()).as_int();
    result[base + 11].mask = (board.theirs() & board.kings()).as_int();

    const int repetitions = position.GetRepetitions();
    if (repetitions >= 1) result[base + 12].SetAll();

    // If en passant flag is set, undo last pawn move by removing the pawn from
    // the new square and putting into pre-move square.
    if (history_idx < 0 && !board.en_passant().empty()) {
      const auto idx = GetLowestBit(board.en_passant().as_int());
      if (idx < 8) {  // "Us" board
        result[base + 0].mask +=
            ((0x0000000000000100ULL - 0x0000000001000000ULL) << idx);
      } else {
        result[base + 6].mask +=
            ((0x0001000000000000ULL - 0x0000000100000000ULL) << (idx - 56));
      }
    }
    if (history_idx > 0) flip = !flip;
  }

  // {
  //   const ChessBoard& board = history.Last().GetBoard();
  //   const bool we_are_black = board.flipped();
  //   switch (input_format) {
  //     case pblczero::NetworkFormat::INPUT_CLASSICAL_112_PLANE: {
  //       // "Legacy" input planes with:
  //       // - Plane 104 (0-based) filled with 1 if white can castle queenside.
  //       // - Plane 105 filled with ones if white can castle kingside.
  //       // - Plane 106 filled with ones if black can castle queenside.
  //       // - Plane 107 filled with ones if white can castle kingside.
  //       if (board.castlings().we_can_000()) result[kAuxPlaneBase + 0].SetAll();
  //       if (board.castlings().we_can_00()) result[kAuxPlaneBase + 1].SetAll();
  //       if (board.castlings().they_can_000()) {
  //         result[kAuxPlaneBase + 2].SetAll();
  //       }
  //       if (board.castlings().they_can_00()) result[kAuxPlaneBase + 3].SetAll();
  //       break;
  //     }

  //     case pblczero::NetworkFormat::INPUT_112_WITH_CASTLING_PLANE: {
  //       // - Plane 104 for positions of rooks (both white and black) which have
  //       // a-side (queenside) castling right.
  //       // - Plane 105 for positions of rooks (both white and black) which have
  //       // h-side (kingside) castling right.
  //       const auto& cast = board.castlings();
  //       result[kAuxPlaneBase + 0].mask =
  //           ((cast.we_can_000() ? ChessBoard::A1 : 0) |
  //            (cast.they_can_000() ? ChessBoard::A8 : 0))
  //           << cast.queenside_rook();
  //       result[kAuxPlaneBase + 1].mask =
  //           ((cast.we_can_00() ? ChessBoard::A1 : 0) |
  //            (cast.they_can_00() ? ChessBoard::A8 : 0))
  //           << cast.kingside_rook();
  //       break;
  //     }

  //     default:
  //       throw Exception("Unsupported input plane encoding " +
  //                       std::to_string(input_format));
  //   };
  //   if (we_are_black) result[kAuxPlaneBase + 4].SetAll();
  //   result[kAuxPlaneBase + 5].Fill(history.Last().GetNoCaptureNoPawnPly());
  //   // Plane kAuxPlaneBase + 6 used to be movecount plane, now it's all zeros.
  //   // Plane kAuxPlaneBase + 7 is all ones to help NN find board edges.
  //   result[kAuxPlaneBase + 7].SetAll();
  // }

  // bool flip = false;
  // int history_idx = history.GetLength() - 1;
  // for (int i = 0; i < std::min(history_planes, kMoveHistory);
  //      ++i, --history_idx) {
  //   const Position& position =
  //       history.GetPositionAt(history_idx < 0 ? 0 : history_idx);
  //   const ChessBoard& board =
  //       flip ? position.GetThemBoard() : position.GetBoard();
  //   if (history_idx < 0 && fill_empty_history == FillEmptyHistory::NO) break;
  //   // Board may be flipped so compare with position.GetBoard().
  //   if (history_idx < 0 && fill_empty_history == FillEmptyHistory::FEN_ONLY &&
  //       position.GetBoard() == ChessBoard::kStartposBoard) {
  //     break;
  //   }

  //   const int base = i * kPlanesPerBoard;
  //   result[base + 0].mask = (board.ours() & board.pawns()).as_int();
  //   result[base + 1].mask = (board.ours() & board.knights()).as_int();
  //   result[base + 2].mask = (board.ours() & board.bishops()).as_int();
  //   result[base + 3].mask = (board.ours() & board.rooks()).as_int();
  //   result[base + 4].mask = (board.ours() & board.queens()).as_int();
  //   result[base + 5].mask = (board.ours() & board.kings()).as_int();

  //   result[base + 6].mask = (board.theirs() & board.pawns()).as_int();
  //   result[base + 7].mask = (board.theirs() & board.knights()).as_int();
  //   result[base + 8].mask = (board.theirs() & board.bishops()).as_int();
  //   result[base + 9].mask = (board.theirs() & board.rooks()).as_int();
  //   result[base + 10].mask = (board.theirs() & board.queens()).as_int();
  //   result[base + 11].mask = (board.theirs() & board.kings()).as_int();

  //   const int repetitions = position.GetRepetitions();
  //   if (repetitions >= 1) result[base + 12].SetAll();


  // InputPlanes result(kPlanesPerBoard);
  // const ChessBoard& board = history.Last().GetBoard();

  // Space game;
  // std::vector<std::shared_ptr<Object>> pieces(32);
  // int i = 0;
  // const int ranks[] = {0,1,7,6};
  // for (auto color : {1, -1}) {
  //   for (auto type : {
  //     ROOK,KNIGHT,BISHOP,QUEEN,KING,BISHOP,KNIGHT,ROOK,
  //     PAWN,PAWN,PAWN,PAWN,PAWN,PAWN,PAWN,PAWN,
  //   }) {
  //     const auto& piece = pieces[i] = std::make_shared<Object>();
  //     piece->properties.emplace(type, 1);
  //     piece->properties.emplace(COLOR, color);
  //     game.addObject(piece, std::make_pair(i%8, ranks[i/8]));
  //     ++i;
  //   }
  // }

  // const auto properties = {BOARD,KING,QUEEN,BISHOP,KNIGHT,ROOK,PAWN};
  // const auto masks = game.asMasks(properties);
  // std::cout << masks[BOARD][0][0];
  // std::cout << masks[BOARD][1][1];
  // std::cout << masks[PAWN][0][0];
  // std::cout << masks[PAWN][1][1];
  // std::cout << masks[ROOK][0][0];
  // std::cout << masks[ROOK][1][1];

  // i = 0;
  // for (const auto property : properties) {
  //   result[i] = masks[property];
  //   i++;
  // }

  // result[5].Fill(history.Last().GetNoCaptureNoPawnPly());
    // if (board.flipped()) result[4].mask = ~0;
    // const int repetitions = position.GetRepetitions();
    // if (repetitions >= 1) result[12].SetAll();

  // // int history_idx = history.GetLength() - 1;
  // // for (int i = 0; i < std::min(history_planes, kMoveHistory); ++i, --history_idx) {
  //   const Position& position = history.GetPositionAt(0);
  //   // const ChessBoard& board = false ? position.GetThemBoard() : position.GetBoard();
  //   // if (history_idx < 0 && fill_empty_history == FillEmptyHistory::NO) break;
  //   // Board may be flipped so compare with position.GetBoard().
  //   // if (history_idx < 0 && fill_empty_history == FillEmptyHistory::FEN_ONLY &&
  //       // position.GetBoard() == ChessBoard::kStartposBoard) {
  //     // break;
  //   // }

    // result[0] = ~0;
    // result[1] = board.ours().as_int();
    // result[2] = board.theirs().as_int();
    // result[3] = board.kings().as_int();
    // result[4] = board.queens().as_int();
    // result[5] = board.bishops().as_int();
    // result[6] = board.knights().as_int();
    // result[7] = board.rooks().as_int();
    // result[8] = board.pawns().as_int();
    // result[9] = board.en_passant().as_int();


    // If en passant flag is set, undo last pawn move by removing the pawn from
  //   // the new square and putting into pre-move square.
  //   if (history_idx < 0 && !board.en_passant().empty()) {
  //     const auto idx = GetLowestBit(board.en_passant().as_int());
  //     if (idx < 8) {  // "Us" board
  //       result[0] +=
  //           ((0x0000000000000100ULL - 0x0000000001000000ULL) << idx);
  //     } else {
  //       result[6] +=
  //           ((0x0001000000000000ULL - 0x0000000100000000ULL) << (idx - 56));
  //     }
  //   }
  //   if (history_idx > 0) flip = !flip;
  // // }

  // InputPlanes result(kPlanesPerBoard);
  // const ChessBoard& board = history.Last().GetBoard();

  // // Space game;
  // // std::vector<std::shared_ptr<Object>> pieces(32);
  // // int i = 0;
  // // const int ranks[] = {0,1,7,6};
  // // for (auto color : {1, -1}) {
  // //   for (auto type : {
  // //     ROOK,KNIGHT,BISHOP,QUEEN,KING,BISHOP,KNIGHT,ROOK,
  // //     PAWN,PAWN,PAWN,PAWN,PAWN,PAWN,PAWN,PAWN,
  // //   }) {
  // //     const auto& piece = pieces[i] = std::make_shared<Object>();
  // //     piece->properties.emplace(type, 1);
  // //     piece->properties.emplace(COLOR, color);
  // //     game.addObject(piece, std::make_pair(i%8, ranks[i/8]));
  // //     ++i;
  // //   }
  // // }

  // // const auto properties = {BOARD,KING,QUEEN,BISHOP,KNIGHT,ROOK,PAWN};
  // // const auto masks = game.asMasks(properties);
  // // std::cout << masks[BOARD][0][0];
  // // std::cout << masks[BOARD][1][1];
  // // std::cout << masks[PAWN][0][0];
  // // std::cout << masks[PAWN][1][1];
  // // std::cout << masks[ROOK][0][0];
  // // std::cout << masks[ROOK][1][1];

  // result[0] = ~0;
  // result[1] = board.ours().as_int();
  // result[2] = board.theirs().as_int();
  // result[3] = board.kings().as_int();
  // result[4] = board.queens().as_int();
  // result[5] = board.bishops().as_int();
  // result[6] = board.knights().as_int();
  // result[7] = board.rooks().as_int();
  // result[8] = board.pawns().as_int();
  // result[9] = board.en_passant().as_int();

  // i = 0;
  // for (const auto property : properties) {
  //   result[i] = masks[property];
  //   i++;
  // }

  // result[5].Fill(history.Last().GetNoCaptureNoPawnPly());
    // if (board.flipped()) result[4] = ~0;
    // const int repetitions = position.GetRepetitions();
    // if (repetitions >= 1) result[12] | ~0;

  // // int history_idx = history.GetLength() - 1;
  // // for (int i = 0; i < std::min(history_planes, kMoveHistory); ++i, --history_idx) {
  //   const Position& position = history.GetPositionAt(0);
  //   // const ChessBoard& board = false ? position.GetThemBoard() : position.GetBoard();
  //   // if (history_idx < 0 && fill_empty_history == FillEmptyHistory::NO) break;
  //   // Board may be flipped so compare with position.GetBoard().
  //   // if (history_idx < 0 && fill_empty_history == FillEmptyHistory::FEN_ONLY &&
  //       // position.GetBoard() == ChessBoard::kStartposBoard) {
  //     // break;
  //   // }

    // If en passant flag is set, undo last pawn move by removing the pawn from
  //   // the new square and putting into pre-move square.
  //   if (history_idx < 0 && !board.en_passant().empty()) {
  //     const auto idx = GetLowestBit(board.en_passant().as_int());
  //     if (idx < 8) {  // "Us" board
  //       result[0] +=
  //           ((0x0000000000000100ULL - 0x0000000001000000ULL) << idx);
  //     } else {
  //       result[6] +=
  //           ((0x0001000000000000ULL - 0x0000000100000000ULL) << (idx - 56));
  //     }
  //   }
  //   if (history_idx > 0) flip = !flip;
  // // }

  return result;
}

}  // namespace lczero
