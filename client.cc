#include "redis.h"
#include "osl/eval/ml/openMidEndingEval.h"
#include "osl/game_playing/alphaBetaPlayer.h"
#include "osl/game_playing/gameState.h"
#include "osl/record/compactBoard.h"
#include "osl/record/kanjiPrint.h"
#include "osl/record/ki2.h"
#include "osl/search/alphaBeta2.h"
#include <hiredis/hiredis.h>
//#include <boost/format.hpp>
#include <boost/program_options.hpp>
//#include <boost/shared_ptr.hpp>
#include <iostream>
//#include <sstream>
#include <string>
//#include <vector>

namespace bp = boost::program_options;
bp::variables_map vm;

redisContext *c = NULL;

void search(const osl::NumEffectState& src)
{
//  static double scale = osl::eval::ml::OpenMidEndingEval::captureValue(osl::newPtypeO(osl::WHITE,osl::PAWN))/200.0;
  osl::game_playing::AlphaBeta2OpenMidEndingEvalPlayer player;
  player.setNextIterationCoefficient(3.0);
  player.setVerbose(2);
  player.setTableLimit(std::numeric_limits<size_t>::max(), 200);
  player.setNodeLimit(std::numeric_limits<size_t>::max());
  player.setDepthLimit(900, 400, 200);

  osl::game_playing::GameState state(src);
  const int sec = 900;
  osl::search::TimeAssigned time(osl::MilliSeconds::Interval(sec*1000));

  const osl::MilliSeconds start_time = osl::MilliSeconds::now();
  osl::search::AlphaBeta2SharedRoot root_info;
  osl::MoveWithComment move = player.analyzeWithSeconds(state, time, root_info);
  const osl::MilliSeconds finish_time = osl::MilliSeconds::now();
  const double consumed = (finish_time - start_time).toSeconds();

  std::ostringstream ret;
  ret << " " << move.value;
  if (move.move.isNormal()) {
    ret << ' ' << osl::record::ki2::show(move.move, src, osl::Move());
  }
  ret << " (" << (int)consumed << "s)";
}

int popPosition(osl::record::CompactBoard& cb) {
  redisReply *reply = (redisReply*)redisCommand(c, "SPOP %s", "tag:new-queue");
  if (!reply) {
    std::cerr << "SADD error: " << c->errstr << std::endl;
    exit(1);
  }
  if (reply->type == REDIS_REPLY_NIL)
    return 1;

  if (reply->type == REDIS_REPLY_STRING) {
    const std::string str(reply->str, reply->len);
    std::istringstream in(str);
    in >> cb;
  }
  freeReplyObject(reply);
  return 0;
}


void doPosition() {
  osl::record::CompactBoard cb;
  if (popPosition(cb)) {
    return;
  }
  const osl::SimpleState state = cb.getState();
  
  osl::record::KanjiPrint printer(std::cerr);
  printer.print(state);

  search(osl::NumEffectState(state));

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

  bp::options_description command_line_options;
  command_line_options.add_options()
    ("radis-host", bp::value<std::string>(&radis_server_host)->default_value(radis_server_host),
     "IP of the radis server")
    ("radis-port", bp::value<int>(&radis_server_port)->default_value(radis_server_port),
     "port number of the radis server")
    ("verbose,v", "output verbose messages.")
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

  connectRedisServer(&c, radis_server_host, radis_server_port);
  if (!c)
    exit(1);

  osl::eval::ml::OpenMidEndingEval::setUp();
  osl::progress::ml::NewProgress::setUp();

  doPosition();
  redisFree(c);

  return 0;
}
// ;;; Local Variables:
// ;;; mode:c++
// ;;; c-basic-offset:2
// ;;; End:
