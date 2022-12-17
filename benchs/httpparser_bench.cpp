#include "httpparser.hh"
#include <benchmark/benchmark.h>

static void BM_HttparserParse(benchmark::State &state) {
  blaze::HttpParser parser{};
  blaze::Buffer<char> data{};
  blaze::ParseResult result;
  data.write("GET / HTTP/1.0\r\nHost: cookie.com\r\nDate: foobar\r\nAccept: "
             "these/that\r\n\r\n",
             82);
  for (auto _ : state) {
    parser.parse(data.view(), result);
    benchmark::DoNotOptimize(&parser);
    benchmark::DoNotOptimize(&result);
  }
}

BENCHMARK(BM_HttparserParse);
BENCHMARK_MAIN();