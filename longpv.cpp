#include "cdbdirect.h"
#include "cdbshorts.h"
#include <cmath>
#include <cstdint>
#include <iostream>

#include "external/chess.hpp"
using namespace chess;

std::vector<Move> pv_explore(Board &board, std::vector<Move> &sequence,
                             const int margin, int budget, size_t &query_count,
                             const std::uintptr_t handle) {

  std::vector<Move> pv;

  if (budget < 0)
    return pv;

  if (board.occ().count() <= 7) // TB
  {
    std::cout << "Hit TB: ";
    for (auto &m : sequence)
      std::cout << uci::moveToUci(m, board.chess960()) << " ";
    std::cout << "\n" << std::endl;
    return pv;
  }

  auto res = board.isGameOver();
  if (res.second != GameResult::NONE)
    return pv;

  query_count++;
  auto r = cdbdirect_get(handle, board.getXfen(false));

  if (r.size() < 2)
    return pv;

  auto &best_move = r[0];

  for (auto &m : r) {

    if (m.first == "a0a0" || budget < 0)
      break;

    if (m.first != best_move.first && m.second + margin < best_move.second)
      break;

    Move move = cdbuci_to_move(board, m.first);
    board.makeMove<true>(move);
    sequence.push_back(move);
    std::vector<Move> sub_pv =
        pv_explore(board, sequence, margin, budget, query_count, handle);
    sequence.pop_back();

    if (sub_pv.size() + 1 > pv.size()) {
      pv.clear();
      pv.push_back(move);
      for (auto &sm : sub_pv)
        pv.push_back(sm);
    }
    board.unmakeMove(move);

    budget--;
  }

  return pv;
}

int main(int argc, char **argv) {
  std::string fen, chess960;
  fen = "rnbqkbnr/pppppppp/8/8/3P4/8/PPP1PPPP/RNBQKBNR b KQkq -"; // 1. d4

  if (argc == 2 || argc == 3) {
    fen = argv[1];
    if (argc == 3)
      chess960 = argv[2];
  } else if (argc != 1) {
    std::cout << "Usage: " << argv[0] << "FEN [true]" << std::endl;
    std::exit(1);
  }

  Board board(fen, chess960 == "true");
  std::cout << "Looking at fen: " << board.getXfen(false) << std::endl;

  std::uintptr_t handle = cdbdirect_initialize(CHESSDB_PATH);
  std::uint64_t db_size = cdbdirect_size(handle);
  std::cout << "DB count: " << db_size << std::endl;

  std::vector<Move> sequence;

  for (int budget = 1; budget < 14; budget += 2) {
    for (int margin = -1; margin <= 0; margin += 1) {
      size_t query_count = 0;
      auto pv =
          pv_explore(board, sequence, margin, budget, query_count, handle);
      std::cout << "cp margin from bestmove " << margin << " search budget "
                << budget << " found PV len: " << pv.size() << " using "
                << query_count << " queries. " << fen << " moves ";
      for (auto &m : pv)
        std::cout << uci::moveToUci(m, board.chess960()) << " ";
      std::cout << "\n" << std::endl;
    }
  }

  cdbdirect_finalize(handle);
  return 0;
}
