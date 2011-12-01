#include "redis.h"
#include "searchResult.h"
#include "osl/record/compactBoard.h"
#include "osl/state/simpleState.h"
#include <hiredis/hiredis.h>
#include <glog/logging.h>
#include <boost/foreach.hpp>
#include <boost/program_options.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <cassert>

/**
 * Global variables
 */

namespace bp = boost::program_options;
bp::variables_map vm;

redisContext *c = NULL;

void getAllBoards(std::vector<osl::record::CompactBoard>& boards)
{
  redisReplyPtr reply((redisReply*)redisCommand(c, "KEYS *", freeRedisReply));
  if (checkRedisReply(reply))
    exit(1);
  assert(reply->type == REDIS_REPLY_ARRAY);

  if (reply->elements == 0) {
    LOG(WARNING) << "No board found";
    return;
  }

  DLOG(INFO) << "size: " << reply->elements;
  boards.reserve(reply->elements);
  for(size_t i=0; i<reply->elements; ++i) {
    const redisReply *r = reply->element[i];
    assert(r->type == REDIS_REPLY_STRING);
    if (r->len == 41*4) {
      const std::string key(r->str, r->len);
      std::stringstream ss;
      ss << key;
      osl::record::CompactBoard cb;
      ss >> cb;
      boards.push_back(cb);
    }
  }
}

void dump(std::ostream& out,
          const std::vector<SearchResult>& results,
          osl::Player player)
{
  /* Header */
  out << "EVAL" << std::endl;

  /* Rows */
  BOOST_FOREACH(const SearchResult& sr, results) {
    const osl::SimpleState state = sr.board.getState();
    if (state.turn() != player)
      continue;

    DLOG(INFO) << sr.toString();
    if (sr.depth >= 1600) {
      out << sr.score << std::endl;
    }
  }  
}

void doMain()
{
  /* Retreive search results */
  std::vector<SearchResult> results;
  {
    std::vector<osl::record::CompactBoard> boards;
    getAllBoards(boards);
    LOG(INFO) << "Loaded candidate boards: " << boards.size();

    results.reserve(boards.size());
    BOOST_FOREACH(const osl::record::CompactBoard& cb, boards) {
      results.push_back(SearchResult(cb));
    }
    querySearchResult(c, results);
  }

  { /* BLACK */
    std::ofstream out("out_black.csv", std::ios_base::trunc);
    dump(out, results, osl::BLACK);
  }
  { /* WHITE */
    std::ofstream out("out_white.csv", std::ios_base::trunc);
    dump(out, results, osl::WHITE);
  }
}

void printUsage(std::ostream& out, 
                char **argv,
                const boost::program_options::options_description& command_line_options)
{
  out <<
    "Usage: " << argv[0] << " [options]\n"
      << command_line_options 
      << std::endl;
}

int main(int argc, char **argv)
{
  std::string redis_server_host = "127.0.0.1";
  int redis_server_port = 6379;
  std::string redis_password;

  /* Set up logging */
  FLAGS_log_dir = ".";
  google::InitGoogleLogging(argv[0]);

  /* Parse command line options */
  bp::options_description command_line_options;
  command_line_options.add_options()
    ("redis-host", bp::value<std::string>(&redis_server_host)->default_value(redis_server_host),
     "IP of the redis server")
    ("redis-password", bp::value<std::string>(&redis_password)->default_value(redis_password),
     "password to connect to the redis server")
    ("redis-port", bp::value<int>(&redis_server_port)->default_value(redis_server_port),
     "port number of the redis server")
    ("help,h", "show this help message.");
  bp::positional_options_description p;

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