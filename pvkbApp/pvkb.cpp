#include <memory>
#include <ncurses.h>
#include <string>
#include <iostream>
#include <cassert>
#include <map>

#include <pva/client.h>
#include <pv/caProvider.h>

#include <toml++/toml.hpp>

// namespace pvd = epics::pvData;

// const std::map<std::string_view, char> key_map{
    // {"key_up" , KEY_UP},
    // {"key_left" , KEY_LEFT},
    // {"key_down" , KEY_DOWN},
    // {"key_right" , KEY_RIGHT},
    // {"key_q" , 'q'},
    // {"key_w" , 'w'},
    // {"key_e" , 'e'},
    // {"key_r" , 'r'},
    // {"key_t" , 't'},
    // {"key_y" , 'y'},
// };


int main(int argc, char *argv[]) {
    
    // if (argc <= 1) {
        // std::cout << "Please provide IOC prefix" << std::endl;
        // return 1;
    // }

    // std::string prefix(argv[1]);

    // epics::pvAccess::ca::CAClientFactory::start();
    // pvac::ClientProvider provider("ca");
    //
    // // Get a PV value and convert it to a primitive type (int, double, etc.)
    // // the template argument in getSubFieldT must be the correct type
    // pvac::ClientChannel channel_test(provider.connect(prefix + "alive"));
    // std::cout << channel_test.get() << std::endl;
    // auto value = channel_test.get()->getSubFieldT<pvd::PVDouble>("value");
    // std::cout << "value = " << value << std::endl;
    // double val_prim = value->getAs<double>();
    // std::cout << "val double = " << val_prim << std::endl;
    
    assert(argc > 1);
    
    toml::table tbl;
    try
    {
        tbl = toml::parse_file(argv[1]);
        // std::cout << tbl << "\n";
    }
    catch (const toml::parse_error& err)
    {
        std::cerr << "Parsing failed:\n" << err << "\n";
        return 1;
    }

    // std::string prefix = tbl["prefix"].value_or("");
    // std::string provider = tbl["provider"].value_or("ca");
    // std::cout << "prefix = " << prefix << std::endl;
    // std::cout << "provider = " << provider << std::endl;
    
    for (const auto &[key, values] : *tbl["keybindings"].as_table()) {
        if (values.is_table()) {
            std::cout << "key = " << key << std::endl;
            const auto &keybind = *values.as_table();
            std::string pv = keybind["pv"].value_or("?");
            std::string val = keybind["value"].value_or("?");
            std::cout << "pv = " << pv << std::endl;
            std::cout << "value = " << val << std::endl << std::endl;
        } else {
            std::cerr << "Invalid keybindings" << std::endl;
            return 1;
        }
    }

    return 0;
}
