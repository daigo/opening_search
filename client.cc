#include "redis.h"
#include "osl/eval/ml/openMidEndingEval.h"
#include "osl/game_playing/alphaBetaPlayer.h"
#include "osl/game_playing/gameState.h"
#include "osl/record/compactBoard.h"
#include "osl/record/csa.h"
#include "osl/record/kanjiPrint.h"
#include "osl/record/ki2.h"
#include "osl/search/alphaBeta2.h"
#include <hiredis/hiredis.h>
#include <glog/logging.h>
#include <boost/format.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <cassert>

#include <time.h>
#include <unistd.h>
#include <sys/types.h>

/**
 * Global variables
 */

namespace bp = boost::program_options;
bp::variables_map vm;

redisContext *c = NULL;
int depth = 900;
int max_thingking_seconds = 900;
int verbose = 2;

/**
 * Functions
 */

struct SearchResult {
  osl::record::CompactBoard board;
  int depth;
  int score;            // evaluation value
  int consumed_seconds; // actual seconds consumed by thinking.
  time_t timestamp;     // current time stamp as seconds from Epoch.
  std::string pv;

  SearchResult(const osl::record::CompactBoard& _board)
    : board(_board),
      depth(0), score(0), timestamp(time(NULL))
  {}

  const std::string toString() {
    std::ostringstream out;
    out << ":depth "     << depth <<
           " :score "     << score <<
           " :consumed "  << consumed_seconds <<
           " :pv "        << pv <<
           " :timestamp " << timestamp
           << std::endl;
    return out.str();
  }
};


const std::string compactBoardToString(const osl::record::CompactBoard& cb) {
  std::ostringstream ss;
  ss << cb;
  return ss.str();
}


void search(const osl::NumEffectState& src, SearchResult& sr) {
  osl::game_playing::AlphaBeta2OpenMidEndingEvalPlayer player;
  player.setNextIterationCoefficient(3.0);
  player.setVerbose(verbose);
  player.setTableLimit(std::numeric_limits<size_t>::max(), 200);
  player.setNodeLimit(std::numeric_limits<size_t>::max());
  player.setDepthLimit(depth, 400, 200);

  osl::game_playing::GameState state(src);
  const int sec = max_thingking_seconds;
  osl::search::TimeAssigned time(osl::MilliSeconds::Interval(sec*1000));

  const osl::MilliSeconds start_time = osl::MilliSeconds::now();
  osl::search::AlphaBeta2SharedRoot root_info;
  osl::MoveWithComment move = player.analyzeWithSeconds(state, time, root_info);
  const osl::MilliSeconds finish_time = osl::MilliSeconds::now();
  const double consumed = (finish_time - start_time).toSeconds();
  sr.consumed_seconds = (int)consumed;
  sr.score = move.value;

  std::ostringstream out;
  if (move.move.isNormal()) {
    out << osl::record::csa::show(move.move);
    for (size_t i=0; i<move.moves.size(); ++i) {
      out << osl::record::csa::show(move.moves[i]);
    }
  }
  sr.pv = out.str();
}


int getQueueLength() {
  redisReplyPtr reply((redisReply*)redisCommand(c, "SCARD %s", "tag:new-queue"),
                      freeRedisReply);
  if (checkRedisReply(reply))
    exit(1);

  assert(reply->type == REDIS_REPLY_INTEGER);
  return reply->integer;
}


int popPosition(osl::record::CompactBoard& cb) {
  redisReplyPtr reply((redisReply*)redisCommand(c, "SPOP %s", "tag:new-queue"),
                      freeRedisReply);
  if (checkRedisReply(reply))
    exit(1);

  if (reply->type == REDIS_REPLY_NIL)
    return 1;

  if (reply->type == REDIS_REPLY_STRING) {
    const std::string str(reply->str, reply->len);
    std::istringstream in(str);
    in >> cb;
  }
  return 0;
}


int setResult(const SearchResult& sr) {
  const std::string key = compactBoardToString(sr.board);
  redisReplyPtr reply((redisReply*)redisCommand(c, "HMSET %b depth %d score %d consumed %d pv %b timestamp %d",
                                                key.c_str(), key.size(),
                                                sr.depth,
                                                sr.score,
                                                sr.consumed_seconds,
                                                sr.pv.c_str(), sr.pv.size(),
                                                sr.timestamp),
                      freeRedisReply);
  LOG(INFO) << ":depth " << sr.depth <<
               " :score " << sr.score <<
               " :consumed " << sr.consumed_seconds <<
               " :pv " << sr.pv <<
               " :timestamp " << sr.timestamp;
  if (checkRedisReply(reply))
    exit(1);

  return 0;
}


int getResult(SearchResult& sr) {
  const std::string key = compactBoardToString(sr.board);
  redisReplyPtr reply((redisReply*)redisCommand(c, "HGETALL %b", key.c_str(), key.size()),
                      freeRedisReply);
  if (checkRedisReply(reply))
    exit(1);
  assert(reply->type == REDIS_REPLY_ARRAY);

  if (reply->elements == 0) {
    LOG(ERROR) << "Found a position without any fields";
    return 1;
  }

  for(size_t i=0; i<reply->elements; /*empty*/) {
    const redisReply *r = reply->element[i++];
    assert(r->type == REDIS_REPLY_STRING);
    const std::string field(r->str);
    if ("depth" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.depth = boost::lexical_cast<int>(r->str);
    } else if ("score" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.score = boost::lexical_cast<int>(r->str);
    } else if ("consumed" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.consumed_seconds = boost::lexical_cast<int>(r->str);
    } else if ("pv" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.pv.assign(r->str);
    } else if ("timestamp" == field) {
      const redisReply *r = reply->element[i++];
      assert(r->type == REDIS_REPLY_STRING);
      sr.timestamp = boost::lexical_cast<int>(r->str);
    } else {
      LOG(WARNING) << "unknown field found: " << field;
    }
  }

  return 0;
}


bool exist(const osl::record::CompactBoard& cb) {
  const std::string key = compactBoardToString(cb);
  redisReplyPtr reply((redisReply*)redisCommand(c, "EXISTS %b", key.c_str(), key.size()),
                      freeRedisReply);
  if (checkRedisReply(reply))
    exit(1);
  assert(reply->type == REDIS_REPLY_INTEGER);
  return reply->integer;
}


int doPosition() {
  osl::record::CompactBoard cb;
  if (popPosition(cb)) {
    return 1;
  }

  if (exist(cb)) {
    DLOG(INFO) << "Found an exisiting key (board)...";
    SearchResult sr(cb);
    if (!getResult(sr)) {
      if (sr.depth >= depth) {
        DLOG(INFO) << "  do not override";
        return 0;
      }
      DLOG(INFO) << "  will override";
    }
  }

  const osl::SimpleState state = cb.getState();
  
  {
    std::ostringstream oss;
    osl::record::KanjiPrint printer(oss);
    printer.print(state);
    LOG(INFO) << std::endl << oss.str();
  }

  SearchResult sr(cb);
  sr.depth = depth;
  search(osl::NumEffectState(state), sr);
  setResult(sr);
  return 0;
}


void doMain() {
  while (true) {
    LOG(INFO) << ">>> Queue length: " << getQueueLength();
    if (doPosition())
      sleep(10);
  }
}


void printUsage(std::ostream& out, 
                char **argv,
                const boost::program_options::options_description& command_line_options) {
  out <<
    "Usage: " << argv[0] << " [options]\n"
      << command_line_options 
      << std::endl;
}

int main(int argc, char **argv)
{
  std::string radis_server_host = "127.0.0.1";
  int radis_server_port = 6379;

  /* Set up logging */
  FLAGS_log_dir = ".";
  google::InitGoogleLogging(argv[0]);

  /* Parse command line options */
  bp::options_description command_line_options;
  command_line_options.add_options()
    ("depth", bp::value<int>(&depth)->default_value(depth),
     "depth to search")
    ("radis-host", bp::value<std::string>(&radis_server_host)->default_value(radis_server_host),
     "IP of the radis server")
    ("radis-port", bp::value<int>(&radis_server_port)->default_value(radis_server_port),
     "port number of the radis server")
    ("verbose,v",  bp::value<int>(&verbose)->default_value(verbose),
     "output verbose messages.")
    ("help,h", "show this help message.");
  bp::positional_options_description p;
  //p.add("input-file", 1);

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

  /* Connect to the Redis server */
  connectRedisServer(&c, radis_server_host, radis_server_port);
  if (!c) {
    LOG(FATAL) << "Failed to connect to the Redis server";
    exit(1);
  }

  /* Set up OSL */
  osl::eval::ml::OpenMidEndingEval::setUp();
  osl::progress::ml::NewProgress::setUp();

  /* MAIN */
  doMain();

  /* Clean up things */
  redisFree(c);
  return 0;
}
// ;;; Local Variables:
// ;;; mode:c++
// ;;; c-basic-offset:2
// ;;; End:
