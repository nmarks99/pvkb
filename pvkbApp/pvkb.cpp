#include <cctype>
#include <iostream>
#include <new>
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
#include "argh.h"


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

using VarType = std::variant<int, double, bool, std::string>;
using TupleVal = std::tuple<pvac::ClientChannel, VarType>;

std::optional<std::string> get_pv_scalar_type(pvac::ClientChannel &channel) {
    auto pvd_scalar_type = std::dynamic_pointer_cast<const epics::pvData::Scalar>(
	channel.get()->getStructure()->getField("value"))->getScalarType();
    if (pvd_scalar_type) {
	return epics::pvData::ScalarTypeFunc::name(pvd_scalar_type);
    } else {
	return std::nullopt;
    }
}

std::optional<std::string> get_variant_type(const VarType& value) {
    std::string result;
    auto visitor = [&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>; // Get the type of the argument
        if constexpr (std::is_same_v<T, int>) {
	    result = "int";
        } else if constexpr (std::is_same_v<T, double>) {
	    result = "double";
        } else if constexpr (std::is_same_v<T, bool>) {
	    result = "bool";
        } else if constexpr (std::is_same_v<T, std::string>) {
	    result = "string";
        }
    };
    std::visit(visitor, value);
    return result.length() > 0 ? std::optional<std::string>(result) : std::nullopt;
}


bool check_type_match(const std::string &pv_scalar_type, const std::string &var_type) {

    bool type_match = true;

    if (pv_scalar_type == "float" || pv_scalar_type == "double") {
	type_match = (var_type == "double" || var_type == "int");
    } else if (pv_scalar_type == "boolean") {
	type_match = (var_type == "bool");
    } else if (pv_scalar_type == "string") {
	type_match = (var_type == "string");
    } else {
	type_match = (var_type == "int");
    } 

    return type_match;
}


std::optional<VarType> extract_variant_value(const toml::node &node) {
    if (node.is_string()) {
	return *node.value<std::string>();
    } else if (node.is_integer()) {
	return *node.value<int>();
    } else if (node.is_floating_point()) {
	return *node.value<double>();
    } else if (node.is_boolean()) {
	return *node.value<bool>();
    } else {
	return std::nullopt;
    }
}


std::map<char, TupleVal> parse_keybindings(const toml::table &tbl, pvac::ClientProvider &provider, const std::string &ioc_prefix) {
    std::stringstream err_ss;
    std::map<char, TupleVal> channel_map;

    // get all the keybindings and construct the map
    for (const auto &[key, value] : *tbl["keybindings"].as_table()) {
	const auto keybind = *value.as_table();

	// Get the name of the PV to write to
	const std::optional<std::string> pv_name = keybind["pv"].value<std::string>();
	if (not pv_name.has_value()) {
	    err_ss.clear();
	    err_ss << "Missing or invalid PV name in keybind " << "'" << key << "'" << std::endl;
	    throw std::runtime_error(err_ss.str());
	}
    
	// Get the char for the cooresponding key for ncurses 
	std::optional<char> key_char = to_key_char(key);
	if (not key_char.has_value()) {
	    err_ss.clear();
	    err_ss << "Invalid key " << "'" << key << "'" << std::endl;
	    throw std::runtime_error(err_ss.str());
	}

	// create channel for the pv
	// FIX: handle connection errors
	pvac::ClientChannel channel(provider.connect(ioc_prefix + pv_name.value()));

	// Get scalar type of PV
	// TODO: support more PV types (enum)
	std::optional<std::string> pv_scalar_type = get_pv_scalar_type(channel);
	if (not pv_scalar_type.has_value()) {
	    err_ss.clear();
	    err_ss << "PV " << pv_name.value() << " is not a scalar type" << std::endl;
	    throw std::runtime_error(err_ss.str());
	}

	// Get variant type of desired PV value
	std::optional<VarType> pv_val = extract_variant_value(*keybind["value"].node());
	if (not pv_val.has_value()) {
	    err_ss.clear();
	    err_ss << "Invalid value for key " << "'" << key << "'" << std::endl;
	    throw std::runtime_error(err_ss.str());
	}
	std::optional<std::string> var_type = get_variant_type(pv_val.value());
    
	// Ensure desired value type matches PV type
	if (not check_type_match(pv_scalar_type.value(), var_type.value())) {
	    err_ss.clear();
	    err_ss << "Type mismatch for key " << "'" << key << "'" << std::endl;
	    throw std::runtime_error(err_ss.str());
	}

	// add the key char, channel, and PV value to the map
	channel_map[key_char.value()] = std::make_tuple(channel, pv_val.value());
    }
    return channel_map;
}

void do_prelim_puts(const toml::table &tbl, pvac::ClientProvider &provider, const std::string &ioc_prefix) {
    std::stringstream err_ss;

    for (const auto &[key, value] : *tbl["put"].as_table()) {
	const std::optional<VarType> val = extract_variant_value(value);
	if (not val.has_value()) {
	    err_ss.clear();
	    err_ss << "Invalid value for put: " << "'" << key << "'" << std::endl;
	    throw std::runtime_error(err_ss.str());
	}

	pvac::ClientChannel channel(provider.connect(ioc_prefix + std::string(key)));

	std::optional<std::string> var_type = get_variant_type(val.value()); 
	std::optional<std::string> pv_scalar_type = get_pv_scalar_type(channel);
	if (not check_type_match(pv_scalar_type.value(), var_type.value())) {
	    err_ss << "Type mismatch for put: " << "'" << key << "'" << std::endl;
	    throw std::runtime_error(err_ss.str());
	}

	if (var_type == "int") {
	    channel.put().set("value", static_cast<int>(std::get<int>(val.value()))).exec();
	} else if (var_type == "double") {
	    channel.put().set("value", static_cast<double>(std::get<double>(val.value()))).exec();
	} else if (var_type == "string") {
	    channel.put().set("value", static_cast<std::string>(std::get<std::string>(val.value()))).exec();
	}
    }

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

    // Get the IOC prefix
    std::string ioc_prefix = tbl["prefix"].value_or("");

    // Execute requested puts before running main loop
    do_prelim_puts(tbl, provider, ioc_prefix);
    
    // Get the mapping key_char -> (pv channel, pv value to write)
    std::map<char, TupleVal> channel_map = parse_keybindings(tbl, provider, ioc_prefix);

    // initialize ncurses
    initscr();
    keypad(stdscr, TRUE);
    noecho();
    
    while (true) {
	int ch = getch();
	if (ch == 'q') {
	    break;
	}
	if (channel_map.count(ch) > 0) {
	    pvac::ClientChannel channel = std::get<0>(channel_map[ch]);
	    VarType val_vnt = std::get<1>(channel_map[ch]);
	    const std::optional<std::string> var_type_str = get_variant_type(val_vnt);
	    if (var_type_str == "int") {
		channel.put().set("value", static_cast<int>(std::get<int>(val_vnt))).exec();
	    } else if (var_type_str == "double") {
		channel.put().set("value", static_cast<double>(std::get<double>(val_vnt))).exec();
	    } else if (var_type_str == "string") {
		channel.put().set("value", static_cast<std::string>(std::get<std::string>(val_vnt))).exec();
	    }
	}
	
	refresh();
    }
    endwin();

    return 0;
}

