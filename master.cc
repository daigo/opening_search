#include "osl/move.h"
#include "osl/eval/pieceEval.h"
#include "osl/hash/hashKey.h"
#include "osl/misc/math.h"
#include "osl/record/csa.h"
#include "osl/record/csaRecord.h"
#include "osl/record/kanjiPrint.h"
#include "osl/record/record.h"
#include "osl/record/opening/openingBook.h"
#include "osl/search/fixedEval.h"
#include "osl/search/simpleHashTable.h"
#include "osl/state/numEffectState.h"
#include "osl/stl/vector.h"
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/progress.hpp>
#include <boost/shared_ptr.hpp>
#include <deque>
#include <iostream>
#include <vector>


namespace bp = boost::program_options;
bp::variables_map vm;

typedef std::vector<osl::record::opening::WMove> WMoveContainer;

osl::Player the_player = osl::BLACK;
bool is_dump = false;
int error_threshold = 500;
int is_determinate = 0;	   // test only top n moves.  0 for all
int max_depth, non_determinate_depth;
double ratio;		   // use moves[n+1] when the weight[n+1] >= ratio*weight[n]
bool is_quick = false;

size_t state_count = 0;

void showStatistics(const std::deque<int>& src)
{
  double sum, mean, var, dev, skew, kurt;
  osl::misc::computeStats(src.begin(), src.end(), sum, mean, var, dev, skew, kurt);

  std::cout << boost::format(" total: %g\n")  % src.size()
            << boost::format(" mean:  %g\n") % mean
            << boost::format(" dev:   %g\n")  % dev;
}

void printUsage(std::ostream& out, 
                char **argv,
                const boost::program_options::options_description& command_line_options)
{
  out <<
    "Usage: " << argv[0] << " [options] <a_joseki_file.dat>\n"
      << command_line_options 
      << std::endl;
}


void doMain(const std::string& file_name)
{
  osl::record::KanjiPrint printer(std::cerr, 
                                  boost::shared_ptr<osl::record::Characters>(
                                            new osl::record::KIFCharacters())
                                  );
  if (vm.count("verbose"))
    std::cout << boost::format("Opening... %s\n ") % file_name;
  osl::record::opening::WeightedBook book(file_name.c_str());

  if (vm.count("verbose"))
    std::cout << boost::format("Total states: %d\n") % book.getTotalState();
  bool states[book.getTotalState()]; // mark states that have been visited.
  memset(states, 0, sizeof(bool) * book.getTotalState());
  boost::progress_display progress(book.getTotalState());

  typedef std::pair<int, int> state_depth_t;
  osl::stl::vector<state_depth_t> stateToVisit;

  if (vm.count("verbose"))
    std::cout << boost::format("Start index: %d\n)") % book.getStartState();
  stateToVisit.push_back(state_depth_t(book.getStartState(), 1)); // root is 1
  // depth-1手目からdepth手目のstate。depth手目はまだ指されていない（これか
  // らdepth手目）

  typedef std::pair<int, int> eval_depth_t;
  std::deque<eval_depth_t> evals;
  long finishing_games = 0;

  while (!stateToVisit.empty()) {
    const state_depth_t state_depth = stateToVisit.back();
    if (vm.count("verbose"))
      std::cout << boost::format("Visiting... %d\n") % state_depth.first;
    const int stateIndex = state_depth.first;
    const int depth      = state_depth.second;
    stateToVisit.pop_back();
    states[stateIndex] = true;
    ++progress;

    /* この局面を処理する */

    WMoveContainer moves = book.getMoves(stateIndex);
    std::sort(moves.begin(), moves.end(), osl::record::opening::WMoveSort());

    if (vm.count("verbose"))
      std::cout << boost::format("  #moves... %d\n") % moves.size();
    
    // 自分（the_player）の手番では、良い手のみ指す
    // 相手はどんな手を指してくるか分からない
    // という前提で、不要なmovesを刈る
    if (!moves.empty() &&
         ((the_player == osl::BLACK && depth % 2 == 1) ||
          (the_player == osl::WHITE && depth % 2 == 0)) ) {
      int min = 1;
      if (is_determinate) {
	min = moves.at(0).getWeight();
	if (depth <= non_determinate_depth) {
	  for (int i=1; i<=std::min(is_determinate, (int)moves.size()-1); ++i) {
	    const int weight = moves.at(i).getWeight();
	    if ((double)weight < (double)moves.at(i-1).getWeight()*ratio)
	      break;
	    min = weight;
	  }
	}
      }
      // Do not play 0-weighted moves.
      if (min == 0) min = 1;

      WMoveContainer::iterator each = moves.begin();
      for (; each != moves.end(); ++each) {
        if (each->getWeight() < min)
          break;
      }
      moves.erase(each, moves.end());
    }

    /* leaf nodes */
    if (moves.empty() || depth > max_depth) {
      continue;
    }

    // recursively search the tree
    for (std::vector<osl::record::opening::WMove>::const_iterator each = moves.begin();
         each != moves.end(); ++each) {
      // consistancy check
      const osl::SimpleState state(book.getBoard(stateIndex));
      const osl::hash::HashKey hash(state);
      const int nextIndex = each->getStateIndex();
      const osl::SimpleState next_state(book.getBoard(nextIndex));
      const osl::hash::HashKey next_hash(next_state);
      const osl::hash::HashKey moved_hash = hash.newMakeMove(each->getMove());
      if (moved_hash != next_hash)
        throw std::string("Illegal move found.");

      if (!states[nextIndex])
	stateToVisit.push_back(state_depth_t(nextIndex, depth+1));
    } // each wmove
  } // while loop
}


int main(int argc, char **argv)
{
  std::string player_str;
  std::string file_name;
  size_t csa_move_index;
  std::string csa_file_name;

  bp::options_description command_line_options;
  command_line_options.add_options()
    ("player,p", bp::value<std::string>(&player_str)->default_value("black"),
     "specify a player, black or white, in whose point of view the book is validated. "
     "default black.")
    ("input-file,f", bp::value<std::string>(&file_name)->default_value("./joseki.dat"),
     "a joseki file to validate.")
    ("dump", bp::value<bool>(&is_dump)->default_value(false),
     "dump finishing games' states")
    ("threshold", bp::value<int>(&error_threshold)->default_value(500),
     "threshold of evaluatoin value to recognize a finishing game.")
    ("determinate", bp::value<int>(&is_determinate)->default_value(0),
     "only search the top n moves.  (0 for all,  1 for determinate).")
    ("non-determinate-depth", bp::value<int>(&non_determinate_depth)->default_value(100),
     "use the best move where the depth is greater than this value")
    ("max-depth", bp::value<int>(&max_depth)->default_value(100),
     "do not go beyond this depth from the root")
    ("ratio", bp::value<double>(&ratio)->default_value(0.0),
     "skip move[i] (i >= n), if weight[n] < weight[n-1]*ratio")
    ("csa-move", bp::value<size_t>(&csa_move_index)->default_value(1),
     "n-th-move state in the csa file")
    ("csa", bp::value<std::string>(&csa_file_name)->default_value(""),
     "a csa file name. See if a state in the game exists in the book or not.")
    ("quick", bp::value<bool>(&is_quick)->default_value(false),
     "skip quiescence search.")
    ("verbose,v", "output verbose messages.")
    ("help,h", "show this help message.");
  bp::positional_options_description p;
  p.add("input-file", 1);

  try
  {
    bp::store(
      bp::command_line_parser(
	argc, argv).options(command_line_options).positional(p).run(), vm);
    bp::notify(vm);
    if (vm.count("help"))
    {
      printUsage(std::cout, argv, command_line_options);
      return 0;
    }
  }
  catch (std::exception &e)
  {
    std::cerr << "error in parsing options\n"
	      << e.what() << std::endl;
    printUsage(std::cerr, argv, command_line_options);
    return 1;
  }

  if (player_str == "black")
    the_player = osl::BLACK;
  else if (player_str == "white")
    the_player = osl::WHITE;
  else
  {
    printUsage(std::cerr, argv, command_line_options);
    return 1;
  }

  if (!csa_file_name.empty())
  {
    is_quick = true;
    const osl::record::csa::CsaFile csa(csa_file_name);
    const osl::record::Record record = csa.getRecord();
    const osl::stl::vector<osl::Move> moves = record.getMoves();
    const osl::SimpleState initialState = record.getInitialState();

    if (csa_move_index < 1) csa_move_index = 1;
    if (csa_move_index > moves.size()) csa_move_index = moves.size();
    if ( (the_player == osl::BLACK && csa_move_index%2 == 0) ||
         (the_player == osl::WHITE && csa_move_index%2 == 1) )
    {
      std::cout << "Invalid csa move index: " << csa_move_index << std::endl;
      return -1;
    }
  }

  doMain(file_name);

  return 0;
}
// ;;; Local Variables:
// ;;; mode:c++
// ;;; c-basic-offset:2
// ;;; End:
