#ifndef CREATION_H
#define CREATION_H

#include "board.h"
#include "common.h"
#include "egtb.h"
#include "eval.h"
#include "eval_normal.h"
#include "eval_suicide.h"
#include "extensions.h"
#include "iterative_deepener.h"
#include "lmr.h"
#include "move_order.h"
#include "movegen.h"
#include "player.h"
#include "pn_search.h"
#include "search_algorithm.h"
#include "timer.h"
#include "transpos.h"

#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <vector>

struct BuildOptions {
  // Initial FEN to construct board with. If this is empty, use default
  // initialization for given variant.
  std::string init_fen;

  // Use this transposition table instead of building a new one. This is useful
  // during pondering where we need a new Player instance to ponder over a
  // different board position but reuse the existing transposition table. If
  // this variable is nullptr, a new transposition table is built.
  TranspositionTable* transpos = nullptr;

  bool build_book = true;

  int rand_moves = 0;
};

class PlayerBuilder {
public:
  PlayerBuilder() : external_transpos_(false) {}

  virtual ~PlayerBuilder() {
    if (external_transpos_) {
      transpos_.release();
    }
  }

  virtual void BuildBoard() = 0;
  virtual void BuildBoard(const std::string& fen) = 0;
  virtual void BuildMoveGenerator() = 0;
  virtual void BuildEGTB() = 0;
  virtual void BuildEvaluator() = 0;

  virtual void BuildTranspositionTable() {
    // ~16M entries.
    transpos_.reset(new TranspositionTable(1U << 23));
  }

  virtual void InjectExternalTranspositionTable(TranspositionTable* transpos) {
    transpos_.reset(transpos);
    external_transpos_ = true;
  }
  virtual void BuildTimer() { timer_.reset(new Timer); }
  virtual void BuildSearchAlgorithm() {
    assert(board_ != nullptr);
    assert(movegen_ != nullptr);
    assert(eval_ != nullptr);
    assert(timer_ != nullptr);
    assert(transpos_ != nullptr);
    assert(extensions_ != nullptr);
    search_algorithm_.reset(
        new SearchAlgorithm(board_.get(), movegen_.get(), eval_.get(),
                            timer_.get(), transpos_.get(), extensions_.get()));
  }
  virtual void BuildIterativeDeepener() {
    assert(board_ != nullptr);
    assert(movegen_ != nullptr);
    assert(search_algorithm_ != nullptr);
    assert(timer_ != nullptr);
    assert(transpos_ != nullptr);
    assert(extensions_ != nullptr);
    iterative_deepener_.reset(new IterativeDeepener(
        board_.get(), movegen_.get(), search_algorithm_.get(), timer_.get(),
        transpos_.get(), extensions_.get()));
  }

  // BuildExtensions only allocates memory for the extensions_ object. The
  // contents of the object will be initialized only after AddExtensions is
  // called. While building our object hierarchy, BuildExtensions will be
  // called before building any other object and AddExtensions will be called
  // after building all other objects. This is because some extensions may
  // require arbitrarily many other objects to have been built. The
  // side-effect of this is that none of the constructors which take extensions_
  // as an argument can refer to its contents.
  virtual void BuildExtensions() { extensions_.reset(new Extensions()); }
  virtual void AddExtensions() = 0;

  virtual void BuildBook() = 0;
  virtual void BuildPlayer(int rand_moves) = 0;

  virtual Player* GetPlayer() const { return player_.get(); }

  virtual MoveGenerator* GetMoveGenerator() const { return movegen_.get(); }

  virtual EGTB* GetEGTB() const { return egtb_.get(); }

  virtual Evaluator* GetEvaluator() const { return eval_.get(); }

  virtual TranspositionTable* GetTranspos() const { return transpos_.get(); }

  virtual Timer* GetTimer() const { return timer_.get(); }

protected:
  const std::map<std::string, std::string> config_map_;
  std::unique_ptr<Board> board_;
  std::unique_ptr<MoveGenerator> movegen_;
  std::unique_ptr<SearchAlgorithm> search_algorithm_;
  std::unique_ptr<IterativeDeepener> iterative_deepener_;
  std::unique_ptr<Evaluator> eval_;
  std::unique_ptr<TranspositionTable> transpos_;
  std::unique_ptr<Timer> timer_;
  std::unique_ptr<Book> book_;
  std::unique_ptr<Extensions> extensions_;
  std::unique_ptr<Player> player_;
  std::unique_ptr<EGTB> egtb_;
  bool external_transpos_;
};

class NormalPlayerBuilder : public PlayerBuilder {
public:
  void BuildBoard() override { board_.reset(new Board(Variant::NORMAL)); }

  void BuildBoard(const std::string& fen) override {
    board_.reset(new Board(Variant::NORMAL, fen));
  }

  void BuildMoveGenerator() override {
    assert(board_ != nullptr);
    movegen_.reset(new MoveGeneratorNormal(board_.get()));
  }

  void BuildEGTB() override {}

  void BuildEvaluator() override {
    assert(board_ != nullptr);
    assert(movegen_ != nullptr);
    eval_.reset(new EvalNormal(board_.get(), movegen_.get()));
  }

  void BuildBook() override {
    book_.reset(new Book(Variant::NORMAL, "nbook.txt"));
  }

  void AddExtensions() override {
    assert(extensions_ != nullptr);
    assert(board_ != nullptr);
    extensions_->move_orderer.reset(new CapturesFirstOrderer(board_.get()));
  }

  void BuildPlayer(int rand_moves) override {
    assert(board_ != nullptr);
    assert(movegen_ != nullptr);
    assert(iterative_deepener_ != nullptr);
    assert(timer_ != nullptr);
    // For Normal player, extensions_ could be NULL as of now.
    player_.reset(new Player(book_.get(), board_.get(), movegen_.get(),
                             iterative_deepener_.get(), timer_.get(),
                             egtb_.get(), extensions_.get(), rand_moves));
  }
};

class SuicidePlayerBuilder : public PlayerBuilder {
public:
  SuicidePlayerBuilder(const bool enable_pns = true)
      : enable_pns_(enable_pns) {}

  void BuildBoard() override { board_.reset(new Board(Variant::SUICIDE)); }

  void BuildBoard(const std::string& fen) override {
    board_.reset(new Board(Variant::SUICIDE, fen));
  }

  void BuildMoveGenerator() override {
    assert(board_ != nullptr);
    movegen_.reset(new MoveGeneratorSuicide(*board_.get()));
  }

  void BuildEGTB() override {
    assert(board_ != nullptr);
    std::vector<std::string> egtb_filenames;
    assert(GlobFiles("egtb/*.egtb", &egtb_filenames));
    egtb_.reset(new EGTB(egtb_filenames, *board_.get()));
    egtb_->Initialize();
  }

  void BuildEvaluator() override {
    assert(board_ != nullptr);
    assert(movegen_ != nullptr);
    eval_.reset(new EvalSuicide(board_.get(), movegen_.get(), egtb_.get()));
  }

  void BuildBook() override {
    book_.reset(new Book(Variant::SUICIDE, "sbook.txt"));
  }

  void AddExtensions() override {
    assert(board_ != nullptr);
    assert(movegen_ != nullptr);
    assert(eval_ != nullptr);
    assert(extensions_ != nullptr);
    extensions_->move_orderer.reset(
        new MobilityOrderer(board_.get(), movegen_.get()));
    extensions_->lmr.reset(new LMR(4 /* full depth moves */,
                                   2 /* reduction limit */,
                                   1 /* depth reduction factor */));
    if (enable_pns_) {
      extensions_->pns_extension.pns_timer.reset(new Timer);
      extensions_->pns_extension.pn_search.reset(new PNSearch(
          board_.get(), movegen_.get(), eval_.get(), egtb_.get(),
          transpos_.get(), extensions_->pns_extension.pns_timer.get()));
    }
  }

  void BuildPlayer(int rand_moves) override {
    assert(board_ != nullptr);
    assert(movegen_ != nullptr);
    assert(iterative_deepener_ != nullptr);
    assert(timer_ != nullptr);
    assert(extensions_ != nullptr);
    player_.reset(new Player(book_.get(), board_.get(), movegen_.get(),
                             iterative_deepener_.get(), timer_.get(),
                             egtb_.get(), extensions_.get(), rand_moves));
  }

private:
  const bool enable_pns_;
};

class PlayerBuilderDirector {
public:
  PlayerBuilderDirector(PlayerBuilder* player_builder)
      : player_builder_(player_builder) {}

  Player* Build(const BuildOptions& options) {
    if (options.transpos) {
      player_builder_->InjectExternalTranspositionTable(options.transpos);
    } else {
      player_builder_->BuildTranspositionTable();
    }
    if (options.build_book) {
      player_builder_->BuildBook();
    }
    if (options.init_fen.empty()) {
      player_builder_->BuildBoard();
    } else {
      player_builder_->BuildBoard(options.init_fen);
    }
    player_builder_->BuildExtensions(); // Must always be called first.
    player_builder_->BuildMoveGenerator();
    player_builder_->BuildEGTB();
    player_builder_->BuildEvaluator();
    player_builder_->BuildTimer();
    player_builder_->BuildSearchAlgorithm();
    player_builder_->BuildIterativeDeepener();
    player_builder_->BuildPlayer(options.rand_moves);
    player_builder_->AddExtensions(); // Must always be called last.
    return player_builder_->GetPlayer();
  }

private:
  PlayerBuilder* player_builder_;
};

#endif
