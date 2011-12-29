#include "redis.h"
#include "searchResult.h"
#include "osl/move.h"
#include "osl/eval/pieceEval.h"
#include "osl/hash/hashKey.h"
#include "osl/misc/math.h"
#include "osl/record/compactBoard.h"
#include "osl/record/csa.h"
#include "osl/record/csaRecord.h"
#include "osl/record/kanjiPrint.h"
#include "osl/record/record.h"
#include "osl/record/opening/openingBook.h"
#include "osl/search/fixedEval.h"
#include "osl/search/simpleHashTable.h"
#include "osl/state/numEffectState.h"
#include "osl/stl/vector.h"
#include <hiredis/hiredis.h>
#include <glog/logging.h>
#include <boost/foreach.hpp>
#include <boost/format.hpp>
#include <boost/program_options.hpp>
#include <boost/shared_ptr.hpp>
#include <deque>
#include <iostream>
#include <sstream>
#include <vector>


namespace bp = boost::program_options;
bp::variables_map vm;

redisContext *c = NULL;

typedef std::vector<osl::record::opening::WMove> WMoveContainer;

osl::Player the_player = osl::BLACK;
int is_determinate = 0;	   // test only top n moves.  0 for all
int max_depth, non_determinate_depth;
double ratio;		   // use moves[n+1] when the weight[n+1] >= ratio*weight[n]


struct Node
{
  int state_index;
  std::string state_key;
  osl::MoveVector moves;

  Node(int _state_index,
       const std::string& _state_key)
    : state_index(_state_index),
      state_key(_state_key)
  {}

  Node(int _state_index,
       const std::string& _state_key,
       const osl::MoveVector& _moves)
    : state_index(_state_index),
      state_key(_state_key),
      moves(_moves)
  {}

  // depth-1手目からdepth手目のstate。depth手目はまだ指されていない（これか
  // らdepth手目）
  int getDepth() const {
    return moves.size() + 1;
  }
};

const Node nextNode(const Node current_node,
                    int next_state_index,
                    const std::string& next_state_str,
                    const osl::Move move) {
  osl::MoveVector moves = current_node.moves;
  moves.push_back(move);
  return Node(next_state_index, next_state_str, moves);
}


const std::string getStateKey(const osl::SimpleState& state) {
  const osl::record::CompactBoard cb(state);
  return compactBoardToString(cb);
}

const std::string getMovesStr(const osl::MoveVector& moves) {
  std::ostringstream ss;
  BOOST_FOREACH(const osl::Move move, moves) {
    osl::record::writeInt(ss, move.intValue());
  }
  return ss.str();
}


void setupServer(osl::Player player) {
  std::string key;
  if (player == osl::BLACK) {
    key = "DEL tag:black-positions";
  } else {
    key = "DEL tag:white-positions";
  }
  assert(!key.empty());
  redisReplyPtr reply((redisReply*)redisCommand(c, key.c_str()),
                      freeRedisReply);
  if (checkRedisReply(reply))
    exit(1);
}


bool isFinished(const std::string& state_key) {
  redisReplyPtr reply((redisReply*)redisCommand(c, "EXISTS %b",
                                                state_key.c_str(), state_key.size()),
                      freeRedisReply);
  if (checkRedisReply(reply))
    exit(1);

  const bool ret = (reply->integer == 1L);
  return ret;
}


int appendPosition(osl::Player player, const Node& node) {
  const std::string state_key = node.state_key;
  const std::string moves_str = getMovesStr(node.moves);

  redisAppendCommand(c, "SADD %s %b", "tag:new-queue", state_key.c_str(), state_key.size());
  
  if (player == osl::BLACK) {
    redisAppendCommand(c, "SADD %s %b", "tag:black-positions", state_key.c_str(), state_key.size());
  } else {
    redisAppendCommand(c, "SADD %s %b", "tag:white-positions", state_key.c_str(), state_key.size());
  }

  redisAppendCommand(c, "HMSET %b moves %b",
                     state_key.c_str(), state_key.size(),
                     moves_str.c_str(), moves_str.size());

  return 3; // two commands
}


void printUsage(std::ostream& out, 
                char **argv,
                const boost::program_options::options_description& command_line_options) {
  out <<
    "Usage: " << argv[0] << " [options] <a_joseki_file.dat>\n"
      << command_line_options 
      << std::endl;
}


void doMain(const std::string& file_name) {
  LOG(INFO) << boost::format("Opening... %s") % file_name;
  osl::record::opening::WeightedBook book(file_name.c_str());

  LOG(INFO) << boost::format("Total states: %d") % book.getTotalState();
  bool states[book.getTotalState()]; // mark states that have been visited.
  memset(states, 0, sizeof(states));

  setupServer(the_player);

  std::vector<Node> stateToVisit;

  LOG(INFO) << boost::format("Start index: %d") % book.getStartState();
  const Node root_node(book.getStartState(),
                       getStateKey(book.getBoard(book.getStartState())));
  stateToVisit.push_back(root_node);

  typedef std::pair<int, int> eval_depth_t;
  std::deque<eval_depth_t> evals;

  /* Append positions to the server in the pipelined mode */
  int counter = 0;

  while (!stateToVisit.empty()) {
    const Node node = stateToVisit.back();
    LOG(INFO) << boost::format("Visiting... %d") % node.state_index;
    stateToVisit.pop_back();
    states[node.state_index] = true;

    /* この局面を処理する */
    const osl::SimpleState state(book.getBoard(node.state_index));
    if (state.turn() == osl::alt(the_player)) {
      // 黒の定跡を評価したい -> 黒の手が指されたあとの局面
      //                      -> 白手番の局面をサーバに登録する
      counter += appendPosition(the_player, node);
    }

    WMoveContainer moves = book.getMoves(node.state_index);
    std::sort(moves.begin(), moves.end(), osl::record::opening::WMoveSort());

    /*
     * 自分（the_player）の手番では、有望な手(weight>0)のみ抽出する
     * 相手はどんな手を指すか分からないので、特にfilterせずに、そのまま。
     */
    if (!moves.empty() && state.turn() == the_player) {
      int min = 1;
      if (is_determinate) {
	min = moves.at(0).getWeight();
	if (node.getDepth() <= non_determinate_depth) {
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
    LOG(INFO) << boost::format("  #moves... %d\n") % moves.size();
    
    /* leaf nodes */
    if (moves.empty() || node.getDepth() > max_depth) {
      continue;
    }

    // recursively search the tree
    for (std::vector<osl::record::opening::WMove>::const_iterator each = moves.begin();
         each != moves.end(); ++each) {
      // consistancy check
      const osl::hash::HashKey hash(state);
      const int nextIndex = each->getStateIndex();
      const osl::SimpleState next_state(book.getBoard(nextIndex));
      const osl::hash::HashKey next_hash(next_state);
      const osl::hash::HashKey moved_hash = hash.newMakeMove(each->getMove());
      if (moved_hash != next_hash)
        throw std::string("Illegal move found.");

      if (!states[nextIndex]) {
	stateToVisit.push_back(nextNode(node,
                                        nextIndex,
                                        getStateKey(next_state),
                                        each->getMove()));
      }
    } // each wmove
  } // while loop

  /* check results */
  LOG(INFO) << "Checkling processed positions...: " << counter/3;
  for (int i=0; i<counter; ++i) {
    void *r;
    redisGetReply(c, &r);
    redisReplyPtr reply((redisReply*)r, freeRedisReply);
    if (i % 3 != 2) {
      assert(reply->type == REDIS_REPLY_INTEGER);
      assert(0 <= reply->integer);
      assert(reply->integer < 2);
    } else {
      checkRedisReply(reply);
    }
  }
}


int main(int argc, char **argv)
{
  std::string player_str;
  std::string file_name;
  std::string redis_server_host = "127.0.0.1";
  int redis_server_port = 6379;
  std::string redis_password;

  /* Set up logging */
  FLAGS_log_dir = ".";
  google::InitGoogleLogging(argv[0]);

  bp::options_description command_line_options;
  command_line_options.add_options()
    ("player,p", bp::value<std::string>(&player_str)->default_value("black"),
     "specify a player, black or white, in whose point of view the book is validated. "
     "default black.")
    ("input-file,f", bp::value<std::string>(&file_name)->default_value("./joseki.dat"),
     "a joseki file to validate.")
    ("determinate", bp::value<int>(&is_determinate)->default_value(0),
     "only search the top n moves.  (0 for all,  1 for determinate).")
    ("non-determinate-depth", bp::value<int>(&non_determinate_depth)->default_value(100),
     "use the best move where the depth is greater than this value")
    ("max-depth", bp::value<int>(&max_depth)->default_value(100),
     "do not go beyond this depth from the root")
    ("redis-host", bp::value<std::string>(&redis_server_host)->default_value(redis_server_host),
     "IP of the redis server")
    ("redis-password", bp::value<std::string>(&redis_password)->default_value(redis_password),
     "password to connect to the redis server")
    ("redis-port", bp::value<int>(&redis_server_port)->default_value(redis_server_port),
     "port number of the redis server")
    ("ratio", bp::value<double>(&ratio)->default_value(0.0),
     "skip move[i] (i >= n), if weight[n] < weight[n-1]*ratio")
    ("verbose,v", "output verbose messages.")
    ("help,h", "show this help message.");
  bp::positional_options_description p;
  p.add("input-file", 1);

  try {
    bp::store(
      bp::command_line_parser(
	argc, argv).options(command_line_options).positional(p).run(), vm);
    bp::notify(vm);
    if (vm.count("help")) {
      printUsage(std::cout, argv, command_line_options);
      return 0;
    }
  } catch (std::exception &e) {
    std::cerr << "error in parsing options\n"
	      << e.what() << std::endl;
    printUsage(std::cerr, argv, command_line_options);
    return 1;
  }

  if (player_str == "black")
    the_player = osl::BLACK;
  else if (player_str == "white")
    the_player = osl::WHITE;
  else {
    printUsage(std::cerr, argv, command_line_options);
    return 1;
  }

  connectRedisServer(&c, redis_server_host, redis_server_port);
  if (!c) {
    LOG(FATAL) << "Failed to connect to the Redis server";
    exit(1);
  }
  if (!redis_password.empty()) {
    if (!authenticate(c, redis_password)) {
      LOG(FATAL) << "Failed to authenticate to the Redis server";
      exit(1);
    }
  }

  doMain(file_name);

  redisFree(c);
  return 0;
}
// ;;; Local Variables:
// ;;; mode:c++
// ;;; c-basic-offset:2
// ;;; End:
