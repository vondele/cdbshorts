#include "cdbdirect.h"
#include <string>
#include <utility>
#include <vector>

#include "external/chess.hpp"

// cdb does not store tb7, or (stale)mates
std::vector<std::pair<std::string, int>>
cdbdirect_wrapper(std::uintptr_t handle, const chess::Board &board) {
  if (board.occ().count() <= 7)
    return {{"a0a0", -3}};
  chess::Movelist movelist;
  chess::movegen::legalmoves(movelist, board);
  if (movelist.empty())
    return {{"a0a0", -3}};
  return cdbdirect_get(handle, board.getFen(false));
}
