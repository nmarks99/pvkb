# pvkb
EPICS **P**rocess **V**ariable **K**ey**B**oard

Command line tool to control arbitrary EPICS PVs with your computer keyboard
with keybindings specified in a TOML file.

## Installation
Dependencies:
- pvAccessCPP (probably just through EPICS Base installation)
- ncurses

Build:  
1. On a system with EPICS base installed, correct the path to EPICS_BASE in the
configure/RELEASE file.
2. Run `make`. The binary will be installed in bin/\<EPICS_HOST_ARCH\>


## Configuration

A TOML configuration file which defines the keybindings must be passed to the `pvkb` program.
The following configurations options are available:

- `prefix`: The IOC prefix which is inserted before all PV names which are provided later on
- `provider`: EPICS client provider which can be either "ca"(default) or "pva" 
- `put`: A TOML array of PVs to write to before starting the main program loop. Each PV/value pair
is specified as a TOML table with a string key for the PV name, and a value key which should have
the same type and the PV itself, e.g. `{pv="m1.DESC", value="My Motor"}`. The CA/PVA puts in this array will
be executed before the main program loop begins listening for key presses.
- `[keybindings]`: A TOML header used to specify keybindings and the associated CA/PVA put to execute when
said key is pressed. Keys are specified in the form `key_<CHAR>` where `<CHAR>` can be almost any alphanumeric
key like "a" (`key_a`), or "1" (`key_1`), as well as some special keys like "left", "right", "up", and "down".
The "q" character is reserved for the "quit" key.
arrow keys. The PV name and target value is specified the same as in the put array section, `{pv="m1.TWF", value=1}`
    - There is an additional optional boolean flag in the keybindings section called `increment`(default=false),
    (e.g. `{pv="m1.TWV", value=0.1, increment=true}`. When `increment=true` instead of ovewriting the current value of the PV
    with the new value, the new value will be *added* to the current value of the PV.


The provided example.toml file demonstrates how the arrow keys can be bound to moving a motor:

```toml
prefix = "myIOCPrefix:"

provider = "ca"

put = [
    {pv="m1.DESC", value="My Motor"},
    {pv="m1.HLM", value=100},
]

[keybindings]
key_right = {pv="m1.TWF", value=1}
key_left = {pv="m1.TWR", value=1}
key_up = {pv="m1.TWV", value=0.1, increment=true}
key_down = {pv="m1.TWV", value=-0.1, increment=true}
```

Before listening for keypresses, the program will write the value "My Motor" to prefix:m1.DESC,
and the value 100 to prefix:m1.HLM.
Then, it will bind the arrow keys such that the right arrow will write a 1 to prefix:m1.TWF (tweaks the motor forward)
and the left arrow will write a 1 to prefix:m1.TWR (tweaks the motor backwards). The up arrow is set to increment
prefix:m1.TWV by +0.1, and the down arrow is set to increment prefix:m1.TWV by -0.1.


## Usage

For example, to run `pvkb` with the example TOML configuration file, run:
```
pvkb example.toml
```

While the program is running, keypresses will only be caught when the terminal window where you ran the program is active.
To stop the program at any time, simple type the `q` key.
