#include <exception>
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

using VarType = std::variant<int, double, bool, std::string>;
using TupleVal = std::tuple<pvac::ClientChannel, VarType, bool>;

// Returns the value of the optional if present,
// otherwise panics with the given message
template <typename T>
T expect(std::optional<T> optional, const std::string &msg) {
    if (optional.has_value()) {
	return optional.value();
    } else {
	throw std::runtime_error(msg);
    }
}

// Returns an optional char given a string like "key_a" 
// which can be interpreted by ncurses getch()
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


// Returns an optional string of the type name of an EPICS PV given a pvac::ClientChannel
std::optional<std::string> get_pv_type(pvac::ClientChannel &channel) {
    std::optional<std::string> type_str;
    try {
	type_str = channel.get()->getStructure()->getField("value")->getID();
    } catch (const std::exception &e) {
	type_str = std::nullopt;	
    }
    return type_str;
}

// Returns an optional string of the type name of a variant
// with possible types int, double, bool, or string
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


// Returns true if the string names of pv_type and var_type are agreeable
bool check_type_match(const std::string &pv_type, const std::string &var_type) {

    bool type_match = true;

    if (pv_type == "float" || pv_type == "double") {
	type_match = (var_type == "double" || var_type == "int");
    } else if (pv_type == "boolean") {
	type_match = (var_type == "bool");
    } else if (pv_type == "string") {
	type_match = (var_type == "string");
    } else {
	type_match = (var_type == "int");
    } 

    return type_match;
}

// Attempts to store the value of the given toml::node in one of
// string, integer, double, bool as a optional variant
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

// Returns a map from char keys to pv channel and target value
std::map<char, TupleVal> parse_keybindings(const toml::table &tbl, pvac::ClientProvider &provider, const std::string &ioc_prefix) {
    std::stringstream err_ss;
    std::map<char, TupleVal> channel_map;
    
    if (auto keybindings_tbl = tbl["keybindings"].as_table()) {
	for (const auto &[key, value] : *keybindings_tbl) {
	    // key is e.g. 'key_right'
	    // value is e.g. '{pv="m1.TWF", value=1}'
	    std::cout << key << ": ";
	    std::cout << *value.as_table() << std::endl;
	    const auto keybind = *value.as_table();

	    // Get the name of the PV to write to
	    std::string pv_name = expect(keybind["pv"].value<std::string>(), "Missing or invalid PV name");
	
	    // Create channel for the pv
	    pvac::ClientChannel channel;
	    pv_name =  ioc_prefix + pv_name;
	    try {
		channel = pvac::ClientChannel(provider.connect(pv_name));
		channel.get();
		std::cout << "Connected to " << channel.name() << std::endl;
	    } catch (const std::exception &e) {
		throw std::runtime_error("Failed to connect to PV");
	    }

	    // Get the char for the cooresponding key for ncurses 
	    const char key_char = expect(to_key_char(key), "Invalid key");

	    // Get type of PV
	    const std::string pv_type = expect(get_pv_type(channel),"PV is not a supported type");

	    // Get variant value pv target value from toml node
	    VarType pv_val = expect(extract_variant_value(*keybind["value"].node()), "Invalid value");
	    const std::string var_type = expect(get_variant_type(pv_val),
					 "get_variant_type() failed. Check type of pv value");
	
	    // Ensure desired value type matches PV type
	    if (not check_type_match(pv_type, var_type)) {
		throw std::runtime_error("Type mismatch between target value and PV value");
	    }

	    // Get flag for increment mode (default: false)
	    // only supported for numbers, not strings
	    bool increment = false;
	    if (var_type == "int" or var_type == "double") {
		increment = keybind["increment"].value<bool>().value_or(false);
	    }

	    // add keybinding to the map
	    channel_map[key_char] = std::make_tuple(channel, pv_val, increment);
	}
    } else {
	throw std::runtime_error("No keybindings section in TOML file");
    }

    return channel_map;
}

// Executes the ca/pva puts to the PVs specfied in the put array in toml file
void do_prelim_puts(const toml::table &tbl, pvac::ClientProvider &provider, const std::string &ioc_prefix) {
    if (auto put_array = tbl["put"].as_array()) {
	for (const auto &item: *put_array) {
	    if (auto table = item.as_table()) {
		std::cout << *table << std::endl;
		
		// Get the PV name
		std::string pv_name = expect(table->get("pv")->value<std::string>(),"Bad or missing PV name");
		pv_name = ioc_prefix + pv_name;

		// Get the PV target value
		VarType val = expect(
		    extract_variant_value(*table->get("value")),
		    "Bad or missing value in put list"
		);

		// Create channel for the pv
		pvac::ClientChannel channel;
		try {
		    channel = pvac::ClientChannel(provider.connect(pv_name));
		    channel.get();
		    std::cout << "Connected to " << channel.name() << std::endl;
		} catch (const std::exception &e) {
		    throw std::runtime_error("Failed to connect to PV");
		}
		
		// Check type match
		std::string pv_type = expect(get_pv_type(channel), "Invalid PV type");
		std::string var_type = expect(get_variant_type(val), "Invalid value type"); 
		if (not check_type_match(pv_type, var_type)) {
		    throw std::runtime_error("Type mismatch between target value and PV value");
		}

		if (var_type == "int") {
		    if (pv_type == "enum_t") {
			channel.put().set("value.index", std::get<int>(val)).exec();
		    }  else {
			channel.put().set("value", std::get<int>(val)).exec();
		    }
		} else if (var_type == "double") {
		    channel.put().set("value", static_cast<double>(std::get<double>(val))).exec();
		} else if (var_type == "string") {
		    channel.put().set("value", static_cast<std::string>(std::get<std::string>(val))).exec();
		}

	    }
	    
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
    
    // Get the provider "ca" or "pva", default: "ca"
    epics::pvAccess::ca::CAClientFactory::start();
    const std::optional<std::string> provider_name = tbl["provider"].value_or("ca");
    pvac::ClientProvider provider(provider_name.value());

    // Get the IOC prefix
    std::string ioc_prefix = tbl["prefix"].value_or("");

    // Execute requested puts before running main loop
    do_prelim_puts(tbl, provider, ioc_prefix);
    
    // Get the mapping key_char -> (pv channel, pv value, increment=true/false)
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
	    const std::string pv_type_str = expect(get_pv_type(channel), "Failed to get pv_type_str");
	    const std::string var_type_str = expect(get_variant_type(val_vnt), "Failed to get var_type_str");
	    
	    // increment mode
	    if (std::get<2>(channel_map[ch])) {
		if (var_type_str == "int") {
		    const int current_val = channel.get()->getSubFieldT<epics::pvData::PVInt>("value")->getAs<int>();
		    const int inc_val = std::get<int>(val_vnt);
		    channel.put().set("value", current_val + inc_val).exec();
		} else if (var_type_str == "double") {
		    const double current_val = channel.get()->getSubFieldT<epics::pvData::PVDouble>("value")->getAs<double>();
		    const double inc_val = std::get<double>(val_vnt);
		    channel.put().set("value", current_val + inc_val).exec();
		} 
	    } else { // standard write mode
		if (var_type_str == "int") {
		    if (pv_type_str == "enum_t") {
			channel.put().set("value.index", std::get<int>(val_vnt)).exec();
		    } else {
			channel.put().set("value", std::get<int>(val_vnt)).exec();
		    }
		} else if (var_type_str == "double") {
		    channel.put().set("value", std::get<double>(val_vnt)).exec();
		} else if (var_type_str == "string") {
		    channel.put().set("value", std::get<std::string>(val_vnt)).exec();
		}
	    }

	}
	
	refresh();
    }
    endwin();

    return 0;
}

