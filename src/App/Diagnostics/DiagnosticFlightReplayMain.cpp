#include "App/Diagnostics/DiagnosticFlightReplay.h"
#include <fstream>
#include <iostream>

int main(int argc, char **argv)
{
  try
  {
    if (argc != 2)
      throw std::runtime_error("usage: diagnostic_flight_replay FILE.jsonl");
    std::ifstream input(argv[1], std::ios::binary);
    const auto ticks = cutum::diagnostic_replay::Verify(input);
    std::cout << "{\"status\":\"PASS\",\"replayed_ticks\":" << ticks << "}\n";
    return 0;
  }
  catch (const std::exception &error)
  {
    std::cerr << "diagnostic replay failed: " << error.what() << '\n';
    return 1;
  }
}
