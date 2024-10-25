#include <cctype>
#include <iostream>
#include <string>
#include <optional>
#include <ncurses.h>
#include <map>
#include <tuple>
#include <variant>

#include <pva/client.h>
#include <pv/caProvider.h>

#include <toml++/toml.hpp>

std::optional<char> to_key_char(const std::string_view str) {
    
    static constexpr std::string_view key_prefix = "key_";

    // all valid key names start with "key_"
    if (str.find("key_") != 0) {
	std::cerr << "Invalid key " << str << std::endl;
	return std::nullopt;
    }
    
    // get everything after "key_"
    std::string tmp_str(str.substr(key_prefix.length(),str.length()));

    // check for special keys, then alpha keys
    if (tmp_str == "up") {
	return KEY_UP;
    } else if (tmp_str == "down") {
	return KEY_DOWN;
    } else if (tmp_str == "right")  {
	return KEY_RIGHT;
    } else if (tmp_str == "left") {
	return KEY_LEFT;
    } else if (tmp_str.length() > 1) {
	return std::nullopt;
    } else {
	const char alpha = tmp_str.at(0);
	return std::isalnum(alpha) ? std::optional<char>(alpha) : std::nullopt;
    }
}

// using VarType = std::variant<int, double, std::string>;
// using MapVal = std::tuple<pvac::ClientChannel, VarType>;
// std::map<char, MapVal> channel_map;
//
// std::map<char, MapVal> get_keybindings(toml::table tbl) {
    // std::map<char, MapVal> channel_map;
    // return channel_map;
// }
//


int main(int argc, char *argv[]) {

    if (argc <= 1) {
	std::cerr << "Please provide toml file" << std::endl;
	return 1;
    }

    toml::table tbl;
    try {
        tbl = toml::parse_file(argv[1]);
        // std::cout << tbl << "\n";
    } catch (const toml::parse_error& err) {
        std::cerr << "Parsing failed:\n" << err << "\n";
        return 1;
    }

    // get the IOC prefix
    const std::optional<std::string> ioc_prefix = tbl["prefix"].value<std::string>();
    if (not ioc_prefix.has_value()) {
	std::cerr << "IOC prefix is required" << std::endl;
	return 1;
    }

    // get the provider (channel access or pv access), default ca
    const std::optional<std::string> provider_name = tbl["provider"].value_or("ca");

    std::cout << "using IOC prefix " << ioc_prefix.value() << std::endl;
    std::cout << "using provider " << provider_name.value() << std::endl;
   
    epics::pvAccess::ca::CAClientFactory::start();
    pvac::ClientProvider provider(provider_name.value());
   
    // store information in a map: key char -> (PVChannel, pv value)
    using VarType = std::variant<int, double, std::string>;
    using MapVal = std::tuple<pvac::ClientChannel, VarType>;
    std::map<char, MapVal> channel_map;

    // get the keybindings
    for (const auto &[key, value] : *tbl["keybindings"].as_table()) {
	const auto keybind = *value.as_table();

	const std::optional<std::string> pv_name = keybind["pv"].value<std::string>();
	if (not pv_name.has_value()) {
	    std::cerr << "Missing or invalid PV name in keybind " << key << std::endl;
	    return 1;
	}

	// TODO: support multiple data types
	const std::optional<int> pv_val = keybind["value"].value<int>();
	if (not pv_val.has_value()) {
	    std::cerr << "Missing or invalid value in keybind " << key << std::endl;
	    return 1;
	}

	std::optional<char> key_char = to_key_char(key);
	if (not key_char.has_value()) {
	    std::cerr << "Invalid key " << key << std::endl;
	    return 1;
	}

	std::cout << key_char.value() << " : " << pv_name.value() << " = " << pv_val.value() << std::endl;

	// connect the channel to the PV
	pvac::ClientChannel channel(provider.connect(ioc_prefix.value() + pv_name.value()));

	// add the key char, channel, and PV value to the map
	channel_map[key_char.value()] = std::make_tuple(channel, pv_val.value());

    }

    for (const auto &[key, value] : channel_map) {
	pvac::ClientChannel channel = std::get<0>(value);
	std::cout << channel.get() << std::endl;
    }

    // TODO: match char from getch() to keys in channel_map,

    return 0;
}

