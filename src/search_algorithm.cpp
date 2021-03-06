#include "search_algorithm.h"
#include "board.h"
#include "common.h"
#include "eval.h"
#include "extensions.h"
#include "lmr.h"
#include "move_order.h"
#include "movegen.h"
#include "stats.h"
#include "timer.h"
#include "transpos.h"

int SearchAlgorithm::NegaScout(int max_depth, int alpha, int beta,
                               SearchStats* search_stats) {
  U64 zkey = board_->ZobristKey();
  TranspositionTableEntry* tentry = transpos_->Get(zkey);
  if (tentry != nullptr) {
    if (tentry->node_type == EXACT_NODE &&
        (tentry->score == WIN || tentry->score == -WIN)) {
      return tentry->score;
    }
    if (tentry->depth >= max_depth &&
        (tentry->node_type == EXACT_NODE ||
         (tentry->node_type == FAIL_HIGH_NODE && tentry->score >= beta) ||
         (tentry->node_type == FAIL_LOW_NODE && tentry->score <= alpha))) {
      return tentry->score;
    }
  }

  if (max_depth == 0 || (timer_ && timer_->Lapsed())) {
    ++search_stats->nodes_evaluated;
    int score = evaluator_->Evaluate();
    transpos_->Put(score, EXACT_NODE, 0, zkey, Move());
    return score;
  }

  MoveArray move_array;
  movegen_->GenerateMoves(&move_array);
  if (extensions_ && extensions_->move_orderer) {
    extensions_->move_orderer->Order(&move_array);
  }

  // We have essentially reached the end of the game, so evaluate.
  if (move_array.size() == 0) {
    ++search_stats->nodes_evaluated;
    return evaluator_->Evaluate();
  }
  // Bring up the transposition entry to the top (if available).
  if (tentry != nullptr && tentry->best_move.is_valid()) {
    move_array.PushToFront(tentry->best_move);
  }

  Move best_move;
  NodeType node_type = FAIL_LOW_NODE;
  int b = beta;
  for (size_t index = 0; index < move_array.size(); ++index) {
    ++search_stats->nodes_searched;
    const Move& move = move_array.get(index);
    board_->MakeMove(move);

    int value = -INF;

    // Apply late move reduction if applicable.
    bool lmr_triggered = false;
    if (extensions_ && extensions_->lmr &&
        extensions_->lmr->CanReduce(index, max_depth)) {
      value =
          -NegaScout(max_depth - (1 + extensions_->lmr->DepthReductionFactor()),
                     -alpha - 1, -alpha, search_stats);
      lmr_triggered = true;
    }

    // If LMR was not triggered or LMR search failed high, proceed with normal
    // search.
    if (!lmr_triggered || value > alpha) {
      value = -NegaScout(max_depth - 1, -b, -alpha, search_stats);
    }

    // Re-search with wider window if null window fails high.
    if (value >= b && value < beta && index > 0 && max_depth > 1) {
      ++search_stats->nodes_researched;
      value = -NegaScout(max_depth - 1, -beta, -alpha, search_stats);
    }

    board_->UnmakeLastMove();

    if (value > alpha) {
      best_move = move;
      node_type = EXACT_NODE;
      alpha = value;
    }

    if (alpha >= beta) {
      node_type = FAIL_HIGH_NODE;
      break;
    }

    b = alpha + 1;
  }

  if (!timer_ || !timer_->Lapsed()) {
    transpos_->Put(alpha, node_type, max_depth, zkey, best_move);
  }
  return alpha;
}
