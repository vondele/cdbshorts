#include "cdbdirect.h"
#include "cdbshorts.h"
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <unordered_set>

#include "external/chess.hpp"

using namespace chess;

using PackedBoard = std::array<std::uint8_t, 24>;

namespace std {
template <> struct hash<PackedBoard> {
  size_t operator()(const PackedBoard pbfen) const {
    std::string_view sv(reinterpret_cast<const char *>(pbfen.data()),
                        pbfen.size());
    return std::hash<std::string_view>{}(sv);
  }
};
} // namespace std

int main(int argc, char **argv) {
  std::uintptr_t handle = cdbdirect_initialize(CHESSDB_PATH);

  std::uint64_t db_size = cdbdirect_size(handle);
  std::cout << "DB count: " << db_size << std::endl;

  std::string fen;
  fen = "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq -"; // 1. d4
  //
  if (argc == 2)
    fen = argv[1];

  std::cout << "Looking at fen: " << fen << std::endl;

  std::unordered_set<PackedBoard> positions_a;
  std::unordered_set<PackedBoard> positions_b;

  std::unordered_set<PackedBoard> *current_positions = &positions_a;
  std::unordered_set<PackedBoard> *next_positions = &positions_b;

  Board board(fen);
  PackedBoard packed = Board::Compact::encode(board);
  current_positions->insert(packed);

  size_t depth = 0;
  size_t missing_count = 0;
  std::string file_name = "shortpv_missing_fens.epd";
  std::cout << "Output missing fens to file: " << file_name << std::endl;
  std::ofstream missing_fens_file(file_name);

  // keep looping until no more positions or we reach a large number
  while (current_positions->size() > 0 and
         current_positions->size() < 1000000000 and missing_count < 100000) {
    next_positions->clear();

    for (auto &p : *current_positions) {
      board = Board::Compact::decode(p);
      auto r = cdbdirect_wrapper(handle, board);

      // if no moves for eligible position, output the fen as missing in cdb
      if (r.size() == 1 && r[0].second == -2) {
        missing_count++;
        missing_fens_file << board.getFen(false) << "\n";
      } else {
        for (int i = 0; i < r.size() - 1; i++) {
          if (r[i].second == r[0].second) {
            Move move = uci::uciToMove(board, r[i].first);
            board.makeMove(move);
            next_positions->insert(Board::Compact::encode(board));
            board.unmakeMove(move);
          } else {
            break;
          }
        }
      }
    }
    std::cout << "Cumulative missing fens at depth " << ++depth << " : "
              << missing_count
              << " number of fens on PV front: " << next_positions->size()
              << std::endl;
    std::swap(current_positions, next_positions);
  }

  missing_fens_file.close();
  std::cout << "Finished. Total missing fens: " << missing_count << std::endl;

  cdbdirect_finalize(handle);
  return 0;
}
