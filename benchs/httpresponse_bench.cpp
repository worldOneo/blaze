#include "httpserver.hh"
#include <benchmark/benchmark.h>

static void BM_HttpResponse(benchmark::State &state) {
  blaze::HttpParser parser{};
  blaze::Buffer<char> data{};
  blaze::ParseResult result;
  data.write("GET / HTTP/1.0\r\nHost: cookie.com\r\nDate: foobar\r\nAccept: "
             "these/that\r\n\r\n",
             82);

  blaze::Pool<blaze::Buffer<char>> buffs{};
  blaze::Buffer<char> respBuff{};
  for (auto _ : state) {
    blaze::HttpResponse resp{};
    resp.buffs = &buffs;
    blaze::HttpRequest request{&parser, data.view()};
    resp.render(request, &respBuff);
    benchmark::DoNotOptimize(&resp);
    benchmark::DoNotOptimize(&respBuff);
    respBuff.reset();
    resp.reset();
  }
}

BENCHMARK(BM_HttpResponse);
BENCHMARK_MAIN();