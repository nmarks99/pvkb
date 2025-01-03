#include <exception>
#include <iomanip>
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
#include "argh.h"

// Stores the value field of keybinding with the appropriate type
// e.g. key_right = {pv="m1.TWF", value=1} 
using TargetVar = std::variant<int, double, bool, std::string>;

// Keybinding tuple (PVA channel, target value, boolean increment)
using TupleVal = std::tuple<pvac::ClientChannel, TargetVar, bool>;


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
    } else if (tmp_str == "enter") {
	return '\n';
    } else if (tmp_str == "space") {
	return ' ';
    } else if (tmp_str.length() > 1) {
	return std::nullopt;
    } else { // alphanumeric char ('a','b',1,2,etc.)
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
std::optional<std::string> get_variant_type(const TargetVar& value) {
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
    } else { // pv could be a variety of integer types like byte, short, long, ubyte, etc.
	type_match = (var_type == "int");
    } 

    return type_match;
}

// Attempts to store the value of the given toml::node in one of
// string, integer, double, bool as a optional variant
std::optional<TargetVar> extract_variant_value(const toml::node &node) {
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
	    // std::cout << key << ": ";
	    // std::cout << *value.as_table() << " -> ";
	    const auto keybind = *value.as_table();

	    // Get the name of the PV to write to
	    std::string pv_name = expect(keybind["pv"].value<std::string>(), "Missing or invalid PV name");
	
	    // Create channel for the pv
	    pvac::ClientChannel channel;
	    pv_name =  ioc_prefix + pv_name;
	    try {
		channel = pvac::ClientChannel(provider.connect(pv_name));
		channel.get();
	    } catch (const std::exception &e) {
		throw std::runtime_error("Failed to connect to PV");
	    }

	    // Get the char for the cooresponding key for ncurses 
	    const char key_char = expect(to_key_char(key), "Invalid key");

	    // Get type of PV
	    const std::string pv_type_str = expect(get_pv_type(channel),"PV is not a supported type");

	    // Get variant value pv target value from toml node
	    TargetVar pv_val = expect(extract_variant_value(*keybind["value"].node()), "Invalid value");
	    const std::string var_type_str = expect(get_variant_type(pv_val),
					 "get_variant_type() failed. Check type of pv value");
	
	    // Get flag for increment mode (default: false)
	    // only supported for numbers, not strings
	    bool increment = false;
	    if (var_type_str == "int" or var_type_str == "double") {
		increment = keybind["increment"].value<bool>().value_or(false);
	    }

	    // Ensure desired value type matches PV type
	    if (not check_type_match(pv_type_str, var_type_str)) {
		throw std::runtime_error("Type mismatch between target value and PV value");
	    }

	    // add keybinding to the map
	    channel_map[key_char] = std::make_tuple(channel, pv_val, increment);
	}
    } else {
	throw std::runtime_error("No keybindings section in TOML file");
    }

    return channel_map;
}

// Executes a ca/pva put of the given value to the given channel
// We assume that the PV and value types match
void execute_put(pvac::ClientChannel &channel, TargetVar val, bool increment=false) {
    const std::string pv_type_str = expect(get_pv_type(channel), "Failed to get pv_type_str");
    const std::string var_type_str = expect(get_variant_type(val), "Failed to get var_type_str");

    std::string target_field = "value";
    if (pv_type_str == "enum_t") {
	target_field = "value.index";
    }
    
    if (increment) {
	if (var_type_str == "int") {
	    const int current_val = channel.get()->getSubFieldT<epics::pvData::PVInt>("value")->getAs<int>();
	    const int inc_val = std::get<int>(val);
	    channel.put().set(target_field, current_val + inc_val).exec();
	} else {
	    const double current_val = channel.get()->getSubFieldT<epics::pvData::PVDouble>("value")->getAs<double>();
	    const double inc_val = std::get<double>(val);
	    channel.put().set(target_field, current_val + inc_val).exec();
	}
    } else {
	if (var_type_str == "int") {
	    channel.put().set(target_field, std::get<int>(val)).exec();
	} else if (var_type_str == "double") {
	    channel.put().set(target_field, std::get<double>(val)).exec();
	} else if (var_type_str == "string") {
	    channel.put().set(target_field, std::get<std::string>(val)).exec();
	} 
    }
}

// Executes the ca/pva puts to the PVs specfied in the put array in toml file
void do_prelim_puts(const toml::table &tbl, pvac::ClientProvider &provider, const std::string &ioc_prefix) {
    if (auto put_array = tbl["put"].as_array()) {
	for (const auto &item: *put_array) {
	    if (auto table = item.as_table()) {
		
		// Get the PV name
		std::string pv_name = expect(table->get("pv")->value<std::string>(),"Bad or missing PV name");
		pv_name = ioc_prefix + pv_name;

		// Create channel for the pv
		pvac::ClientChannel channel;
		try {
		    channel = pvac::ClientChannel(provider.connect(pv_name));
		    channel.get();
		} catch (const std::exception &e) {
		    throw std::runtime_error("Failed to connect to PV");
		}

		// Get the PV target value
		TargetVar val = expect(
		    extract_variant_value(*table->get("value")),
		    "Bad or missing value in put list"
		);

		// Ensure desired value type matches PV type
		const std::string pv_type_str = expect(get_pv_type(channel), "Failed to get pv_type_str");
		const std::string var_type_str = expect(get_variant_type(val), "Failed to get var_type_str");
		if (not check_type_match(pv_type_str, var_type_str)) {
		    throw std::runtime_error("Type mismatch between target value and PV value");
		}
	    
		// Puts the value to the channel, assumes the types match
		execute_put(channel, val);
	    }
	    
	} 
    }
}

void show_keybindings(const toml::table &tbl) {
    init_pair(1, COLOR_BLUE, COLOR_BLACK);
    auto keybindings = tbl["keybindings"].as_table();
    const char quit_char = *tbl["quit"].value_or("q");
    attron(COLOR_PAIR(1));
    printw("--------------\n");
    printw("     PVKB\n");
    printw("--------------\n");
    attroff(COLOR_PAIR(1));
    printw("Type %c to quit\n\n", quit_char);
    attron(A_ITALIC);
    attron(A_BOLD);
    printw("Keybindings:\n");
    attroff(A_ITALIC);
    attroff(A_BOLD);
    for (const auto &entry : *keybindings) {
	std::stringstream ss;
        auto entry_table = *entry.second.as_table();
	ss << entry.first.str() << ": ";
	if (entry_table["increment"]) {
	    ss << entry_table["pv"] << " += ";
	} else {
	    ss << entry_table["pv"] << " = ";
	}
	if (entry_table["value"].value<float>().has_value()) {
	    ss << entry_table["value"].value<float>().value() << std::endl;
	}
	printw("%s",ss.str().c_str());
    }
}

int main(int argc, char *argv[]) {

    // Parse command line arguments
    // Command line args take precedence over config file
    argh::parser cmdl;
    cmdl.add_params({"-p","--prefix"});
    cmdl.parse(argc, argv);
    
    // Path to TOML config file is first positional arg
    const std::string toml_path = cmdl[1];
    if (!toml_path.length()) {
	std::cerr << "Please provide a TOML configuration file\n";
	return 1;
    }

    // Named argument for IOC prefix
    std::string ioc_prefix = cmdl({"-p","--prefix"}).str();
    
    // Parse the TOML config file into a toml::table
    toml::table tbl;
    try {
        tbl = toml::parse_file(toml_path);
    } catch (const toml::parse_error& err) {
        std::cerr << "Parsing failed:\n" << err << "\n";
        return 1;
    }

    // Get IOC prefix from config file if not overridden
    if (ioc_prefix.empty()) {
	ioc_prefix = tbl["prefix"].value_or("");
    }
    
    // Get character used to quit the program
    const char quit_char = *tbl["quit"].value_or("q");

    // Get the provider "ca" or "pva", default: "ca"
    epics::pvAccess::ca::CAClientFactory::start();
    const std::optional<std::string> provider_name = tbl["provider"].value_or("ca");
    pvac::ClientProvider provider(provider_name.value());

    // Execute requested puts before running main loop
    do_prelim_puts(tbl, provider, ioc_prefix);
    
    // Get the mapping key_char -> (pv channel, pv value, increment=true/false)
    std::map<char, TupleVal> channel_map = parse_keybindings(tbl, provider, ioc_prefix);
    
    // Initialize ncurses
    initscr();
    keypad(stdscr, TRUE);
    noecho();
    start_color();

    // Print out active keybindings
    show_keybindings(tbl);

    // Listen for keypresses and execute requested puts
    while (true) {
	int ch = getch();
	if (ch == quit_char) {
	    break;
	}

	if (channel_map.count(ch) > 0) {
	    const TupleVal key_tuple = channel_map.at(ch);
	    pvac::ClientChannel channel = std::get<0>(key_tuple);
	    const TargetVar val = std::get<1>(key_tuple);
	    const bool increment = std::get<2>(key_tuple);
	    execute_put(channel, val, increment);
	}

	refresh();
    }
    endwin();

    return 0;
}


