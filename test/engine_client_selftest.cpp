#include "../plugin/engine_client.h"

#include <cstdio>

int main() {
  nr_eval_desc desc{};
  if (engine_client::Eval(desc) != NR_ERR_MISSING_RUNTIME) return 1;
  engine_client::Cancel(1, 1);
  engine_client::Shutdown();
  std::puts("engine_client_selftest: PASS (engine DLL absent beside test executable)");
  return 0;
}
