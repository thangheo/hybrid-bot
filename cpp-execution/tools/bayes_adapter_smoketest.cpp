#include "perception/bayesian_trading_adapter.hpp"
#include <iostream>
int main(){
    auto h = trading::perception::bayes_create();
    if(!h) { std::cerr<<"create failed\n"; return 1; }
    bool ok = trading::perception::bayes_set_forgetting(h, 0.95, 1e-3);
    std::cout << "set_forgetting returned: " << ok << std::endl;
    trading::perception::bayes_destroy(h);
    return 0;
}
