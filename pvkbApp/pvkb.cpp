#include <cctype>
#include <iostream>
#include <stdexcept>
#include <string>
#include <optional>
#include <map>
#include <tuple>
#include <variant>
#include <ncurses.h>

#include <pva/client.h>
#include <pv/caProvider.h>

#include "toml++/toml.hpp"


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

using VarType = std::variant<int, double, std::string>;
using TupleVal = std::tuple<pvac::ClientChannel, VarType>;

std::map<char, TupleVal> get_keybindings(const toml::table &tbl, pvac::ClientProvider &provider) {
    std::stringstream err_ss;
    std::map<char, TupleVal> channel_map;

    // get the IOC prefix
    const std::optional<std::string> ioc_prefix = tbl["prefix"].value<std::string>();
    if (not ioc_prefix.has_value()) {
	err_ss.clear();
	err_ss << "IOC prefix is required" << std::endl;
	throw std::runtime_error(err_ss.str());
    }

    // get all the keybindings and construct the map
    for (const auto &[key, value] : *tbl["keybindings"].as_table()) {
	const auto keybind = *value.as_table();
	const std::optional<std::string> pv_name = keybind["pv"].value<std::string>();
	if (not pv_name.has_value()) {
	    err_ss.clear();
	    err_ss << "Missing or invalid PV name in keybind " << "'" << key << "'" << std::endl;
	    throw std::runtime_error(err_ss.str());
	}

	// TODO: support multiple data types
	const std::optional<int> pv_val = keybind["value"].value_or(1);

	std::optional<char> key_char = to_key_char(key);
	if (not key_char.has_value()) {
	    err_ss.clear();
	    err_ss << "Invalid key " << "'" << key << "'" << std::endl;
	    throw std::runtime_error(err_ss.str());
	}

	// connect the channel to the PV
	pvac::ClientChannel channel(provider.connect(ioc_prefix.value() + pv_name.value()));

	// add the key char, channel, and PV value to the map
	channel_map[key_char.value()] = std::make_tuple(channel, pv_val.value());

    }
    return channel_map;
}


int main(int argc, char *argv[]) {

    if (argc <= 1) {
	std::cerr << "Please provide toml file" << std::endl;
	return 1;
    }

    // parse the toml file into a toml::table
    toml::table tbl;
    try {
        tbl = toml::parse_file(argv[1]);
    } catch (const toml::parse_error& err) {
        std::cerr << "Parsing failed:\n" << err << "\n";
        return 1;
    }
    
    // Get the provider "ca" or "pva", default ca
    epics::pvAccess::ca::CAClientFactory::start();
    const std::optional<std::string> provider_name = tbl["provider"].value_or("ca");
    pvac::ClientProvider provider(provider_name.value());
    
    // Get the mapping key_char -> (pv channel, pv value to write)
    std::map<char, TupleVal> channel_map = get_keybindings(tbl, provider);

    // initialize ncurses
    initscr();
    keypad(stdscr, TRUE);
    noecho();
    
    while (true) {
	int ch = getch();
	if (ch == 'q') {
	    break;
	}
	const auto val = std::get<int>(std::get<1>(channel_map[ch]));
	std::get<0>(channel_map[ch]).put().set("value", val).exec();
	refresh();
    }
    endwin();

    return 0;
}

