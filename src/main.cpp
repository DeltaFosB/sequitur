#include <iostream>
#include <sequitur/core/MatchingEngine.hpp>
#include <sequitur/utils/Timer.hpp>

int main() {

  sequitur::core::MatchingEngine engine(10'000'000);

  std::cout << "Sequitur Exchange Engine: Online" << std::endl;
  return 0;
}
