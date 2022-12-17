#include "httpserver.hh"
#include <benchmark/benchmark.h>

static void BM_HttpResponse(benchmark::State &state) {
  blaze::HttpParser parser{};
  blaze::Buffer<char> data{};
  blaze::ParseResult result;
  data.write("GET / HTTP/1.0\r\nHost: cookie.com\r\nDate: foobar\r\nAccept: "
             "these/that\r\n\r\n",
             82);

  blaze::Buffer<char> respBuff{};
  blaze::HttpRequest request{parser, data.view()};
  blaze::HttpResponse resp{};
  blaze::Pool<blaze::Buffer<char>> buffs{};
  resp.buffs = &buffs;
  for (auto _ : state) {
    resp.render(request, &respBuff);
    benchmark::DoNotOptimize(&resp);
    benchmark::DoNotOptimize(&respBuff);
    respBuff.reset();
    resp.reset();
  }
}

BENCHMARK(BM_HttpResponse);
BENCHMARK_MAIN();