#include "redis.h"
#include <iostream>

void connectRedisServer(redisContext **context, const std::string& host, const int port) {
  const struct timeval timeout = { 1, 500000 }; // 1.5 seconds
  redisContext *c = redisConnectWithTimeout(host.c_str(), port, timeout);
  if (c->err) {
    std::cerr << "Connection error: " << c->errstr << std::endl;
  } else {
    *context = c;
  }
}

